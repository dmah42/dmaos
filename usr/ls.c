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

int main(int argc _unused, char **argv _unused) {
  size_t longest_name = 0;
  int longest_size_len = 0;

  char name[MAX_FILENAME];
  for (int i = 0; get_file_name(i, name, sizeof(name)) >= 0; i++) {
    size_t len = strlen(name);
    if (len > longest_name) {
      longest_name = len;
    }
    int size = get_file_size(i);
    int digits = num_digits(size);
    if (digits > longest_size_len) {
      longest_size_len = digits;
    }
  }

  for (int i = 0; get_file_name(i, name, sizeof(name)) >= 0; i++) {
    int size = get_file_size(i);
    printf("  %-*s %*db\n", longest_name, name, longest_size_len, size);
  }
  return 0;
}
