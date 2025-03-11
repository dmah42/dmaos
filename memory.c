#include "memory.h"

#include "kernel.h"
#include "stdlib.h"

extern char __free_ram[], __free_ram_end[];

paddr_t alloc_pages(uint32_t n) {
  static paddr_t npaddr = (paddr_t) __free_ram;
  paddr_t paddr = npaddr;
  npaddr += n * PAGE_SIZE;

  if (npaddr > (paddr_t) __free_ram_end) {
    PANIC("+++ OUT OF CHEESE +++");
  }

  memset((void*) paddr, 0xfe, n * PAGE_SIZE);
  return paddr;
}