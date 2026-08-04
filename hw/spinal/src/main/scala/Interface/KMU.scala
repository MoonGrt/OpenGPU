package gpu.interfaces

import spinal.core._

/** CTA payload broadcast by the KMU and accepted by one ready core. */
class CtaRequest extends Bundle {
  val startupPc = UInt(32 bits)
  val argsAddr = UInt(32 bits)
  val blockIdx = Vec(UInt(32 bits), 3)
  val blockDim = Vec(UInt(32 bits), 3)
  val gridDim = Vec(UInt(32 bits), 3)
  val blockSize = UInt(32 bits)
}
