package gpu

import spinal.core._
import spinal.lib._
import gpu.riscv.GPUCore
import gpu.peripheral.{SimDataMem, SimInstMem}
import gpu.util.DpiGpuStateBB

class GPUTop(numCores: Int, numWarps: Int, numThreads: Int, difftest: Boolean)
    extends Component {
  ClockDomain.current.clock.setName("clock")
  val io = new Bundle {
    val dcr_valid = in Bool()
    val dcr_addr = in UInt(12 bits)
    val dcr_data = in UInt(32 bits)
    val gpu_launch = in Bool()
    val gpu_busy = out Bool()
    val gpu_fault = out Bool()
    val gpu_done = out Bits(numCores bits)
    val gpu_active_warps = out Bits(numCores * numWarps bits)
    val gpu_issue_warp = out Bits(numCores * scala.math.max(1, log2Up(numWarps)) bits)
    val gpu_issue_mask = out Bits(numCores * numThreads bits)
  }
  val kmu = new Kmu(numCores, difftest)
  kmu.io.dcr.valid := io.dcr_valid
  kmu.io.dcr.addr := io.dcr_addr
  kmu.io.dcr.data := io.dcr_data
  kmu.io.launch := io.gpu_launch
  val cores = (0 until numCores).map(i =>
    new GPUCore(i, numWarps, numThreads, difftest))
  for (i <- 0 until numCores) {
    val imem = new SimInstMem
    val dmem = new SimDataMem
    cores(i).io.ctaValid := kmu.io.ctaValid(i)
    cores(i).io.cta := kmu.io.cta
    imem.io.ren := cores(i).io.imemRen
    imem.io.addr := cores(i).io.imemAddr
    cores(i).io.imemRdata := imem.io.rdata
    dmem.io.ren := cores(i).io.dmemRen
    dmem.io.wen := cores(i).io.dmemWen
    dmem.io.mask := cores(i).io.dmemMask
    dmem.io.addr := cores(i).io.dmemAddr
    dmem.io.wdata := cores(i).io.dmemWdata
    cores(i).io.dmemRdata := dmem.io.rdata
  }
  kmu.io.coreReady := Vec(cores.map(_.io.ctaReady)).asBits
  kmu.io.coreBusy := Vec(cores.map(_.io.busy)).asBits
  io.gpu_busy := kmu.io.busy
  io.gpu_fault := Vec(cores.map(_.io.fault)).asBits.orR
  io.gpu_done := Vec(cores.map(_.io.done)).asBits
  io.gpu_active_warps := Vec(cores.map(_.io.activeWarps)).asBits
  io.gpu_issue_warp := Vec(cores.map(_.io.issueWarp.asBits)).asBits
  io.gpu_issue_mask := Vec(cores.map(_.io.issueMask)).asBits
  if (difftest) {
    val diff = Vec(UInt(32 bits), 3)
    diff(0) := io.gpu_busy.asUInt.resize(32)
    diff(1) := io.gpu_fault.asUInt.resize(32)
    diff(2) := io.gpu_done.asUInt.resize(32)
    val diffBridge = new DpiGpuStateBB(3, 0)
    diffBridge.io.state := diff.asBits
  }
}

object GPUTop extends App {
  SpinalConfig(
    targetDirectory = sys.env.getOrElse("SPINAL_TARGET_DIR", "."),
    defaultConfigForClockDomains = ClockDomainConfig(resetKind = SYNC),
    bitVectorWidthMax = 1 << 20
  ).generateVerilog(new GPUTop(
    sys.env.getOrElse("GPU_NUM_CORES", "2").toInt,
    sys.env.getOrElse("GPU_NUM_WARPS", "4").toInt,
    sys.env.getOrElse("GPU_NUM_THREADS", "4").toInt,
    sys.env.get("GPU_DIFFTEST").contains("y")))
}
