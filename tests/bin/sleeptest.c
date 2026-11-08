// Measures sleep_ms() accuracy. For each requested duration it sleeps
// ITERATIONS times and writes "<requested us> <min us> <avg us> <max us>" to
// the file named by argv[1].

#include <stdint.h>
#include <stdio.h>

#include "k_time.h"

#define ITERATIONS 50

static const uint32_t durations_ms[] = {1, 3, 15};

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: sleeptest <output file>\n");
    return 1;
  }
  FILE *out = fopen(argv[1], "w");
  if (out == NULL) {
    perror("sleeptest");
    return 1;
  }

  for (size_t d = 0; d < sizeof(durations_ms) / sizeof(durations_ms[0]); ++d) {
    uint64_t min = UINT64_MAX, max = 0, total = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
      uint64_t start = uptime();
      sleep_ms(durations_ms[d]);
      uint64_t elapsed = uptime() - start;
      min = elapsed < min ? elapsed : min;
      max = elapsed > max ? elapsed : max;
      total += elapsed;
    }
    fprintf(out, "%lu %lu %lu %lu\n", (unsigned long)durations_ms[d] * 1000,
            (unsigned long)(min / TICKS_PER_US),
            (unsigned long)(total / ITERATIONS / TICKS_PER_US),
            (unsigned long)(max / TICKS_PER_US));
  }
  fclose(out);
  return 0;
}
