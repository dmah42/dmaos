#include "user.h"

#include "stdlib.h"

void main(void) {
  printf("wElCoMe To DmAoS\n");

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

    if (strcmp(cmdline, "hello") == 0) {
      printf("hello world\n");
    } else if (strcmp(cmdline, "exit") == 0) {
      exit();
    } else {
      printf("unknown command: %s\n", cmdline);
    }
  }
}