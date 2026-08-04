package gpu.interfaces

import chisel3._

/** CTA payload broadcast by the KMU and accepted by one ready core. */
class CtaRequest extends Bundle {
  val startupPc = UInt(32.W)
  val argsAddr = UInt(32.W)
  val blockIdx = Vec(3, UInt(32.W))
  val blockDim = Vec(3, UInt(32.W))
  val gridDim = Vec(3, UInt(32.W))
  val blockSize = UInt(32.W)
}
