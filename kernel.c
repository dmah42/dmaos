#include "kernel.h"

#include "fs.h"
#include "process.h"
#include "stdlib.h"
#include "syscall.h"
#include "virtio.h"

#define SCAUSE_ECALL (8)

extern char __bss[], __bss_end[], __stack_top[];
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;
  register long a4 __asm__("a4") = arg4;
  register long a5 __asm__("a5") = arg5;
  register long a6 __asm__("a6") = fid;
  register long a7 __asm__("a7") = eid;

  __asm__ __volatile__("ecall"
                       : "=r"(a0), "=r"(a1)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                         "r"(a6), "r"(a7)
                       : "memory");
  return (struct sbiret){.error = a0, .value = a1};
}

#define KMESG_SIZE 4096
char kmesg_buf[KMESG_SIZE];
int kmesg_len = 0;

void dump_kmesg(int limit) {
  int start = 0;
  if (limit > 0 && kmesg_len > limit) {
    start = kmesg_len - limit;
  }
  for (int i = start; i < kmesg_len; i++) {
    sbi_call((long)kmesg_buf[i], 0, 0, 0, 0, 0, 0, 1 /* putchar */);
  }
}

void putchar(const char ch) {
  sbi_call((long)ch, 0, 0, 0, 0, 0, 0, 1 /* putchar */);
}

void klog_putchar(char ch) {
  if (kmesg_len < KMESG_SIZE - 1) {
    kmesg_buf[kmesg_len++] = ch;
    kmesg_buf[kmesg_len] = '\0';
  }
}

void kprintf(const char *fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);
  vprintf(klog_putchar, fmt, vargs);
  va_end(vargs);
}

long getchar(void) {
  struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
  return ret.error;
}

void handle_syscall(struct trap_frame *f) {
  switch (f->a3) {
  case SYSCALL_PUTCHAR:
    putchar(f->a0);
    break;
  case SYSCALL_GETCHAR:
    f->a0 = getchar();
    break;
  case SYSCALL_YIELD:
    yield();
    break;
  case SYSCALL_EXIT:
    exit_current_process();
    // TODO: unmap and deallocate pages.
    yield();
    PANIC("unreachable");
    break;
  case SYSCALL_READ_FILE:
    f->a0 = fs_read_file((const char *)f->a0, (char *)f->a1, f->a2);
    break;
  case SYSCALL_GET_FILE_NAME:
    f->a0 = fs_get_file_name(f->a0, (char *)f->a1, f->a2);
    break;
  case SYSCALL_GET_FILE_SIZE:
    f->a0 = fs_get_file_size(f->a0);
    break;
  case SYSCALL_SPAWN:
    f->a0 = spawn_process((const char *)f->a0);
    break;
  case SYSCALL_WAIT:
    f->a0 = wait_process(f->a0);
    break;
  case SYSCALL_KMESG: {
    char *user_buf = (char *)f->a0;
    int limit = f->a1;
    if (limit > kmesg_len) {
      limit = kmesg_len;
    }
    memcpy(user_buf, kmesg_buf, limit);
    f->a0 = limit;
    break;
  }
  default:
    PANIC("unexpected syscall %x\n", f->a3);
  }
}

__attribute__((naked)) __attribute__((aligned(4))) void kentry(void) {
  __asm__ __volatile__(
      // Retrive the kernel stack of the current process from sscratch.
      "csrrw sp, sscratch, sp\n"
      "addi sp, sp, -4 * 31\n"
      "sw ra, 4 * 0(sp)\n"
      "sw gp,  4 * 1(sp)\n"
      "sw tp,  4 * 2(sp)\n"
      "sw t0,  4 * 3(sp)\n"
      "sw t1,  4 * 4(sp)\n"
      "sw t2,  4 * 5(sp)\n"
      "sw t3,  4 * 6(sp)\n"
      "sw t4,  4 * 7(sp)\n"
      "sw t5,  4 * 8(sp)\n"
      "sw t6,  4 * 9(sp)\n"
      "sw a0,  4 * 10(sp)\n"
      "sw a1,  4 * 11(sp)\n"
      "sw a2,  4 * 12(sp)\n"
      "sw a3,  4 * 13(sp)\n"
      "sw a4,  4 * 14(sp)\n"
      "sw a5,  4 * 15(sp)\n"
      "sw a6,  4 * 16(sp)\n"
      "sw a7,  4 * 17(sp)\n"
      "sw s0,  4 * 18(sp)\n"
      "sw s1,  4 * 19(sp)\n"
      "sw s2,  4 * 20(sp)\n"
      "sw s3,  4 * 21(sp)\n"
      "sw s4,  4 * 22(sp)\n"
      "sw s5,  4 * 23(sp)\n"
      "sw s6,  4 * 24(sp)\n"
      "sw s7,  4 * 25(sp)\n"
      "sw s8,  4 * 26(sp)\n"
      "sw s9,  4 * 27(sp)\n"
      "sw s10, 4 * 28(sp)\n"
      "sw s11, 4 * 29(sp)\n"

      "csrr a0, sscratch\n"
      "sw a0, 4 * 30(sp)\n"

      // Reset the kernel stack.
      "addi a0, sp, 4 * 31\n"
      "csrw sscratch, a0\n"

      "mv a0, sp\n"
      "call handle_trap\n"

      "lw ra,  4 * 0(sp)\n"
      "lw gp,  4 * 1(sp)\n"
      "lw tp,  4 * 2(sp)\n"
      "lw t0,  4 * 3(sp)\n"
      "lw t1,  4 * 4(sp)\n"
      "lw t2,  4 * 5(sp)\n"
      "lw t3,  4 * 6(sp)\n"
      "lw t4,  4 * 7(sp)\n"
      "lw t5,  4 * 8(sp)\n"
      "lw t6,  4 * 9(sp)\n"
      "lw a0,  4 * 10(sp)\n"
      "lw a1,  4 * 11(sp)\n"
      "lw a2,  4 * 12(sp)\n"
      "lw a3,  4 * 13(sp)\n"
      "lw a4,  4 * 14(sp)\n"
      "lw a5,  4 * 15(sp)\n"
      "lw a6,  4 * 16(sp)\n"
      "lw a7,  4 * 17(sp)\n"
      "lw s0,  4 * 18(sp)\n"
      "lw s1,  4 * 19(sp)\n"
      "lw s2,  4 * 20(sp)\n"
      "lw s3,  4 * 21(sp)\n"
      "lw s4,  4 * 22(sp)\n"
      "lw s5,  4 * 23(sp)\n"
      "lw s6,  4 * 24(sp)\n"
      "lw s7,  4 * 25(sp)\n"
      "lw s8,  4 * 26(sp)\n"
      "lw s9,  4 * 27(sp)\n"
      "lw s10, 4 * 28(sp)\n"
      "lw s11, 4 * 29(sp)\n"
      "lw sp,  4 * 30(sp)\n"
      "sret\n");
}

void kmain(void) {
  // Clear BSS
  memset(__bss, 0, (size_t)__bss_end - (size_t)__bss);

  // Tell the CPU where the exception handler is
  WRITE_CSR(stvec, (uint32_t)kentry);

  // Clear the screen of any bios messages.
  printf("\033[2J\033[3J\033[H");

  kprintf("dmaOS kernel boot started\n");

  kprintf("Initializing VirtIO block device\t");
  virtio_blk_init();
  kprintf(RALIGN GREEN "[DONE]\n" DEFAULT);

  kprintf("Initializing tar file system\t");
  fs_init();
  kprintf(RALIGN GREEN "[DONE]\n" DEFAULT);

  kprintf("Initializing process scheduler\n");
  process_init();

  kprintf("Spawning shell process\n");
  create_process(_binary_shell_bin_start, (size_t)_binary_shell_bin_size, 0,
                 NULL);

  yield();

  kprintf("Powering off\n");
  sbi_call(0, 0, 0, 0, 0, 0, 0, 8); // sbi_shutdown

  for (;;) {
    __asm__ __volatile__("wfi");
  }
}

__attribute__((section(".text.boot"))) __attribute__((naked)) void boot(void) {
  __asm__ __volatile__("mv sp, %[stack_top]\n"
                       "j kmain\n"
                       :
                       : [stack_top] "r"(__stack_top));
}

void handle_trap(struct trap_frame *f) {
  (void)f;
  uint32_t scause = READ_CSR(scause);
  uint32_t stval = READ_CSR(stval);
  uint32_t user_pc = READ_CSR(sepc);

  if (scause == SCAUSE_ECALL) {
    handle_syscall(f);
    user_pc += 4;
  } else {
    PANIC("unexpected trap:\n  scause %x\n  stval %x\n  sepc %x\n", scause,
          stval, user_pc);
  }

  WRITE_CSR(sepc, user_pc);
}

#define SSTATUS_SPIE (1 << 5)
#define SSTATUS_SUM (1 << 18)

__attribute__((naked)) void user_entry(void) {
  __asm__ __volatile__("mv sp, s0\n"
                       "mv a0, s1\n"
                       "mv a1, s2\n"
                       "lui t0, 0x1000\n" // USER_BASE (0x1000000)
                       "csrw sepc, t0\n"
                       "lui t1, 0x40\n" // SSTATUS_SPIE | SSTATUS_SUM (0x40020)
                       "addi t1, t1, 0x20\n"
                       "csrw sstatus, t1\n"
                       "sret\n");
}

void shutdown(void) {
  sbi_call(0, 0, 0, 0, 0, 0, 0, 8); // sbi_shutdown
  for (;;) {
    __asm__ __volatile__("wfi");
  }
}