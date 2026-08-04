#include <runtime.h>
#include "common.h"
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(x) do { auto r_ = (x); if (r_ != GPU_SUCCESS) { \
  std::fprintf(stderr, "%s: %s\n", #x, gpu_result_string(r_)); return 1; \
} } while (0)

static bool validate(const std::vector<topology_result_t> &output,
                     const gpu_launch_info_t &launch,
                     const gpu_device_config_t &cfg) {
  const unsigned bx = launch.block_dim[0];
  const unsigned by = launch.block_dim[1];
  const unsigned block = bx * by * launch.block_dim[2];
  const unsigned grid = launch.grid_dim[0] * launch.grid_dim[1] *
                        launch.grid_dim[2];
  for (unsigned id = 0; id < block * grid; ++id) {
    const unsigned local = id % block;
    const unsigned cta = id / block;
    const topology_result_t &r = output[id];
    const unsigned expected_thread[3] = {
      local % bx, (local / bx) % by, local / (bx * by)};
    const unsigned expected_block[3] = {
      cta % launch.grid_dim[0],
      (cta / launch.grid_dim[0]) % launch.grid_dim[1],
      cta / (launch.grid_dim[0] * launch.grid_dim[1])};
    for (unsigned axis = 0; axis < 3; ++axis)
      if (r.thread_idx[axis] != expected_thread[axis] ||
          r.block_idx[axis] != expected_block[axis] ||
          r.block_dim[axis] != launch.block_dim[axis] ||
          r.grid_dim[axis] != launch.grid_dim[axis]) return false;
    if (r.global_id != id || r.global_size != block * grid ||
        r.warp_id != local / cfg.threads_per_warp ||
        r.lane_id != local % cfg.threads_per_warp ||
        r.physical_id >= cfg.num_harts) return false;
  }
  return true;
}

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
  const unsigned capacity = cfg.warps_per_core * cfg.threads_per_warp;
  const unsigned max_threads = 8 * capacity;
  gpu_addr_t output_addr;
  CHECK(gpu_mem_alloc(dev,
      max_threads * sizeof(topology_result_t), &output_addr));
  CHECK(gpu_kernel_load_file(dev, path, &kernel));
  kernel_arg_t args = {output_addr};
  gpu_launch_info_t launch = {
    {2, 2, 2}, {cfg.threads_per_warp, 1, cfg.warps_per_core},
    &args, sizeof(args)};
  CHECK(gpu_launch(dev, kernel, &launch));
  CHECK(gpu_wait(dev));
  std::vector<topology_result_t> output(max_threads);
  CHECK(gpu_mem_read(dev, output.data(), output_addr,
      output.size() * sizeof(topology_result_t)));
  if (!validate(output, launch, cfg)) return 1;

  if (capacity > 1) {
    std::memset(output.data(), 0, output.size() * sizeof(topology_result_t));
    CHECK(gpu_mem_write(dev, output_addr, output.data(),
        output.size() * sizeof(topology_result_t)));
    launch = {{cfg.physical_cores + 1, 1, 1}, {capacity - 1, 1, 1},
              &args, sizeof(args)};
    CHECK(gpu_launch(dev, kernel, &launch));
    CHECK(gpu_wait(dev));
    const unsigned count = (cfg.physical_cores + 1) * (capacity - 1);
    CHECK(gpu_mem_read(dev, output.data(), output_addr,
        count * sizeof(topology_result_t)));
    if (!validate(output, launch, cfg)) return 1;
  }
  CHECK(gpu_device_close(dev));
  std::puts("[HOST] topology: PASS");
  return 0;
}
