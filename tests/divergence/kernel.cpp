#include <gpu.h>
#include "common.h"
extern "C" void kernel_main(const kernel_arg_t *args) {
  gpu_u32 id = gpu_global_id();
  volatile gpu_u32 *output =
    reinterpret_cast<volatile gpu_u32 *>(args->output_addr);
  if (id & 1) {
    if (id & 2)
      output[id] = 0x33000000u | id;
    else
      output[id] = 0x11000000u | id;
  } else {
    if (id & 2)
      output[id] = 0x22000000u | id;
    else
      output[id] = 0x44000000u | id;
  }
}
