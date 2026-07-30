package gpu.util

import chisel3._

class DpiMem extends BlackBox {
  val io = IO(new Bundle {
    val ren = Input(Bool())
    val wen = Input(Bool())
    val mask = Input(UInt(8.W))
    val addr = Input(UInt(32.W))
    val wdata = Input(UInt(32.W))
    val rdata = Output(UInt(32.W))
  })
}

class DpiGpuTraceBB extends BlackBox {
  val io = IO(new Bundle {
    val clk = Input(Clock())
    val en = Input(Bool())
    val hartid = Input(UInt(32.W))
    val pc = Input(UInt(32.W))
    val inst = Input(UInt(32.W))
  })
}

class DpiGpuStoreTraceBB extends BlackBox {
  val io = IO(new Bundle {
    val clk = Input(Clock())
    val en = Input(Bool())
    val hartid = Input(UInt(32.W))
    val addr = Input(UInt(32.W))
    val mask = Input(UInt(32.W))
    val data = Input(UInt(32.W))
  })
}
