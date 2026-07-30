package gpu.spinal.riscv

import spinal.core._
import spinal.lib._
import gpu.spinal.peripheral.SimDpiMem

/** Independent SpinalHDL RV32E SIMT core. */

class GPUCore(coreId: Int, warps: Int, threads: Int) extends Component {
  require(warps > 0 && threads > 0)
  private val contexts = warps * threads
  private val wb = scala.math.max(1, log2Up(warps))
  private val tb = scala.math.max(1, log2Up(threads))
  private val regIndexBits = scala.math.max(1, log2Up(contexts * 16))
  val io = new Bundle {
    val clockEnable = in Bool()
    val done = out Bool()
    val activeWarps = out Bits(warps bits)
    val issueWarp = out UInt(wb bits)
    val issueMask = out Bits(threads bits)
  }
  val pcs = Vec(Reg(UInt(32 bits)) init(U(0x81000000L, 32 bits)), contexts)
  val halted = Vec(Reg(Bool()) init(False), contexts)
  val regs = Vec(Reg(UInt(32 bits)) init(0), contexts * 16)
  // 0=schedule, 1=fetch, 2=execute, 3=memory request, 4=memory response.
  // A separate response state gives the DPI memory a full cycle to produce
  // read data, matching the existing Chisel backend's memory contract.
  val state = Reg(UInt(3 bits)) init(0)
  val rr = Reg(UInt(wb bits)) init(0)
  val iw = Reg(UInt(wb bits)) init(0)
  val im = Reg(Bits(threads bits)) init(0)
  val ip = Reg(UInt(32 bits)) init(0)
  val inst = Reg(UInt(32 bits)) init(0)
  val mt = Reg(UInt(tb bits)) init(0)
  val memReadData = Reg(UInt(32 bits)) init(0)
  val imem = new SimDpiMem
  val dmem = new SimDpiMem
  imem.io.ren := state === 1
  imem.io.wen := False; imem.io.mask := 0; imem.io.addr := ip; imem.io.wdata := U(0, 32 bits)
  dmem.io.ren := False; dmem.io.wen := False; dmem.io.mask := 0; dmem.io.addr := U(0,32 bits); dmem.io.wdata := U(0,32 bits)

  val active = Bits(warps bits)
  for (w <- 0 until warps) active(w) := !halted.slice(w*threads, (w+1)*threads).andR
  io.activeWarps := active; io.done := halted.asBits.andR; io.issueWarp := iw; io.issueMask := im

  val selected = UInt(wb bits); selected := rr
  val anyActive = active.orR
  // Reverse priority assignment makes the nearest active warp win without
  // feeding the selected signal back into its own selection condition.
  for (o <- (0 until warps).reverse) when(active((rr + o).resized)) { selected := (rr + o).resized }
  val firstThread = UInt(tb bits); firstThread := 0
  for (t <- 0 until threads) when(!halted((selected * threads + t).resized)) { firstThread := t }
  val selectedPc = pcs((selected * threads + firstThread).resized)
  val selectedMask = Bits(threads bits)
  for (t <- 0 until threads) selectedMask(t) := !halted((selected*threads+t).resized) && pcs((selected*threads+t).resized) === selectedPc

  val opcode = inst(6 downto 0); val rd = inst(11 downto 7); val f3 = inst(14 downto 12)
  val rs1 = inst(19 downto 15); val rs2 = inst(24 downto 20); val f7 = inst(31 downto 25)
  val immI = inst(31 downto 20).asSInt.resize(32).asUInt
  val immS = (inst(31 downto 25) ## inst(11 downto 7)).asSInt.resize(32).asUInt
  val immB = (inst(31) ## inst(7) ## inst(30 downto 25) ## inst(11 downto 8) ## False).asSInt.resize(32).asUInt
  val immJ = (inst(31) ## inst(19 downto 12) ## inst(20) ## inst(30 downto 21) ## False).asSInt.resize(32).asUInt
  val isLoad = opcode === U"7'b0000011"; val isStore = opcode === U"7'b0100011"
  val usesRd = opcode===U"7'b0110111" || opcode===U"7'b0010111" || opcode===U"7'b0010011" || opcode===U"7'b0110011" || opcode===U"7'b0000011" || opcode===U"7'b1101111" || opcode===U"7'b1100111" || opcode===U"7'b1110011"
  val usesRs1 = opcode===U"7'b0010011" || opcode===U"7'b0110011" || opcode===U"7'b0000011" || opcode===U"7'b0100011" || opcode===U"7'b1100011" || opcode===U"7'b1100111"
  val usesRs2 = opcode===U"7'b0110011" || opcode===U"7'b0100011" || opcode===U"7'b1100011"
  val ctx = (iw * threads + mt).resized
  // Explicitly widen before multiplying: resizing the product to ctx's width
  // aliases every context to register bank zero.
  val base = UInt(regIndexBits bits)
  base := (ctx.resize(regIndexBits) * U(16, regIndexBits bits)).resize(regIndexBits)
  val memAddr = regs((base + rs1(3 downto 0)).resized) + Mux(isLoad, immI, immS)

  when(!io.clockEnable) { state := 0; rr := 0; mt := 0
    for (n <- 0 until contexts) { pcs(n) := U(0x81000000L, 32 bits); halted(n) := False }
    for (n <- 0 until contexts*16) regs(n) := U(0, 32 bits)
  } otherwise {
    switch(state) {
      is(0) { when(anyActive) { iw := selected; im := selectedMask; ip := selectedPc; state := 1 } }
      is(1) { inst := imem.io.rdata; state := 2 }
      is(2) {
        when(isLoad || isStore) { mt := 0; state := 3 }
        .otherwise {
          for (t <- 0 until threads) when(im(t)) {
            val c = (iw * threads + t).resized
            val b = UInt(regIndexBits bits)
            b := (c.resize(regIndexBits) * U(16, regIndexBits bits)).resize(regIndexBits)
            val a = regs((b + rs1(3 downto 0)).resized); val bv = regs((b + rs2(3 downto 0)).resized)
            val result = UInt(32 bits); result := U(0, 32 bits); val npc = UInt(32 bits); npc := ip + 4
            val wr = Bool(); wr := True; val good = Bool(); good := True
            switch(opcode) {
              is(U"7'b0110111") { result := (inst(31 downto 12) ## U(0,12 bits)).asUInt }
              is(U"7'b0010011") { switch(f3) { is(0) { result:=a+immI }; is(4) { result:=a^immI }; is(6) { result:=a|immI }; is(7) { result:=a&immI }; is(1) { result:=a |<< rs2 }; is(5) { result:=Mux(inst(30), (a.asSInt >> rs2).asUInt, a >> rs2) }; default { good:=False } } }
              is(U"7'b0110011") { switch(f3) { is(0) { result:=Mux(f7===U"7'b0100000",a-bv,a+bv) }; is(4){result:=a^bv}; is(6){result:=a|bv}; is(7){result:=a&bv}; is(1){result:=a |<< bv(4 downto 0)}; is(5){result:=Mux(f7===U"7'b0100000",(a.asSInt >> bv(4 downto 0)).asUInt,a >> bv(4 downto 0))}; default{good:=False} } }
              is(U"7'b1100011") { wr:=False; val take = Bool(); take:=False; switch(f3) { is(0){take:=a===bv}; is(1){take:=a=/=bv}; is(4){take:=a.asSInt<bv.asSInt}; is(5){take:=a.asSInt>=bv.asSInt}; is(6){take:=a<bv}; is(7){take:=a>=bv}; default{good:=False} }; npc:=Mux(take,ip+immB,ip+4) }
              is(U"7'b1101111") { result:=ip+4; npc:=ip+immJ }
              is(U"7'b1100111") { result:=ip+4; npc:=(a+immI)&U(0xfffffffeL,32 bits) }
              is(U"7'b1110011") { wr:=False; when(inst===U(0x00100073L,32 bits)){ halted(c):=True } elsewhen(opcode===U"7'b1110011" && f3===2 && rs1===0 && inst(31 downto 20)===U(0xf14,12 bits)){wr:=True; result:=(U(coreId*warps*threads,32 bits)+(iw.resize(32)*threads)+t).resize(32)} otherwise {good:=False} }
              default {good:=False}
            }
            when(!good || (usesRd && rd(4)) || (usesRs1 && rs1(4)) || (usesRs2 && rs2(4))) { halted(c):=True } otherwise { pcs(c):=npc; when(wr && rd=/=0){regs((b+rd(3 downto 0)).resized):=result} }; regs(b):=U(0,32 bits)
          }
          rr := Mux(iw === warps-1, U(0,wb bits), iw+1); state := 0
        }
      }
      is(3) {
        when(im(mt)) {
          dmem.io.ren := isLoad; dmem.io.wen := isStore; dmem.io.addr := memAddr & U(0xfffffffcL,32 bits)
          dmem.io.mask := B"00001111"; dmem.io.wdata := regs((base+rs2(3 downto 0)).resized) |<< (memAddr(1 downto 0) << 3)
          memReadData := dmem.io.rdata
        }
        state := 4
      }
      is(4) {
        when(im(mt)) { when(isLoad && rd=/=0) { regs((base+rd(3 downto 0)).resized):=memReadData }; pcs(ctx):=ip+4; regs(base):=U(0,32 bits) }
        when(mt === threads-1) { rr:=Mux(iw===warps-1,U(0,wb bits),iw+1); state:=0 } otherwise { mt:=mt+1; state:=3 }
      }
    }
  }
}
