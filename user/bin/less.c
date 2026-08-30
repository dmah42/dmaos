#include "user.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/k_ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

enum Key {
  KEY_NONE = 0,
  KEY_UP = 1000,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_PAGE_UP,
  KEY_PAGE_DOWN,
  KEY_HOME,
  KEY_END,
};

static int read_key(int fd) {
  int nread;
  char c;
  while ((nread = read(fd, &c, 1)) == 0) {
    yield();
  }
  if (nread <= 0) {
    return 'q';
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(fd, seq, 1) == 0) {
      return '\x1b';
    }
    if (read(fd, seq + 1, 1) == 0) {
      return '\x1b';
    }

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(fd, seq + 2, 1) == 0) {
          return '\x1b';
        }
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return KEY_HOME;
          case '4':
            return KEY_END;
          case '5':
            return KEY_PAGE_UP;
          case '6':
            return KEY_PAGE_DOWN;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return KEY_UP;
        case 'B':
          return KEY_DOWN;
        case 'C':
          return KEY_RIGHT;
        case 'D':
          return KEY_LEFT;
        case 'H':
          return KEY_HOME;
        case 'F':
          return KEY_END;
        }
      }
    }
    return '\x1b';
  }

  return (int)(unsigned char)c;
}

static void draw_screen(char **lines, int num_lines, int top_line, int rows,
                        int cols, const char *title) {
  int content_rows = rows - 1;
  if (content_rows < 1) {
    content_rows = 1;
  }

  for (int r = 0; r < content_rows; ++r) {
    printf("\x1b[%d;1H", r + 1);
    int idx = top_line + r;
    if (idx < num_lines) {
      const char *line = lines[idx];
      printf("%s\x1b[0m\x1b[K", line);
    } else {
      printf("~\x1b[0m\x1b[K");
    }
  }

  // Draw reverse-video status bar on the bottom row with explicit color reset
  int visible_end = top_line + content_rows;
  if (visible_end > num_lines) {
    visible_end = num_lines;
  }
  int visible_start = num_lines > 0 ? (top_line + 1) : 0;

  char status[256];
  if (visible_end >= num_lines) {
    snprintf(status, sizeof(status), " %s (lines %d-%d/%d) (END) [q to quit]",
             title, visible_start, visible_end, num_lines);
  } else {
    snprintf(status, sizeof(status), " %s (lines %d-%d/%d) [q to quit]", title,
             visible_start, visible_end, num_lines);
  }

  int max_status_cols = cols - 1;
  if (max_status_cols < 1) {
    max_status_cols = 1;
  }

  int slen = strlen(status);
  printf("\x1b[%d;1H\x1b[0m\x1b[7m%s", rows, status);
  for (int i = slen; i < max_status_cols; ++i) {
    putchar(' ');
  }
  printf("\x1b[0m\x1b[K");
  fflush(stdout);
}

static void add_line(char ***lines, int *num_lines, int *capacity,
                     const char *str) {
  if (*num_lines >= *capacity) {
    int new_cap = *capacity == 0 ? 64 : *capacity * 2;
    char **new_lines = realloc(*lines, new_cap * sizeof(char *));
    if (!new_lines) {
      return;
    }
    *lines = new_lines;
    *capacity = new_cap;
  }
  (*lines)[(*num_lines)++] = strdup(str);
}

int main(int argc, char **argv) {
  if (argc > 2) {
    printf("usage: less [filename]\n");
    return 1;
  }

  FILE *f = stdin;
  const char *title = "(stdin)";

  if (argc == 2) {
    const char *filename = argv[1];
    struct stat st;
    if (stat(filename, &st) < 0) {
      printf("less: '%s': %s\n", filename, strerror(errno));
      return 1;
    }
    if (S_ISDIR(st.st_mode)) {
      printf("less: '%s': %s\n", filename, strerror(EISDIR));
      return 1;
    }

    f = fopen(filename, "r");
    if (f == NULL) {
      printf("less: '%s': %s\n", filename, strerror(errno));
      return 1;
    }
    title = filename;
  }

  // Get terminal dimensions
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_row == 0 ||
      ws.ws_col == 0) {
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0 || ws.ws_row == 0 ||
        ws.ws_col == 0) {
      fprintf(stderr, "less: failed to get terminal dimensions\n");
      if (f != stdin) {
        fclose(f);
      }
      return 1;
    }
  }
  int rows = (int)ws.ws_row;
  int cols = (int)ws.ws_col;

  // Load lines into memory with line wrapping and box-drawing continuation
  // prefix
  char **lines = NULL;
  int num_lines = 0;
  int capacity = 0;

  char *line = NULL;
  size_t len = 0;
  ssize_t nread;
  while ((nread = __getline(&line, &len, f)) != -1) {
    while (nread > 0 && (line[nread - 1] == '\n' || line[nread - 1] == '\r')) {
      line[--nread] = '\0';
    }
    if (nread == 0) {
      add_line(&lines, &num_lines, &capacity, "");
      continue;
    }

    if (nread <= cols) {
      add_line(&lines, &num_lines, &capacity, line);
    } else {
      // First line segment (full width)
      char chunk[256];
      memcpy(chunk, line, cols);
      chunk[cols] = '\0';
      add_line(&lines, &num_lines, &capacity, chunk);

      // Continuation segments with box-drawing prefix: └─
      int wrap_cols = cols - 3;
      if (wrap_cols < 1) {
        wrap_cols = 1;
      }
      for (int pos = cols; pos < nread; pos += wrap_cols) {
        int clen = nread - pos;
        if (clen > wrap_cols) {
          clen = wrap_cols;
        }
        char cont[512];
        snprintf(cont, sizeof(cont),
                 "\x1b[90m\xe2\x94\x94\xe2\x94\x80 \x1b[0m%.*s", clen,
                 line + pos);
        add_line(&lines, &num_lines, &capacity, cont);
      }
    }
  }
  free(line);

  if (f != stdin) {
    fclose(f);
  }

  if (num_lines == 0) {
    lines = malloc(sizeof(char *));
    lines[0] = strdup("");
    num_lines = 1;
    capacity = 1;
  }

  // Determine TTY descriptor for interactive input
  int tty_fd = STDIN_FILENO;
  if (!isatty(STDIN_FILENO)) {
    tty_fd = open("console", O_RDONLY);
    if (tty_fd < 0) {
      tty_fd = STDIN_FILENO;
    }
  }

  // Configure raw mode on tty_fd
  struct termios orig_termios, raw;
  tcgetattr(tty_fd, &orig_termios);
  raw = orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1; // 100ms timeout for escape sequences
  tcsetattr(tty_fd, TCSAFLUSH, &raw);

  int top_line = 0;
  int content_rows = rows - 1;
  if (content_rows < 1) {
    content_rows = 1;
  }

  // Main interactive loop
  while (true) {
    int max_top = num_lines > content_rows ? num_lines - content_rows : 0;
    if (top_line < 0) {
      top_line = 0;
    }
    if (top_line > max_top) {
      top_line = max_top;
    }

    draw_screen(lines, num_lines, top_line, rows, cols, title);

    int key = read_key(tty_fd);
    if (key == 'q' || key == 'Q') {
      break;
    } else if (key == 'j' || key == KEY_DOWN || key == '\r' || key == '\n') {
      if (top_line < max_top) {
        ++top_line;
      }
    } else if (key == 'k' || key == KEY_UP) {
      if (top_line > 0) {
        --top_line;
      }
    } else if (key == ' ' || key == KEY_PAGE_DOWN || key == 'f' || key == 'd' ||
               key == 6 /* Ctrl+F */ || key == 4 /* Ctrl+D */) {
      top_line += content_rows;
      if (top_line > max_top) {
        top_line = max_top;
      }
    } else if (key == 'b' || key == KEY_PAGE_UP || key == 'u' ||
               key == 2 /* Ctrl+B */ || key == 21 /* Ctrl+U */) {
      top_line -= content_rows;
      if (top_line < 0) {
        top_line = 0;
      }
    } else if (key == 'g' || key == KEY_HOME) {
      top_line = 0;
    } else if (key == 'G' || key == KEY_END) {
      top_line = max_top;
    }
  }

  // Restore screen and terminal settings
  printf("\x1b[2J\x1b[H");
  fflush(stdout);
  tcsetattr(tty_fd, TCSAFLUSH, &orig_termios);

  if (tty_fd != STDIN_FILENO) {
    close(tty_fd);
  }

  for (int i = 0; i < num_lines; ++i) {
    free(lines[i]);
  }
  free(lines);

  return 0;
}
