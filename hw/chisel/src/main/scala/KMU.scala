package gpu

import chisel3._
import chisel3.util._

class CtaRequest extends Bundle {
  val startupPc = UInt(32.W)
  val argsAddr = UInt(32.W)
  val blockIdx = Vec(3, UInt(32.W))
  val blockDim = Vec(3, UInt(32.W))
  val gridDim = Vec(3, UInt(32.W))
  val blockSize = UInt(32.W)
}

class Kmu(numCores: Int) extends Module {
  private val CoreBits = math.max(1, log2Ceil(numCores))
  val io = IO(new Bundle {
    val dcrValid = Input(Bool())
    val dcrAddr = Input(UInt(12.W))
    val dcrData = Input(UInt(32.W))
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

  when(io.dcrValid) {
    switch(io.dcrAddr) {
      is("h010".U) { startupPc := io.dcrData }
      is("h011".U) { argsAddr := io.dcrData }
      is("h012".U) { argsSize := io.dcrData }
      is("h013".U) { blockDim(0) := io.dcrData }
      is("h014".U) { blockDim(1) := io.dcrData }
      is("h015".U) { blockDim(2) := io.dcrData }
      is("h016".U) { gridDim(0) := io.dcrData }
      is("h017".U) { gridDim(1) := io.dcrData }
      is("h018".U) { gridDim(2) := io.dcrData }
      is("h019".U) { blockSize := io.dcrData }
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
  dontTouch(argsSize)
}
