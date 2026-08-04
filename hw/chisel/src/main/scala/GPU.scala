import chisel3._
import chisel3.util.{log2Ceil, isPow2}
import gpu.Kmu
import gpu.riscv.GPUCore
import gpu.perip.mem.{SimDataMem, SimInstMem}

class TOP(numCores: Int, numWarps: Int, numThreads: Int) extends Module {
  override def desiredName: String = "GPUTop"
  require(numCores >= 1 && numCores <= 16)
  require(numWarps >= 1 && isPow2(numWarps))
  require(numThreads >= 1 && isPow2(numThreads))
  private val warpBits = math.max(1, log2Ceil(numWarps))

  val io = IO(new Bundle {
    val dcr_valid = Input(Bool())
    val dcr_addr = Input(UInt(12.W))
    val dcr_data = Input(UInt(32.W))
    val gpu_launch = Input(Bool())
    val gpu_busy = Output(Bool())
    val gpu_fault = Output(Bool())
    val gpu_done = Output(UInt(numCores.W))
    val gpu_active_warps = Output(Vec(numCores, UInt(numWarps.W)))
    val gpu_issue_warp = Output(Vec(numCores, UInt(warpBits.W)))
    val gpu_issue_mask = Output(Vec(numCores, UInt(numThreads.W)))
  })

  val kmu = Module(new Kmu(numCores))
  kmu.io.dcr.valid := io.dcr_valid
  kmu.io.dcr.addr := io.dcr_addr
  kmu.io.dcr.data := io.dcr_data
  kmu.io.launch := io.gpu_launch

  val cores = (0 until numCores).map { coreId =>
    val core = Module(new GPUCore(coreId, numWarps, numThreads))
    val imem = Module(new SimInstMem)
    val dmem = Module(new SimDataMem)
    core.io.cta.valid := kmu.io.ctaValid(coreId)
    core.io.cta.bits := kmu.io.cta
    imem.io <> core.io.ibus
    dmem.io <> core.io.dbus
    io.gpu_active_warps(coreId) := core.io.activeWarps
    io.gpu_issue_warp(coreId) := core.io.issueWarp
    io.gpu_issue_mask(coreId) := core.io.issueMask
    core
  }
  kmu.io.coreReady := VecInit(cores.map(_.io.cta.ready)).asUInt
  kmu.io.coreBusy := VecInit(cores.map(_.io.busy)).asUInt
  io.gpu_busy := kmu.io.busy
  io.gpu_fault := VecInit(cores.map(_.io.fault)).asUInt.orR
  io.gpu_done := VecInit(cores.map(_.io.done)).asUInt
}

object TOP extends App {
  val options = Array("--lowering-options=" + List(
    "disallowLocalVariables", "disallowPackedArrays",
    "locationInfoStyle=wrapInAtSquareBracket").mkString(","))
  val cores = sys.env.get("GPU_NUM_CORES").map(_.toInt).getOrElse(2)
  val warps = sys.env.get("GPU_NUM_WARPS").map(_.toInt).getOrElse(4)
  val threads = sys.env.get("GPU_NUM_THREADS").map(_.toInt).getOrElse(4)
  circt.stage.ChiselStage.emitSystemVerilogFile(
    new TOP(cores, warps, threads), args, options)
}
