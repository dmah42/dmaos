#include "sys/ioctl.h"

int ioctl(int fd, unsigned long request, ...) {
  (void)fd;
  if (request == TIOCGWINSZ) {
    __builtin_va_list ap;
    __builtin_va_start(ap, request);
    struct winsize *ws = __builtin_va_arg(ap, struct winsize *);
    __builtin_va_end(ap);
    if (ws) {
      ws->ws_row = 24;
      ws->ws_col = 80;
      ws->ws_xpixel = 0;
      ws->ws_ypixel = 0;
      return 0;
    }
  }
  return -1;
}
