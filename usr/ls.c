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

int main(int argc, char **argv) {
  char path[128] = "";
  if (argc == 2) {
    strncpy(path, argv[1], sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
  } else if (argc > 2) {
    printf("usage: ls [directory]\n");
    return 1;
  }

  // Try to stat the path to make sure it exists
  struct stat st;
  if (stat(path, &st) < 0) {
    printf("ls: %s: no such file or directory\n", path);
    return 1;
  }

  if (st.type != FS_DIR) {
    // If it's a file, just list it
    printf("  %s %db\n", path, st.size);
    return 0;
  }

  // It's a directory! Read directory entries using read_file
  char chunk[FS_CHUNK_SIZE];
  int offset = 0;
  int read_bytes;

  struct dirent entries[64];
  struct stat stats[64];
  int entry_count = 0;

  while ((read_bytes = read_file(path, chunk, offset)) > 0) {
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
      char full_path[256];
      if (strlen(path) == 0) {
        strncpy(full_path, de->name, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
      } else {
        int path_len = strlen(path);
        strncpy(full_path, path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
        if (path[path_len - 1] != '/') {
          strncat(full_path, "/", sizeof(full_path) - strlen(full_path) - 1);
        }
        strncat(full_path, de->name, sizeof(full_path) - strlen(full_path) - 1);
      }

      struct stat est;
      if (stat(full_path, &est) < 0) {
        continue;
      }

      if (entry_count < 64) {
        entries[entry_count] = *de;
        stats[entry_count] = est;
        entry_count++;
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
    char name_buf[64];
    strncpy(name_buf, entries[i].name, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    if (stats[i].type == FS_DIR) {
      strncat(name_buf, "/", sizeof(name_buf) - strlen(name_buf) - 1);
    }
    printf("  %-*s %*db\n", (int)longest_name, name_buf, longest_size_len, stats[i].size);
  }

  return 0;
}
