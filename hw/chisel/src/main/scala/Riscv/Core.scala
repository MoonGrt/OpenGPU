package gpu.riscv

import chisel3._
import chisel3.util._
import gpu.perip.mem.{DataBus, InstBus, SimDataMem, SimInstMem}
import gpu.util.{DpiGpuStoreTraceBB, DpiGpuTraceBB}


/** A compact SIMT-style RV32E core.
  *
  * The core owns one shared fetch/decode/execute path and a parameterized set
  * of architectural contexts arranged as warps and threads. A warp is selected
  * round-robin. Threads in that warp which currently share the same PC form the
  * issue mask and share one instruction fetch. Per-thread PCs allow divergent
  * paths to make progress independently and naturally reconverge when their PCs
  * become equal again.
  *
  * Memory operations are serialized over the shared data port one active lane
  * at a time. This keeps the first implementation small while preserving the
  * WARPS/THREADS programming model and precise per-thread state.
  */
class Core(
  coreId: Int,
  numWarps: Int,
  numThreads: Int,
  resetPc: BigInt = 0x81000000L,
  enableGpuTrace: Boolean = true
) extends Module {
  private val DataWidth = 32
  require(numWarps >= 1 && isPow2(numWarps), "numWarps must be a power of two")
  require(numThreads >= 1 && isPow2(numThreads), "numThreads must be a power of two")
  require(resetPc >= 0 && resetPc < (BigInt(1) << DataWidth))

  private val WarpBits = math.max(1, log2Ceil(numWarps))
  private val ThreadBits = math.max(1, log2Ceil(numThreads))
  private val NumRegs = 16  // riscv32e
  private val NumContexts = numWarps * numThreads
  private val ContextBits = math.max(1, log2Ceil(NumContexts))
  private val GprIndexBits = math.max(1, log2Ceil(NumContexts * NumRegs))

  val io = IO(new Bundle {
    val enable = Input(Bool())
    val ibus = new InstBus
    val dbus = new DataBus
    val done = Output(Bool())
    val busy = Output(Bool())
    val activeWarps = Output(UInt(numWarps.W))
    val issueWarp = Output(UInt(WarpBits.W))
    val issueMask = Output(UInt(numThreads.W))
  })

  // Keep the arrays flat. Besides producing simpler RAM-like RTL, scalar
  // dynamic indexing avoids packed-array muxes rejected by the Verilog
  // lowering options used by MEMU.
  val pcs = RegInit(VecInit(Seq.fill(NumContexts)(resetPc.U(DataWidth.W))))
  val halted = RegInit(VecInit(Seq.fill(NumContexts)(false.B)))
  val gprs = RegInit(VecInit(
    Seq.fill(NumContexts * NumRegs)(0.U(DataWidth.W))))

  val warpActive = Wire(Vec(numWarps, Bool()))
  for (w <- 0 until numWarps) {
    warpActive(w) := !VecInit((0 until numThreads).map { t =>
      halted(w * numThreads + t)
    }).asUInt.andR
  }
  io.activeWarps := warpActive.asUInt
  io.done := halted.asUInt.andR
  io.busy := !io.done

  private val sSchedule :: sFetchRsp :: sExecute :: sMemReq :: sMemRsp :: Nil =
    Enum(5)
  val state = RegInit(sSchedule)
  val rrWarp = RegInit(0.U(WarpBits.W))
  val issueWarp = Reg(UInt(WarpBits.W))
  val issueMask = Reg(UInt(numThreads.W))
  val issuePc = Reg(UInt(DataWidth.W))
  val issueInst = Reg(UInt(DataWidth.W))
  val memThread = RegInit(0.U(ThreadBits.W))

  io.issueWarp := issueWarp
  io.issueMask := issueMask
  dontTouch(state)
  dontTouch(rrWarp)
  dontTouch(pcs)
  dontTouch(halted)

  // Round-robin warp selection. Duplicating and shifting the active mask makes
  // bit zero correspond to rrWarp while retaining a cheap priority encoder.
  val activeBits = warpActive.asUInt
  val rotatedActive = (Cat(activeBits, activeBits) >> rrWarp)(numWarps - 1, 0)
  val selectedOffset = PriorityEncoder(rotatedActive)
  val selectedWarp = (rrWarp + selectedOffset)(WarpBits - 1, 0)
  val selectedHalted = VecInit((0 until numThreads).map { t =>
    if (NumContexts == 1) {
      halted(0)
    } else {
      val context = (selectedWarp * numThreads.U + t.U)(ContextBits - 1, 0)
      halted(context)
    }
  }).asUInt
  val selectedThread =
    if (numThreads == 1) 0.U(ThreadBits.W) else PriorityEncoder(~selectedHalted)
  val selectedContext =
    (selectedWarp * numThreads.U + selectedThread)(ContextBits - 1, 0)
  val selectedPc = if (NumContexts == 1) pcs(0) else pcs(selectedContext)
  val selectedMask = Wire(UInt(numThreads.W))
  selectedMask := VecInit((0 until numThreads).map { t =>
    if (NumContexts == 1) {
      !halted(0) && pcs(0) === selectedPc
    } else {
      val context = (selectedWarp * numThreads.U + t.U)(ContextBits - 1, 0)
      !halted(context) && pcs(context) === selectedPc
    }
  }).asUInt

  io.ibus.req.valid := io.enable && state === sSchedule && activeBits.orR
  io.ibus.req.bits.addr := selectedPc
  io.ibus.resp.ready := state === sFetchRsp

  io.dbus.req.valid := false.B
  io.dbus.req.bits := 0.U.asTypeOf(io.dbus.req.bits)
  io.dbus.resp.ready := state === sMemRsp

  when(state === sSchedule && io.ibus.req.fire) {
    issueWarp := selectedWarp
    issueMask := selectedMask
    issuePc := selectedPc
    state := sFetchRsp
  }
  when(state === sFetchRsp && io.ibus.resp.fire) {
    issueInst := io.ibus.resp.bits.data
    state := sExecute
  }

  val opcode = issueInst(6, 0)
  val rd = issueInst(11, 7)
  val funct3 = issueInst(14, 12)
  val rs1 = issueInst(19, 15)
  val rs2 = issueInst(24, 20)
  val funct7 = issueInst(31, 25)

  val isLoad = opcode === "b0000011".U
  val isStore = opcode === "b0100011".U
  val isMemory = isLoad || isStore
  val isEbreak = issueInst === "h00100073".U
  val isMhartid = opcode === "b1110011".U && funct3 === 2.U &&
    rs1 === 0.U && issueInst(31, 20) === "hf14".U

  def sext(value: UInt, width: Int): UInt =
    Cat(Fill(DataWidth - width, value(width - 1)), value(width - 1, 0))

  val immI = sext(issueInst(31, 20), 12)
  val immS = sext(Cat(issueInst(31, 25), issueInst(11, 7)), 12)
  val immB = Cat(
    Fill(19, issueInst(31)), issueInst(31), issueInst(7),
    issueInst(30, 25), issueInst(11, 8), 0.U(1.W))
  val immU = Cat(issueInst(31, 12), 0.U(12.W))
  val immJ = Cat(
    Fill(11, issueInst(31)), issueInst(31), issueInst(19, 12),
    issueInst(20), issueInst(30, 21), 0.U(1.W))

  val usesRd = Seq(
    "b0110111".U, "b0010111".U, "b0010011".U, "b0110011".U,
    "b0000011".U, "b1101111".U, "b1100111".U, "b1110011".U
  ).map(opcode === _).reduce(_ || _)
  val usesRs1 = Seq(
    "b0010011".U, "b0110011".U, "b0000011".U, "b0100011".U,
    "b1100011".U, "b1100111".U
  ).map(opcode === _).reduce(_ || _)
  val usesRs2 = Seq(
    "b0110011".U, "b0100011".U, "b1100011".U
  ).map(opcode === _).reduce(_ || _)
  val badRegister = (usesRd && rd(4)) || (usesRs1 && rs1(4)) ||
    (usesRs2 && rs2(4))

  def nextWarp(): Unit = {
    if (numWarps == 1) {
      rrWarp := 0.U
    } else {
      rrWarp := Mux(issueWarp === (numWarps - 1).U, 0.U, issueWarp + 1.U)
    }
    state := sSchedule
  }

  // Shared ALU/decode, applied to every lane in the current issue mask.
  when(state === sExecute) {
    when(isMemory && !badRegister) {
      memThread := 0.U
      state := sMemReq
    }.otherwise {
      for (t <- 0 until numThreads) {
        when(issueMask(t)) {
          val context =
            (issueWarp * numThreads.U + t.U)(ContextBits - 1, 0)
          val regBase =
            (context * NumRegs.U)(GprIndexBits - 1, 0)
          val rs1Index =
            (regBase + rs1(3, 0))(GprIndexBits - 1, 0)
          val rs2Index =
            (regBase + rs2(3, 0))(GprIndexBits - 1, 0)
          val a = Mux(usesRs1, gprs(rs1Index), 0.U)
          val b = Mux(usesRs2, gprs(rs2Index), 0.U)
          val result = WireDefault(0.U(DataWidth.W))
          val nextPc = WireDefault(issuePc + 4.U)
          val writeRd = WireDefault(usesRd)
          val supported = WireDefault(false.B)

          switch(opcode) {
            is("b0110111".U) {
              supported := true.B
              result := immU
            } // LUI
            is("b0010111".U) {
              supported := true.B
              result := issuePc + immU
            } // AUIPC
            is("b0010011".U) { // OP-IMM
              supported := true.B
              switch(funct3) {
                is(0.U) { result := a + immI }
                is(2.U) { result := (a.asSInt < immI.asSInt).asUInt }
                is(3.U) { result := a < immI }
                is(4.U) { result := a ^ immI }
                is(6.U) { result := a | immI }
                is(7.U) { result := a & immI }
                is(1.U) { result := a << issueInst(24, 20) }
                is(5.U) {
                  result := Mux(issueInst(30),
                    (a.asSInt >> issueInst(24, 20)).asUInt,
                    a >> issueInst(24, 20))
                }
              }
            }
            is("b0110011".U) { // OP
              supported := true.B
              switch(funct3) {
                is(0.U) { result := Mux(funct7 === "b0100000".U, a - b, a + b) }
                is(1.U) { result := a << b(4, 0) }
                is(2.U) { result := (a.asSInt < b.asSInt).asUInt }
                is(3.U) { result := a < b }
                is(4.U) { result := a ^ b }
                is(5.U) {
                  result := Mux(funct7 === "b0100000".U,
                    (a.asSInt >> b(4, 0)).asUInt, a >> b(4, 0))
                }
                is(6.U) { result := a | b }
                is(7.U) { result := a & b }
              }
            }
            is("b1100011".U) { // BRANCH
              supported := true.B
              writeRd := false.B
              val take = WireDefault(false.B)
              switch(funct3) {
                is(0.U) { take := a === b }
                is(1.U) { take := a =/= b }
                is(4.U) { take := a.asSInt < b.asSInt }
                is(5.U) { take := a.asSInt >= b.asSInt }
                is(6.U) { take := a < b }
                is(7.U) { take := a >= b }
              }
              nextPc := Mux(take, issuePc + immB, issuePc + 4.U)
            }
            is("b1101111".U) { // JAL
              supported := true.B
              result := issuePc + 4.U
              nextPc := issuePc + immJ
            }
            is("b1100111".U) { // JALR
              supported := true.B
              result := issuePc + 4.U
              nextPc := (a + immI) & "hfffffffe".U
            }
            is("b1110011".U) {
              when(isEbreak) {
                supported := true.B
                writeRd := false.B
                if (NumContexts == 1) halted(0) := true.B
                else halted(context) := true.B
              }.elsewhen(isMhartid) {
                supported := true.B
                result := ((coreId * numWarps * numThreads) +
                  (t + 0)).U + issueWarp * numThreads.U
              }
            }
          }

          when(badRegister || !supported) {
            if (NumContexts == 1) halted(0) := true.B
            else halted(context) := true.B
          }.elsewhen(!isEbreak) {
            if (NumContexts == 1) pcs(0) := nextPc
            else pcs(context) := nextPc
            when(writeRd && rd =/= 0.U) {
              val rdIndex =
                (regBase + rd(3, 0))(GprIndexBits - 1, 0)
              gprs(rdIndex) := result
            }
          }
          gprs(regBase) := 0.U
        }
      }
      nextWarp()
    }
  }

  val memContext =
    (issueWarp * numThreads.U + memThread)(ContextBits - 1, 0)
  val memRegBase =
    (memContext * NumRegs.U)(GprIndexBits - 1, 0)
  val memRs1Index =
    (memRegBase + rs1(3, 0))(GprIndexBits - 1, 0)
  val memRs2Index =
    (memRegBase + rs2(3, 0))(GprIndexBits - 1, 0)
  val memRdIndex =
    (memRegBase + rd(3, 0))(GprIndexBits - 1, 0)
  val memA = gprs(memRs1Index)
  val memB = gprs(memRs2Index)
  val memAddr = memA + Mux(isLoad, immI, immS)
  val memOffset = memAddr(1, 0)
  val memSize = MuxLookup(funct3, 2.U)(Seq(
    0.U -> 0.U, 1.U -> 1.U, 2.U -> 2.U,
    4.U -> 0.U, 5.U -> 1.U))
  val storeMask = MuxLookup(funct3, 0.U(4.W))(Seq(
    0.U -> (1.U(4.W) << memOffset),
    1.U -> (3.U(4.W) << memOffset),
    2.U -> "b1111".U))

  def advanceMemoryThread(): Unit = {
    if (numThreads == 1) {
      nextWarp()
    } else {
      when(memThread === (numThreads - 1).U) {
        nextWarp()
      }.otherwise {
        memThread := memThread + 1.U
        state := sMemReq
      }
    }
  }

  when(state === sMemReq) {
    val memoryLaneActive =
      if (numThreads == 1) issueMask(0) else issueMask(memThread)
    when(!memoryLaneActive) {
      advanceMemoryThread()
    }.otherwise {
      io.dbus.req.valid := io.enable
      io.dbus.req.bits.ren := isLoad
      io.dbus.req.bits.wen := isStore
      io.dbus.req.bits.size := memSize
      io.dbus.req.bits.mask := storeMask
      io.dbus.req.bits.addr := Cat(memAddr(31, 2), 0.U(2.W))
      io.dbus.req.bits.wdata := memB << (memOffset << 3)
      when(io.dbus.req.fire) {
        state := sMemRsp
      }
    }
  }

  when(state === sMemRsp && io.dbus.resp.fire) {
    val shifted = io.dbus.resp.bits.rdata >> (memOffset << 3)
    val loadData = MuxLookup(funct3, io.dbus.resp.bits.rdata)(Seq(
      0.U -> Cat(Fill(24, shifted(7)), shifted(7, 0)),
      1.U -> Cat(Fill(16, shifted(15)), shifted(15, 0)),
      2.U -> io.dbus.resp.bits.rdata,
      4.U -> Cat(0.U(24.W), shifted(7, 0)),
      5.U -> Cat(0.U(16.W), shifted(15, 0))))
    when(isLoad && rd =/= 0.U) {
      gprs(memRdIndex) := loadData
    }
    gprs(memRegBase) := 0.U
    if (NumContexts == 1) pcs(0) := issuePc + 4.U
    else pcs(memContext) := issuePc + 4.U
    advanceMemoryThread()
  }

  if (enableGpuTrace) {
    for (w <- 0 until numWarps; t <- 0 until numThreads) {
      val trace = Module(new DpiGpuTraceBB)
      val nonMemCommit = state === sExecute && !isMemory &&
        issueWarp === w.U && issueMask(t)
      val memCommit = state === sMemRsp && io.dbus.resp.fire &&
        issueWarp === w.U && memThread === t.U
      trace.io.clk := clock
      trace.io.en := !reset.asBool && (nonMemCommit || memCommit)
      trace.io.hartid := (coreId * numWarps * numThreads + w * numThreads + t).U
      trace.io.pc := issuePc
      trace.io.inst := issueInst
    }

    val storeTrace = Module(new DpiGpuStoreTraceBB)
    storeTrace.io.clk := clock
    storeTrace.io.en := state === sMemReq && io.dbus.req.fire && isStore
    storeTrace.io.hartid :=
      (coreId * numWarps * numThreads).U + issueWarp * numThreads.U + memThread
    storeTrace.io.addr := memAddr
    storeTrace.io.mask := storeMask
    storeTrace.io.data := memB
  }
}
