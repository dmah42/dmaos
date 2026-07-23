#include "stdlib.h"
#include "user.h"

int num_digits(int n) {
  if (n == 0)
    return 1;
  int count = 0;
  while (n > 0) {
    count++;
    n /= 10;
  }
  return count;
}

void normalize_path(const char *base, const char *rel, char *dst, int dst_len) {
  char buf[MAX_PATH];
  if (rel[0] == '/') {
    strncpy(buf, rel, sizeof(buf) - 1);
  } else {
    strncpy(buf, base, sizeof(buf) - 1);
    int len = strlen(buf);
    if (len > 0 && buf[len - 1] != '/') {
      strncat(buf, "/", sizeof(buf) - len - 1);
    }
    strncat(buf, rel, sizeof(buf) - strlen(buf) - 1);
  }
  buf[sizeof(buf) - 1] = '\0';

  char tokens[MAX_PATH_DEPTH][MAX_FILENAME];
  int num_tokens = 0;
  char *p = buf;
  while (*p == '/') {
    p++;
  }

  while (*p) {
    char *next = p;
    while (*next && *next != '/') {
      next++;
    }
    char orig = *next;
    *next = '\0';

    if (strcmp(p, ".") == 0) {
      // skip
    } else if (strcmp(p, "..") == 0) {
      if (num_tokens > 0) {
        num_tokens--;
      }
    } else if (strlen(p) > 0) {
      if (num_tokens < MAX_PATH_DEPTH) {
        strncpy(tokens[num_tokens], p, sizeof(tokens[num_tokens]) - 1);
        tokens[num_tokens][sizeof(tokens[num_tokens]) - 1] = '\0';
        ++num_tokens;
      }
    }

    if (orig) {
      *next = orig;
      p = next + 1;
      while (*p == '/') {
        p++;
      }
    } else {
      break;
    }
  }

  dst[0] = '/';
  dst[1] = '\0';
  for (int i = 0; i < num_tokens; i++) {
    if (i > 0 || dst[1] != '\0') {
      strncat(dst, "/", dst_len - strlen(dst) - 1);
    }
    strncat(dst, tokens[i], dst_len - strlen(dst) - 1);
  }
}

int main(int argc, char **argv) {
  if (argc > 2) {
    printf("usage: ls [directory]\n");
    return 1;
  }

  char cwd[MAX_PATH];
  if (getcwd(cwd, sizeof(cwd)) < 0) {
    strncpy(cwd, "/", sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
  }

  char target_path[MAX_PATH];
  if (argc == 2) {
    normalize_path(cwd, argv[1], target_path, sizeof(target_path));
  } else {
    strncpy(target_path, cwd, sizeof(target_path) - 1);
    target_path[sizeof(target_path) - 1] = '\0';
  }

  // Try to stat the path to make sure it exists
  struct stat st;
  if (stat(target_path, &st) < 0) {
    printf("ls: %s: no such file or directory\n", argc == 2 ? argv[1] : ".");
    return 1;
  }

  if (st.type != FS_DIR) {
    // If it's a file, just list it
    printf("  %s %db\n", target_path, st.size);
    return 0;
  }

  // Print target directory directory header
  printf("Directory of %s\n", target_path);

  // Read directory entries using read_file
  char chunk[FS_CHUNK_SIZE];
  int offset = 0;
  int read_bytes;

  struct dirent entries[MAX_DIR_ENTRIES];
  struct stat stats[MAX_DIR_ENTRIES];
  int entry_count = 0;

  while ((read_bytes = read_file(target_path, chunk, offset)) > 0) {
    for (int i = 0; i < read_bytes; i += sizeof(struct dirent)) {
      struct dirent *de = (struct dirent *)(chunk + i);
      if (de->inum == 0) {
        continue;
      }

      // Skip "." and ".." to match default ls behavior
      if (strcmp(de->name, ".") == 0 || strcmp(de->name, "..") == 0) {
        continue;
      }

      // Construct full path to stat it
      char full_path[MAX_PATH];
      int path_len = strlen(target_path);
      strncpy(full_path, target_path, sizeof(full_path) - 1);
      full_path[sizeof(full_path) - 1] = '\0';
      if (target_path[path_len - 1] != '/') {
        strncat(full_path, "/", sizeof(full_path) - strlen(full_path) - 1);
      }
      strncat(full_path, de->name, sizeof(full_path) - strlen(full_path) - 1);

      struct stat est;
      if (stat(full_path, &est) < 0) {
        continue;
      }

      if (entry_count < MAX_DIR_ENTRIES) {
        entries[entry_count] = *de;
        stats[entry_count] = est;
        ++entry_count;
      }
    }
    offset += read_bytes;
  }

  // Now print the entries aligned nicely!
  size_t longest_name = 0;
  int longest_size_len = 0;

  for (int i = 0; i < entry_count; i++) {
    size_t len = strlen(entries[i].name);
    if (stats[i].type == FS_DIR) {
      len += 1; // for trailing '/'
    }
    if (len > longest_name) {
      longest_name = len;
    }
    int digits = num_digits(stats[i].size);
    if (digits > longest_size_len) {
      longest_size_len = digits;
    }
  }

  for (int i = 0; i < entry_count; i++) {
    int visible_len = strlen(entries[i].name);
    if (stats[i].type == FS_DIR) {
      ++visible_len;
      printf("  " BOLD YELLOW "%s/" DEFAULT, entries[i].name);
    } else {
      printf("  %s", entries[i].name);
    }

    int padding = longest_name - visible_len;
    for (int p = 0; p < padding; ++p) {
      putchar(' ');
    }

    printf(" %*db\n", longest_size_len, stats[i].size);
  }

  return 0;
}
