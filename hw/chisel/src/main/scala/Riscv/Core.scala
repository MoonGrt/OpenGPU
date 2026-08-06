package gpu.riscv

import chisel3._
import chisel3.util._
import gpu.interfaces.CtaRequest
import gpu.perip.mem.{DataBus, InstBus}
import gpu.util.DpiGpuStateBB

class GPUCore(coreId: Int, numWarps: Int, numThreads: Int, difftest: Boolean) extends Module {
  private val DataWidth = 32
  private val NumRegs = 16
  private val NumContexts = numWarps * numThreads
  private val WarpBits = math.max(1, log2Ceil(numWarps))
  private val ThreadBits = math.max(1, log2Ceil(numThreads))
  private val ContextBits = math.max(1, log2Ceil(NumContexts))
  private val RegBits = math.max(1, log2Ceil(NumContexts * NumRegs))
  private val StackDepth = 8
  private val StackBits = 4
  private val StackIndexBits = math.max(1, log2Ceil(numWarps * StackDepth))
  private val DiffWords = 28 + 20 * numWarps + 20 * NumContexts

  val io = IO(new Bundle {
    val cta = Flipped(Decoupled(new CtaRequest))
    val ibus = new InstBus
    val dbus = new DataBus
    val busy = Output(Bool())
    val fault = Output(Bool())
    val done = Output(Bool())
    val activeWarps = Output(UInt(numWarps.W))
    val issueWarp = Output(UInt(WarpBits.W))
    val issueMask = Output(UInt(numThreads.W))
  })

  val warpPc = RegInit(VecInit(Seq.fill(numWarps)(0.U(32.W))))
  val warpMask = RegInit(VecInit(Seq.fill(numWarps)(0.U(numThreads.W))))
  val warpValid = RegInit(VecInit(Seq.fill(numWarps)(false.B)))
  val stackSp = RegInit(VecInit(Seq.fill(numWarps)(0.U(StackBits.W))))
  val stackPc = RegInit(VecInit(Seq.fill(numWarps * StackDepth)(0.U(32.W))))
  val stackMask = RegInit(VecInit(Seq.fill(numWarps * StackDepth)(0.U(numThreads.W))))
  val gprs = RegInit(VecInit(Seq.fill(NumContexts * NumRegs)(0.U(32.W))))
  val threadX = RegInit(VecInit(Seq.fill(NumContexts)(0.U(32.W))))
  val threadY = RegInit(VecInit(Seq.fill(NumContexts)(0.U(32.W))))
  val threadZ = RegInit(VecInit(Seq.fill(NumContexts)(0.U(32.W))))
  val globalId = RegInit(VecInit(Seq.fill(NumContexts)(0.U(32.W))))
  val blockIdx = RegInit(VecInit(Seq.fill(3)(0.U(32.W))))
  val blockDim = RegInit(VecInit(Seq.fill(3)(1.U(32.W))))
  val gridDim = RegInit(VecInit(Seq.fill(3)(1.U(32.W))))
  val argsAddr = RegInit(0.U(32.W))

  val ctaActive = RegInit(false.B)
  val fault = RegInit(false.B)
  io.busy := ctaActive
  io.done := !ctaActive
  io.fault := fault
  io.cta.ready := !ctaActive
  io.activeWarps := warpValid.asUInt

  val sSchedule :: sFetch :: sExecute :: sMemReq :: sMemResp :: Nil = Enum(5)
  val state = RegInit(sSchedule)
  val rrWarp = RegInit(0.U(WarpBits.W))
  val issueWarp = RegInit(0.U(WarpBits.W))
  val issueMask = RegInit(0.U(numThreads.W))
  val issuePc = RegInit(0.U(32.W))
  val issueInst = RegInit(0.U(32.W))
  val memThread = RegInit(0.U(ThreadBits.W))
  io.issueWarp := issueWarp
  io.issueMask := issueMask

  when(io.cta.fire) {
    ctaActive := true.B
    fault := false.B
    state := sSchedule
    rrWarp := 0.U
    blockIdx := io.cta.bits.blockIdx
    blockDim := io.cta.bits.blockDim
    gridDim := io.cta.bits.gridDim
    argsAddr := io.cta.bits.argsAddr
    val blockLinear = ((io.cta.bits.blockIdx(2) * io.cta.bits.gridDim(1)) +
      io.cta.bits.blockIdx(1)) * io.cta.bits.gridDim(0) + io.cta.bits.blockIdx(0)
    for (w <- 0 until numWarps) {
      warpPc(w) := io.cta.bits.startupPc
      stackSp(w) := 0.U
      warpMask(w) := VecInit((0 until numThreads).map(t =>
        (w * numThreads + t).U < io.cta.bits.blockSize)).asUInt
      warpValid(w) := (w * numThreads).U < io.cta.bits.blockSize
    }
    for (i <- 0 until NumContexts) {
      threadX(i) := i.U % io.cta.bits.blockDim(0)
      threadY(i) := (i.U / io.cta.bits.blockDim(0)) % io.cta.bits.blockDim(1)
      threadZ(i) := i.U / (io.cta.bits.blockDim(0) * io.cta.bits.blockDim(1))
      globalId(i) := blockLinear * io.cta.bits.blockSize + i.U
    }
    gprs.foreach(_ := 0.U)
  }

  val activeBits = warpValid.asUInt
  val rotated = (Cat(activeBits, activeBits) >> rrWarp)(numWarps - 1, 0)
  val selectedWarp = (rrWarp + PriorityEncoder(rotated))(WarpBits - 1, 0)

  io.ibus.req.valid := ctaActive && state === sSchedule && activeBits.orR
  io.ibus.req.bits.addr := warpPc(selectedWarp)
  io.ibus.resp.ready := state === sFetch
  io.dbus.req.valid := false.B
  io.dbus.req.bits := 0.U.asTypeOf(io.dbus.req.bits)
  io.dbus.resp.ready := state === sMemResp

  when(ctaActive && state === sSchedule) {
    when(activeBits.orR) {
      when(io.ibus.req.fire) {
        issueWarp := selectedWarp
        issueMask := warpMask(selectedWarp)
        issuePc := warpPc(selectedWarp)
        state := sFetch
      }
    }.otherwise { ctaActive := false.B }
  }
  when(state === sFetch && io.ibus.resp.fire) {
    issueInst := io.ibus.resp.bits.data
    state := sExecute
  }

  val opcode = issueInst(6, 0)
  val rd = issueInst(11, 7)
  val funct3 = issueInst(14, 12)
  val rs1 = issueInst(19, 15)
  val rs2 = issueInst(24, 20)
  val funct7 = issueInst(31, 25)
  val csr = issueInst(31, 20)
  def sext(value: UInt, width: Int): UInt =
    Cat(Fill(32 - width, value(width - 1)), value(width - 1, 0))
  val immI = sext(issueInst(31, 20), 12)
  val immS = sext(Cat(issueInst(31, 25), issueInst(11, 7)), 12)
  val immB = Cat(Fill(19, issueInst(31)), issueInst(31), issueInst(7),
    issueInst(30, 25), issueInst(11, 8), 0.U(1.W))
  val immU = Cat(issueInst(31, 12), 0.U(12.W))
  val immJ = Cat(Fill(11, issueInst(31)), issueInst(31), issueInst(19, 12),
    issueInst(20), issueInst(30, 21), 0.U(1.W))
  val isLoad = opcode === "b0000011".U
  val isStore = opcode === "b0100011".U
  val isBranch = opcode === "b1100011".U
  val isEbreak = issueInst === "h00100073".U
  val usesRd = Seq(0x37,0x17,0x13,0x33,0x03,0x6f,0x67,0x73)
    .map(x => opcode === x.U).reduce(_ || _)
  val usesRs1 = Seq(0x13,0x33,0x03,0x23,0x63,0x67)
    .map(x => opcode === x.U).reduce(_ || _)
  val usesRs2 = Seq(0x33,0x23,0x63).map(x => opcode === x.U).reduce(_ || _)
  val badReg = (usesRd && rd(4)) || (usesRs1 && rs1(4)) || (usesRs2 && rs2(4))

  def nextWarp(): Unit = {
    rrWarp := Mux(issueWarp === (numWarps - 1).U, 0.U, issueWarp + 1.U)
    state := sSchedule
  }
  def context(t: Int): UInt =
    (issueWarp * numThreads.U + t.U)(ContextBits - 1, 0)
  def regBase(c: UInt): UInt = (c * NumRegs.U)(RegBits - 1, 0)

  val branchTaken = Wire(Vec(numThreads, Bool()))
  for (t <- 0 until numThreads) {
    val c = context(t); val base = regBase(c)
    val a = gprs((base + rs1(3,0))(RegBits-1,0))
    val b = gprs((base + rs2(3,0))(RegBits-1,0))
    branchTaken(t) := MuxLookup(funct3, false.B)(Seq(
      0.U -> (a === b), 1.U -> (a =/= b), 4.U -> (a.asSInt < b.asSInt),
      5.U -> (a.asSInt >= b.asSInt), 6.U -> (a < b), 7.U -> (a >= b)))
  }
  val takenMask = branchTaken.asUInt & issueMask
  val fallMask = ~branchTaken.asUInt & issueMask
  val branchSupported = Seq(0,1,4,5,6,7).map(funct3 === _.U).reduce(_ || _)

  val firstLane = PriorityEncoder(issueMask)
  val firstContext = (issueWarp * numThreads.U + firstLane)(ContextBits-1,0)
  val firstBase = regBase(firstContext)
  val firstTarget = (gprs((firstBase + rs1(3,0))(RegBits-1,0)) + immI) & "hfffffffe".U
  val jalrDivergent = VecInit((0 until numThreads).map { t =>
    val c = context(t); val base = regBase(c)
    issueMask(t) && (((gprs((base + rs1(3,0))(RegBits-1,0)) + immI) &
      "hfffffffe".U) =/= firstTarget)
  }).asUInt.orR

  val csrSupported = Seq(0xf14,0xcc0,0xcc1,0xcc2,0xcc3,0xcc4,0xcc5,
    0xcc6,0xcc7,0xcc8,0xcc9,0xcca,0xccb,0xccc,0xccd,0xcce,0xccf)
    .map(csr === _.U).reduce(_ || _)
  val opcodeSupported = Seq(0x37,0x17,0x13,0x33,0x6f,0x67)
    .map(opcode === _.U).reduce(_ || _) ||
    (opcode === 0x73.U && funct3 === 2.U && rs1 === 0.U && csrSupported)

  when(state === sExecute) {
    when(badReg) { fault := true.B; ctaActive := false.B; state := sSchedule }
    .elsewhen(isLoad || isStore) { memThread := 0.U; state := sMemReq }
    .elsewhen(isEbreak) {
      when(stackSp(issueWarp) =/= 0.U) {
        val index = (issueWarp * StackDepth.U + stackSp(issueWarp) - 1.U)(
          StackIndexBits - 1, 0)
        stackSp(issueWarp) := stackSp(issueWarp) - 1.U
        warpPc(issueWarp) := stackPc(index)
        warpMask(issueWarp) := stackMask(index)
      }.otherwise { warpValid(issueWarp) := false.B }
      nextWarp()
    }.elsewhen(isBranch) {
      when(!branchSupported) { fault := true.B; ctaActive := false.B }
      .elsewhen(takenMask.orR && fallMask.orR) {
        when(stackSp(issueWarp) === StackDepth.U) {
          fault := true.B; ctaActive := false.B
        }.otherwise {
          val index = (issueWarp * StackDepth.U + stackSp(issueWarp))(
            StackIndexBits - 1, 0)
          stackPc(index) := issuePc + immB
          stackMask(index) := takenMask
          stackSp(issueWarp) := stackSp(issueWarp) + 1.U
          warpPc(issueWarp) := issuePc + 4.U
          warpMask(issueWarp) := fallMask
        }
      }.elsewhen(takenMask.orR) {
        warpPc(issueWarp) := issuePc + immB; warpMask(issueWarp) := takenMask
      }.otherwise {
        warpPc(issueWarp) := issuePc + 4.U; warpMask(issueWarp) := fallMask
      }
      nextWarp()
    }.otherwise {
      for (t <- 0 until numThreads) when(issueMask(t)) {
        val c = context(t); val base = regBase(c)
        val a = gprs((base + rs1(3,0))(RegBits-1,0))
        val b = gprs((base + rs2(3,0))(RegBits-1,0))
        val aluImm = MuxLookup(funct3, 0.U)(Seq(
          0.U -> (a + immI), 2.U -> (a.asSInt < immI.asSInt).asUInt,
          3.U -> (a < immI), 4.U -> (a ^ immI), 6.U -> (a | immI),
          7.U -> (a & immI), 1.U -> (a << issueInst(24,20)),
          5.U -> Mux(issueInst(30), (a.asSInt >> issueInst(24,20)).asUInt,
            a >> issueInst(24,20))))
        val aluReg = MuxLookup(funct3, 0.U)(Seq(
          0.U -> Mux(funct7 === "b0100000".U, a-b, a+b), 1.U -> (a << b(4,0)),
          2.U -> (a.asSInt < b.asSInt).asUInt, 3.U -> (a < b), 4.U -> (a ^ b),
          5.U -> Mux(funct7 === "b0100000".U,(a.asSInt >> b(4,0)).asUInt,a >> b(4,0)),
          6.U -> (a | b), 7.U -> (a & b)))
        val csrValue = MuxLookup(csr, 0.U)(Seq(
          "hf14".U -> globalId(c), "hcc0".U -> threadX(c), "hcc1".U -> threadY(c),
          "hcc2".U -> threadZ(c), "hcc3".U -> blockIdx(0), "hcc4".U -> blockIdx(1),
          "hcc5".U -> blockIdx(2), "hcc6".U -> blockDim(0), "hcc7".U -> blockDim(1),
          "hcc8".U -> blockDim(2), "hcc9".U -> gridDim(0), "hcca".U -> gridDim(1),
          "hccb".U -> gridDim(2), "hccc".U -> argsAddr,
          "hccd".U -> ((coreId*numWarps*numThreads).U +
            (issueWarp * numThreads.U) + t.U),
          "hcce".U -> issueWarp, "hccf".U -> t.U))
        val result = MuxLookup(opcode, 0.U)(Seq(
          "b0110111".U -> immU, "b0010111".U -> (issuePc+immU),
          "b0010011".U -> aluImm, "b0110011".U -> aluReg,
          "b1101111".U -> (issuePc+4.U), "b1100111".U -> (issuePc+4.U),
          "b1110011".U -> csrValue))
        when(usesRd && rd =/= 0.U) { gprs((base + rd(3,0))(RegBits-1,0)) := result }
        gprs(base) := 0.U
      }
      when(!opcodeSupported || (opcode === "b1100111".U && jalrDivergent)) {
        fault := true.B; ctaActive := false.B
      }.otherwise {
        warpPc(issueWarp) := MuxLookup(opcode, issuePc + 4.U)(Seq(
          "b1101111".U -> (issuePc + immJ), "b1100111".U -> firstTarget))
      }
      nextWarp()
    }
  }

  val memContext = (issueWarp * numThreads.U + memThread)(ContextBits-1,0)
  val memBase = regBase(memContext)
  val memAddr = gprs((memBase+rs1(3,0))(RegBits-1,0)) + Mux(isLoad,immI,immS)
  val memOffset = memAddr(1,0)
  io.dbus.req.bits.ren := isLoad
  io.dbus.req.bits.wen := isStore
  io.dbus.req.bits.size := MuxLookup(funct3,2.U)(Seq(0.U->0.U,1.U->1.U,2.U->2.U,4.U->0.U,5.U->1.U))
  io.dbus.req.bits.mask := MuxLookup(funct3,0.U(4.W))(Seq(
    0.U->(1.U(4.W)<<memOffset),1.U->(3.U(4.W)<<memOffset),2.U->"b1111".U))
  io.dbus.req.bits.addr := Cat(memAddr(31,2),0.U(2.W))
  io.dbus.req.bits.wdata := gprs((memBase+rs2(3,0))(RegBits-1,0)) << (memOffset<<3)

  def advanceMem(): Unit = {
    when(memThread === (numThreads-1).U) {
      warpPc(issueWarp) := issuePc+4.U; nextWarp()
    }.otherwise { memThread := memThread+1.U; state := sMemReq }
  }
  when(state === sMemReq) {
    when(!issueMask(memThread)) { advanceMem() }
    .otherwise { io.dbus.req.valid := true.B; when(io.dbus.req.fire){state:=sMemResp} }
  }
  when(state === sMemResp && io.dbus.resp.fire) {
    val shifted = io.dbus.resp.bits.rdata >> (memOffset<<3)
    val loaded = MuxLookup(funct3,0.U)(Seq(
      0.U->Cat(Fill(24,shifted(7)),shifted(7,0)),
      1.U->Cat(Fill(16,shifted(15)),shifted(15,0)),2.U->io.dbus.resp.bits.rdata,
      4.U->Cat(0.U(24.W),shifted(7,0)),5.U->Cat(0.U(16.W),shifted(15,0))))
    when(isLoad && rd=/=0.U){gprs((memBase+rd(3,0))(RegBits-1,0)):=loaded}
    gprs(memBase):=0.U
    advanceMem()
  }

  if (difftest) {
    val diff = Wire(Vec(DiffWords, UInt(32.W)))
    diff.foreach(_ := 0.U)
    diff(0) := state
    diff(1) := ctaActive
    diff(2) := fault
    diff(3) := rrWarp
    diff(4) := issueWarp
    diff(5) := issueMask
    diff(6) := issuePc
    diff(7) := issueInst
    diff(8) := memThread
    for (axis <- 0 until 3) {
      diff(9 + axis) := blockIdx(axis)
      diff(12 + axis) := blockDim(axis)
      diff(15 + axis) := gridDim(axis)
    }
    diff(18) := argsAddr
    diff(19) := warpValid.asUInt
    diff(20) := state === sFetch
    diff(21) := issuePc
    val diffLaneActive = issueMask(memThread)
    when((state === sMemReq || state === sMemResp) && diffLaneActive) {
      diff(22) := isLoad && state === sMemReq
      diff(23) := isStore && state === sMemReq
      diff(24) := io.dbus.req.bits.mask
      diff(25) := io.dbus.req.bits.addr
      diff(26) := io.dbus.req.bits.wdata
      when(isLoad && state === sMemResp) {
        diff(27) := io.dbus.resp.bits.rdata
      }
    }
    for (warp <- 0 until numWarps) {
      val base = 28 + warp * 20
      diff(base) := warpValid(warp)
      diff(base + 1) := warpPc(warp)
      diff(base + 2) := warpMask(warp)
      diff(base + 3) := stackSp(warp)
      for (entry <- 0 until StackDepth) {
        diff(base + 4 + entry * 2) := stackPc(warp * StackDepth + entry)
        diff(base + 5 + entry * 2) := stackMask(warp * StackDepth + entry)
      }
    }
    for (context <- 0 until NumContexts) {
      val base = 28 + 20 * numWarps + context * 20
      diff(base) := threadX(context)
      diff(base + 1) := threadY(context)
      diff(base + 2) := threadZ(context)
      diff(base + 3) := globalId(context)
      for (register <- 0 until NumRegs)
        diff(base + 4 + register) := gprs(context * NumRegs + register)
    }
    val diffBridge = Module(new DpiGpuStateBB(DiffWords, 23 + coreId * DiffWords))
    diffBridge.io.state := diff.asUInt
  }
}
