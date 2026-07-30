#include <gpu.h>
#include "common.h"
extern "C" void kernel_main(
    const kernel_arg_t *args, const gpu_launch_t *launch) {
  gpu_u32 id = gpu_hartid();
  const volatile gpu_u32 *a =
    reinterpret_cast<const volatile gpu_u32 *>(args->src0_addr);
  const volatile gpu_u32 *b =
    reinterpret_cast<const volatile gpu_u32 *>(args->src1_addr);
  volatile gpu_u32 *c = reinterpret_cast<volatile gpu_u32 *>(args->dst_addr);
  for (gpu_u32 i = id; i < args->count; i += launch->num_harts)
    c[i] = a[i] ^ b[i];
}
