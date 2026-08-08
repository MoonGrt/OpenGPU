#ifndef OPENGPU_SIM_HM_BACKEND_H
#define OPENGPU_SIM_HM_BACKEND_H

#include <common.h>

#ifdef __cplusplus
extern "C" {
#endif

void rtl_init(int argc, char **argv);
void rtl_exit(void);
void gpu_rtl_init(void);
bool gpu_rtl_launch(uint32_t entry);
bool gpu_rtl_wait(uint64_t max_cycles);
void gpu_rtl_dcr_write(uint32_t addr, uint32_t data);
bool gpu_rtl_fault(void);

#if defined(CONFIG_DIFFTEST)
void dpi_diff_reset(void);
const uint32_t *dpi_diff_state(void);
bool dpi_diff_state_seen(void);
bool dpi_diff_failed(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
