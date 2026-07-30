#ifndef __VECADD_COMMON_H__
#define __VECADD_COMMON_H__
typedef unsigned int app_u32;
typedef struct {
  app_u32 count, src0_addr, src1_addr, dst_addr;
} kernel_arg_t;
#endif
