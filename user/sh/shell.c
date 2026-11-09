#include "colors.h"
#include "fd_remap.h"
#include "fs_shared.h"
#include "user.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_PATH_DIRS (32)
#define MAX_CMD_NAME (64)
#define MAX_CONFIG_LEN (1024)
#define MAX_HISTORY (64)
#define MAX_CMD_LEN (128)

const char *path_dirs[MAX_PATH_DIRS];
int num_path_dirs = 0;

// Default fallbacks
char path_buf[MAX_PATH] = "/bin";
char prompt_fmt[256] = "# ";
char msg_fmt[256] = "\nwelcome to dmash\n";

char history[MAX_HISTORY][MAX_CMD_LEN];
int history_count = 0;

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

void init_config(void) {
  char config[MAX_CONFIG_LEN];
  memset(config, 0, sizeof(config));
  int offset = 0;
  int bytes_read = 0;
  int fd = open("/cfg/dmash.cfg", O_RDONLY);
  if (fd >= 0) {
    while (offset < MAX_CONFIG_LEN - 1) {
      bytes_read = read(fd, config + offset, MAX_CONFIG_LEN - 1 - offset);
      if (bytes_read <= 0) {
        break;
      }
      offset += bytes_read;
    }
    close(fd);
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

    path_dirs[num_path_dirs] = p;
    ++num_path_dirs;

    char *colon = strchr(p, ':');
    if (colon) {
      *colon = '\0';
      p = colon + 1;
    } else {
      break;
    }
  }
}

static int execute_command(const char *cmdline, struct FdRemap *remaps,
                           int nremaps) {
  while (*cmdline == ' ') {
    ++cmdline;
  }
  // Extract command name (first word of cmdline)
  char cmd_name[MAX_CMD_NAME];
  int i = 0;
  while (cmdline[i] && cmdline[i] != ' ' && i < (int)sizeof(cmd_name) - 1) {
    cmd_name[i] = cmdline[i];
    ++i;
  }
  cmd_name[i] = '\0';

  if (i == 0) {
    return -1;
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
    int pid = spawn(cmdline, remaps, nremaps);
    if (pid < 0) {
      printf("unknown command: %s\n", cmdline);
    }
    return pid;
  }

  // Search in pre-parsed path_dirs
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
    if (stat(full_path, &st) >= 0 && S_ISREG(st.st_mode)) {
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

      int pid = spawn(new_cmdline, remaps, nremaps);
      if (pid < 0) {
        printf("unknown command: %s\n", cmdline);
      }
      return pid;
    }
  }

  printf("unknown command: %s\n", cmdline);
  return -1;
}

struct Builtin {
  const char *name;
  void (*func)(const char *args, int out_fd);
};

static void builtin_exit(const char *args, int out_fd) {
  (void)args;
  (void)out_fd;
  exit(0);
}

static void builtin_clear(const char *args, int out_fd) {
  (void)args;
  write(out_fd, "\033[2J\033[H", 7);
}

static void builtin_kmesg(const char *args, int out_fd) {
  (void)args;
  char buf[4097];
  int len = kmesg(buf, sizeof(buf) - 1);
  if (len > 0) {
    write(out_fd, buf, len);
  }
}

static void builtin_cd(const char *args, int out_fd) {
  while (*args == ' ') {
    ++args;
  }
  const char *path = (*args == '\0') ? "/" : args;
  int ret = chdir(path);
  if (ret < 0) {
    char err_msg[256];
    snprintf(err_msg, sizeof(err_msg), "cd: %s: %s\n", path, strerror(errno));
    write(out_fd, err_msg, strlen(err_msg));
  }
}

static const struct Builtin builtins[] = {
    {"exit", builtin_exit},
    {"clear", builtin_clear},
    {"kmesg", builtin_kmesg},
    {"cd", builtin_cd},
};

static bool is_builtin(const char *cmdline) {
  while (*cmdline == ' ') {
    ++cmdline;
  }
  if (*cmdline == '\0') {
    return false;
  }

  char cmd_name[MAX_CMD_NAME];
  int i = 0;
  while (cmdline[i] && cmdline[i] != ' ' && i < (int)sizeof(cmd_name) - 1) {
    cmd_name[i] = cmdline[i];
    ++i;
  }
  cmd_name[i] = '\0';

  for (size_t b = 0; b < sizeof(builtins) / sizeof(builtins[0]); ++b) {
    if (strcmp(cmd_name, builtins[b].name) == 0) {
      return true;
    }
  }
  return false;
}

static bool run_builtin(const char *cmdline, int out_fd) {
  while (*cmdline == ' ') {
    ++cmdline;
  }
  if (*cmdline == '\0') {
    return false;
  }

  char cmd_name[MAX_CMD_NAME];
  int i = 0;
  while (cmdline[i] && cmdline[i] != ' ' && i < (int)sizeof(cmd_name) - 1) {
    cmd_name[i] = cmdline[i];
    ++i;
  }
  cmd_name[i] = '\0';

  const char *args = cmdline + i;
  while (*args == ' ') {
    ++args;
  }

  for (size_t b = 0; b < sizeof(builtins) / sizeof(builtins[0]); ++b) {
    if (strcmp(cmd_name, builtins[b].name) == 0) {
      builtins[b].func(args, out_fd);
      return true;
    }
  }
  return false;
}

#define MAX_STAGES 8

void run_command(const char *cmdline) {
  // Parse stages separated by '|'
  char stages[MAX_STAGES][MAX_CMD_LEN];
  int num_stages = 0;

  const char *p = cmdline;
  while (*p && num_stages < MAX_STAGES) {
    const char *next_pipe = strchr(p, '|');
    size_t stage_len;
    if (next_pipe) {
      stage_len = (size_t)(next_pipe - p);
    } else {
      stage_len = strlen(p);
    }
    if (stage_len >= MAX_CMD_LEN) {
      stage_len = MAX_CMD_LEN - 1;
    }
    memcpy(stages[num_stages], p, stage_len);
    stages[num_stages][stage_len] = '\0';

    // Trim leading whitespace
    char *s = stages[num_stages];
    while (*s == ' ') {
      memmove(s, s + 1, strlen(s));
    }
    // Trim trailing whitespace
    size_t slen = strlen(s);
    while (slen > 0 && s[slen - 1] == ' ') {
      s[--slen] = '\0';
    }

    if (slen > 0) {
      ++num_stages;
    }

    if (!next_pipe) {
      break;
    }
    p = next_pipe + 1;
  }

  if (num_stages == 0) {
    return;
  }

  // Single command (no pipes)
  if (num_stages == 1) {
    if (run_builtin(stages[0], STDOUT_FILENO)) {
      return;
    }
    int pid = execute_command(stages[0], NULL, 0);
    if (pid >= 0) {
      wait(pid);
    }
    return;
  }

  // Create (num_stages - 1) pipes
  int pipes[MAX_STAGES - 1][2];
  for (int i = 0; i < num_stages - 1; ++i) {
    if (pipe(pipes[i]) < 0) {
      printf("pipe: failed\n");
      for (int j = 0; j < i; ++j) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      return;
    }
  }

  int pids[MAX_STAGES];
  for (int i = 0; i < num_stages; ++i) {
    pids[i] = -1;
  }

  bool first_is_builtin = is_builtin(stages[0]);
  int start_stage = first_is_builtin ? 1 : 0;

  for (int i = start_stage; i < num_stages; ++i) {
    struct FdRemap remaps[2];
    int nremaps = 0;

    // stdin: from previous stage's pipe read-end
    if (i > 0) {
      remaps[nremaps].from = pipes[i - 1][0];
      remaps[nremaps].to = 0;
      ++nremaps;
    }

    // stdout: to current stage's pipe write-end
    if (i < num_stages - 1) {
      remaps[nremaps].from = pipes[i][1];
      remaps[nremaps].to = 1;
      ++nremaps;
    }

    pids[i] = execute_command(stages[i], remaps, nremaps);
  }

  if (first_is_builtin) {
    // Close all pipe ends in shell EXCEPT pipes[0][1] (which stage 0 writes to)
    for (int i = 0; i < num_stages - 1; ++i) {
      close(pipes[i][0]);
      if (i > 0) {
        close(pipes[i][1]);
      }
    }

    // Stream stage 0 builtin output to pipes[0][1]
    run_builtin(stages[0], pipes[0][1]);
    close(pipes[0][1]);
  } else {
    // Shell closes all pipe ends so only children hold references
    for (int i = 0; i < num_stages - 1; ++i) {
      close(pipes[i][0]);
      close(pipes[i][1]);
    }
  }

  // Wait for all spawned child processes
  for (int i = 0; i < num_stages; ++i) {
    if (pids[i] >= 0) {
      wait(pids[i]);
    }
  }
}

void history_add(const char *cmd) {
  if (!cmd || *cmd == '\0') {
    return;
  }
  // Avoid duplicate if same as the most recent command
  if (history_count > 0 && strcmp(history[history_count - 1], cmd) == 0) {
    return;
  }
  if (history_count < MAX_HISTORY) {
    strncpy(history[history_count], cmd, MAX_CMD_LEN - 1);
    history[history_count][MAX_CMD_LEN - 1] = '\0';
    ++history_count;
  } else {
    // Shift left to make room
    for (int i = 0; i < MAX_HISTORY - 1; ++i) {
      strcpy(history[i], history[i + 1]);
    }
    strncpy(history[MAX_HISTORY - 1], cmd, MAX_CMD_LEN - 1);
    history[MAX_HISTORY - 1][MAX_CMD_LEN - 1] = '\0';
  }
}

void read_line(char *buf, size_t max_len) {
  int pos = 0;
  int history_idx = history_count;
  char saved_line[MAX_CMD_LEN] = "";

  buf[0] = '\0';

  while (1) {
    char ch = getchar();

    if (ch == '\r' || ch == '\n') {
      putchar('\n');
      buf[pos] = '\0';
      break;
    } else if (ch == 0x03) {
      // Ctrl+C with no job running: discard the line.
      printf("^C\n");
      buf[0] = '\0';
      break;
    } else if (ch == '\b' || ch == 127) {
      if (pos > 0) {
        putchar('\b');
        putchar(' ');
        putchar('\b');
        --pos;
        buf[pos] = '\0';
      }
    } else if (ch == '\033') {
      // Parse ANSI escape sequence
      char seq1 = getchar();
      if (seq1 == '[' || seq1 == 'O') {
        char seq2 = getchar();
        if (seq2 == 'A') {
          // UP ARROW: Recall previous command in history
          if (history_idx > 0) {
            if (history_idx == history_count) {
              // Save in-progress line
              buf[pos] = '\0';
              strncpy(saved_line, buf, sizeof(saved_line) - 1);
              saved_line[sizeof(saved_line) - 1] = '\0';
            }
            --history_idx;

            // Clear current line on screen
            while (pos > 0) {
              putchar('\b');
              putchar(' ');
              putchar('\b');
              --pos;
            }

            // Copy and print history command
            strncpy(buf, history[history_idx], max_len - 1);
            buf[max_len - 1] = '\0';
            pos = strlen(buf);
            printf("%s", buf);
          }
        } else if (seq2 == 'B') {
          // DOWN ARROW: Recall next command in history
          if (history_idx < history_count) {
            ++history_idx;

            // Clear current line on screen
            while (pos > 0) {
              putchar('\b');
              putchar(' ');
              putchar('\b');
              --pos;
            }

            if (history_idx == history_count) {
              // Restore in-progress line
              strncpy(buf, saved_line, max_len - 1);
              buf[max_len - 1] = '\0';
            } else {
              strncpy(buf, history[history_idx], max_len - 1);
              buf[max_len - 1] = '\0';
            }
            pos = strlen(buf);
            printf("%s", buf);
          }
        } else if (seq2 >= '0' && seq2 <= '9') {
          // Extended escape sequence: consume until terminating character
          while (1) {
            char next = getchar();
            if (next == '~' || (next >= 'A' && next <= 'Z') ||
                (next >= 'a' && next <= 'z')) {
              break;
            }
          }
        }
      }
    } else if (ch >= 32 && ch < 127) {
      if (pos < (int)max_len - 1) {
        putchar(ch);
        buf[pos] = ch;
        ++pos;
        buf[pos] = '\0';
      } else {
        putchar('\a'); // Ring bell on buffer full
      }
    }
  }
}

int main(void) {
  init_config();

  // Clear the screen of any bios messages.
  printf("\033[2J\033[3J\033[H");

  print_formatted(msg_fmt, "");

  while (1) {
    // Get CWD
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd)) < 0) {
      strncpy(cwd, "/", sizeof(cwd) - 1);
      cwd[sizeof(cwd) - 1] = '\0';
    }

    print_formatted(prompt_fmt, cwd);

    char cmdline[MAX_CMD_LEN];
    read_line(cmdline, sizeof(cmdline));

    if (cmdline[0] != '\0') {
      history_add(cmdline);
      run_command(cmdline);
    }
  }
  return 0;
}