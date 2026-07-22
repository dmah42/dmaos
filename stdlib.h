#pragma once

typedef int bool;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
typedef unsigned long long uint64_t;

typedef char int8_t;
typedef short int16_t;
typedef long int32_t;
typedef long long int64_t;

typedef uint32_t size_t;
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

#define true (1)
#define false (0)
#define NULL ((void *)0)
#define FS_CHUNK_SIZE (512)
// NOTE: this is limited by tar header restrictions and can be changed
// once we are not using a tar disk as a filesystem
#define MAX_FILENAME (100)

#define align_up(value, align) __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)
#define offsetof(type, member) __builtin_offsetof(type, member)

#define _unused __attribute__((unused))

// ANSI colour codes
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BOLD "\033[1m"
#define DEFAULT "\033[0m"
#define RALIGN "\033[999C\033[6D"

#define va_list __builtin_va_list
#define va_start __builtin_va_start
#define va_end __builtin_va_end
#define va_arg __builtin_va_arg

void *memset(void *buf, char c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);

void printf(const char *fmt, ...);
void vprintf(void (*putc)(char), const char *fmt, va_list vargs);

void srand(uint32_t seed);
uint32_t rand(void);
uint64_t uptime(void);
void sleep_ms(uint32_t ms);
