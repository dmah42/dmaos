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

void vprintf(void (*putc)(char), const char *fmt, va_list vargs) {
  while (*fmt) {
    if (*fmt == '%') {
      ++fmt;
      bool left_align = false;
      if (*fmt == '-') {
        left_align = true;
        ++fmt;
      }

      int width = 0;
      if (*fmt == '*') {
        width = va_arg(vargs, int);
        ++fmt;
      }

      switch (*fmt) {
      case '\0':
        putc('%');
        return;
      case '%':
        putc('%');
        break;
      case 's': {
        const char *s = va_arg(vargs, const char *);
        int len = strlen(s);
        int pad = width - len;

        if (!left_align) {
          for (int i = 0; i < pad; i++) {
            putc(' ');
          }
        }

        while (*s) {
          putc(*s);
          ++s;
        }

        if (left_align) {
          for (int i = 0; i < pad; i++) {
            putc(' ');
          }
        }
        break;
      }
      case 'd': {
        int value = va_arg(vargs, int);
        unsigned mag = value;
        int len = 0;

        if (value < 0) {
          len++; // For '-'
          mag = -mag;
        }

        unsigned temp = mag;
        if (temp == 0) {
          len++;
        } else {
          while (temp > 0) {
            len++;
            temp /= 10;
          }
        }

        int pad = width - len;
        if (!left_align) {
          for (int i = 0; i < pad; i++) {
            putc(' ');
          }
        }

        if (value < 0) {
          putc('-');
        }

        unsigned div = 1;
        while (mag / div > 9)
          div *= 10;

        while (div > 0) {
          putc('0' + mag / div);
          mag %= div;
          div /= 10;
        }

        if (left_align) {
          for (int i = 0; i < pad; i++) {
            putc(' ');
          }
        }
        break;
      }
      case 'x': {
        unsigned val = va_arg(vargs, unsigned);
        for (int i = 7; i >= 0; --i) {
          unsigned nibble = (val >> (i * 4)) & 0xf;
          putc("0123456789abcdef"[nibble]);
        }
        break;
      }
      }
    } else {
      putc(*fmt);
    }
    ++fmt;
  }
}

void printf(const char *fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);
  vprintf(putchar, fmt, vargs);
  va_end(vargs);
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
