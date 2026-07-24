#include "stdlib.h"
#include "user.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("usage: mkdir <directory>\n");
    return 1;
  }

  const char *path = argv[1];
  if (strlen(path) == 0) {
    printf("usage: mkdir <directory>\n");
    return 1;
  }

  int ret = mkdir(path);
  if (ret < 0) {
    printf("mkdir: failed to create directory '%s': %s\n", path, strerror(ret));
    return 1;
  }
  return 0;
}
