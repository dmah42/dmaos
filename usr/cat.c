#include "stdlib.h"
#include "user.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("usage: cat <filename>\n");
    return 1;
  }
  if (strncmp(argv[0], "cat", 3) != 0) {
    printf("unexpected call to cat");
    return 1;
  }

  const char *filename = argv[1];

  if (strlen(filename) == 0) {
    printf("usage: cat <filename>\n");
    return 1;
  }

  char buf[FS_CHUNK_SIZE + 1];
  int offset = 0;
  int read_bytes;
  while ((read_bytes = read_file(filename, buf, offset)) > 0) {
    buf[read_bytes] = '\0';
    printf("%s", buf);
    offset += read_bytes;
  }
  if (read_bytes < 0) {
    printf("file not found: %s\n", filename);
  }
  return 0;
}