#pragma once

#include "stdlib.h"

#define PAGE_SIZE (4096)

paddr_t alloc_pages(uint32_t n);