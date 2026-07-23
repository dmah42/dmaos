#include "user.h"

#include "stdlib.h"
#include "syscall.h"

extern char __stack_top[];
extern char __bss[], __bss_end[];

int syscall1(int sysno, uint32_t arg0) {
  register int a0 __asm__("a0") = arg0;
  register int a3 __asm__("a3") = sysno;

  __asm__ __volatile__("ecall"
                       : "=r"(a0)
                       : "r"(a0), "r"(a3)
                       : "memory");
  return a0;
}

int syscall2(int sysno, uint32_t arg0, uint32_t arg1) {
  register int a0 __asm__("a0") = arg0;
  register int a1 __asm__("a1") = arg1;
  register int a3 __asm__("a3") = sysno;

  __asm__ __volatile__("ecall"
                       : "=r"(a0)
                       : "r"(a0), "r"(a1), "r"(a3)
                       : "memory");
  return a0;
}

int syscall3(int sysno, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
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

int syscall4(int sysno, uint32_t arg0, uint32_t arg1, uint32_t arg2,
             uint32_t arg3) {
  register int a0 __asm__("a0") = arg0;
  register int a1 __asm__("a1") = arg1;
  register int a2 __asm__("a2") = arg2;
  register int a4 __asm__("a4") = arg3;
  register int a3 __asm__("a3") = sysno;

  __asm__ __volatile__("ecall"
                       : "=r"(a0)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4)
                       : "memory");
  return a0;
}

__attribute__((noreturn)) void exit(void) {
  syscall1(SYSCALL_EXIT, 0);
  for (;;)
    ; // unreachable but just in case.
}

void putchar(char ch) { syscall1(SYSCALL_PUTCHAR, ch); }
int getchar_nonblock(void) { return syscall1(SYSCALL_GETCHAR, 0); }

void yield(void) { syscall1(SYSCALL_YIELD, 0); }

int getchar(void) {
  while (1) {
    int ch = getchar_nonblock();
    if (ch >= 0) {
      return ch;
    }
    yield();
  }
}

int read_file(const char *name, char *buf, int offset) {
  return syscall3(SYSCALL_READ_FILE, (uint32_t)name, (uint32_t)buf, offset);
}

int get_file_name(int index, char *buf, int buf_len) {
  return syscall3(SYSCALL_GET_FILE_NAME, index, (uint32_t)buf, buf_len);
}

int get_file_size(int index) {
  return syscall1(SYSCALL_GET_FILE_SIZE, index);
}

int stat(const char *name, struct stat *st) {
  return syscall2(SYSCALL_STAT, (uint32_t)name, (uint32_t)st);
}

int spawn(const char *name) {
  return syscall1(SYSCALL_SPAWN, (uint32_t)name);
}

int wait(int pid) { return syscall1(SYSCALL_WAIT, pid); }

int kmesg(char *buf, int buf_len) {
  return syscall2(SYSCALL_KMESG, (uint32_t)buf, buf_len);
}

int chdir(const char *path) {
  return syscall1(SYSCALL_CHDIR, (uint32_t)path);
}

int getcwd(char *buf, int size) {
  return syscall2(SYSCALL_GETCWD, (uint32_t)buf, size);
}

int write_file(const char *name, const char *buf, int len, int offset) {
  return syscall4(SYSCALL_WRITE_FILE, (uint32_t)name, (uint32_t)buf, len,
                  offset);
}

int mkdir(const char *path) {
  return syscall1(SYSCALL_MKDIR, (uint32_t)path);
}

int rm(const char *path) {
  return syscall1(SYSCALL_RM, (uint32_t)path);
}

extern int main(int argc, char **argv);

void umain(int argc, char **argv) {
  memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);

  main(argc, argv);
  exit();
}

__attribute__((section(".text.start"))) __attribute__((naked)) void
start(void) {
  __asm__ __volatile__("call umain \n");
}