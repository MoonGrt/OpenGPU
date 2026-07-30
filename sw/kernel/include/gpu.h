#ifndef __AM_GPU_H__
#define __AM_GPU_H__

typedef unsigned int gpu_u32;

#define GPU_LAUNCH_ADDR 0x8100f000u
#define GPU_ARGS_ADDR   0x8100f100u

typedef struct {
  gpu_u32 num_harts;
  gpu_u32 physical_cores;
  gpu_u32 warps_per_core;
  gpu_u32 threads_per_warp;
  gpu_u32 args_addr;
  gpu_u32 args_size;
} gpu_launch_t;

static inline gpu_u32 gpu_hartid(void) {
  gpu_u32 id;
  __asm__ volatile("csrr %0, mhartid" : "=r"(id));
  return id;
}

#endif
