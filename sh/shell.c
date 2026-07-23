#include "stdlib.h"
#include "user.h"

#define MAX_PATH_DIRS 32

const char *path_dirs[MAX_PATH_DIRS];
int num_path_dirs = 0;

// Default fallbacks
char path_buf[MAX_PATH] = "/bin";
char prompt_fmt[256] = "# ";
char msg_fmt[256] = "\nwelcome to dmash\n";

void print_formatted(const char *fmt, const char *cwd) {
  const char *p = fmt;
  if (*p == '"') {
    ++p;
  }

  int len = strlen(p);
  if (len > 0 && p[len - 1] == '"') {
    --len;
  }

  for (int i = 0; i < len;) {
    if (p[i] == '\\' && i + 1 < len) {
      if (p[i + 1] == 'n') {
        printf("\n");
      } else if (p[i + 1] == 'r') {
        printf("\r");
      } else if (p[i + 1] == 't') {
        printf("\t");
      } else {
        putchar(p[i]);
        putchar(p[i + 1]);
      }
      i += 2;
    } else if (p[i] == '<') {
      int start = i + 1;
      int end = start;
      while (end < len && p[end] != '>') {
        ++end;
      }
      if (end < len && p[end] == '>') {
        char tag[32];
        int tag_len = end - start;
        if (tag_len >= (int)sizeof(tag)) {
          tag_len = sizeof(tag) - 1;
        }
        memcpy(tag, p + start, tag_len);
        tag[tag_len] = '\0';

        if (strcmp(tag, "cwd") == 0) {
          printf("%s", cwd);
        } else if (strcmp(tag, "blue") == 0) {
          printf(BLUE);
        } else if (strcmp(tag, "green") == 0) {
          printf(GREEN);
        } else if (strcmp(tag, "red") == 0) {
          printf(RED);
        } else if (strcmp(tag, "yellow") == 0) {
          printf(YELLOW);
        } else if (strcmp(tag, "magenta") == 0) {
          printf(MAGENTA);
        } else if (strcmp(tag, "cyan") == 0) {
          printf(CYAN);
        } else if (strcmp(tag, "bold") == 0) {
          printf(BOLD);
        } else if (strcmp(tag, "default") == 0) {
          printf(DEFAULT);
        } else {
          printf("<%s>", tag);
        }
        i = end + 1;
      } else {
        putchar('<');
        ++i;
      }
    } else {
      putchar(p[i]);
      ++i;
    }
  }
}

#define MAX_CONFIG_LEN (1024)

void init_config(void) {
  char config[MAX_CONFIG_LEN];
  memset(config, 0, sizeof(config));
  int offset = 0;
  int bytes_read = 0;
  while (offset < MAX_CONFIG_LEN - 1) {
    bytes_read = read_file("/cfg/dmash.cfg", config + offset, offset);
    if (bytes_read < 0) {
      break;
    }
    if (bytes_read == 0) {
      break;
    }
    offset += bytes_read;
  }
  config[offset] = '\0';

  if (offset > 0) {
    char *line = config;
    while (*line) {
      char *eol = line;
      while (*eol && *eol != '\n' && *eol != '\r') {
        ++eol;
      }
      char orig_char = *eol;
      *eol = '\0';

      if (strncmp(line, "PATH=", 5) == 0) {
        strncpy(path_buf, line + 5, sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
      } else if (strncmp(line, "PROMPT=", 7) == 0) {
        strncpy(prompt_fmt, line + 7, sizeof(prompt_fmt) - 1);
        prompt_fmt[sizeof(prompt_fmt) - 1] = '\0';
      } else if (strncmp(line, "MSG=", 4) == 0) {
        strncpy(msg_fmt, line + 4, sizeof(msg_fmt) - 1);
        msg_fmt[sizeof(msg_fmt) - 1] = '\0';
      }

      if (orig_char == '\r' || orig_char == '\n') {
        *eol = orig_char;
        line = eol + 1;
        if (orig_char == '\r' && *line == '\n') {
          ++line;
        }
      } else {
        break;
      }
    }
  }

  // Split path_buf by ':' and store pointers in path_dirs
  char *p = path_buf;
  num_path_dirs = 0;
  while (*p && num_path_dirs < MAX_PATH_DIRS) {
    while (*p == ':') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }

    path_dirs[num_path_dirs++] = p;

    char *colon = strchr(p, ':');
    if (colon) {
      *colon = '\0';
      p = colon + 1;
    } else {
      break;
    }
  }
}

void run_command(const char *cmdline) {
  // Extract command name (first word of cmdline)
  char cmd_name[MAX_CMD_NAME];
  int i = 0;
  while (cmdline[i] && cmdline[i] != ' ' && i < (int)sizeof(cmd_name) - 1) {
    cmd_name[i] = cmdline[i];
    ++i;
  }
  cmd_name[i] = '\0';

  if (i == 0) {
    return;
  }

  // Check if cmd_name has a slash
  bool has_slash = false;
  for (int j = 0; j < i; ++j) {
    if (cmd_name[j] == '/') {
      has_slash = true;
      break;
    }
  }

  if (has_slash) {
    int pid = spawn(cmdline);
    if (pid >= 0) {
      wait(pid);
    } else {
      printf("unknown command: %s\n", cmdline);
    }
    return;
  }

  // Search in pre-parsed path_dirs
  bool found = false;
  for (int d = 0; d < num_path_dirs; ++d) {
    const char *dir = path_dirs[d];

    // Construct full path: dir + "/" + cmd_name
    char full_path[MAX_PATH];
    strncpy(full_path, dir, sizeof(full_path) - 1);
    full_path[sizeof(full_path) - 1] = '\0';

    int d_len = strlen(full_path);
    if (d_len > 0 && full_path[d_len - 1] != '/') {
      strncat(full_path, "/", sizeof(full_path) - d_len - 1);
    }
    strncat(full_path, cmd_name, sizeof(full_path) - strlen(full_path) - 1);

    struct stat st;
    if (stat(full_path, &st) >= 0 && st.type == FS_FILE) {
      // Reconstruct the cmdline: full_path + rest of cmdline arguments
      char new_cmdline[MAX_PATH];
      strncpy(new_cmdline, full_path, sizeof(new_cmdline) - 1);
      new_cmdline[sizeof(new_cmdline) - 1] = '\0';

      const char *args = cmdline + i;
      while (*args == ' ') {
        ++args;
      }

      if (*args != '\0') {
        strncat(new_cmdline, " ",
                sizeof(new_cmdline) - strlen(new_cmdline) - 1);
        strncat(new_cmdline, args,
                sizeof(new_cmdline) - strlen(new_cmdline) - 1);
      }

      int pid = spawn(new_cmdline);
      if (pid >= 0) {
        wait(pid);
        found = true;
        break;
      }
    }
  }

  if (!found) {
    printf("unknown command: %s\n", cmdline);
  }
}

void kmesg_cmd(void) {
  char buf[4097];
  int len = kmesg(buf, sizeof(buf) - 1);
  if (len >= 0) {
    buf[len] = '\0';
  }
  printf("%s", buf);
}

void cd_cmd(const char *cmdline) {
  const char *path = cmdline + 2;
  while (*path == ' ') {
    ++path;
  }
  if (*path == '\0') {
    path = "/";
  }
  int ret = chdir(path);
  if (ret < 0) {
    printf("cd: %s: %s\n", path, strerror(ret));
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

int main(void) {
  init_config();
  detect_utf8();

  print_formatted(msg_fmt, "");

  while (1) {
    // Get CWD
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd)) < 0) {
      strncpy(cwd, "/", sizeof(cwd) - 1);
      cwd[sizeof(cwd) - 1] = '\0';
    }

    print_formatted(prompt_fmt, cwd);

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
          --i;
        }
      } else {
        if (i < (int)sizeof(cmdline) - 1) {
          putchar(ch);
          cmdline[i] = ch;
          ++i;
        } else {
          putchar('\a'); // Ring terminal bell when buffer is full
        }
      }
    }

    if (strcmp(cmdline, "exit") == 0) {
      exit();
    } else if (strcmp(cmdline, "kmesg") == 0) {
      kmesg_cmd();
    } else if (strncmp(cmdline, "cd", 2) == 0 &&
               (cmdline[2] == ' ' || cmdline[2] == '\0')) {
      cd_cmd(cmdline);
    } else {
      run_command(cmdline);
    }
  }
  return 0;
}