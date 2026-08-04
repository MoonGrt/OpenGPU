#ifndef __MEMU_GPU_ISS_H__
#define __MEMU_GPU_ISS_H__

#include <common.h>

typedef struct {
  uint32_t gpr[16];
  uint32_t pc;
  uint32_t hartid;
  uint32_t physical_id;
  uint32_t args_addr;
  uint32_t thread_idx[3];
  uint32_t block_idx[3];
  uint32_t block_dim[3];
  uint32_t grid_dim[3];
  uint32_t warp_id;
  uint32_t lane_id;
  bool halted;
} gpu_iss_core_t;

void gpu_iss_init(uint32_t num_cores);
void gpu_iss_configure(uint32_t args_addr,
    const uint32_t grid_dim[3], const uint32_t block_dim[3]);
bool gpu_iss_launch(uint32_t entry);
bool gpu_iss_step(void);
bool gpu_iss_done(void);
bool gpu_iss_fault(void);
uint32_t gpu_iss_num_cores(void);

#endif
