#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: write <filename> [content...]\n");
    return 1;
  }

  const char *filename = argv[1];
  if (strlen(filename) == 0) {
    printf("usage: write <filename> [content...]\n");
    return 1;
  }

  // If only a filename is provided, stream from stdin
  if (argc == 2) {
    FILE *f = fopen(filename, "w");
    if (f == NULL) {
      printf("write: failed to open file '%s': %s\n", filename,
             strerror(errno));
      return 1;
    }

    uint8_t buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
      if (fwrite(buf, 1, n, f) != n) {
        printf("write: failed to write to file '%s'\n", filename);
        fclose(f);
        return 1;
      }
    }
    fclose(f);
    return 0;
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

  FILE *f = fopen(filename, "a");
  if (f == NULL) {
    printf("write: failed to open file '%s': %s\n", filename, strerror(errno));
    return 1;
  }

  int ret = fprintf(f, "%s", buf);
  fclose(f);
  if (ret < 0) {
    printf("write: failed to write to file '%s'\n", filename);
    return 1;
  }
  return 0;
}
