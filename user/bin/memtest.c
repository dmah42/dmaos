#include "memory.h"
#include "stdlib.h"
#include "user.h"

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("Starting memory allocation tests...\n");

  // Test 1: Basic malloc and free
  int *p1 = malloc(sizeof(int) * 100);
  if (p1 == NULL) {
    printf(RED "Test 1 failed: malloc returned NULL\n" DEFAULT);
    return 1;
  }
  printf("p1 allocated at %x\n", p1);

  for (int i = 0; i < 100; ++i) {
    p1[i] = i * i;
  }
  for (int i = 0; i < 100; ++i) {
    if (p1[i] != i * i) {
      printf(RED "Test 1 failed: data corruption at index %d\n" DEFAULT, i);
      return 1;
    }
  }
  free(p1);
  printf(GREEN "Test 1 passed: Basic malloc/free working\n" DEFAULT);

  // Test 2: Multiple allocations and coalescing
  void *a = malloc(128);
  void *b = malloc(256);
  void *c = malloc(64);
  printf("Allocated a=%x, b=%x, c=%x\n", a, b, c);

  free(b);               // Free middle block
  void *d = malloc(128); // Should fit in b's space
  printf("Allocated d=%x (should be close to b)\n", d);

  free(a);
  free(d);
  free(c);
  printf(GREEN "Test 2 passed: Multiple allocations\n" DEFAULT);

  // Test 3: Large allocation causing heap expansion
  void *large = malloc(1024 * 16); // 16KB
  if (large == NULL) {
    printf(RED "Test 3 failed: large malloc returned NULL\n" DEFAULT);
    return 1;
  }
  printf("Large block allocated at %x\n", large);
  free(large);
  printf(GREEN "Test 3 passed: Heap expansion\n" DEFAULT);

  // Test 4: calloc and realloc
  int *p2 = calloc(50, sizeof(int));
  for (int i = 0; i < 50; ++i) {
    if (p2[i] != 0) {
      printf(RED "Test 4 failed: calloc did not zero memory\n" DEFAULT);
      return 1;
    }
    p2[i] = i;
  }

  p2 = realloc(p2, 100 * sizeof(int));
  for (int i = 0; i < 50; ++i) {
    if (p2[i] != i) {
      printf(RED "Test 4 failed: realloc corrupted old data\n" DEFAULT);
      return 1;
    }
  }
  free(p2);
  printf(GREEN "Test 4 passed: calloc/realloc working\n" DEFAULT);

  printf(BOLD GREEN "All malloc/free tests passed successfully!\n" DEFAULT);
  return 0;
}
