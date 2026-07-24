#pragma once

#include <stdint.h>

typedef int bool;

typedef uint32_t size_t;
typedef uint32_t paddr_t;
typedef uint32_t vaddr_t;

#define true (1)
#define false (0)
#define NULL ((void *)0)
#define FS_CHUNK_SIZE (512)

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define MAX_FILENAME (128)
#define MAX_PATH (256)
#define MAX_CMD_NAME (64)
#define MAX_PATH_DEPTH (32)

#define align_up(value, align) __builtin_align_up(value, align)
#define is_aligned(value, align) __builtin_is_aligned(value, align)
#define offsetof(type, member) __builtin_offsetof(type, member)

#define _unused __attribute__((unused))

// ANSI colour codes
#define DEFAULT "\033[0m"
#define BOLD "\033[1m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define RALIGN "\033[999C\033[6D"

struct stat {
  int type;
  int size;
};

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

int memcmp(const void *s1, const void *s2, size_t n);
void *memmove(void *dst, const void *src, size_t n);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
size_t strlen(const char *s);
char *strncpy(char *dst, const char *src, size_t n);
char *strncat(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

__attribute__((noreturn)) void exit(int);
int atexit(void (*func)(void));
void run_exit_handlers(void);

void srand(uint32_t seed);
uint32_t rand(void);
uint64_t uptime(void);
void sleep_ms(uint32_t ms);

const char *strerror(int err);
