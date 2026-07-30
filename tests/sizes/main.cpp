#include <runtime.h>
#include "common.h"
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(x) do { auto r_ = (x); if (r_ != GPU_SUCCESS) { \
  std::fprintf(stderr, "%s: %s\n", #x, gpu_result_string(r_)); return 1; \
} } while (0)
int main(int argc, char **argv) {
  const char *path = nullptr;
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "-k") && i + 1 < argc) path = argv[++i];
    else return 2;
  if (!path) return 2;
  constexpr unsigned max_count = 64;
  const unsigned counts[] = {1, 2, 3, 35, 64};
  gpu_device_h dev; gpu_kernel_h kernel;
  CHECK(gpu_device_open(0, &dev));
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
  for (unsigned count : counts) {
    kernel_arg_t args = {count, aa, ba, ca};
    CHECK(gpu_launch(dev, kernel, &args, sizeof(args)));
    CHECK(gpu_wait(dev));
    CHECK(gpu_mem_read(dev, c.data(), ca, count * sizeof(app_u32)));
    for (unsigned i = 0; i < count; ++i)
      if (c[i] != a[i] + b[i]) return 1;
  }
  std::vector<unsigned char> oversized_args(4096);
  if (gpu_launch(dev, kernel, oversized_args.data(), oversized_args.size()) !=
      GPU_ERROR_INVALID_ARGUMENT) return 1;
  gpu_kernel_h replacement;
  CHECK(gpu_kernel_load_file(dev, path, &replacement));
  kernel_arg_t stale_args = {1, aa, ba, ca};
  if (gpu_launch(dev, kernel, &stale_args, sizeof(stale_args)) !=
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
  CHECK(gpu_device_close(dev));
  std::puts("[HOST] sizes, allocator, arguments and kernel checks: PASS");
  return 0;
}
