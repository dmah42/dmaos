#include "memory.h"

#include "kernel.h"
#include "stdlib.h"

extern char __free_ram[], __free_ram_end[];

#define NUM_PAGES (64 * 1024 * 1024 / PAGE_SIZE)
static uint8_t page_allocated[NUM_PAGES];

paddr_t alloc_pages(uint32_t n) {
  for (uint32_t i = 0; i <= NUM_PAGES - n; i++) {
    bool found = true;
    for (uint32_t j = 0; j < n; j++) {
      if (page_allocated[i + j]) {
        found = false;
        break;
      }
    }
    if (found) {
      for (uint32_t j = 0; j < n; j++) {
        page_allocated[i + j] = 1;
      }
      paddr_t paddr = (paddr_t)__free_ram + i * PAGE_SIZE;
      memset((void *)paddr, 0, n * PAGE_SIZE);
      return paddr;
    }
  }
  PANIC("+++ OUT OF CHEESE +++");
}

void free_pages(paddr_t paddr, uint32_t n) {
  if (paddr < (paddr_t)__free_ram || paddr >= (paddr_t)__free_ram_end) {
    PANIC("free_pages: invalid paddr %x", paddr);
  }
  uint32_t start_page = (paddr - (paddr_t)__free_ram) / PAGE_SIZE;
  if (start_page + n > NUM_PAGES) {
    PANIC("free_pages: out of bounds %x", paddr);
  }
  for (uint32_t i = 0; i < n; i++) {
    page_allocated[start_page + i] = 0;
  }
}

void map_page(uint32_t *t1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
  if (!is_aligned(vaddr, PAGE_SIZE)) {
    PANIC("unaligned vaddr %x", vaddr);
  }

  if (!is_aligned(paddr, PAGE_SIZE)) {
    PANIC("unaligned paddr %x", paddr);
  }

  uint32_t vpn1 = (vaddr >> 22) & 0x3ff;
  if ((t1[vpn1] & PAGE_V) == 0) {
    // Create the non-existent 2nd level page.
    uint32_t pt_paddr = alloc_pages(1);
    t1[vpn1] = (PAGE_TABLE_TO_INDEX(pt_paddr) << 10) | PAGE_V;
  }

  // Set the 2nd level page entry to map the physical page
  uint32_t vpn0 = (vaddr >> 12) & 0x3ff;
  uint32_t *t0 = (uint32_t *)((t1[vpn1] >> 10) * PAGE_SIZE);
  t0[vpn0] = (PAGE_TABLE_TO_INDEX(paddr) << 10) | flags | PAGE_V;
}