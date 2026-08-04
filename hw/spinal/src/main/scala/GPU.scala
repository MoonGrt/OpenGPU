package gpu.spinal

import spinal.core._
import spinal.lib._
import gpu.spinal.riscv.GPUCore

class GPUTop(cores: Int, warps: Int, threads: Int) extends Component {
  ClockDomain.current.clock.setName("clock")
  val io = new Bundle {
    val dcr_valid = in Bool()
    val dcr_addr = in UInt(12 bits)
    val dcr_data = in UInt(32 bits)
    val gpu_launch = in Bool()
    val gpu_busy = out Bool()
    val gpu_fault = out Bool()
    val gpu_done = out Bits(cores bits)
    val gpu_active_warps = out Bits(cores * warps bits)
    val gpu_issue_warp = out Bits(cores * scala.math.max(1, log2Up(warps)) bits)
    val gpu_issue_mask = out Bits(cores * threads bits)
  }
  val kmu = new Kmu(cores)
  kmu.io.dcrValid := io.dcr_valid
  kmu.io.dcrAddr := io.dcr_addr
  kmu.io.dcrData := io.dcr_data
  kmu.io.launch := io.gpu_launch
  val cs = (0 until cores).map(i => new GPUCore(i, warps, threads))
  for (i <- 0 until cores) {
    cs(i).io.ctaValid := kmu.io.ctaValid(i)
    cs(i).io.cta := kmu.io.cta
  }
  kmu.io.coreReady := Vec(cs.map(_.io.ctaReady)).asBits
  kmu.io.coreBusy := Vec(cs.map(_.io.busy)).asBits
  io.gpu_busy := kmu.io.busy
  io.gpu_fault := Vec(cs.map(_.io.fault)).asBits.orR
  io.gpu_done := Vec(cs.map(_.io.done)).asBits
  io.gpu_active_warps := Vec(cs.map(_.io.activeWarps)).asBits
  io.gpu_issue_warp := Vec(cs.map(_.io.issueWarp.asBits)).asBits
  io.gpu_issue_mask := Vec(cs.map(_.io.issueMask)).asBits
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
