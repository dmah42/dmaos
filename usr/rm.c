#include "stdlib.h"
#include "user.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("usage: rm <path>\n");
    return 1;
  }

  const char *path = argv[1];
  if (strlen(path) == 0) {
    printf("usage: rm <path>\n");
    return 1;
  }

  if (rm(path) < 0) {
    printf("rm: failed to remove '%s'\n", path);
    return 1;
  }
  return 0;
}
