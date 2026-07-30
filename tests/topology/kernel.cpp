#include <gpu.h>
#include "common.h"
extern "C" void kernel_main(
    const kernel_arg_t *args, const gpu_launch_t *launch) {
  gpu_u32 id = gpu_hartid();
  gpu_u32 thread = id;
  gpu_u32 warp_linear = 0;
  while (thread >= launch->threads_per_warp) {
    thread -= launch->threads_per_warp;
    ++warp_linear;
  }
  gpu_u32 warp = warp_linear;
  gpu_u32 core = 0;
  while (warp >= launch->warps_per_core) {
    warp -= launch->warps_per_core;
    ++core;
  }
  volatile gpu_u32 *output =
    reinterpret_cast<volatile gpu_u32 *>(args->output_addr);
  output[id] = (core << 16) | (warp << 8) | thread;
}
