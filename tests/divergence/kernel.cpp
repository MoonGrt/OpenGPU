#include <gpu.h>
#include "common.h"
extern "C" void kernel_main(
    const kernel_arg_t *args, const gpu_launch_t *) {
  gpu_u32 id = gpu_hartid();
  volatile gpu_u32 *output =
    reinterpret_cast<volatile gpu_u32 *>(args->output_addr);
  if (id & 1)
    output[id] = 0x5a5a0000u | id;
  else
    output[id] = 0xa5a50000u | id;
}
