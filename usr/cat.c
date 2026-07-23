#include "errno.h"
#include "stdlib.h"
#include "user.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("usage: cat <filename>\n");
    return 1;
  }
  const char *progname = argv[0];
  for (const char *p = argv[0]; *p; ++p) {
    if (*p == '/') {
      progname = p + 1;
    }
  }
  if (strncmp(progname, "cat", 3) != 0) {
    printf("unexpected call to cat\n");
    return 1;
  }

  const char *filename = argv[1];

  if (strlen(filename) == 0) {
    printf("usage: cat <filename>\n");
    return 1;
  }

  struct stat st;
  int ret = stat(filename, &st);
  if (ret < 0) {
    printf("cat: '%s': %s\n", filename, strerror(ret));
    return 1;
  }
  if (st.type == FS_DIR) {
    printf("cat: '%s': %s\n", filename, strerror(ERR_IS_A_DIRECTORY));
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
    printf("\ncat: error reading '%s': %s\n", filename, strerror(read_bytes));
    return 1;
  }
  return 0;
}