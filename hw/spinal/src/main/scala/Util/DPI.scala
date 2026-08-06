package gpu.util

import spinal.core._

/** Shared PMEM DPI port, matching hw/chisel/src/main/scala/Util/DPI.scala. */
class DpiMem extends BlackBox {
  val io = new Bundle {
    val ren, wen = in Bool()
    val mask = in Bits(8 bits)
    val addr, wdata = in UInt(32 bits)
    val rdata = out UInt(32 bits)
  }
  noIoPrefix()
}

class DpiGpuStateBB(words: Int, base: Int) extends BlackBox {
  addGeneric("WORDS", words)
  addGeneric("BASE", base)
  val io = new Bundle {
    val state = in Bits(words * 32 bits)
  }
  noIoPrefix()
}
