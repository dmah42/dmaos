#include "kernel.h"

#include "errno.h"
#include "fs.h"
#include "memory.h"
#include "process.h"
#include "stdlib.h"
#include "syscall.h"
#include "virtio.h"

#define SCAUSE_ECALL (8)

#define SSTATUS_SPIE (1 << 5)
#define SSTATUS_SPP (1 << 8)
#define SSTATUS_SUM (1 << 18)

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

/*
 * Reads the active page table directory pointer from the satp Control Status
 * Register. In RISC-V SV32 mode, the lower 22 bits of satp contain the physical
 * page number (PPN) of the Level-1 page table directory. Since the kernel maps
 * physical memory 1:1, the virtual address is equal to the physical address.
 */
static uint32_t *get_active_page_table(void) {
  uint32_t satp = READ_CSR(satp);
  return (uint32_t *)((satp & 0x3fffff) * PAGE_SIZE);
}

/*
 * Validates that a user-provided buffer range [addr, addr + len) is:
 * 1. Entirely within user space (below 0x80000000 to prevent kernel access).
 * 2. Fully mapped in the active process's page table directory.
 * 3. Configured with user-accessible (PAGE_U) and writable (PAGE_W) page flags.
 */
static bool validate_user_write_buffer(const void *addr, size_t len) {
  uint32_t start = (uint32_t)addr;
  uint32_t end = start + len;
  if (end < start)
    return false; // Overflow check

  if (end > 0x80000000)
    return false;

  uint32_t *t1 = get_active_page_table();
  uint32_t page_start = start & ~(PAGE_SIZE - 1);
  uint32_t page_end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  // Check every page spanned by the buffer
  for (uint32_t va = page_start; va < page_end; va += PAGE_SIZE) {
    // SV32 Level 1: VPN1 occupies bits [31:22] of the virtual address
    uint32_t vpn1 = (va >> 22) & 0x3ff;
    if ((t1[vpn1] & PAGE_V) == 0)
      return false; // L1 page table entry must be valid

    // Get the address of the Level 2 page table page
    uint32_t *t0 = (uint32_t *)((t1[vpn1] >> 10) * PAGE_SIZE);

    // SV32 Level 2: VPN0 occupies bits [21:12] of the virtual address
    uint32_t vpn0 = (va >> 12) & 0x3ff;
    uint32_t entry = t0[vpn0];

    // Ensure the page entry is valid, user-accessible, and writable
    if ((entry & PAGE_V) == 0)
      return false;
    if ((entry & PAGE_U) == 0)
      return false;
    if ((entry & PAGE_W) == 0)
      return false;
  }
  return true;
}

static bool validate_user_read_buffer(const void *addr, size_t len) {
  uint32_t start = (uint32_t)addr;
  uint32_t end = start + len;
  if (end < start)
    return false; // Overflow check

  if (end > 0x80000000)
    return false;

  uint32_t *t1 = get_active_page_table();
  uint32_t page_start = start & ~(PAGE_SIZE - 1);
  uint32_t page_end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  for (uint32_t va = page_start; va < page_end; va += PAGE_SIZE) {
    uint32_t vpn1 = (va >> 22) & 0x3ff;
    if ((t1[vpn1] & PAGE_V) == 0)
      return false;

    uint32_t *t0 = (uint32_t *)((t1[vpn1] >> 10) * PAGE_SIZE);
    uint32_t vpn0 = (va >> 12) & 0x3ff;
    uint32_t entry = t0[vpn0];

    if ((entry & PAGE_V) == 0)
      return false;
    if ((entry & PAGE_U) == 0)
      return false;
  }
  return true;
}

/*
 * Validates that a user-provided null-terminated string lies entirely within
 * valid, mapped user space. It scans the string byte by byte, checking page
 * table validity only when crossing page boundaries to optimize performance.
 */
static bool validate_user_string(const char *str) {
  uint32_t *t1 = get_active_page_table();
  uint32_t va = (uint32_t)str;
  if (va >= 0x80000000)
    return false;

  // Validate the first page containing the string's start
  uint32_t current_page = va & ~(PAGE_SIZE - 1);
  uint32_t vpn1 = (current_page >> 22) & 0x3ff;
  if ((t1[vpn1] & PAGE_V) == 0)
    return false;
  uint32_t *t0 = (uint32_t *)((t1[vpn1] >> 10) * PAGE_SIZE);
  uint32_t vpn0 = (current_page >> 12) & 0x3ff;
  if ((t0[vpn0] & PAGE_V) == 0 || (t0[vpn0] & PAGE_U) == 0)
    return false;

  while (true) {
    // When va aligns to a page boundary, check that the new page is mapped and
    // valid
    if ((va & (PAGE_SIZE - 1)) == 0) {
      current_page = va;
      vpn1 = (current_page >> 22) & 0x3ff;
      if ((t1[vpn1] & PAGE_V) == 0)
        return false;
      t0 = (uint32_t *)((t1[vpn1] >> 10) * PAGE_SIZE);
      vpn0 = (current_page >> 12) & 0x3ff;
      if ((t0[vpn0] & PAGE_V) == 0 || (t0[vpn0] & PAGE_U) == 0)
        return false;
    }
    // Read string character safely
    if (*(volatile char *)va == '\0') {
      return true;
    }
    va++;
    if (va >= 0x80000000)
      return false;
  }
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
  case SYSCALL_READ_FILE: {
    const char *name = (const char *)f->a0;
    char *buf = (char *)f->a1;
    int offset = f->a2;
    if (!validate_user_string(name) ||
        !validate_user_write_buffer(buf, FS_CHUNK_SIZE)) {
      kprintf("read_file: invalid user pointer(s)\n");
      f->a0 = ERR_INVALID_ARGUMENT;
    } else {
      int ret = fs_read_file(name, buf, offset);
      if (ret < 0) {
        kprintf("read_file: file '%s' not found or read failed\n", name);
      }
      f->a0 = ret;
    }
    break;
  }
  case SYSCALL_GET_FILE_NAME: {
    int index = f->a0;
    char *buf = (char *)f->a1;
    int buf_len = f->a2;
    if (buf_len <= 0 || !validate_user_write_buffer(buf, buf_len)) {
      kprintf("get_file_name: invalid buffer or buf_len %d\n", buf_len);
      f->a0 = ERR_INVALID_ARGUMENT;
    } else {
      f->a0 = fs_get_file_name(index, buf, buf_len);
    }
    break;
  }
  case SYSCALL_GET_FILE_SIZE:
    f->a0 = fs_get_file_size(f->a0);
    break;
  case SYSCALL_STAT: {
    const char *path = (const char *)f->a0;
    struct stat *st = (struct stat *)f->a1;
    if (!validate_user_string(path) ||
        !validate_user_write_buffer(st, sizeof(struct stat))) {
      kprintf("stat: invalid pointer(s)\n");
      f->a0 = ERR_INVALID_ARGUMENT;
    } else {
      f->a0 = fs_stat(path, st);
    }
    break;
  }
  case SYSCALL_CHDIR: {
    const char *path = (const char *)f->a0;
    if (!validate_user_string(path)) {
      f->a0 = ERR_INVALID_ARGUMENT;
    } else {
      struct inode *ip = NULL;
      int ret = fs_chdir(path, &ip);
      if (ret < 0) {
        f->a0 = ret;
      } else {
        struct Process *proc = get_current_process();
        char new_path[MAX_PATH];
        fs_normalize_path(proc->cwd_path, path, new_path, sizeof(new_path));
        strncpy(proc->cwd_path, new_path, sizeof(proc->cwd_path) - 1);
        proc->cwd_path[sizeof(proc->cwd_path) - 1] = '\0';

        iput(proc->cwd);
        proc->cwd = ip; // Keep the reference returned by fs_chdir -> namei
        f->a0 = 0;
      }
    }
    break;
  }
  case SYSCALL_GETCWD: {
    char *buf = (char *)f->a0;
    int size = f->a1;
    if (size <= 0 || !validate_user_write_buffer(buf, size)) {
      f->a0 = ERR_INVALID_ARGUMENT;
    } else {
      struct Process *proc = get_current_process();
      int len = strlen(proc->cwd_path);
      if (len >= size) {
        f->a0 = -1;
      } else {
        memcpy(buf, proc->cwd_path, len + 1);
        f->a0 = 0;
      }
    }
    break;
  }
  case SYSCALL_WRITE_FILE: {
    const char *name = (const char *)f->a0;
    const char *buf = (const char *)f->a1;
    int len = f->a2;
    int offset = f->a4;
    if (!validate_user_string(name) ||
        (len > 0 && !validate_user_read_buffer(buf, len))) {
      kprintf("write_file: invalid user pointer(s)\n");
      f->a0 = ERR_INVALID_ARGUMENT;
    } else {
      f->a0 = fs_write_file(name, buf, len, offset);
    }
    break;
  }
  case SYSCALL_MKDIR: {
    const char *path = (const char *)f->a0;
    if (!validate_user_string(path)) {
      kprintf("mkdir: invalid path pointer\n");
      f->a0 = ERR_INVALID_ARGUMENT;
    } else {
      f->a0 = fs_mkdir(path);
    }
    break;
  }
  case SYSCALL_RM: {
    const char *path = (const char *)f->a0;
    if (!validate_user_string(path)) {
      kprintf("rm: invalid path pointer\n");
      f->a0 = ERR_INVALID_ARGUMENT;
    } else {
      f->a0 = fs_rm(path);
    }
    break;
  }
  case SYSCALL_SPAWN: {
    const char *cmdline = (const char *)f->a0;
    kprintf("spawn: '%s'\n", cmdline);
    if (!validate_user_string(cmdline)) {
      kprintf("spawn: invalid cmdline pointer\n");
      f->a0 = -1;
    } else {
      int ret = spawn_process(cmdline);
      if (ret < 0) {
        kprintf("spawn: failed to spawn '%s'\n", cmdline);
      }
      f->a0 = ret;
    }
    break;
  }
  case SYSCALL_WAIT: {
    int ret = wait_process(f->a0);
    if (ret < 0) {
      kprintf("wait: invalid pid %d\n", f->a0);
    }
    f->a0 = ret;
    break;
  }
  case SYSCALL_KMESG: {
    char *user_buf = (char *)f->a0;
    int limit = f->a1;
    if (limit <= 0 || !validate_user_write_buffer(user_buf, limit)) {
      kprintf("kmesg: invalid buffer or limit %d\n", limit);
      f->a0 = -1;
    } else {
      if (limit > kmesg_len) {
        limit = kmesg_len;
      }
      memcpy(user_buf, kmesg_buf, limit);
      f->a0 = limit;
    }
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

  kprintf("Initializing VirtIO block device\n");
  virtio_blk_init();
  kprintf(RALIGN GREEN "[VirtIO initialized]\n" DEFAULT);

  kprintf("Initializing file system\n");
  fs_init();
  kprintf(RALIGN GREEN "[File system initialized]\n" DEFAULT);

  kprintf("Initializing process scheduler\n");
  process_init();
  kprintf(RALIGN GREEN "[Process scheduler initialized]\n" DEFAULT);

  printf("dmaos boot complete\n");

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
  uint32_t scause = READ_CSR(scause);
  uint32_t stval = READ_CSR(stval);
  uint32_t user_pc = READ_CSR(sepc);
  uint32_t sstatus = READ_CSR(sstatus);

  if (scause == SCAUSE_ECALL) {
    handle_syscall(f);
    user_pc += 4;
  } else if ((sstatus & SSTATUS_SPP) == 0) {
    struct Process *proc = get_current_process();
    kprintf(RED
            "Process %d (%s) crashed: scause %x, stval %x, sepc %x\n" DEFAULT,
            proc->pid, proc->name, scause, stval, user_pc);
    exit_current_process();
    yield();
  } else {
    kprintf("TRAP REGISTERS:\n");
    kprintf("  ra: %x, sp: %x, gp: %x, tp: %x\n", f->ra, f->sp, f->gp, f->tp);
    kprintf("  t0: %x, t1: %x, t2: %x, t3: %x\n", f->t0, f->t1, f->t2, f->t3);
    kprintf("  t4: %x, t5: %x, t6: %x\n", f->t4, f->t5, f->t6);
    kprintf("  a0: %x, a1: %x, a2: %x, a3: %x\n", f->a0, f->a1, f->a2, f->a3);
    kprintf("  a4: %x, a5: %x, a6: %x, a7: %x\n", f->a4, f->a5, f->a6, f->a7);
    kprintf("  s0: %x, s1: %x, s2: %x, s3: %x\n", f->s0, f->s1, f->s2, f->s3);
    kprintf("  s4: %x, s5: %x, s6: %x, s7: %x\n", f->s4, f->s5, f->s6, f->s7);
    kprintf("  s8: %x, s9: %x, s10: %x, s11: %x\n", f->s8, f->s9, f->s10,
            f->s11);
    PANIC("unexpected trap:\n  scause %x\n  stval %x\n  sepc %x\n", scause,
          stval, user_pc);
  }

  WRITE_CSR(sepc, user_pc);
}

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
