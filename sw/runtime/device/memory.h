#ifndef __OPENGPU_DEVICE_MEMORY_H__
#define __OPENGPU_DEVICE_MEMORY_H__

#include <common.h>

#define PMEM_LEFT  ((paddr_t)CONFIG_MBASE)
#define PMEM_RIGHT ((paddr_t)(CONFIG_MBASE + CONFIG_MSIZE - 1u))

#ifdef __cplusplus
extern "C" {
#endif

void init_mem(void);
void free_mem(void);
uint8_t *guest_to_host(paddr_t addr);
bool in_pmem(paddr_t addr);
word_t paddr_read(paddr_t addr, int len);
void paddr_write(paddr_t addr, int len, word_t data);

#ifdef __cplusplus
}
#endif

#endif
