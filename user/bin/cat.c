#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

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
    printf("cat: '%s': %s\n", filename, strerror(errno));
    return 1;
  }
  if (S_ISDIR(st.st_mode)) {
    printf("cat: '%s': %s\n", filename, strerror(EISDIR));
    return 1;
  }

  FILE *f = fopen(filename, "r");
  if (f == NULL) {
    printf("cat: '%s': %s\n", filename, strerror(errno));
    return 1;
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t nread;
  while ((nread = __getline(&line, &len, f)) != -1) {
    printf("%s", line);
  }
  free(line);
  fclose(f);
  return 0;
}