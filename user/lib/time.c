#include "time.h"
#include "stdlib.h"

time_t time(time_t *t) {
  time_t sec = ((uint32_t)(uptime() & 0xFFFFFFFF)) / 10000000;
  if (t) {
    *t = sec;
  }
  return sec;
}
