package gpu.interfaces

import spinal.core._

/** Write-only device-control-register channel consumed by the KMU. */
class DcrWrite extends Bundle {
  val valid = Bool()
  val addr = UInt(12 bits)
  val data = UInt(32 bits)
}
