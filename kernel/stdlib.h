#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if __riscv_xlen == 64
typedef uint64_t paddr_t;
typedef uint64_t vaddr_t;
typedef uint64_t reg_t;
#else
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;
typedef uint32_t reg_t;
#endif

#define align_up(value, align) __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)

#define _unused __attribute__((unused))
#define _packed __attribute__((packed))

uint64_t uptime(void);
uint32_t uptime_ms(void);
uint32_t rand32(void);

int atoi(const char *s);
int abs(int x);
