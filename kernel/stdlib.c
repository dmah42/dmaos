#include "stdlib.h"

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
    --n;
  }
  while (n > 0) {
    *dst++ = '\0';
    --n;
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
