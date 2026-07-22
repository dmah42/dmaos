#include "stdlib.h"
#include "user.h"

int main(int argc _unused, char **argv _unused) {
  char name[MAX_FILENAME];
  for (int i = 0; get_file_name(i, name, sizeof(name)) >= 0; i++) {
    int size = get_file_size(i);
    printf("  %s (%d bytes)\n", name, size);
  }
  return 0;
}
