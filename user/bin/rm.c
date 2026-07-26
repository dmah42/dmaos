#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

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

  int ret = unlink(path);
  if (ret < 0) {
    printf("rm: failed to remove '%s': %s\n", path, strerror(errno));
    return 1;
  }
  return 0;
}
