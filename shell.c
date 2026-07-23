#include "stdlib.h"
#include "user.h"

#define MAX_PATH_DIRS 32

const char *path_dirs[MAX_PATH_DIRS];
int num_path_dirs = 0;
char path_buf[MAX_PATH] = "/bin"; // Default fallback

void init_path(void) {
  char config[MAX_PATH];
  memset(config, 0, sizeof(config));
  int n = read_file("/cfg/dmash.cfg", config, 0);
  if (n > 0) {
    config[n] = '\0';

    // Parse config lines to find PATH=
    char *line = config;
    bool found_path = false;
    while (*line) {
      char *eol = line;
      while (*eol && *eol != '\n' && *eol != '\r') {
        eol++;
      }
      char orig_char = *eol;
      *eol = '\0';

      if (strncmp(line, "PATH=", 5) == 0) {
        strncpy(path_buf, line + 5, sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
        found_path = true;
        break;
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

    if (!found_path) {
      // Check if first line doesn't contain '=' (a plain path string)
      line = config;
      char *eol = line;
      while (*eol && *eol != '\n' && *eol != '\r') {
        ++eol;
      }
      *eol = '\0';

      bool has_equals = false;
      for (char *p = line; *p; ++p) {
        if (*p == '=') {
          has_equals = true;
          break;
        }
      }
      if (!has_equals && strlen(line) > 0) {
        strncpy(path_buf, line, sizeof(path_buf) - 1);
        path_buf[sizeof(path_buf) - 1] = '\0';
      }
    }
  }

  // Now, split path_buf by ':' and store pointers in path_dirs
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
  if (chdir(path) < 0) {
    printf("cd: %s: no such directory\n", path);
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

const char *utf8_welcome = "\n" BOLD GREEN "ᴡᴇʟᴄᴏᴍᴇ ᴛᴏ ᴅᴍᴀsʜ" DEFAULT "\n";
const char *ascii_welcome = "\nwelcome to dmash\n";

const char *utf8_prompt = BOLD " ∅" DEFAULT " ";
const char *ascii_prompt = BOLD " #" DEFAULT " ";

int main(void) {
  init_path();
  bool use_utf8 = detect_utf8();

  if (use_utf8) {
    printf(utf8_welcome);
  } else {
    printf(ascii_welcome);
  }

  while (1) {
    // Get CWD
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd)) < 0) {
      strncpy(cwd, "/", sizeof(cwd) - 1);
      cwd[sizeof(cwd) - 1] = '\0';
    }

    const char *prompt = use_utf8 ? utf8_prompt : ascii_prompt;
    printf(BLUE "%s" DEFAULT "%s", cwd, prompt);

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