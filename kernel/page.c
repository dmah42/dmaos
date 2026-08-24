#include "page.h"

#include "kernel.h"
#include "stdlib.h"
#include "string.h"

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

pte_t *walk_page_table(pte_t *root, vaddr_t vaddr, bool alloc) {
  pte_t *table = root;
  for (int level = PAGE_LEVELS - 1; level > 0; --level) {
    uint32_t shift = 12 + level * VPN_SHIFT;
    uint32_t vpn = (vaddr >> shift) & VPN_MASK;
    pte_t *entry = &table[vpn];

    if ((*entry & PAGE_V) == 0) {
      if (!alloc) {
        return NULL;
      }
      paddr_t pt_paddr = alloc_pages(1);
      *entry = PADDR_TO_PTE(pt_paddr) | PAGE_V;
    }
    table = (pte_t *)PTE_TO_PADDR(*entry);
  }
  uint32_t vpn0 = (vaddr >> 12) & VPN_MASK;
  return &table[vpn0];
}

void map_page(pte_t *root, vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
  if (!is_aligned(vaddr, PAGE_SIZE)) {
    PANIC("unaligned vaddr %x", (uint32_t)vaddr);
  }

  if (!is_aligned(paddr, PAGE_SIZE)) {
    PANIC("unaligned paddr %x", (uint32_t)paddr);
  }

  pte_t *leaf = walk_page_table(root, vaddr, true);
  *leaf = PADDR_TO_PTE(paddr) | flags | PAGE_V;
}

void free_page_table(pte_t *table, int level) {
  if (table == NULL) {
    return;
  }

  size_t entries = 1 << VPN_SHIFT;
  for (size_t i = 0; i < entries; ++i) {
    pte_t entry = table[i];
    if ((entry & PAGE_V) == 0) {
      continue;
    }

    paddr_t paddr = PTE_TO_PADDR(entry);

    if (level > 1 && (entry & (PAGE_R | PAGE_W | PAGE_X)) == 0) {
      free_page_table((pte_t *)paddr, level - 1);
    } else {
      if ((entry & PAGE_U) && paddr >= (paddr_t)__free_ram &&
          paddr < (paddr_t)__free_ram_end) {
        free_pages(paddr, 1);
      }
    }
  }

  free_pages((paddr_t)table, 1);
}