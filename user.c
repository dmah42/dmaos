#include "user.h"

#include "stdlib.h"

extern char __stack_top[];
extern char __bss[], __bss_end[];

__attribute__((noreturn)) void exit(void) {
  for (;;)
    ;
}

void putchar() {
  // TODO
}

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