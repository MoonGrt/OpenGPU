package gpu.spinal.riscv

import spinal.core._
import spinal.lib._
import gpu.spinal.CtaRequest
import gpu.spinal.peripheral.SimDpiMem

class GPUCore(coreId: Int, warps: Int, threads: Int) extends Component {
  private val contexts = warps * threads
  private val wb = scala.math.max(1, log2Up(warps))
  private val tb = scala.math.max(1, log2Up(threads))
  private val cb = scala.math.max(1, log2Up(contexts))
  private val rb = scala.math.max(1, log2Up(contexts * 16))
  private val stackDepth = 8
  private val sb = scala.math.max(1, log2Up(warps * stackDepth))

  val io = new Bundle {
    val ctaValid = in Bool()
    val ctaReady = out Bool()
    val cta = in(new CtaRequest)
    val busy = out Bool()
    val fault = out Bool()
    val done = out Bool()
    val activeWarps = out Bits(warps bits)
    val issueWarp = out UInt(wb bits)
    val issueMask = out Bits(threads bits)
  }

  val warpPc = Vec(Reg(UInt(32 bits)) init 0, warps)
  val warpMask = Vec(Reg(Bits(threads bits)) init 0, warps)
  val warpValid = Vec(Reg(Bool()) init False, warps)
  val stackSp = Vec(Reg(UInt(4 bits)) init 0, warps)
  val stackPc = Vec(Reg(UInt(32 bits)) init 0, warps * stackDepth)
  val stackMask = Vec(Reg(Bits(threads bits)) init 0, warps * stackDepth)
  val regs = Vec(Reg(UInt(32 bits)) init 0, contexts * 16)
  val threadX = Vec(Reg(UInt(32 bits)) init 0, contexts)
  val threadY = Vec(Reg(UInt(32 bits)) init 0, contexts)
  val threadZ = Vec(Reg(UInt(32 bits)) init 0, contexts)
  val globalId = Vec(Reg(UInt(32 bits)) init 0, contexts)
  val blockIdx = Vec(Reg(UInt(32 bits)) init 0, 3)
  val blockDim = Vec(Reg(UInt(32 bits)) init 1, 3)
  val gridDim = Vec(Reg(UInt(32 bits)) init 1, 3)
  val argsAddr = Reg(UInt(32 bits)) init 0
  val ctaActive = Reg(Bool()) init False
  val faultR = Reg(Bool()) init False
  val state = Reg(UInt(3 bits)) init 0
  val rr = Reg(UInt(wb bits)) init 0
  val iw = Reg(UInt(wb bits)) init 0
  val im = Reg(Bits(threads bits)) init 0
  val ip = Reg(UInt(32 bits)) init 0
  val inst = Reg(UInt(32 bits)) init 0
  val mt = Reg(UInt(tb bits)) init 0
  val memReadData = Reg(UInt(32 bits)) init 0

  io.busy := ctaActive
  io.done := !ctaActive
  io.fault := faultR
  io.ctaReady := !ctaActive && state === 0
  io.activeWarps := warpValid.asBits
  io.issueWarp := iw
  io.issueMask := im

  val imem = new SimDpiMem
  val dmem = new SimDpiMem
  imem.io.ren := state === 1
  imem.io.wen := False; imem.io.mask := 0; imem.io.addr := ip; imem.io.wdata := 0
  dmem.io.ren := False; dmem.io.wen := False; dmem.io.mask := 0
  dmem.io.addr := 0; dmem.io.wdata := 0

  when(io.ctaValid && io.ctaReady) {
    ctaActive := True; faultR := False; state := 0; rr := 0
    blockIdx := io.cta.blockIdx; blockDim := io.cta.blockDim
    gridDim := io.cta.gridDim; argsAddr := io.cta.argsAddr
    val blockLinear = ((io.cta.blockIdx(2) * io.cta.gridDim(1)) +
      io.cta.blockIdx(1)) * io.cta.gridDim(0) + io.cta.blockIdx(0)
    for (w <- 0 until warps) {
      warpPc(w) := io.cta.startupPc; stackSp(w) := 0
      for (t <- 0 until threads) warpMask(w)(t) := w * threads + t < io.cta.blockSize
      warpValid(w) := w * threads < io.cta.blockSize
    }
    for (i <- 0 until contexts) {
      val localId = U(i, 32 bits)
      val blockPlane = (io.cta.blockDim(0) * io.cta.blockDim(1)).resize(32)
      threadX(i) := localId % io.cta.blockDim(0)
      threadY(i) := (localId / io.cta.blockDim(0)) % io.cta.blockDim(1)
      threadZ(i) := (localId / blockPlane).resize(32)
      globalId(i) := (blockLinear * io.cta.blockSize + i).resize(32)
    }
    regs.foreach(_ := U(0, 32 bits))
  }

  val active = warpValid.asBits
  val selected = UInt(wb bits)
  if (warps > 1) {
    selected := rr
    for (o <- (0 until warps).reverse)
      when(active((rr + o).resized)) { selected := (rr + o).resized }
  } else
    selected := 0

  val opcode = inst(6 downto 0)
  val rd = inst(11 downto 7)
  val f3 = inst(14 downto 12)
  val rs1 = inst(19 downto 15)
  val rs2 = inst(24 downto 20)
  val f7 = inst(31 downto 25)
  val csr = inst(31 downto 20)
  val immI = inst(31 downto 20).asSInt.resize(32).asUInt
  val immS = (inst(31 downto 25) ## inst(11 downto 7)).asSInt.resize(32).asUInt
  val immB = (inst(31) ## inst(7) ## inst(30 downto 25) ## inst(11 downto 8) ## False).asSInt.resize(32).asUInt
  val immJ = (inst(31) ## inst(19 downto 12) ## inst(20) ## inst(30 downto 21) ## False).asSInt.resize(32).asUInt
  val isLoad = opcode === U"7'b0000011"
  val isStore = opcode === U"7'b0100011"
  val usesRd = opcode===U"7'b0110111" || opcode===U"7'b0010111" || opcode===U"7'b0010011" || opcode===U"7'b0110011" || opcode===U"7'b0000011" || opcode===U"7'b1101111" || opcode===U"7'b1100111" || opcode===U"7'b1110011"
  val usesRs1 = opcode===U"7'b0010011" || opcode===U"7'b0110011" || opcode===U"7'b0000011" || opcode===U"7'b0100011" || opcode===U"7'b1100011" || opcode===U"7'b1100111"
  val usesRs2 = opcode===U"7'b0110011" || opcode===U"7'b0100011" || opcode===U"7'b1100011"
  val badReg = (usesRd && rd(4)) || (usesRs1 && rs1(4)) || (usesRs2 && rs2(4))

  def ctx(t: UInt): UInt = (iw.resize(cb) * threads + t.resize(cb)).resize(cb)
  def base(c: UInt): UInt = (c.resize(rb) |<< 4).resize(rb)
  def wPc(index: UInt): UInt = if (warps == 1) warpPc(0) else warpPc(index)
  def wMask(index: UInt): Bits = if (warps == 1) warpMask(0) else warpMask(index)
  def wValid(index: UInt): Bool = if (warps == 1) warpValid(0) else warpValid(index)
  def wSp(index: UInt): UInt = if (warps == 1) stackSp(0) else stackSp(index)
  def globalFor(index: UInt): UInt = if (contexts == 1) globalId(0) else globalId(index)
  def threadXFor(index: UInt): UInt = if (contexts == 1) threadX(0) else threadX(index)
  def threadYFor(index: UInt): UInt = if (contexts == 1) threadY(0) else threadY(index)
  def threadZFor(index: UInt): UInt = if (contexts == 1) threadZ(0) else threadZ(index)
  def laneActive(index: UInt): Bool = if (threads == 1) im(0) else im(index)
  def advanceWarp(): Unit = {
    rr := Mux(iw === warps - 1, U(0, wb bits), iw + 1)
    state := 0
  }

  val taken = Bits(threads bits)
  for (t <- 0 until threads) {
    val c = ctx(U(t, tb bits)); val b0 = base(c)
    val a = regs((b0 + rs1(3 downto 0)).resized)
    val b = regs((b0 + rs2(3 downto 0)).resized)
    taken(t) := False
    switch(f3) {
      is(0) { taken(t) := a === b }; is(1) { taken(t) := a =/= b }
      is(4) { taken(t) := a.asSInt < b.asSInt }; is(5) { taken(t) := a.asSInt >= b.asSInt }
      is(6) { taken(t) := a < b }; is(7) { taken(t) := a >= b }
    }
  }
  val takenMask = taken & im
  val fallMask = ~taken & im
  val branchGood = f3===0 || f3===1 || f3===4 || f3===5 || f3===6 || f3===7
  val firstLane = OHToUInt(OHMasking.first(im))
  val firstC = ctx(firstLane.resized); val firstB = base(firstC)
  val firstTarget = (regs((firstB + rs1(3 downto 0)).resized) + immI) & U(0xfffffffeL,32 bits)
  val jalrDivergent = Bool(); jalrDivergent := False
  for (t <- 0 until threads) {
    val c = ctx(U(t,tb bits)); val b0 = base(c)
    when(im(t) && (((regs((b0+rs1(3 downto 0)).resized)+immI)&U(0xfffffffeL,32 bits))=/=firstTarget)) {
      jalrDivergent := True
    }
  }

  switch(state) {
    is(0) {
      when(ctaActive) {
        when(active.orR) { iw:=selected; im:=wMask(selected); ip:=wPc(selected); state:=1 }
        .otherwise { ctaActive:=False }
      }
    }
    is(1) { inst:=imem.io.rdata; state:=2 }
    is(2) {
      when(badReg) { faultR:=True;ctaActive:=False;state:=0 }
      .elsewhen(isLoad||isStore) { mt:=0;state:=3 }
      .elsewhen(inst===U(0x00100073L,32 bits)) {
        when(wSp(iw)=/=0) {
          val index=(iw.resize(sb)*stackDepth+wSp(iw).resize(sb)-1).resize(sb)
          wSp(iw):=wSp(iw)-1;wPc(iw):=stackPc(index);wMask(iw):=stackMask(index)
        } otherwise { wValid(iw):=False }
        advanceWarp()
      }
      .elsewhen(opcode===U"7'b1100011") {
        when(!branchGood) {faultR:=True;ctaActive:=False}
        .elsewhen(takenMask.orR && fallMask.orR) {
          when(wSp(iw)===stackDepth) {faultR:=True;ctaActive:=False}
          .otherwise {
            val index=(iw.resize(sb)*stackDepth+wSp(iw).resize(sb)).resize(sb)
            stackPc(index):=ip+immB;stackMask(index):=takenMask;wSp(iw):=wSp(iw)+1
            wPc(iw):=ip+4;wMask(iw):=fallMask
          }
        }.elsewhen(takenMask.orR) {wPc(iw):=ip+immB;wMask(iw):=takenMask}
        .otherwise {wPc(iw):=ip+4;wMask(iw):=fallMask}
        advanceWarp()
      }
      .otherwise {
        val good = Bool(); good:=True
        when(opcode===U"7'b1100111" && jalrDivergent){good:=False}
        for(t <- 0 until threads) when(im(t)) {
          val c=ctx(U(t,tb bits));val b0=base(c)
          val a=regs((b0+rs1(3 downto 0)).resized);val bv=regs((b0+rs2(3 downto 0)).resized)
          val result=UInt(32 bits);result:=U(0,32 bits)
          switch(opcode) {
            is(U"7'b0110111"){result:=(inst(31 downto 12)##U(0,12 bits)).asUInt}
            is(U"7'b0010111"){result:=ip+(inst(31 downto 12)##U(0,12 bits)).asUInt}
            is(U"7'b0010011"){switch(f3){is(0){result:=a+immI};is(2){result:=Mux(a.asSInt<immI.asSInt,U(1,32 bits),U(0,32 bits))};is(3){result:=Mux(a<immI,U(1,32 bits),U(0,32 bits))};is(4){result:=a^immI};is(6){result:=a|immI};is(7){result:=a&immI};is(1){result:=a|<<inst(24 downto 20)};is(5){result:=Mux(inst(30),(a.asSInt>>inst(24 downto 20)).asUInt,a>>inst(24 downto 20))}}}
            is(U"7'b0110011"){switch(f3){is(0){result:=Mux(f7===U"7'b0100000",a-bv,a+bv)};is(1){result:=a|<<bv(4 downto 0)};is(2){result:=Mux(a.asSInt<bv.asSInt,U(1,32 bits),U(0,32 bits))};is(3){result:=Mux(a<bv,U(1,32 bits),U(0,32 bits))};is(4){result:=a^bv};is(5){result:=Mux(f7===U"7'b0100000",(a.asSInt>>bv(4 downto 0)).asUInt,a>>bv(4 downto 0))};is(6){result:=a|bv};is(7){result:=a&bv}}}
            is(U"7'b1101111"){result:=ip+4}
            is(U"7'b1100111"){result:=ip+4}
            is(U"7'b1110011"){
              when(f3=/=2||rs1=/=0){good:=False}
              switch(csr){
                is(U(0xf14)){result:=globalFor(c)};is(U(0xcc0)){result:=threadXFor(c)};is(U(0xcc1)){result:=threadYFor(c)};is(U(0xcc2)){result:=threadZFor(c)}
                is(U(0xcc3)){result:=blockIdx(0)};is(U(0xcc4)){result:=blockIdx(1)};is(U(0xcc5)){result:=blockIdx(2)}
                is(U(0xcc6)){result:=blockDim(0)};is(U(0xcc7)){result:=blockDim(1)};is(U(0xcc8)){result:=blockDim(2)}
                is(U(0xcc9)){result:=gridDim(0)};is(U(0xcca)){result:=gridDim(1)};is(U(0xccb)){result:=gridDim(2)}
                is(U(0xccc)){result:=argsAddr};is(U(0xccd)){result:=(U(coreId*warps*threads,32 bits)+(iw.resize(32)*U(threads,32 bits)).resize(32)+U(t,32 bits)).resize(32)}
                is(U(0xcce)){result:=iw.resize(32)};is(U(0xccf)){result:=U(t,32 bits)}
                default{good:=False}
              }
            }
            default{good:=False}
          }
          when(usesRd&&rd=/=0){regs((b0+rd(3 downto 0)).resized):=result};regs(b0):=U(0,32 bits)
        }
        when(!good){faultR:=True;ctaActive:=False}
        .otherwise {wPc(iw):=Mux(opcode===U"7'b1101111",ip+immJ,Mux(opcode===U"7'b1100111",firstTarget,ip+4))}
        advanceWarp()
      }
    }
    is(3) {
      val c=ctx(mt);val b0=base(c);val addr=regs((b0+rs1(3 downto 0)).resized)+Mux(isLoad,immI,immS)
      when(!laneActive(mt)){when(mt===threads-1){wPc(iw):=ip+4;advanceWarp()}otherwise{mt:=mt+1}}
      .otherwise {
        dmem.io.ren:=isLoad;dmem.io.wen:=isStore;dmem.io.addr:=addr&U(0xfffffffcL,32 bits)
        dmem.io.mask:=Mux(f3===0,(B(1,8 bits)|<<addr(1 downto 0)),Mux(f3===1,(B(3,8 bits)|<<addr(1 downto 0)),B(15,8 bits)))
        dmem.io.wdata:=regs((b0+rs2(3 downto 0)).resized)|<<(addr(1 downto 0)<<3)
        when(isLoad) { memReadData := dmem.io.rdata }
        state:=4
      }
    }
    is(4) {
      val c=ctx(mt);val b0=base(c);val addr=regs((b0+rs1(3 downto 0)).resized)+Mux(isLoad,immI,immS)
      val shifted=memReadData>>(addr(1 downto 0)<<3);val loaded=UInt(32 bits);loaded:=memReadData
      switch(f3){is(0){loaded:=shifted(7 downto 0).asSInt.resize(32).asUInt};is(1){loaded:=shifted(15 downto 0).asSInt.resize(32).asUInt};is(4){loaded:=shifted(7 downto 0).resize(32)};is(5){loaded:=shifted(15 downto 0).resize(32)}}
      when(isLoad&&rd=/=0){regs((b0+rd(3 downto 0)).resized):=loaded};regs(b0):=U(0,32 bits)
      when(mt===threads-1){wPc(iw):=ip+4;advanceWarp()}otherwise{mt:=mt+1;state:=3}
    }
  }
}
