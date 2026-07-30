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
  constexpr unsigned count = 17;
  gpu_device_h dev; gpu_kernel_h kernel;
  CHECK(gpu_device_open(0, &dev));
  size_t bytes = count * sizeof(app_u32);
  gpu_addr_t aa, ba, ca;
  CHECK(gpu_mem_alloc(dev, bytes, &aa));
  CHECK(gpu_mem_alloc(dev, bytes, &ba));
  CHECK(gpu_mem_alloc(dev, bytes, &ca));
  std::vector<app_u32> a(count), b(count), c(count);
  for (unsigned i = 0; i < count; ++i) { a[i] = 500 - i * 10; b[i] = i * 2 + 1; }
  CHECK(gpu_mem_write(dev, aa, a.data(), bytes));
  CHECK(gpu_mem_write(dev, ba, b.data(), bytes));
  CHECK(gpu_kernel_load_file(dev, path, &kernel));
  kernel_arg_t args = {count, aa, ba, ca};
  CHECK(gpu_launch(dev, kernel, &args, sizeof(args)));
  CHECK(gpu_wait(dev));
  CHECK(gpu_mem_read(dev, c.data(), ca, bytes));
  for (unsigned i = 0; i < count; ++i) if (c[i] != a[i] - b[i]) return 1;
  CHECK(gpu_device_close(dev));
  std::puts("[HOST] vecsub: PASS");
  return 0;
}
