#include "user.h"

#include "stdlib.h"
#include "syscall.h"

extern char __stack_top[];
extern char __bss[], __bss_end[];

int syscall(int sysno, int arg0, int arg1, int arg2) {
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

void umain() {
  memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);

  __asm__ __volatile__("call main");
}

__attribute__((section(".text.start"))) __attribute__((naked)) void
start(void) {
  __asm__ __volatile__("mv sp, %[stack_top] \n"
                       "call umain          \n"
                       "call exit           \n" ::[stack_top] "r"(__stack_top));
}