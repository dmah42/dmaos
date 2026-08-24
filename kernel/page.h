#pragma once

#include "stdlib.h"
#include <stdint.h>

#define PAGE_SIZE (4096)

#if __riscv_xlen == 64
typedef uint64_t pte_t;
#define PAGE_LEVELS (3)
#define VPN_SHIFT (9)
#define VPN_MASK (0x1FF)
#define SATP_MODE (8ULL << 60)
#else
typedef uint32_t pte_t;
#define PAGE_LEVELS (2)
#define VPN_SHIFT (10)
#define VPN_MASK (0x3FF)
#define SATP_MODE (1UL << 31)
#endif

#define PTE_TO_PADDR(pte) (((paddr_t)(pte) >> 10) << 12)
#define PADDR_TO_PTE(paddr) ((((paddr_t)(paddr) >> 12) << 10))
#define SATP_PPN(page_table) (((paddr_t)(page_table)) >> 12)

enum PageFlags {
  PAGE_V = (1 << 0), // Valid bit
  PAGE_R = (1 << 1), // Readable
  PAGE_W = (1 << 2), // Writable
  PAGE_X = (1 << 3), // Executable
  PAGE_U = (1 << 4), // User (accessible in user mode)
};

paddr_t alloc_pages(uint32_t n);
void free_pages(paddr_t paddr, uint32_t n);
pte_t *walk_page_table(pte_t *root, vaddr_t vaddr, bool alloc);
void map_page(pte_t *root, vaddr_t vaddr, paddr_t paddr, uint32_t flags);
void free_page_table(pte_t *table, int level);