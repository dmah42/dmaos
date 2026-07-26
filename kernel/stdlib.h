#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

#define align_up(value, align) __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)

#define _unused __attribute__((unused))

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *buf, char c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);
char *strncpy(char *dst, const char *src, size_t n);
char *strncat(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
