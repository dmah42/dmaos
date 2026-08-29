#include "user.h"
#include "arpa/inet.h"
#include "k_fcntl.h"
#include "k_stat.h"
#include "netdb.h"
#include "netinet/in.h"
#include "sys/k_ioctl.h"
#include "sys/socket.h"
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

static intptr_t syscall1(int sysno, uintptr_t arg0) {
  register intptr_t a0 __asm__("a0") = (intptr_t)arg0;
  register intptr_t a3 __asm__("a3") = sysno;
  __asm__ __volatile__("ecall" : "=r"(a0) : "r"(a0), "r"(a3) : "memory");
  return a0;
}

static intptr_t syscall2(int sysno, uintptr_t arg0, uintptr_t arg1) {
  register intptr_t a0 __asm__("a0") = (intptr_t)arg0;
  register intptr_t a1 __asm__("a1") = (intptr_t)arg1;
  register intptr_t a3 __asm__("a3") = sysno;
  __asm__ __volatile__("ecall"
                       : "=r"(a0)
                       : "r"(a0), "r"(a1), "r"(a3)
                       : "memory");
  return a0;
}

static intptr_t syscall3(int sysno, uintptr_t arg0, uintptr_t arg1,
                         uintptr_t arg2) {
  register intptr_t a0 __asm__("a0") = (intptr_t)arg0;
  register intptr_t a1 __asm__("a1") = (intptr_t)arg1;
  register intptr_t a2 __asm__("a2") = (intptr_t)arg2;
  register intptr_t a3 __asm__("a3") = sysno;
  __asm__ __volatile__("ecall"
                       : "+r"(a0)
                       : "r"(a1), "r"(a2), "r"(a3)
                       : "memory");
  return a0;
}

static intptr_t syscall6(int sysno, uintptr_t arg0, uintptr_t arg1,
                         uintptr_t arg2, uintptr_t arg3, uintptr_t arg4,
                         uintptr_t arg5) {
  register intptr_t a0 __asm__("a0") = (intptr_t)arg0;
  register intptr_t a1 __asm__("a1") = (intptr_t)arg1;
  register intptr_t a2 __asm__("a2") = (intptr_t)arg2;
  register intptr_t a3 __asm__("a3") = sysno;
  register intptr_t a4 __asm__("a4") = (intptr_t)arg3;
  register intptr_t a5 __asm__("a5") = (intptr_t)arg4;
  register intptr_t a6 __asm__("a6") = (intptr_t)arg5;
  __asm__ __volatile__("ecall"
                       : "+r"(a0)
                       : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6)
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

static intptr_t handle_retval(intptr_t ret) {
  if (ret < 0) {
    errno = translate_error((int)ret);
    return -1;
  }
  return ret;
}

uint64_t uptime(void) {
#if __riscv_xlen == 64
  uint64_t val;
  __asm__ __volatile__("rdtime %0" : "=r"(val));
  return val;
#else
  uint32_t low, high, temp;
  __asm__ __volatile__("1:\n"
                       "rdtimeh %0\n"
                       "rdtime %1\n"
                       "rdtimeh %2\n"
                       "bne %0, %2, 1b\n"
                       : "=&r"(high), "=&r"(low), "=&r"(temp));
  return ((uint64_t)high << 32) | low;
#endif
}

// ---------------------------------------------------------------------------
// dmaOS user API
// ---------------------------------------------------------------------------

void yield(void) { syscall1(SYSCALL_YIELD, 0); }
int spawn(const char *name, struct FdRemap *remaps, int remap_count) {
  return syscall3(SYSCALL_SPAWN, (uintptr_t)name, (uintptr_t)remaps,
                  (uintptr_t)remap_count);
}
int pipe(int fds[2]) {
  return handle_retval(syscall1(SYSCALL_PIPE, (uintptr_t)fds));
}
int wait(int pid) { return syscall1(SYSCALL_WAIT, pid); }
int kmesg(char *buf, int buf_len) {
  return syscall2(SYSCALL_KMESG, (uintptr_t)buf, buf_len);
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

int _isatty(int fd) { return syscall1(SYSCALL_ISATTY, fd); }

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

  return handle_retval(syscall2(SYSCALL_OPEN, (uintptr_t)path, kernel_flags));
}

int _read(int fd, void *buf, size_t count) {
  if (fd == STDIN_FILENO && _isatty(STDIN_FILENO)) {
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
  return handle_retval(syscall3(SYSCALL_READ, fd, (uintptr_t)buf, count));
}

int _stat(const char *path, struct stat *st) {
  struct k_stat kst;
  int ret = syscall2(SYSCALL_STAT, (uintptr_t)path, (uintptr_t)&kst);
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
  return handle_retval(syscall1(SYSCALL_RM, (uintptr_t)path));
}

int framebuffer_present(uint32_t *framebuffer, uint32_t width,
                        uint32_t height) {
  return handle_retval(syscall3(SYSCALL_PRESENT_FRAMEBUFFER,
                                (uintptr_t)framebuffer, width, height));
}

int _write(int fd, const void *buf, size_t count) {
  if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && _isatty(fd)) {
    const char *cbuf = (const char *)buf;
    for (size_t i = 0; i < count; i++)
      syscall1(SYSCALL_PUTCHAR, cbuf[i]);
    return count;
  }
  return handle_retval(syscall3(SYSCALL_WRITE, fd, (uintptr_t)buf, count));
}

int _fcntl(int fd, int cmd, int arg) {
  if (fd == STDIN_FILENO) {
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

  if (fd == STDIN_FILENO) {
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
  return (void *)handle_retval(syscall1(SYSCALL_SBRK, (uintptr_t)incr));
}

int chdir(const char *path) {
  return handle_retval(syscall1(SYSCALL_CHDIR, (uintptr_t)path));
}

int mkdir(const char *path, mode_t mode) {
  return handle_retval(
      syscall2(SYSCALL_MKDIR, (uintptr_t)path, (uintptr_t)mode));
}

int ftruncate(int fd, off_t length) {
  return syscall2(SYSCALL_FTRUNCATE, fd, length);
}

char *getcwd(char *buf, size_t size) {
  int ret = syscall2(SYSCALL_GETCWD, (uintptr_t)buf, (uintptr_t)size);
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
      syscall2(SYSCALL_INPUT_POLL, (uintptr_t)code, (uintptr_t)pressed));
}

int net_send(const uint8_t *buf, uint32_t len) {
  return handle_retval(
      syscall2(SYSCALL_NET_SEND, (uintptr_t)buf, (uintptr_t)len));
}

int net_recv(uint8_t *buf, uint32_t len) {
  return handle_retval(
      syscall2(SYSCALL_NET_RECV, (uintptr_t)buf, (uintptr_t)len));
}

// ---------------------------------------------------------------------------
// POSIX Sockets & Networking
// ---------------------------------------------------------------------------

int socket(int domain, int type, int protocol) {
  return handle_retval(syscall3(SYSCALL_SOCKET, domain, type, protocol));
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  return handle_retval(
      syscall3(SYSCALL_BIND, sockfd, (uintptr_t)addr, addrlen));
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  return handle_retval(
      syscall3(SYSCALL_CONNECT, sockfd, (uintptr_t)addr, addrlen));
}

int listen(int sockfd, int backlog) {
  return handle_retval(syscall2(SYSCALL_LISTEN, sockfd, backlog));
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  return handle_retval(
      syscall3(SYSCALL_ACCEPT, sockfd, (uintptr_t)addr, (uintptr_t)addrlen));
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
  return handle_retval(syscall6(SYSCALL_SENDTO, sockfd, (uintptr_t)buf, len,
                                flags, (uintptr_t)dest_addr, addrlen));
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
  return handle_retval(syscall6(SYSCALL_RECVFROM, sockfd, (uintptr_t)buf, len,
                                flags, (uintptr_t)src_addr,
                                (uintptr_t)addrlen));
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
  return sendto(sockfd, buf, len, flags, NULL, 0);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
  return recvfrom(sockfd, buf, len, flags, NULL, NULL);
}

in_addr_t inet_addr(const char *cp) {
  struct in_addr val;
  if (inet_aton(cp, &val)) {
    return val.s_addr;
  }
  return INADDR_NONE;
}

int inet_aton(const char *cp, struct in_addr *inp) {
  uint32_t parts[4] = {0};
  int part = 0;
  while (*cp) {
    if (*cp >= '0' && *cp <= '9') {
      parts[part] = parts[part] * 10 + (*cp - '0');
      if (parts[part] > 255)
        return 0;
    } else if (*cp == '.') {
      part++;
      if (part > 3)
        return 0;
    } else {
      return 0;
    }
    cp++;
  }
  if (part != 3)
    return 0;
  if (inp) {
    inp->s_addr =
        (parts[0]) | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
  }
  return 1;
}

char *inet_ntoa(struct in_addr in) {
  static char buffer[16];
  uint32_t addr = in.s_addr;
  snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", addr & 0xFF,
           (addr >> 8) & 0xFF, (addr >> 16) & 0xFF, (addr >> 24) & 0xFF);
  return buffer;
}

int h_errno = 0;

struct hostent s_hostent;
char s_h_name[128];
uint32_t s_h_addr;
char *s_h_addr_list[2];

struct hostent *gethostbyname(const char *name) {
  if (!name || *name == '\0') {
    h_errno = HOST_NOT_FOUND;
    return NULL;
  }

  // Check if name is already an IPv4 dot-notation address
  struct in_addr direct_ip;
  if (inet_aton(name, &direct_ip)) {
    strncpy(s_h_name, name, sizeof(s_h_name) - 1);
    s_h_name[sizeof(s_h_name) - 1] = '\0';
    s_h_addr = direct_ip.s_addr;
    s_h_addr_list[0] = (char *)&s_h_addr;
    s_h_addr_list[1] = NULL;

    s_hostent.h_name = s_h_name;
    s_hostent.h_aliases = NULL;
    s_hostent.h_addrtype = AF_INET;
    s_hostent.h_length = sizeof(uint32_t);
    s_hostent.h_addr_list = s_h_addr_list;
    h_errno = 0;
    return &s_hostent;
  }

  uint32_t ip = 0;
  int ret = syscall2(SYSCALL_DNS_LOOKUP, (uintptr_t)name, (uintptr_t)&ip);
  if (ret < 0) {
    h_errno = HOST_NOT_FOUND;
    return NULL;
  }

  strncpy(s_h_name, name, sizeof(s_h_name) - 1);
  s_h_name[sizeof(s_h_name) - 1] = '\0';
  s_h_addr = ip;
  s_h_addr_list[0] = (char *)&s_h_addr;
  s_h_addr_list[1] = NULL;

  s_hostent.h_name = s_h_name;
  s_hostent.h_aliases = NULL;
  s_hostent.h_addrtype = AF_INET;
  s_hostent.h_length = sizeof(uint32_t);
  s_hostent.h_addr_list = s_h_addr_list;
  h_errno = 0;
  return &s_hostent;
}