#pragma once

#include "stdlib.h"

#define PAGE_SIZE (4096)

#define PAGE_TABLE_TO_INDEX(table) (((uint32_t)table) / PAGE_SIZE)

#define SATP_SV32 (1u << 31)

enum PageFlags {
  PAGE_V = (1 << 0), // Valid bit
  PAGE_R = (1 << 1), // Readable
  PAGE_W = (1 << 2), // Writable
  PAGE_X = (1 << 3), // Executable
  PAGE_U = (1 << 4), // User (accessible in user mode)
};

paddr_t alloc_pages(uint32_t n);
void free_pages(paddr_t paddr, uint32_t n);
void map_page(uint32_t *t1, uint32_t vaddr, paddr_t paddr, uint32_t flags);