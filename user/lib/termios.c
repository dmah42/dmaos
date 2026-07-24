#include "termios.h"

#include "string.h"

int tcgetattr(int fd, struct termios *termios_p) {
  (void)fd;
  if (!termios_p)
    return -1;
  // Initialize to zero or sensible values so rawmode toggling behaves
  memset(termios_p, 0, sizeof(struct termios));
  return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
  (void)fd;
  (void)optional_actions;
  (void)termios_p;
  return 0;
}
