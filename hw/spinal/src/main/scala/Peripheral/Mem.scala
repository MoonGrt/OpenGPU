package gpu.peripheral

import spinal.core._
import gpu.util.DpiMem

/** PMEM adapter counterpart of Chisel's Peripheral/Mem.scala.
  *
  * The compact Spinal core owns its request/response sequencing, while this
  * component provides the same stable DpiMem port abstraction for instruction
  * and data accesses.
  */
class SimDpiMem extends Component {
  val io = new Bundle {
    val ren, wen = in Bool()
    val mask = in Bits(8 bits)
    val addr, wdata = in UInt(32 bits)
    val rdata = out UInt(32 bits)
  }
  private val mem = new DpiMem
  mem.io.ren := io.ren
  mem.io.wen := io.wen
  mem.io.mask := io.mask
  mem.io.addr := io.addr
  mem.io.wdata := io.wdata
  io.rdata := mem.io.rdata
}

/** Read-only instruction-memory wrapper instantiated by the GPU top level. */
class SimInstMem extends Component {
  val io = new Bundle {
    val ren = in Bool()
    val addr = in UInt(32 bits)
    val rdata = out UInt(32 bits)
  }
  private val mem = new SimDpiMem
  mem.io.ren := io.ren
  mem.io.wen := False
  mem.io.mask := 0
  mem.io.addr := io.addr
  mem.io.wdata := 0
  io.rdata := mem.io.rdata
}

/** Data-memory wrapper instantiated by the GPU top level. */
class SimDataMem extends Component {
  val io = new Bundle {
    val ren, wen = in Bool()
    val mask = in Bits(8 bits)
    val addr, wdata = in UInt(32 bits)
    val rdata = out UInt(32 bits)
  }
  private val mem = new SimDpiMem
  mem.io.ren := io.ren
  mem.io.wen := io.wen
  mem.io.mask := io.mask
  mem.io.addr := io.addr
  mem.io.wdata := io.wdata
  io.rdata := mem.io.rdata
}
