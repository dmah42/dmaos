#include "stdlib.h"

void putchar(char);

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

void printf(const char *fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);

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
        putchar('%');
        goto end;
      case '%':
        putchar('%');
        break;
      case 's': {
        const char *s = va_arg(vargs, const char *);
        int len = strlen(s);
        int pad = width - len;

        if (!left_align) {
          for (int i = 0; i < pad; i++) {
            putchar(' ');
          }
        }

        while (*s) {
          putchar(*s);
          ++s;
        }

        if (left_align) {
          for (int i = 0; i < pad; i++) {
            putchar(' ');
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
            putchar(' ');
          }
        }

        if (value < 0) {
          putchar('-');
        }

        unsigned div = 1;
        while (mag / div > 9)
          div *= 10;

        while (div > 0) {
          putchar('0' + mag / div);
          mag %= div;
          div /= 10;
        }

        if (left_align) {
          for (int i = 0; i < pad; i++) {
            putchar(' ');
          }
        }
        break;
      }
      case 'x': {
        unsigned val = va_arg(vargs, unsigned);
        for (int i = 7; i >= 0; --i) {
          unsigned nibble = (val >> (i * 4)) & 0xf;
          putchar("0123456789abcdef"[nibble]);
        }
        break;
      }
      }
    } else {
      putchar(*fmt);
    }
    ++fmt;
  }
end:
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
  }
}
