// Turns ISIG off (as kilo does), reads one byte from the console and writes
// its value to the file named by argv[1]. It exits without restoring the
// terminal, like a crashed raw-mode program, so the test can check that the
// kernel turns ISIG back on.

#include <stdio.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: rawkey <output file>\n");
    return 1;
  }
  struct termios t;
  if (tcgetattr(STDIN_FILENO, &t) != 0) {
    perror("tcgetattr");
    return 1;
  }
  t.c_lflag &= ~(ISIG | ICANON | ECHO);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) {
    perror("tcsetattr");
    return 1;
  }

  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) != 1) {
    perror("read");
    return 1;
  }
  FILE *out = fopen(argv[1], "w");
  if (out == NULL) {
    perror("rawkey");
    return 1;
  }
  fprintf(out, "%d\n", c);
  fclose(out);
  return 0;
}
