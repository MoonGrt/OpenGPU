#include <gpu.h>
#include "common.h"
extern "C" void kernel_main(const kernel_arg_t *args) {
  gpu_u32 id = gpu_global_id();
  volatile topology_result_t *output =
    reinterpret_cast<volatile topology_result_t *>(args->output_addr);
  output[id].thread_idx[0] = gpu_thread_idx_x();
  output[id].thread_idx[1] = gpu_thread_idx_y();
  output[id].thread_idx[2] = gpu_thread_idx_z();
  output[id].block_idx[0] = gpu_block_idx_x();
  output[id].block_idx[1] = gpu_block_idx_y();
  output[id].block_idx[2] = gpu_block_idx_z();
  output[id].block_dim[0] = gpu_block_dim_x();
  output[id].block_dim[1] = gpu_block_dim_y();
  output[id].block_dim[2] = gpu_block_dim_z();
  output[id].grid_dim[0] = gpu_grid_dim_x();
  output[id].grid_dim[1] = gpu_grid_dim_y();
  output[id].grid_dim[2] = gpu_grid_dim_z();
  output[id].global_id = id;
  output[id].global_size = gpu_global_size();
  output[id].warp_id = gpu_warp_id();
  output[id].lane_id = gpu_lane_id();
  output[id].physical_id = gpu_physical_id();
}
