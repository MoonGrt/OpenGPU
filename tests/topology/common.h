#ifndef __TOPOLOGY_COMMON_H__
#define __TOPOLOGY_COMMON_H__
typedef unsigned int app_u32;
typedef struct {
  app_u32 thread_idx[3];
  app_u32 block_idx[3];
  app_u32 block_dim[3];
  app_u32 grid_dim[3];
  app_u32 global_id;
  app_u32 global_size;
  app_u32 warp_id;
  app_u32 lane_id;
  app_u32 physical_id;
} topology_result_t;
typedef struct {
  app_u32 output_addr;
} kernel_arg_t;
#endif
