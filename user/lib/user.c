#include "user.h"
#include "k_fcntl.h"
#include "k_stat.h"
#include "sys/k_ioctl.h"
#include "syscall.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// ioctl request codes (sys/ioctl.h not available in newlib)
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TIOCGWINSZ 0x5413

// Termios state for stdin (fd 0)
static struct termios stdin_termios = {.c_cc = {[VMIN] = 1, [VTIME] = 0}};

// Flags state for stdin (fd 0)
static int stdin_flags = 0;

// ---------------------------------------------------------------------------
// Syscall wrappers
// ---------------------------------------------------------------------------

static int syscall1(int sysno, uint32_t arg0) {
  register int a0 __asm__("a0") = arg0;
  register int a3 __asm__("a3") = sysno;
  __asm__ __volatile__("ecall" : "=r"(a0) : "r"(a0), "r"(a3) : "memory");
  return a0;
}

static int syscall2(int sysno, uint32_t arg0, uint32_t arg1) {
  register int a0 __asm__("a0") = arg0;
  register int a1 __asm__("a1") = arg1;
  register int a3 __asm__("a3") = sysno;
  __asm__ __volatile__("ecall"
                       : "=r"(a0)
                       : "r"(a0), "r"(a1), "r"(a3)
                       : "memory");
  return a0;
}

static int syscall3(int sysno, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
  register int a0 __asm__("a0") = arg0;
  register int a1 __asm__("a1") = arg1;
  register int a2 __asm__("a2") = arg2;
  register int a3 __asm__("a3") = sysno;
  __asm__ __volatile__("ecall"
                       : "=r"(a0)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3)
                       : "memory");
  return a0;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Convert negative kernel errors to positive POSIX error codes
static int translate_error(int err) {
  if (err >= 0)
    return err;
  int val = -err;
  if (val == 4)
    return ENOMEM; // ERR_NO_MEMORY
  return val;
}

static int handle_retval(int ret) {
  if (ret < 0) {
    errno = translate_error(ret);
    return -1;
  }
  return ret;
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

// ---------------------------------------------------------------------------
// dmaOS user API
// ---------------------------------------------------------------------------

void yield(void) { syscall1(SYSCALL_YIELD, 0); }
int spawn(const char *name) { return syscall1(SYSCALL_SPAWN, (uint32_t)name); }
int wait(int pid) { return syscall1(SYSCALL_WAIT, pid); }
int kmesg(char *buf, int buf_len) {
  return syscall2(SYSCALL_KMESG, (uint32_t)buf, buf_len);
}

int getchar_nonblock(void) {
  char c;
  int old_flags = fcntl(0, F_GETFL);
  if (old_flags < 0)
    return -1;
  if (fcntl(0, F_SETFL, old_flags | O_NONBLOCK) < 0)
    return -1;
  int ret = read(0, &c, 1);
  fcntl(0, F_SETFL, old_flags);
  return (ret <= 0) ? -1 : c;
}

// ---------------------------------------------------------------------------
// Program entry
// ---------------------------------------------------------------------------

extern char __bss[], __bss_end[];
extern int main(int argc, char **argv);

__attribute__((used)) void umain(int argc, char **argv) {
  memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
  exit(main(argc, argv));
}

__attribute__((section(".text.start"))) __attribute__((naked)) void
start(void) {
  __asm__ __volatile__("call umain\n");
}

// ---------------------------------------------------------------------------
// Newlib syscall stubs
// ---------------------------------------------------------------------------

void _exit(int status) {
  (void)status;
  syscall1(SYSCALL_EXIT, 0);
  for (;;)
    ;
}

int _close(int fd) { return handle_retval(syscall1(SYSCALL_CLOSE, fd)); }

int _execve(const char *name, char *const argv[], char *const envp[]) {
  (void)name;
  (void)argv;
  (void)envp;
  errno = ENOSYS;
  return -1;
}

int _fork(void) {
  errno = ENOSYS;
  return -1;
}

int _fstat(int fd, struct stat *st) {
  int size = syscall1(SYSCALL_GET_FILE_SIZE, fd);
  if (size < 0) {
    errno = translate_error(size);
    return -1;
  }
  memset(st, 0, sizeof(*st));
  st->st_size = size;
  st->st_mode = (fd >= 0 && fd <= 2) ? (S_IFCHR | 0666) : (S_IFREG | 0644);
  return 0;
}

int _getpid(void) { return 1; }

int _isatty(int fd) { return (fd >= 0 && fd <= 2) ? 1 : 0; }

int _kill(int pid, int sig) {
  (void)pid;
  (void)sig;
  errno = ENOSYS;
  return -1;
}

int _link(const char *oldpath, const char *newpath) {
  (void)oldpath;
  (void)newpath;
  errno = ENOSYS;
  return -1;
}

int _lseek(int fd, int ptr, int dir) {
  return handle_retval(syscall3(SYSCALL_LSEEK, fd, ptr, dir));
}

int _open(const char *path, int flags, ...) {
  int kernel_flags = 0;
  int access = flags & 3;
  if (access == 0)
    kernel_flags |= FILECTRL_READONLY;
  else if (access == 1)
    kernel_flags |= FILECTRL_WRITEONLY;
  else if (access == 2)
    kernel_flags |= FILECTRL_READWRITE;

  if (flags & O_CREAT)
    kernel_flags |= FILECTRL_CREATE;
  if (flags & O_TRUNC)
    kernel_flags |= FILECTRL_TRUNCATE;
  if (flags & O_APPEND)
    kernel_flags |= FILECTRL_APPEND;

  return handle_retval(syscall2(SYSCALL_OPEN, (uint32_t)path, kernel_flags));
}

int _read(int fd, void *buf, size_t count) {
  if (fd == 0) {
    char *cbuf = (char *)buf;
    size_t num_read = 0;
    int vmin = stdin_termios.c_cc[VMIN];
    int vtime = stdin_termios.c_cc[VTIME];

    if (stdin_flags & O_NONBLOCK) {
      while (num_read < count) {
        int ch = syscall1(SYSCALL_GETCHAR, 0);
        if (ch >= 0) {
          cbuf[num_read++] = (char)ch;
        } else {
          if (num_read == 0) {
            errno = EAGAIN;
            return -1;
          }
          break;
        }
      }
      return num_read;
    }

    if (vmin == 0 && vtime == 0) {
      while (num_read < count) {
        int ch = syscall1(SYSCALL_GETCHAR, 0);
        if (ch >= 0)
          cbuf[num_read++] = (char)ch;
        else
          break;
      }
      return num_read;
    } else if (vmin == 0 && vtime > 0) {
      uint64_t start_time = uptime();
      uint64_t timeout_ticks = (uint64_t)vtime * 1000000;
      while (num_read < count) {
        int ch = syscall1(SYSCALL_GETCHAR, 0);
        if (ch >= 0) {
          cbuf[num_read++] = (char)ch;
          break;
        }
        if (uptime() - start_time >= timeout_ticks)
          break;
        syscall1(SYSCALL_YIELD, 0);
      }
      return num_read;
    } else {
      while (num_read < (size_t)vmin && num_read < count) {
        int ch = syscall1(SYSCALL_GETCHAR, 0);
        if (ch >= 0)
          cbuf[num_read++] = (char)ch;
        else
          syscall1(SYSCALL_YIELD, 0);
      }
      return num_read;
    }
  }
  return handle_retval(syscall3(SYSCALL_READ, fd, (uint32_t)buf, count));
}

int _stat(const char *path, struct stat *st) {
  struct k_stat kst;
  int ret = syscall2(SYSCALL_STAT, (uint32_t)path, (uint32_t)&kst);
  if (ret < 0) {
    errno = translate_error(ret);
    return -1;
  }
  memset(st, 0, sizeof(*st));
  st->st_size = kst.size;
  st->st_mode = (kst.type == 1) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
  return 0;
}

int _unlink(const char *path) {
  return handle_retval(syscall1(SYSCALL_RM, (uint32_t)path));
}

int framebuffer_present(uint32_t *framebuffer, uint32_t width,
                        uint32_t height) {
  return handle_retval(syscall3(SYSCALL_PRESENT_FRAMEBUFFER,
                                (uint32_t)framebuffer, width, height));
}

int _write(int fd, const void *buf, size_t count) {
  if (fd == 1 || fd == 2) {
    const char *cbuf = (const char *)buf;
    for (size_t i = 0; i < count; i++)
      syscall1(SYSCALL_PUTCHAR, cbuf[i]);
    return count;
  }
  return handle_retval(syscall3(SYSCALL_WRITE, fd, (uint32_t)buf, count));
}

int _fcntl(int fd, int cmd, int arg) {
  if (fd == 0) {
    if (cmd == F_GETFL)
      return stdin_flags;
    if (cmd == F_SETFL) {
      stdin_flags = arg;
      return 0;
    }
  }
  errno = ENOSYS;
  return -1;
}

int _ioctl(int fd, unsigned long request, ...) {
  va_list ap;
  va_start(ap, request);
  void *arg = va_arg(ap, void *);
  va_end(ap);

  if (fd == 0) {
    if (request == TCGETS) {
      memcpy(arg, &stdin_termios, sizeof(struct termios));
      return 0;
    } else if (request == TCSETS || request == TCSETSW || request == TCSETSF) {
      memcpy(&stdin_termios, arg, sizeof(struct termios));
      return 0;
    }
  }

  if (request == TIOCGWINSZ) {
    struct winsize *ws = (struct winsize *)arg;
    ws->ws_row = 24;
    ws->ws_col = 80;
    return 0;
  }

  errno = ENOTTY;
  return -1;
}

int ioctl(int fd, unsigned long request, ...) {
  va_list ap;
  va_start(ap, request);
  void *arg = va_arg(ap, void *);
  va_end(ap);
  return _ioctl(fd, request, arg);
}

int _gettimeofday(struct timeval *tv, void *tz) {
  (void)tz;
  if (tv) {
    uint64_t ticks = uptime();
    tv->tv_sec = ticks / 10000000;
    tv->tv_usec = (ticks % 10000000) / 10;
  }
  return 0;
}

int _nanosleep(const struct timespec *req, struct timespec *rem) {
  if (!req) {
    errno = EINVAL;
    return -1;
  }
  uint64_t start_time = uptime();
  uint64_t ticks =
      (uint64_t)req->tv_sec * 10000000 + (uint64_t)req->tv_nsec / 100;
  while (1) {
    if (uptime() - start_time >= ticks)
      break;
    syscall1(SYSCALL_YIELD, 0);
  }
  if (rem) {
    rem->tv_sec = 0;
    rem->tv_nsec = 0;
  }
  return 0;
}

void *_sbrk(int incr) {
  return (void *)handle_retval(syscall1(SYSCALL_SBRK, incr));
}

int chdir(const char *path) {
  return handle_retval(syscall1(SYSCALL_CHDIR, (uint32_t)path));
}

int mkdir(const char *path, mode_t mode) {
  return handle_retval(syscall2(SYSCALL_MKDIR, (uint32_t)path, (uint32_t)mode));
}

int ftruncate(int fd, off_t length) {
  return syscall2(SYSCALL_FTRUNCATE, fd, length);
}

char *getcwd(char *buf, size_t size) {
  int ret = syscall2(SYSCALL_GETCWD, (uint32_t)buf, (uint32_t)size);
  if (ret < 0) {
    errno = translate_error(ret);
    return NULL;
  }
  return buf;
}

void sleep_ms(uint32_t ms) {
  uint64_t start = uptime();
  uint64_t ticks = (uint64_t)ms * 10000;
  while (uptime() - start < ticks) {
    yield();
  }
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
  return __getline(lineptr, n, stream);
}

// shims for third party binaries
int tcgetattr(int fd, struct termios *termios_p) {
  (void)fd;
  if (!termios_p)
    return -1;
  // Initialize to zero or sensible values so rawmode toggling behaves
  memset(termios_p, 0, sizeof(struct termios));
  return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
  (void)fd;
  (void)optional_actions;
  (void)termios_p;
  return 0;
}

int input_poll(uint16_t *code, int *pressed) {
  return handle_retval(
      syscall2(SYSCALL_INPUT_POLL, (uint32_t)code, (uint32_t)pressed));
}