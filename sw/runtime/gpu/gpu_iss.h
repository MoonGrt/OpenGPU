#ifndef __MEMU_GPU_ISS_H__
#define __MEMU_GPU_ISS_H__

#include <common.h>

typedef struct {
  uint32_t gpr[16];
  uint32_t pc;
  uint32_t hartid;
  bool halted;
} gpu_iss_core_t;

void gpu_iss_init(uint32_t num_cores);
bool gpu_iss_launch(uint32_t entry);
bool gpu_iss_step(void);
bool gpu_iss_done(void);
uint32_t gpu_iss_num_cores(void);

#endif
