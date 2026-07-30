package gpu.perip.mem

import chisel3._
import chisel3.util._
import gpu.util.DpiMem

class InstReq extends Bundle {
  val addr = UInt(32.W)
}

class InstResp extends Bundle {
  val data = UInt(32.W)
}

class InstBus extends Bundle {
  val req = Decoupled(new InstReq)
  val resp = Flipped(Decoupled(new InstResp))
}

class DataReq extends Bundle {
  val ren = Bool()
  val wen = Bool()
  val size = UInt(2.W)
  val mask = UInt(4.W)
  val addr = UInt(32.W)
  val wdata = UInt(32.W)
}

class DataResp extends Bundle {
  val rdata = UInt(32.W)
}

class DataBus extends Bundle {
  val req = Decoupled(new DataReq)
  val resp = Flipped(Decoupled(new DataResp))
}

/** One-cycle instruction port backed by the simulator's shared PMEM. */
class SimInstMem extends Module {
  val io = IO(Flipped(new InstBus))
  val mem = Module(new DpiMem)
  val pending = RegInit(false.B)
  val response = Reg(UInt(32.W))

  io.req.ready := !pending
  io.resp.valid := pending
  io.resp.bits.data := response

  mem.io.ren := io.req.fire
  mem.io.wen := false.B
  mem.io.mask := 0.U
  mem.io.addr := io.req.bits.addr
  mem.io.wdata := 0.U

  when(io.req.fire) {
    response := mem.io.rdata
    pending := true.B
  }
  when(io.resp.fire) {
    pending := false.B
  }
}

/** One-request-at-a-time data port backed by the shared simulator PMEM.
  *
  * Stores receive an explicit response because Core retires memory
  * instructions only after the response handshake.
  */
class SimDataMem extends Module {
  val io = IO(Flipped(new DataBus))
  val mem = Module(new DpiMem)
  val pending = RegInit(false.B)
  val response = Reg(UInt(32.W))

  io.req.ready := !pending
  io.resp.valid := pending
  io.resp.bits.rdata := response

  mem.io.ren := io.req.fire && io.req.bits.ren
  mem.io.wen := io.req.fire && io.req.bits.wen
  mem.io.mask := io.req.bits.mask
  mem.io.addr := io.req.bits.addr
  mem.io.wdata := io.req.bits.wdata

  when(io.req.fire) {
    response := mem.io.rdata
    pending := true.B
  }
  when(io.resp.fire) {
    pending := false.B
  }
}
