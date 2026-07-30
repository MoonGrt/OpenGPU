package gpu.spinal

import spinal.core._
import spinal.lib._
import gpu.spinal.riscv.GPUCore

/** Spinal counterpart of hw/chisel/src/main/scala/GPU.scala. */
class GPUTop(cores: Int, warps: Int, threads: Int) extends Component {
  val io = new Bundle {
    val gpu_launch = in Bool()
    val gpu_busy = out Bool()
    val gpu_done = out Bits(cores bits)
    val gpu_active_warps = out Bits(cores * warps bits)
    val gpu_issue_warp = out Bits(cores * scala.math.max(1, log2Up(warps)) bits)
    val gpu_issue_mask = out Bits(cores * threads bits)
  }
  val cs = (0 until cores).map(i => new GPUCore(i, warps, threads))
  cs.foreach(_.io.clockEnable := io.gpu_launch)
  io.gpu_done := cs.map(_.io.done).asBits
  io.gpu_busy := io.gpu_launch && !io.gpu_done.andR
  io.gpu_active_warps := cs.map(_.io.activeWarps).asBits
  io.gpu_issue_warp := cs.map(_.io.issueWarp.asBits).asBits
  io.gpu_issue_mask := cs.map(_.io.issueMask).asBits
}

object GPUTop extends App {
  SpinalConfig(
    targetDirectory = sys.env.getOrElse("SPINAL_TARGET_DIR", "."),
    defaultConfigForClockDomains = ClockDomainConfig(resetKind = SYNC)
  ).generateVerilog(new GPUTop(
    sys.env.getOrElse("GPU_NUM_CORES", "2").toInt,
    sys.env.getOrElse("GPU_NUM_WARPS", "2").toInt,
    sys.env.getOrElse("GPU_NUM_THREADS", "2").toInt))
}
