#pragma once

#include <stdint.h>

struct timeval {
  uint64_t tv_sec;
  uint64_t tv_usec;
};

struct timezone {
  int tz_minuteswest;
  int tz_dsttime;
};
