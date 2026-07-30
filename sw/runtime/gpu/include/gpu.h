#ifndef __MEMU_GPU_H__
#define __MEMU_GPU_H__

#include <common.h>

/* Internal memory map and launch ABI shared with riscv32-gpu AM. */
#define GPU_KERNEL_BASE   0x81000000u
#define GPU_LAUNCH_ADDR   0x8100f000u
#define GPU_ARGS_ADDR     0x8100f100u
#define GPU_ARGS_END      0x81010000u
#define GPU_STACK_TOP     0x81800000u
#define GPU_HEAP_BASE     0x82000000u
#define GPU_MAX_ALLOCS    64u

typedef struct {
  uint32_t num_harts;
  uint32_t physical_cores;
  uint32_t warps_per_core;
  uint32_t threads_per_warp;
  uint32_t args_addr;
  uint32_t args_size;
} gpu_launch_info_t;

#ifdef __cplusplus
extern "C" {
#endif

int memu_runtime_init(int argc, char **argv, int *app_argc, char ***app_argv);
void memu_runtime_fini(void);
bool gpu_trace_enabled(void);
void gpu_trace_suppress(bool suppress);
void gpu_trace_commit(int hartid, int pc, int inst);
void gpu_trace_store(int hartid, int addr, int mask, int data);

#ifdef __cplusplus
}
#endif

#endif
