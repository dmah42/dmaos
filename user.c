#include "user.h"

#include "stdlib.h"
#include "syscall.h"

extern char __stack_top[];
extern char __bss[], __bss_end[];

int syscall(int sysno, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
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

__attribute__((noreturn)) void exit(void) {
  syscall(SYSCALL_EXIT, 0, 0, 0);
  for (;;)
    ; // unreachable but just in case.
}

void putchar(char ch) { syscall(SYSCALL_PUTCHAR, ch, 0, 0); }
char getchar() { return syscall(SYSCALL_GETCHAR, 0, 0, 0); }

int read_file(const char *name, char *buf, int offset) {
  return syscall(SYSCALL_READ_FILE, (uint32_t)name, (uint32_t)buf, offset);
}

int get_file_name(int index, char *buf, int buf_len) {
  return syscall(SYSCALL_GET_FILE_NAME, index, (uint32_t)buf, buf_len);
}

int get_file_size(int index) {
  return syscall(SYSCALL_GET_FILE_SIZE, index, 0, 0);
}

int spawn(const char *name) {
  return syscall(SYSCALL_SPAWN, (uint32_t)name, 0, 0);
}

int wait(int pid) {
  return syscall(SYSCALL_WAIT, pid, 0, 0);
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