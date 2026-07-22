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

// Queries the terminal for the current cursor column index
int get_cursor_column(void) {
  printf("\033[6n"); // VT100 cursor position query

  // Response format: \033[<line>;<col>R
  char ch = getchar();
  if (ch != '\033')
    return -1;
  ch = getchar();
  if (ch != '[')
    return -1;

  // Skip line number digits
  while (1) {
    ch = getchar();
    if (ch == ';')
      break;
    if (ch < '0' || ch > '9')
      return -1;
  }

  // Parse column number
  int col = 0;
  while (1) {
    ch = getchar();
    if (ch == 'R')
      break;
    if (ch < '0' || ch > '9')
      return -1;
    col = col * 10 + (ch - '0');
  }

  return col;
}

// Returns true if the terminal resolves multi-byte UTF-8 chars to 1 column
// width
bool detect_utf8(void) {
  int col1 = get_cursor_column();
  if (col1 < 0) {
    return false; // Fallback to ASCII if query fails
  }

  printf("❯"); // Print test character

  int col2 = get_cursor_column();
  if (col2 < 0) {
    return false;
  }

  int width = col2 - col1;

  // Erase the test character from the screen
  for (int i = 0; i < width; i++) {
    printf("\b \b");
  }

  return (width == 1);
}

const char *utf8_welcome = "";
const char *ascii_welcome = "\nWeLcOmE tO dMaShElL\n";

const char *utf8_prompt = "∅ ";
const char *ascii_prompt = "# ";

int main(void) {
  bool use_utf8 = detect_utf8();

  if (use_utf8) {
    printf(utf8_welcome);
  } else {
    printf(ascii_welcome);
  }

  while (1) {
    if (use_utf8) {
      printf(utf8_prompt);
    } else {
      printf(ascii_prompt);
    }
    char cmdline[128];
    int i = 0;
    while (1) {
      char ch = getchar();
      if (ch == '\r') {
        printf("\n");
        cmdline[i] = '\0';
        break;
      } else if (ch == '\b' || ch == 127) {
        if (i > 0) {
          putchar('\b');
          putchar(' ');
          putchar('\b');
          i--;
        }
      } else {
        if (i < (int)sizeof(cmdline) - 1) {
          putchar(ch);
          cmdline[i] = ch;
          i++;
        } else {
          putchar('\a'); // Ring terminal bell when buffer is full
        }
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