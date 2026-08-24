#pragma once

#include <stdbool.h>

#define INITRD_PADDR 0x46000000ULL
#define INITRD_SIZE (8 * 1024 * 1024)

bool initrd_init(void);
