package gpu.interfaces

import chisel3._

/** Write-only device-control-register channel consumed by the KMU. */
class DcrWrite extends Bundle {
  val valid = Bool()
  val addr = UInt(12.W)
  val data = UInt(32.W)
}
