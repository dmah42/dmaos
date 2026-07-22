#include "user.h"

#include "stdlib.h"

void run_command(const char *cmdline) {
  int pid = spawn(cmdline);
  if (pid >= 0) {
    wait(pid);
  } else {
    printf("unknown command: %s\n", cmdline);
  }
}

void cat(const char *cmdline) {
  if (cmdline[3] == '\0') {
    printf("usage: cat <filename>\n");
  } else if (cmdline[3] == ' ') {
    const char *filename = &cmdline[4];
    if (*filename == '\0') {
      printf("usage: cat <filename>\n");
    } else {
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
    }
  } else {
    printf("unknown command: %s\n", cmdline);
  }
}

int main(void) {
  printf("\nwElCoMe To DmAsHeLl\n");

  while (1) {
  prompt:
    printf("> ");
    char cmdline[128];
    for (int i = 0;; ++i) {
      char ch = getchar();
      putchar(ch);
      if (i == sizeof(cmdline) - 1) {
        printf("command line too long\n");
        goto prompt;
      } else if (ch == '\r') {
        printf("\n");
        cmdline[i] = '\0';
        break;
      } else {
        cmdline[i] = ch;
      }
    }

    if (strcmp(cmdline, "exit") == 0) {
      exit();
    } else if (strncmp(cmdline, "cat", 3) == 0) {
      cat(cmdline);
    } else {
      run_command(cmdline);
    }
  }
  return 0;
}