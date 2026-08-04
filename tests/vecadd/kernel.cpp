#include <gpu.h>
#include "common.h"
extern "C" void kernel_main(const kernel_arg_t *args) {
  gpu_u32 id = gpu_global_id();
  const volatile gpu_u32 *a =
    reinterpret_cast<const volatile gpu_u32 *>(args->src0_addr);
  const volatile gpu_u32 *b =
    reinterpret_cast<const volatile gpu_u32 *>(args->src1_addr);
  volatile gpu_u32 *c = reinterpret_cast<volatile gpu_u32 *>(args->dst_addr);
  if (id < args->count) c[id] = a[id] + b[id];
}
