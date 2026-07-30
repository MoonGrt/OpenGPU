#include <device/memory.h>

static uint8_t *pmem;

void init_mem(void) {
  pmem = calloc(1, CONFIG_MSIZE);
  Assert(pmem != NULL, "cannot allocate %u-byte PMEM", CONFIG_MSIZE);
  Log("PMEM [0x%08x, 0x%08x]", PMEM_LEFT, PMEM_RIGHT);
}

void free_mem(void) {
  free(pmem);
  pmem = NULL;
}

bool in_pmem(paddr_t addr) {
  return addr - CONFIG_MBASE < CONFIG_MSIZE;
}

uint8_t *guest_to_host(paddr_t addr) {
  Assert(in_pmem(addr), "address 0x%08x is outside PMEM", addr);
  return pmem + addr - CONFIG_MBASE;
}

word_t paddr_read(paddr_t addr, int len) {
  Assert(in_pmem(addr) && in_pmem(addr + len - 1),
      "read 0x%08x+%d is outside PMEM", addr, len);
  word_t value = 0;
  memcpy(&value, guest_to_host(addr), len);
  return value;
}

void paddr_write(paddr_t addr, int len, word_t data) {
  Assert(in_pmem(addr) && in_pmem(addr + len - 1),
      "write 0x%08x+%d is outside PMEM", addr, len);
  memcpy(guest_to_host(addr), &data, len);
}
