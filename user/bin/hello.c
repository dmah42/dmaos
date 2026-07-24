#include "stdlib.h"
#include "user.h"

int main(int argc, char **argv) {
  if (argc == 1) {
    printf("Hello from a dynamically loaded user binary!\n");
  } else {
    printf("Hello %s from a dynamically loaded user binary!\n", argv[1]);
  }
  return 0;
}
