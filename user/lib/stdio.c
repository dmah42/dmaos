#include "stdio.h"
#include "errno.h"
#include "fcntl.h"
#include "user.h"

struct _FILE {
  int fd;
};

static FILE _stdin = {0};
static FILE _stdout = {1};
static FILE _stderr = {2};

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

FILE *fopen(const char *path, const char *mode) {
  int flags = 0;
  if (strcmp(mode, "r") == 0) {
    flags = O_RDONLY;
  } else if (strcmp(mode, "w") == 0) {
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  } else if (strcmp(mode, "a") == 0) {
    flags = O_WRONLY | O_CREAT | O_APPEND;
  } else if (strcmp(mode, "r+") == 0) {
    flags = O_RDWR;
  } else if (strcmp(mode, "w+") == 0) {
    flags = O_RDWR | O_CREAT | O_TRUNC;
  } else if (strcmp(mode, "a+") == 0) {
    flags = O_RDWR | O_CREAT | O_APPEND;
  } else {
    flags = O_RDONLY;
  }

  int fd = open(path, flags);
  if (fd < 0) {
    errno = -fd; // Mapping kernel error to positive errno
    return NULL;
  }

  FILE *f = malloc(sizeof(FILE));
  if (!f) {
    close(fd);
    return NULL;
  }
  f->fd = fd;
  return f;
}

int fclose(FILE *stream) {
  if (!stream)
    return -1;
  int ret = close(stream->fd);
  free(stream);
  return ret;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
  if (!lineptr || !n || !stream) {
    return -1;
  }
  if (!*lineptr) {
    *n = 128;
    *lineptr = malloc(*n);
    if (!*lineptr)
      return -1;
  }

  size_t pos = 0;
  char ch;
  while (1) {
    int ret = read(stream->fd, &ch, 1);
    if (ret <= 0) {
      if (pos == 0) {
        return -1;
      }
      break;
    }

    if (pos + 2 > *n) {
      size_t new_n = *n * 2;
      char *new_ptr = realloc(*lineptr, new_n);
      if (!new_ptr)
        return -1;
      *lineptr = new_ptr;
      *n = new_n;
    }

    (*lineptr)[pos++] = ch;
    if (ch == '\n') {
      break;
    }
  }
  (*lineptr)[pos] = '\0';
  return pos;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
  if (size == 0)
    return 0;
  size_t pos = 0;

#define PUTC(c)                                                                \
  do {                                                                         \
    if (pos < size - 1) {                                                      \
      str[pos++] = (c);                                                        \
    }                                                                          \
  } while (0)

  while (*fmt) {
    if (*fmt == '%') {
      ++fmt;
      int left_align = 0;
      if (*fmt == '-') {
        left_align = 1;
        ++fmt;
      }

      int width = 0;
      if (*fmt == '*') {
        width = va_arg(ap, int);
        ++fmt;
      } else {
        while (*fmt >= '0' && *fmt <= '9') {
          width = width * 10 + (*fmt - '0');
          ++fmt;
        }
      }

      int has_precision = 0;
      int precision = -1;
      if (*fmt == '.') {
        has_precision = 1;
        ++fmt;
        if (*fmt == '*') {
          precision = va_arg(ap, int);
          ++fmt;
        } else {
          precision = 0;
          while (*fmt >= '0' && *fmt <= '9') {
            precision = precision * 10 + (*fmt - '0');
            ++fmt;
          }
        }
      }

      switch (*fmt) {
      case '\0':
        PUTC('%');
        goto done;
      case '%':
        PUTC('%');
        break;
      case 's': {
        const char *s = va_arg(ap, const char *);
        if (!s)
          s = "(null)";
        int len = 0;
        while (s[len]) {
          if (has_precision && len >= precision) {
            break;
          }
          ++len;
        }
        int pad = width - len;

        if (!left_align) {
          for (int i = 0; i < pad; i++)
            PUTC(' ');
        }
        for (int i = 0; i < len; i++)
          PUTC(s[i]);
        if (left_align) {
          for (int i = 0; i < pad; i++)
            PUTC(' ');
        }
        break;
      }
      case 'd': {
        int value = va_arg(ap, int);
        unsigned int mag = value;
        int len = 0;

        if (value < 0) {
          len++;
          mag = -mag;
        }

        unsigned int temp = mag;
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
          for (int i = 0; i < pad; i++)
            PUTC(' ');
        }
        if (value < 0) {
          PUTC('-');
        }

        unsigned int div = 1;
        while (mag / div > 9)
          div *= 10;
        while (div > 0) {
          PUTC('0' + mag / div);
          mag %= div;
          div /= 10;
        }
        if (left_align) {
          for (int i = 0; i < pad; i++)
            PUTC(' ');
        }
        break;
      }
      case 'x': {
        unsigned int val = va_arg(ap, unsigned int);
        for (int i = 7; i >= 0; --i) {
          unsigned int nibble = (val >> (i * 4)) & 0xf;
          PUTC("0123456789abcdef"[nibble]);
        }
        break;
      }
      default:
        PUTC(*fmt);
        break;
      }
    } else {
      PUTC(*fmt);
    }
    ++fmt;
  }
done:
  str[pos] = '\0';
  return pos;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(str, size, fmt, ap);
  va_end(ap);
  return ret;
}

static int vfprintf(FILE *stream, const char *fmt, va_list ap) {
  if (!stream)
    return -1;
  char buf[1024];
  int len = vsnprintf(buf, sizeof(buf), fmt, ap);
  return write(stream->fd, buf, len);
}

int fprintf(FILE *stream, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = vfprintf(stream, fmt, ap);
  va_end(ap);
  return ret;
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return ret;
}

int sscanf(const char *str, const char *fmt, ...) {
  if (strcmp(fmt, "%d;%d") != 0) {
    return 0;
  }

  va_list ap;
  va_start(ap, fmt);
  int *rows = va_arg(ap, int *);
  int *cols = va_arg(ap, int *);
  va_end(ap);

  int r = 0;
  while (*str >= '0' && *str <= '9') {
    r = r * 10 + (*str - '0');
    ++str;
  }
  if (*str != ';') {
    return 0;
  }
  ++str;

  int c = 0;
  while (*str >= '0' && *str <= '9') {
    c = c * 10 + (*str - '0');
    ++str;
  }

  *rows = r;
  *cols = c;
  return 2;
}

void perror(const char *s) {
  if (s && *s) {
    fprintf(stderr, "%s: ", s);
  }
  fprintf(stderr, "%s\n", strerror(-errno));
}
