package gpu

import spinal.core._
import gpu.interfaces.{CtaRequest, DcrWrite}

class Kmu(numCores: Int) extends Component {
  private val cb = scala.math.max(1, log2Up(numCores))
  val io = new Bundle {
    val dcr = in(new DcrWrite)
    val launch = in Bool()
    val coreReady = in Bits(numCores bits)
    val coreBusy = in Bits(numCores bits)
    val ctaValid = out Bits(numCores bits)
    val cta = out(new CtaRequest)
    val busy = out Bool()
  }
  val startupPc = Reg(UInt(32 bits)) init U(0x81000000L, 32 bits)
  val argsAddr = Reg(UInt(32 bits)) init 0
  val argsSize = Reg(UInt(32 bits)) init 0
  val blockDim = Vec(Reg(UInt(32 bits)) init 1, 3)
  val gridDim = Vec(Reg(UInt(32 bits)) init 1, 3)
  val blockSize = Reg(UInt(32 bits)) init 1
  val blockIdx = Vec(Reg(UInt(32 bits)) init 0, 3)
  val running = Reg(Bool()) init False
  val rrCore = Reg(UInt(cb bits)) init 0

  when(io.dcr.valid) {
    switch(io.dcr.addr) {
      is(U(0x010)) { startupPc := io.dcr.data }
      is(U(0x011)) { argsAddr := io.dcr.data }
      is(U(0x012)) { argsSize := io.dcr.data }
      is(U(0x013)) { blockDim(0) := io.dcr.data }
      is(U(0x014)) { blockDim(1) := io.dcr.data }
      is(U(0x015)) { blockDim(2) := io.dcr.data }
      is(U(0x016)) { gridDim(0) := io.dcr.data }
      is(U(0x017)) { gridDim(1) := io.dcr.data }
      is(U(0x018)) { gridDim(2) := io.dcr.data }
      is(U(0x019)) { blockSize := io.dcr.data }
    }
  }

  val selected = UInt(cb bits)
  selected := rrCore
  for (o <- (0 until numCores).reverse)
    when(io.coreReady((rrCore + o).resized)) { selected := (rrCore + o).resized }
  val anyReady = io.coreReady.orR
  for (i <- 0 until numCores)
    io.ctaValid(i) := running && anyReady && selected === i
  val selectedReady = if (numCores == 1) io.coreReady(0) else io.coreReady(selected)
  val fire = running && anyReady && selectedReady

  when(io.launch) {
    blockIdx.foreach(_ := 0)
    running := blockSize =/= 0 && gridDim.map(_ =/= 0).reduce(_ && _)
  } elsewhen (fire) {
    rrCore := Mux(selected === numCores - 1, U(0, cb bits), selected + 1)
    when(blockIdx(0) + 1 === gridDim(0)) {
      blockIdx(0) := 0
      when(blockIdx(1) + 1 === gridDim(1)) {
        blockIdx(1) := 0
        when(blockIdx(2) + 1 === gridDim(2)) {
          blockIdx(2) := 0; running := False
        } otherwise { blockIdx(2) := blockIdx(2) + 1 }
      } otherwise { blockIdx(1) := blockIdx(1) + 1 }
    } otherwise { blockIdx(0) := blockIdx(0) + 1 }
  }
  io.cta.startupPc := startupPc
  io.cta.argsAddr := argsAddr
  io.cta.blockIdx := blockIdx
  io.cta.blockDim := blockDim
  io.cta.gridDim := gridDim
  io.cta.blockSize := blockSize
  io.busy := running || io.coreBusy.orR
  argsSize.addAttribute("keep")
}
