#pragma once

#include <stdint.h>

typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

#define FS_CHUNK_SIZE (512)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define MAX_FILENAME (128)
#define MAX_PATH (256)
#define MAX_CMD_NAME (64)

#define align_up(value, align) __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)

#define _unused __attribute__((unused))
