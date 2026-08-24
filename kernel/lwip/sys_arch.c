#include "stdlib.h"

#include <lwip/sys.h>

// System uptime in milliseconds for lwIP timers and protocol timeouts
u32_t sys_now(void) { return uptime_ms(); }
