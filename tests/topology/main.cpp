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
  gpu_device_h dev; gpu_kernel_h kernel;
  gpu_device_config_t cfg;
  CHECK(gpu_device_open(0, &dev));
  CHECK(gpu_device_get_config(dev, &cfg));
  gpu_addr_t output_addr;
  CHECK(gpu_mem_alloc(dev, cfg.num_harts * sizeof(app_u32), &output_addr));
  CHECK(gpu_kernel_load_file(dev, path, &kernel));
  kernel_arg_t args = {output_addr};
  CHECK(gpu_launch(dev, kernel, &args, sizeof(args)));
  CHECK(gpu_wait(dev));
  std::vector<app_u32> output(cfg.num_harts);
  CHECK(gpu_mem_read(dev, output.data(), output_addr,
      output.size() * sizeof(app_u32)));
  for (unsigned id = 0; id < cfg.num_harts; ++id) {
    unsigned thread = id % cfg.threads_per_warp;
    unsigned linear_warp = id / cfg.threads_per_warp;
    unsigned warp = linear_warp % cfg.warps_per_core;
    unsigned core = linear_warp / cfg.warps_per_core;
    if (output[id] != ((core << 16) | (warp << 8) | thread)) return 1;
  }
  CHECK(gpu_device_close(dev));
  std::puts("[HOST] topology: PASS");
  return 0;
}
