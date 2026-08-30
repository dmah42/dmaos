#include "stdlib.h"

#include <stdint.h>

#include "k_time.h"

uint64_t uptime(void) {
#if __riscv_xlen == 64
  uint64_t val;
  __asm__ __volatile__("rdtime %0" : "=r"(val));
  return val;
#else
  uint32_t low, high, temp;
  __asm__ __volatile__("1:\n"
                       "rdtimeh %0\n"
                       "rdtime %1\n"
                       "rdtimeh %2\n"
                       "bne %0, %2, 1b\n"
                       : "=&r"(high), "=&r"(low), "=&r"(temp));
  return ((uint64_t)high << 32) | low;
#endif
}

uint32_t uptime_ms(void) { return (uint32_t)(uptime() / TICKS_PER_MS); }

uint32_t rand32(void) {
  static uint32_t state = 0x853c49e6;
  state ^= (uint32_t)uptime();
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

int atoi(const char *s) {
  int res = 0;
  int sign = 1;
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
    s++;
  }
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }
  while (*s >= '0' && *s <= '9') {
    res = res * 10 + (*s - '0');
    s++;
  }
  return sign * res;
}

int abs(int x) { return (x < 0) ? -x : x; }
