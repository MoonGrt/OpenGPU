#ifndef __MEMU_GPU_H__
#define __MEMU_GPU_H__

#include <common.h>

/* Internal memory map and launch ABI shared with riscv32-gpu AM. */
#define GPU_KERNEL_BASE   0x81000000u
#define GPU_KERNEL_END    0x81010000u
#define GPU_STACK_TOP     0x81800000u
#define GPU_HEAP_BASE     0x82000000u
#define GPU_MAX_ALLOCS    64u

#define GPU_DCR_STARTUP_PC  0x010u
#define GPU_DCR_ARGS_ADDR   0x011u
#define GPU_DCR_ARGS_SIZE   0x012u
#define GPU_DCR_BLOCK_DIM_X 0x013u
#define GPU_DCR_BLOCK_DIM_Y 0x014u
#define GPU_DCR_BLOCK_DIM_Z 0x015u
#define GPU_DCR_GRID_DIM_X  0x016u
#define GPU_DCR_GRID_DIM_Y  0x017u
#define GPU_DCR_GRID_DIM_Z  0x018u
#define GPU_DCR_BLOCK_SIZE  0x019u

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
