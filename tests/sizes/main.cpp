#include <runtime.h>
#include "common.h"
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(x) do { auto r_ = (x); if (r_ != GPU_SUCCESS) { \
  std::fprintf(stderr, "%s: %s\n", #x, gpu_result_string(r_)); return 1; \
} } while (0)

static app_u32 encode_beq(unsigned rs1, unsigned rs2, unsigned offset) {
  return ((offset >> 12) & 1u) << 31 |
         ((offset >> 5) & 0x3fu) << 25 |
         (rs2 & 0x1fu) << 20 | (rs1 & 0x1fu) << 15 |
         ((offset >> 1) & 0xfu) << 8 |
         ((offset >> 11) & 1u) << 7 | 0x63u;
}

int main(int argc, char **argv) {
  const char *path = nullptr;
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "-k") && i + 1 < argc) path = argv[++i];
    else return 2;
  if (!path) return 2;
  constexpr unsigned max_count = 64;
  const unsigned counts[] = {1, 2, 3, 35, 64};
  gpu_device_h dev; gpu_kernel_h kernel; gpu_device_config_t cfg;
  CHECK(gpu_device_open(0, &dev));
  CHECK(gpu_device_get_config(dev, &cfg));
  const size_t bytes = max_count * sizeof(app_u32);
  gpu_addr_t aa, ba, ca;
  CHECK(gpu_mem_alloc(dev, bytes, &aa));
  CHECK(gpu_mem_alloc(dev, bytes, &ba));
  CHECK(gpu_mem_alloc(dev, bytes, &ca));
  std::vector<app_u32> a(max_count), b(max_count), c(max_count);
  for (unsigned i = 0; i < max_count; ++i) { a[i] = i + 7; b[i] = i + 0x100; }
  CHECK(gpu_mem_write(dev, aa, a.data(), bytes));
  CHECK(gpu_mem_write(dev, ba, b.data(), bytes));
  CHECK(gpu_kernel_load_file(dev, path, &kernel));
  unsigned block = cfg.warps_per_core * cfg.threads_per_warp;
  struct {
    kernel_arg_t args;
    app_u32 padding[4];
  } arg_blob = {};
  for (unsigned count : counts) {
    arg_blob.args = {count, aa, ba, ca};
    for (unsigned i = 0; i < 4; ++i)
      arg_blob.padding[i] = count ^ (0x12340000u + i);
    gpu_launch_info_t launch = {
      {(count + block - 1) / block, 1, 1}, {block, 1, 1}, &arg_blob,
      sizeof(kernel_arg_t) + ((count & 1) ? 0 : sizeof(arg_blob.padding))};
    CHECK(gpu_launch(dev, kernel, &launch));
    CHECK(gpu_wait(dev));
    CHECK(gpu_mem_read(dev, c.data(), ca, count * sizeof(app_u32)));
    for (unsigned i = 0; i < count; ++i)
      if (c[i] != a[i] + b[i]) return 1;
  }
  gpu_launch_info_t invalid = {{1, 1, 1}, {block + 1, 1, 1}, nullptr, 0};
  if (gpu_launch(dev, kernel, &invalid) !=
      GPU_ERROR_INVALID_ARGUMENT) return 1;
  invalid = {{0, 1, 1}, {1, 1, 1}, nullptr, 0};
  if (gpu_launch(dev, kernel, &invalid) !=
      GPU_ERROR_INVALID_ARGUMENT) return 1;
  invalid = {{1, 1, 1}, {1, 1, 1}, nullptr, 4};
  if (gpu_launch(dev, kernel, &invalid) !=
      GPU_ERROR_INVALID_ARGUMENT) return 1;
  invalid = {{0xffffffffu, 2, 1}, {1, 1, 1}, nullptr, 0};
  if (gpu_launch(dev, kernel, &invalid) !=
      GPU_ERROR_INVALID_ARGUMENT) return 1;
  gpu_kernel_h replacement;
  CHECK(gpu_kernel_load_file(dev, path, &replacement));
  kernel_arg_t stale_args = {1, aa, ba, ca};
  gpu_launch_info_t stale = {{1, 1, 1}, {1, 1, 1},
                             &stale_args, sizeof(stale_args)};
  if (gpu_launch(dev, kernel, &stale) !=
      GPU_ERROR_INVALID_ARGUMENT) return 1;
  gpu_addr_t impossible;
  if (gpu_mem_alloc(dev, static_cast<size_t>(-1), &impossible) !=
      GPU_ERROR_INVALID_ARGUMENT) return 1;
  if (gpu_mem_read(dev, c.data(), ca + bytes, 4) !=
      GPU_ERROR_INVALID_ARGUMENT) return 1;
  CHECK(gpu_mem_free(dev, ba));
  gpu_addr_t reused;
  CHECK(gpu_mem_alloc(dev, bytes, &reused));
  if (reused != ba) return 1;
  if (gpu_mem_free(dev, ba) != GPU_SUCCESS ||
      gpu_mem_free(dev, ba) != GPU_ERROR_INVALID_ARGUMENT) return 1;

  const app_u32 illegal_instruction = 0xffffffffu;
  gpu_kernel_h illegal_kernel;
  CHECK(gpu_kernel_load_memory(dev, &illegal_instruction,
      sizeof(illegal_instruction), &illegal_kernel));
  gpu_launch_info_t fault_launch = {{1, 1, 1}, {1, 1, 1}, nullptr, 0};
  CHECK(gpu_launch(dev, illegal_kernel, &fault_launch));
  if (gpu_wait(dev) != GPU_ERROR_BACKEND) return 1;

  if (cfg.threads_per_warp >= 2) {
    const app_u32 divergent_jalr[] = {
      0xf1402573u, 0x00157513u, 0x00251513u,
      0x00000597u, 0x01058593u, 0x00b50533u,
      0x00050067u, 0x00100073u, 0x00100073u,
    };
    gpu_kernel_h jalr_kernel;
    CHECK(gpu_kernel_load_memory(dev, divergent_jalr,
        sizeof(divergent_jalr), &jalr_kernel));
    fault_launch = {{1, 1, 1}, {2, 1, 1}, nullptr, 0};
    CHECK(gpu_launch(dev, jalr_kernel, &fault_launch));
    if (gpu_wait(dev) != GPU_ERROR_BACKEND) return 1;
  }

  if (cfg.threads_per_warp >= 10) {
    app_u32 stack_overflow[20] = {};
    stack_overflow[0] = 0xccf02573u;  // csrr a0, lane_id
    for (unsigned level = 0; level < 9; ++level) {
      stack_overflow[1 + 2 * level] = (level << 20) | 0x00000593u;
      const unsigned branch_pc = (2 + 2 * level) * 4;
      stack_overflow[2 + 2 * level] =
          encode_beq(10, 11, 19 * 4 - branch_pc);
    }
    stack_overflow[19] = 0x00100073u;
    gpu_kernel_h overflow_kernel;
    CHECK(gpu_kernel_load_memory(dev, stack_overflow,
        sizeof(stack_overflow), &overflow_kernel));
    fault_launch = {{1, 1, 1}, {10, 1, 1}, nullptr, 0};
    CHECK(gpu_launch(dev, overflow_kernel, &fault_launch));
    if (gpu_wait(dev) != GPU_ERROR_BACKEND) return 1;
  }
  CHECK(gpu_device_close(dev));
  std::puts("[HOST] sizes, allocator, arguments and kernel checks: PASS");
  return 0;
}
