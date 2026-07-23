#include "stdlib.h"
#include "user.h"

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("usage: write <filename> <content...>\n");
    return 1;
  }

  const char *filename = argv[1];
  if (strlen(filename) == 0) {
    printf("usage: write <filename> <content...>\n");
    return 1;
  }

  char buf[1024];
  buf[0] = '\0';

  for (int i = 2; i < argc; ++i) {
    int current_len = strlen(buf);
    int arg_len = strlen(argv[i]);
    // +2 for the space and the null terminator
    if (current_len + arg_len + 2 > (int)sizeof(buf)) {
      printf("write: content too long (max %zu bytes)\n", sizeof(buf) - 1);
      return 1;
    }
    strncat(buf, argv[i], sizeof(buf) - current_len - 1);
    if (i < argc - 1) {
      strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
    }
  }

  int ret = write_file(filename, buf, strlen(buf), 0);
  if (ret < 0) {
    printf("write: failed to write to file '%s': %s\n", filename, strerror(ret));
    return 1;
  }
  return 0;
}
