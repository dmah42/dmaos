#pragma once

#include "stdlib.h"
#include <stdint.h>

struct sbiret {
  long error;
  long value;
};

struct trap_frame {
  reg_t ra;
  reg_t gp;
  reg_t tp;
  reg_t t0;
  reg_t t1;
  reg_t t2;
  reg_t t3;
  reg_t t4;
  reg_t t5;
  reg_t t6;
  reg_t a0;
  reg_t a1;
  reg_t a2;
  reg_t a3;
  reg_t a4;
  reg_t a5;
  reg_t a6;
  reg_t a7;
  reg_t s0;
  reg_t s1;
  reg_t s2;
  reg_t s3;
  reg_t s4;
  reg_t s5;
  reg_t s6;
  reg_t s7;
  reg_t s8;
  reg_t s9;
  reg_t s10;
  reg_t s11;
  reg_t sp;
} _packed;

#define READ_CSR(reg)                                                          \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                      \
    __tmp;                                                                     \
  })

#define WRITE_CSR(reg, value)                                                  \
  do {                                                                         \
    unsigned long __tmp = (unsigned long)(value);                              \
    __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                    \
  } while (0)

void dump_kmesg(int limit);
void kprintf(const char *fmt, ...);

#define PANIC(fmt, ...)                                                        \
  do {                                                                         \
    kprintf("+++ WHOOPS!  HERE COMES MR JELLY! +++\n");                        \
    kprintf("%s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);            \
    kprintf("---\n");                                                          \
    dump_kmesg(1024);                                                          \
    shutdown();                                                                \
  } while (0)

#define USER_BASE 0x1000000

void user_entry(void);
__attribute__((noreturn)) void shutdown(void);

// syscall implementations
void putchar(const char ch);
long getchar(void);