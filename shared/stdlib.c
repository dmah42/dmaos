#include "stdlib.h"

#include "errno.h"

void putchar(char);
void yield(void);

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  while (n--) {
    *d++ = *s++;
  }
  return dst;
}

void *memset(void *buf, char c, size_t n) {
  uint8_t *p = (uint8_t *)buf;
  while (n--) {
    *p++ = c;
  }
  return buf;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s2) {
    if (*s1 != *s2)
      break;
    ++s1;
    ++s2;
  }
  return *(uint8_t *)s1 - *(uint8_t *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  while (n && *s1 && (*s1 == *s2)) {
    ++s1;
    ++s2;
    --n;
  }
  if (n == 0) {
    return 0;
  }
  return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}

size_t strlen(const char *s) {
  size_t len = 0;
  while (*s++) {
    ++len;
  }
  return len;
}

char *strncpy(char *dst, const char *src, size_t n) {
  char *orig = dst;
  while (n > 0 && *src) {
    *dst++ = *src++;
    n--;
  }
  while (n > 0) {
    *dst++ = '\0';
    n--;
  }
  return orig;
}

char *strncat(char *dst, const char *src, size_t n) {
  char *orig = dst;
  while (*dst) {
    ++dst;
  }
  while (n > 0 && *src) {
    *dst++ = *src++;
    --n;
  }
  *dst = '\0';
  return orig;
}

char *strchr(const char *s, int c) {
  while (*s) {
    if (*s == (char)c) {
      return (char *)s;
    }
    ++s;
  }
  if (c == '\0') {
    return (char *)s;
  }
  return NULL;
}

static uint32_t rand_state = 12345;

void srand(uint32_t seed) { rand_state = seed; }

uint32_t rand(void) {
  rand_state = rand_state * 1103515245 + 12345;
  return rand_state % 32768;
}

uint64_t uptime(void) {
  uint32_t low, high, temp;
  __asm__ __volatile__("1:\n"
                       "rdtimeh %0\n"
                       "rdtime %1\n"
                       "rdtimeh %2\n"
                       "bne %0, %2, 1b\n"
                       : "=&r"(high), "=&r"(low), "=&r"(temp));
  return ((uint64_t)high << 32) | low;
}

void sleep_ms(uint32_t ms) {
  uint64_t start = uptime();
  uint64_t ticks = (uint64_t)ms * 10000;
  while (uptime() - start < ticks) {
    yield();
  }
}

const char *strerror(int err) {
  switch (err) {
  case 0:
    return "success";
  case ERR_NOT_PERMITTED:
    return "operation not permitted";
  case ERR_NOT_FOUND:
    return "no such file or directory";
  case ERR_IO:
    return "I/O error";
  case ERR_BAD_FILE:
    return "bad file number";
  case ERR_PERMISSION_DENIED:
    return "permission denied";
  case ERR_ALREADY_EXISTS:
    return "file exists";
  case ERR_NOT_A_DIRECTORY:
    return "not a directory";
  case ERR_IS_A_DIRECTORY:
    return "is a directory";
  case ERR_INVALID_ARGUMENT:
    return "invalid argument";
  case ERR_NO_SPACE:
    return "no space left on device";
  case ERR_DIRECTORY_NOT_EMPTY:
    return "directory not empty";
  default:
    return "unknown error";
  }
}

char *strstr(const char *haystack, const char *needle) {
  size_t needle_len = strlen(needle);
  if (needle_len == 0) {
    return (char *)haystack;
  }
  while (*haystack) {
    if (strncmp(haystack, needle, needle_len) == 0) {
      return (char *)haystack;
    }
    ++haystack;
  }
  return NULL;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *p1 = s1;
  const uint8_t *p2 = s2;
  while (n--) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    ++p1;
    ++p2;
  }
  return 0;
}

void *memmove(void *dst, const void *src, size_t n) {
  uint8_t *d = dst;
  const uint8_t *s = src;
  if (d < s) {
    while (n--) {
      *d++ = *s++;
    }
  } else if (d > s) {
    d += n;
    s += n;
    while (n--) {
      *--d = *--s;
    }
  }
  return dst;
}

int isdigit(int c) { return c >= '0' && c <= '9'; }

int isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
         c == '\f';
}

int isprint(int c) { return c >= 32 && c < 127; }

#define MAX_ATEXIT_FUNCS (32)

static void (*atexit_funcs[MAX_ATEXIT_FUNCS])(void);
static int atexit_count = 0;

int atexit(void (*func)(void)) {
  if (atexit_count >= MAX_ATEXIT_FUNCS) {
    return -1;
  }
  atexit_funcs[atexit_count++] = func;
  return 0;
}

void run_exit_handlers(void) {
  for (int i = atexit_count - 1; i >= 0; --i) {
    if (atexit_funcs[i]) {
      atexit_funcs[i]();
    }
  }
}
