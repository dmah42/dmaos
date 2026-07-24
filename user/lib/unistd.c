#include "unistd.h"

int errno = 0;

int isatty(int fd) {
  return (fd >= STDIN_FILENO && fd <= STDERR_FILENO);
}
