package gpu

import chisel3._
import chisel3.util._
import gpu.interfaces.{CtaRequest, DcrWrite}
import gpu.util.DpiGpuStateBB

class Kmu(numCores: Int, difftest: Boolean) extends Module {
  private val CoreBits = math.max(1, log2Ceil(numCores))
  val io = IO(new Bundle {
    val dcr = Input(new DcrWrite)
    val launch = Input(Bool())
    val coreReady = Input(UInt(numCores.W))
    val coreBusy = Input(UInt(numCores.W))
    val ctaValid = Output(UInt(numCores.W))
    val cta = Output(new CtaRequest)
    val busy = Output(Bool())
  })

  val startupPc = RegInit("h81000000".U(32.W))
  val argsAddr = RegInit(0.U(32.W))
  val argsSize = RegInit(0.U(32.W))
  val blockDim = RegInit(VecInit(Seq.fill(3)(1.U(32.W))))
  val gridDim = RegInit(VecInit(Seq.fill(3)(1.U(32.W))))
  val blockSize = RegInit(1.U(32.W))
  val blockIdx = RegInit(VecInit(Seq.fill(3)(0.U(32.W))))
  val running = RegInit(false.B)
  val rrCore = RegInit(0.U(CoreBits.W))

  when(io.dcr.valid) {
    switch(io.dcr.addr) {
      is("h010".U) { startupPc := io.dcr.data }
      is("h011".U) { argsAddr := io.dcr.data }
      is("h012".U) { argsSize := io.dcr.data }
      is("h013".U) { blockDim(0) := io.dcr.data }
      is("h014".U) { blockDim(1) := io.dcr.data }
      is("h015".U) { blockDim(2) := io.dcr.data }
      is("h016".U) { gridDim(0) := io.dcr.data }
      is("h017".U) { gridDim(1) := io.dcr.data }
      is("h018".U) { gridDim(2) := io.dcr.data }
      is("h019".U) { blockSize := io.dcr.data }
    }
  }

  val rotated = (Cat(io.coreReady, io.coreReady) >> rrCore)(numCores - 1, 0)
  val anyReady = rotated.orR
  val selected = (rrCore + PriorityEncoder(rotated))(CoreBits - 1, 0)
  io.ctaValid := VecInit((0 until numCores).map(i =>
    running && anyReady && selected === i.U)).asUInt
  val fire = running && anyReady && io.coreReady(selected)

  when(io.launch) {
    blockIdx.foreach(_ := 0.U)
    running := blockSize =/= 0.U && gridDim.map(_ =/= 0.U).reduce(_ && _)
  }.elsewhen(fire) {
    rrCore := Mux(selected === (numCores - 1).U, 0.U, selected + 1.U)
    when(blockIdx(0) + 1.U === gridDim(0)) {
      blockIdx(0) := 0.U
      when(blockIdx(1) + 1.U === gridDim(1)) {
        blockIdx(1) := 0.U
        when(blockIdx(2) + 1.U === gridDim(2)) {
          blockIdx(2) := 0.U
          running := false.B
        }.otherwise { blockIdx(2) := blockIdx(2) + 1.U }
      }.otherwise { blockIdx(1) := blockIdx(1) + 1.U }
    }.otherwise { blockIdx(0) := blockIdx(0) + 1.U }
  }

  io.cta.startupPc := startupPc
  io.cta.argsAddr := argsAddr
  io.cta.blockIdx := blockIdx
  io.cta.blockDim := blockDim
  io.cta.gridDim := gridDim
  io.cta.blockSize := blockSize
  io.busy := running || io.coreBusy.orR
  if (difftest) {
    val diff = Wire(Vec(20, UInt(32.W)))
    diff.foreach(_ := 0.U)
    diff(0) := startupPc
    diff(1) := argsAddr
    diff(2) := argsSize
    for (axis <- 0 until 3) {
      diff(3 + axis) := blockDim(axis)
      diff(6 + axis) := gridDim(axis)
      diff(10 + axis) := blockIdx(axis)
    }
    diff(9) := blockSize
    diff(13) := running
    diff(14) := rrCore
    diff(15) := io.ctaValid
    diff(16) := io.coreReady
    diff(17) := io.coreBusy
    diff(18) := io.busy
    diff(19) := Mux(anyReady, selected, 0.U) | (anyReady.asUInt << 31)
    val diffBridge = Module(new DpiGpuStateBB(20, 3))
    diffBridge.io.state := diff.asUInt
  }
  dontTouch(argsSize)
}
