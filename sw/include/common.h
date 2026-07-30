#ifndef __MEMU_COMMON_H__
#define __MEMU_COMMON_H__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <generated/autoconf.h>

typedef uint32_t word_t;
typedef uint32_t paddr_t;

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define Log(fmt, ...) \
  do { printf("[MEMU] " fmt "\n", ##__VA_ARGS__); } while (0)
#define Assert(cond, fmt, ...) \
  do { \
    if (!(cond)) { \
      fprintf(stderr, "[MEMU] " fmt "\n", ##__VA_ARGS__); \
      abort(); \
    } \
  } while (0)
#define panic(fmt, ...) Assert(false, fmt, ##__VA_ARGS__)

#endif
