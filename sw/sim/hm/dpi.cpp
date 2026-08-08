#include "backend.h"
#include <common.h>
#include <device/memory.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <svdpi.h>

#if defined(CONFIG_DIFFTEST)
enum {
  DIFF_CORE_WORDS =
      28 + 20 * CONFIG_GPU_NUM_WARPS + 20 * CONFIG_GPU_NUM_WARPS * CONFIG_GPU_NUM_THREADS,
  DIFF_STATE_WORDS = 23 + CONFIG_GPU_NUM_CORES * DIFF_CORE_WORDS,
};

static uint32_t diff_state[DIFF_STATE_WORDS];
static bool diff_state_valid[DIFF_STATE_WORDS];
static bool diff_state_seen;
static bool diff_failed;
#endif

extern "C" int dpi_paddr_read(int addr) {
  if (!in_pmem((uint32_t)addr))
    return 0;
  return (int)paddr_read((uint32_t)addr, 4);
}

extern "C" void dpi_paddr_write(int addr, char mask, int data) {
#if defined(CONFIG_DIFFTEST)
  (void)addr;
  (void)mask;
  (void)data;
  return;
#endif
  for (int byte = 0; byte < 4; ++byte) {
    if (mask & (1 << byte))
      paddr_write((uint32_t)addr + byte, 1, ((uint32_t)data >> (8 * byte)) & 0xff);
  }
}

extern "C" void gpu_diff_state(int base, const svOpenArrayHandle state) {
#if defined(CONFIG_DIFFTEST)
  if (base < 0 || (size_t)base > DIFF_STATE_WORDS) {
    diff_failed = true;
    return;
  }
  size_t words = (size_t)svSize(state, 1);
  if (words > DIFF_STATE_WORDS - (size_t)base) {
    diff_failed = true;
    return;
  }
  const void *contiguous_state = svGetArrayPtr(state);
  if (contiguous_state) {
    memcpy(diff_state + base, contiguous_state, words * sizeof(uint32_t));
  } else {
    for (size_t word = 0; word < words; ++word) {
      const void *element = svGetArrElemPtr1(state, (int)word);
      if (!element) {
        diff_failed = true;
        return;
      }
      memcpy(&diff_state[base + word], element, sizeof(diff_state[word]));
    }
  }
  memset(diff_state_valid + base, true, words * sizeof(diff_state_valid[0]));
  diff_state_seen = true;
  for (size_t word = 0; word < DIFF_STATE_WORDS; ++word)
    diff_state_seen &= diff_state_valid[word];
#else
  (void)base;
  (void)state;
#endif
}

extern "C" void dpi_diff_reset(void) {
#if defined(CONFIG_DIFFTEST)
  memset(diff_state, 0, sizeof(diff_state));
  memset(diff_state_valid, 0, sizeof(diff_state_valid));
  diff_state_seen = false;
  diff_failed = false;
#endif
}

extern "C" const uint32_t *dpi_diff_state(void) {
#if defined(CONFIG_DIFFTEST)
  return diff_state;
#else
  return NULL;
#endif
}

extern "C" bool dpi_diff_state_seen(void) {
#if defined(CONFIG_DIFFTEST)
  return diff_state_seen;
#else
  return false;
#endif
}

extern "C" bool dpi_diff_failed(void) {
#if defined(CONFIG_DIFFTEST)
  return diff_failed;
#else
  return false;
#endif
}
