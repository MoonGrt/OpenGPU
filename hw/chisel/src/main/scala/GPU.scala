import chisel3._
import chisel3.util.{log2Ceil, isPow2}
import gpu.riscv.Core
import gpu.perip.mem.{SimDataMem, SimInstMem}


/** Simulation top containing only multithreaded GPU cores and shared PMEM.
  *
  * Every core has an independent simulation memory port backed by the same
  * host PMEM. There is no scalar host CPU in this design.
  */
class TOP(
  numGpuCores: Int,
  numWarps: Int,
  numThreads: Int,
  gpuResetPc: BigInt = 0x81000000L
) extends Module {
  // Verilator 4.x reserves "TOP" for its synthetic hierarchy root. Keep the
  // Scala top class named TOP while giving the emitted RTL a legal module name.
  override def desiredName: String = "GPUTop"

  require(numGpuCores >= 1 && numGpuCores <= 16)
  require(numWarps >= 1 && isPow2(numWarps))
  require(numThreads >= 1 && isPow2(numThreads))

  private val warpBits = math.max(1, log2Ceil(numWarps))

  val io = IO(new Bundle {
    val gpu_launch = Input(Bool())
    val gpu_busy = Output(Bool())
    val gpu_done = Output(UInt(numGpuCores.W))
    val gpu_active_warps = Output(Vec(numGpuCores, UInt(numWarps.W)))
    val gpu_issue_warp = Output(Vec(numGpuCores, UInt(warpBits.W)))
    val gpu_issue_mask = Output(Vec(numGpuCores, UInt(numThreads.W)))
  })

  val gpu = (0 until numGpuCores).map { coreId =>
    val core = withReset((reset.asBool || !io.gpu_launch).asAsyncReset) {
      Module(new Core(
        coreId, numWarps, numThreads, gpuResetPc, enableGpuTrace = true))
    }
    val imem = Module(new SimInstMem)
    val dmem = Module(new SimDataMem)

    core.io.enable := io.gpu_launch
    imem.io <> core.io.ibus
    dmem.io <> core.io.dbus

    io.gpu_active_warps(coreId) := core.io.activeWarps
    io.gpu_issue_warp(coreId) := core.io.issueWarp
    io.gpu_issue_mask(coreId) := core.io.issueMask
    core
  }

  val done = VecInit(gpu.map(_.io.done))
  io.gpu_done := done.asUInt
  io.gpu_busy := io.gpu_launch && !done.asUInt.andR
  dontTouch(done)
}


/** Elaborate
  *
  * ...
  */
object TOP extends App {
  val options = Array(
    "--lowering-options=" + List(
      "disallowLocalVariables",
      "disallowPackedArrays",
      "locationInfoStyle=wrapInAtSquareBracket"
    ).mkString(",")
  )
  val cores = sys.env.get("GPU_NUM_CORES").map(_.toInt).getOrElse(2)
  val warps = sys.env.get("GPU_NUM_WARPS").map(_.toInt).getOrElse(2)
  val threads = sys.env.get("GPU_NUM_THREADS").map(_.toInt).getOrElse(2)
  circt.stage.ChiselStage.emitSystemVerilogFile(
    new TOP(cores, warps, threads), args, options)
}
