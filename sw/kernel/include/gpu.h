#ifndef __AM_GPU_H__
#define __AM_GPU_H__

typedef unsigned int gpu_u32;

#define GPU_CSR_THREAD_IDX_X 0xcc0
#define GPU_CSR_THREAD_IDX_Y 0xcc1
#define GPU_CSR_THREAD_IDX_Z 0xcc2
#define GPU_CSR_BLOCK_IDX_X  0xcc3
#define GPU_CSR_BLOCK_IDX_Y  0xcc4
#define GPU_CSR_BLOCK_IDX_Z  0xcc5
#define GPU_CSR_BLOCK_DIM_X  0xcc6
#define GPU_CSR_BLOCK_DIM_Y  0xcc7
#define GPU_CSR_BLOCK_DIM_Z  0xcc8
#define GPU_CSR_GRID_DIM_X   0xcc9
#define GPU_CSR_GRID_DIM_Y   0xcca
#define GPU_CSR_GRID_DIM_Z   0xccb
#define GPU_CSR_ARGS_ADDR    0xccc
#define GPU_CSR_PHYSICAL_ID  0xccd
#define GPU_CSR_WARP_ID      0xcce
#define GPU_CSR_LANE_ID      0xccf

#define GPU_STRINGIFY_INNER(value) #value
#define GPU_STRINGIFY(value) GPU_STRINGIFY_INNER(value)
#define GPU_CSR_READ(name, number) \
  static inline gpu_u32 name(void) { \
    gpu_u32 value; \
    __asm__ volatile("csrr %0, " GPU_STRINGIFY(number) : "=r"(value)); \
    return value; \
  }

GPU_CSR_READ(gpu_thread_idx_x, GPU_CSR_THREAD_IDX_X)
GPU_CSR_READ(gpu_thread_idx_y, GPU_CSR_THREAD_IDX_Y)
GPU_CSR_READ(gpu_thread_idx_z, GPU_CSR_THREAD_IDX_Z)
GPU_CSR_READ(gpu_block_idx_x, GPU_CSR_BLOCK_IDX_X)
GPU_CSR_READ(gpu_block_idx_y, GPU_CSR_BLOCK_IDX_Y)
GPU_CSR_READ(gpu_block_idx_z, GPU_CSR_BLOCK_IDX_Z)
GPU_CSR_READ(gpu_block_dim_x, GPU_CSR_BLOCK_DIM_X)
GPU_CSR_READ(gpu_block_dim_y, GPU_CSR_BLOCK_DIM_Y)
GPU_CSR_READ(gpu_block_dim_z, GPU_CSR_BLOCK_DIM_Z)
GPU_CSR_READ(gpu_grid_dim_x, GPU_CSR_GRID_DIM_X)
GPU_CSR_READ(gpu_grid_dim_y, GPU_CSR_GRID_DIM_Y)
GPU_CSR_READ(gpu_grid_dim_z, GPU_CSR_GRID_DIM_Z)
GPU_CSR_READ(gpu_physical_id, GPU_CSR_PHYSICAL_ID)
GPU_CSR_READ(gpu_warp_id, GPU_CSR_WARP_ID)
GPU_CSR_READ(gpu_lane_id, GPU_CSR_LANE_ID)

#undef GPU_CSR_READ
#undef GPU_STRINGIFY
#undef GPU_STRINGIFY_INNER

static inline gpu_u32 gpu_hartid(void) {
  gpu_u32 id;
  __asm__ volatile("csrr %0, mhartid" : "=r"(id));
  return id;
}

static inline gpu_u32 gpu_global_id(void) { return gpu_hartid(); }

static inline gpu_u32 gpu_mul_u32(gpu_u32 left, gpu_u32 right) {
  gpu_u32 product = 0;
  while (right) {
    if (right & 1u) product += left;
    left <<= 1;
    right >>= 1;
  }
  return product;
}

static inline gpu_u32 gpu_global_size(void) {
  gpu_u32 size = gpu_mul_u32(gpu_grid_dim_x(), gpu_grid_dim_y());
  size = gpu_mul_u32(size, gpu_grid_dim_z());
  size = gpu_mul_u32(size, gpu_block_dim_x());
  size = gpu_mul_u32(size, gpu_block_dim_y());
  return gpu_mul_u32(size, gpu_block_dim_z());
}

#endif
