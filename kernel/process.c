#include "process.h"

#include "colors.h"
#include "fd_remap.h"
#include "file.h"
#include "fs.h"
#include "k_fcntl.h"
#include "k_time.h"
#include "kernel.h"
#include "page.h"
#include "string.h"
#include "timer.h"
#include "virtio.h"

struct Process procs[PROCS_MAX];
struct Process *current_proc = NULL;
struct Process *idle_proc = NULL;

extern char __kernel_base[], __free_ram[], __free_ram_end[];

void process_init() {
  idle_proc = create_process(NULL, 0, 0, NULL);
  idle_proc->pid = 0;
  current_proc = idle_proc;
}

// TODO: clean this API up. image is only set for the shell which suggests
// maybe we can reuse it for disk.img?
struct Process *create_process(const void *image, size_t image_size, int argc,
                               char **argv) {
  // Find an unused PCB
  struct Process *proc = NULL;
  int i;
  for (i = 0; i < PROCS_MAX; ++i) {
    if (procs[i].state == PROCSTATE_UNUSED) {
      proc = &procs[i];
      break;
    }
  }

  if (proc == NULL) {
    PANIC("+++ REDO FROM START +++");
  }

  memset(proc->ofile, 0, sizeof(proc->ofile));

  if (image != NULL || argc > 0) {
    struct File *f0 = file_create(FD_CONSOLE, FILECTRL_READONLY);
    struct File *f1 = file_create(FD_CONSOLE, FILECTRL_WRITEONLY);
    struct File *f2 = file_create(FD_CONSOLE, FILECTRL_WRITEONLY);
    if (f0 && f1 && f2) {
      proc->ofile[0] = f0;
      proc->ofile[1] = f1;
      proc->ofile[2] = f2;
    } else {
      if (f0)
        file_close(f0);
      if (f1)
        file_close(f1);
      if (f2)
        file_close(f2);
    }
  }

  if (current_proc != NULL && current_proc->cwd != NULL) {
    proc->cwd = iget(current_proc->cwd->dev, current_proc->cwd->inum);
    strncpy(proc->cwd_path, current_proc->cwd_path, sizeof(proc->cwd_path) - 1);
    proc->cwd_path[sizeof(proc->cwd_path) - 1] = '\0';
  } else {
    proc->cwd = iget(0, 1);
    strncpy(proc->cwd_path, "/", sizeof(proc->cwd_path) - 1);
    proc->cwd_path[sizeof(proc->cwd_path) - 1] = '\0';
  }

  // Stack callee-saved registers. These register values will be restored in
  // the first context switch in switch_context.
  reg_t *sp = (reg_t *)&proc->stack[sizeof(proc->stack)];

  // Map kernel pages.
  pte_t *page_table = (pte_t *)alloc_pages(1);
  for (paddr_t paddr = (paddr_t)__kernel_base; paddr < (paddr_t)__free_ram_end;
       paddr += PAGE_SIZE) {
    map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
  }

  // Map MMIO
  map_page(page_table, VIRTIO_BLK0_PADDR, VIRTIO_BLK0_PADDR, PAGE_R | PAGE_W);
  map_page(page_table, VIRTIO_BLK1_PADDR, VIRTIO_BLK1_PADDR, PAGE_R | PAGE_W);
  map_page(page_table, VIRTIO_INPUT_PADDR, VIRTIO_INPUT_PADDR, PAGE_R | PAGE_W);
  map_page(page_table, VIRTIO_NET_PADDR, VIRTIO_NET_PADDR, PAGE_R | PAGE_W);

  // Map user pages.
  struct inode *ip = NULL;
  size_t size = image_size;
  if (image == NULL && argc > 0 && argv != NULL) {
    ip = namei(argv[0]);
    if (ip == NULL) {
      free_pages((paddr_t)page_table, 1);
      return NULL;
    }
    size = fs_get_inode_size(ip);
  }

  paddr_t last_page = 0;
  for (vaddr_t off = 0; off < size; off += PAGE_SIZE) {
    paddr_t page = alloc_pages(1);

    size_t remain = size - off;
    size_t copy_size = PAGE_SIZE <= remain ? PAGE_SIZE : remain;

    if (image != NULL) {
      memcpy((void *)page, image + off, copy_size);
    } else if (ip != NULL) {
      readi(ip, (uint8_t *)page, off, copy_size);
    }

    map_page(page_table, USER_BASE + off, page,
             PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    if (off + PAGE_SIZE >= size) {
      last_page = page;
    }
  }

  if (ip != NULL) {
    iput(ip);
  }

  // Copy arguments to the user stack
  vaddr_t user_sp = USER_BASE + size;
  if (argc > 0 && last_page != 0) {
    uint32_t stack_top_offset = size % PAGE_SIZE;
    if (stack_top_offset == 0) {
      stack_top_offset = PAGE_SIZE;
    }

    uint8_t *kernel_stack_top = (uint8_t *)(last_page + stack_top_offset);
    uint8_t *kernel_sp = kernel_stack_top;

    // Copy argument strings
    vaddr_t user_argv_va[16];
    for (int j = argc - 1; j >= 0; --j) {
      size_t len = strlen(argv[j]) + 1;
      kernel_sp -= len;
      memcpy(kernel_sp, argv[j], len);

      uint32_t offset_from_top = kernel_stack_top - kernel_sp;
      user_argv_va[j] = (USER_BASE + size) - offset_from_top;
    }

    // Align stack pointer to 16-byte boundary
    kernel_sp = (uint8_t *)((uintptr_t)kernel_sp & ~0xF);

    // Write argv array of pointers
    kernel_sp -= (argc + 1) * sizeof(vaddr_t);
    vaddr_t *argv_array = (vaddr_t *)kernel_sp;
    for (int j = 0; j < argc; ++j) {
      argv_array[j] = user_argv_va[j];
    }
    argv_array[argc] = 0; // NULL terminator

    uint32_t offset_from_top = kernel_stack_top - kernel_sp;
    user_sp = (USER_BASE + size) - offset_from_top;
  }

  // Initialize process register state
  *--sp = 0;                 // s11
  *--sp = 0;                 // s10
  *--sp = 0;                 // s9
  *--sp = 0;                 // s8
  *--sp = 0;                 // s7
  *--sp = 0;                 // s6
  *--sp = 0;                 // s5
  *--sp = 0;                 // s4
  *--sp = 0;                 // s3
  *--sp = (reg_t)user_sp;    // s2 (will be loaded into a1 / argv)
  *--sp = (reg_t)argc;       // s1 (will be loaded into a0 / argc)
  *--sp = (reg_t)user_sp;    // s0 (will be loaded into sp / user stack pointer)
  *--sp = (reg_t)user_entry; // ra

  // Initialize Process
  proc->pid = i + 1;
  proc->state = PROCSTATE_RUNNABLE;
  proc->wait_chan = NULL;
  proc->wake_time = 0;
  proc->page_table = page_table;
  proc->sp = (vaddr_t)sp;
  proc->heap_start = USER_BASE + align_up(size, PAGE_SIZE);
  proc->heap_end = proc->heap_start;
  if (argv != NULL && argc > 0) {
    strncpy(proc->name, argv[0], sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    kprintf(YELLOW "Process created: PID=%d, name=%s, image_size=%d\n" DEFAULT,
            proc->pid, argv[0], (int)size);
  } else {
    strncpy(proc->name, "idle/init", sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    kprintf(YELLOW "Process created: PID=%d, idle/init\n" DEFAULT, proc->pid);
  }
  return proc;
}

void exit_current_process(int exit_code) {
  kprintf(YELLOW "Process exited: PID=%d with exit code %d\n" DEFAULT,
          current_proc->pid, exit_code);
  current_proc->state = PROCSTATE_EXITED;
  current_proc->exit_code = exit_code;
  wakeup(current_proc); // a parent in wait_process()

  if (current_proc->cwd != NULL) {
    iput(current_proc->cwd);
    current_proc->cwd = NULL;
  }

  for (int fd = 0; fd < NUM_FILES_PER_PROCESS; ++fd) {
    if (current_proc->ofile[fd] != NULL) {
      kprintf(RED "process exit: PID %d leaked file descriptor %d!\n" DEFAULT,
              current_proc->pid, fd);
      file_close(current_proc->ofile[fd]);
      current_proc->ofile[fd] = NULL;
    }
  }
}

void free_process_pages(struct Process *proc) {
  if (proc->cwd != NULL) {
    iput(proc->cwd);
    proc->cwd = NULL;
  }

  for (int fd = 0; fd < NUM_FILES_PER_PROCESS; ++fd) {
    if (proc->ofile[fd] != NULL) {
      file_close(proc->ofile[fd]);
      proc->ofile[fd] = NULL;
    }
  }

  if (!proc->page_table)
    return;

  free_page_table(proc->page_table, PAGE_LEVELS);
  proc->page_table = NULL;
}

// TODO: consider benefits of storing the context in the Process.
#if __riscv_xlen == 64
__attribute__((naked)) void switch_context(vaddr_t *prev_sp, vaddr_t *next_sp) {
  __asm__ __volatile__(
      // Save callee-saved registers onto the current process's stack (13 8-byte
      // registers).
      "addi sp, sp, -13 * 8\n"
      "sd ra,  0  * 8(sp)\n"
      "sd s0,  1  * 8(sp)\n"
      "sd s1,  2  * 8(sp)\n"
      "sd s2,  3  * 8(sp)\n"
      "sd s3,  4  * 8(sp)\n"
      "sd s4,  5  * 8(sp)\n"
      "sd s5,  6  * 8(sp)\n"
      "sd s6,  7  * 8(sp)\n"
      "sd s7,  8  * 8(sp)\n"
      "sd s8,  9  * 8(sp)\n"
      "sd s9,  10 * 8(sp)\n"
      "sd s10, 11 * 8(sp)\n"
      "sd s11, 12 * 8(sp)\n"

      // Switch the stack pointer.
      "sd sp, (a0)\n"
      "ld sp, (a1)\n"

      // Restore callee-saved registers from the next process's stack.
      "ld ra,  0  * 8(sp)\n"
      "ld s0,  1  * 8(sp)\n"
      "ld s1,  2  * 8(sp)\n"
      "ld s2,  3  * 8(sp)\n"
      "ld s3,  4  * 8(sp)\n"
      "ld s4,  5  * 8(sp)\n"
      "ld s5,  6  * 8(sp)\n"
      "ld s6,  7  * 8(sp)\n"
      "ld s7,  8  * 8(sp)\n"
      "ld s8,  9  * 8(sp)\n"
      "ld s9,  10 * 8(sp)\n"
      "ld s10, 11 * 8(sp)\n"
      "ld s11, 12 * 8(sp)\n"
      "addi sp, sp, 13 * 8\n"
      "ret\n");
}
#else
__attribute__((naked)) void switch_context(vaddr_t *prev_sp, vaddr_t *next_sp) {
  __asm__ __volatile__(
      // Save callee-saved registers onto the current process's stack (13 4-byte
      // registers).
      "addi sp, sp, -13 * 4\n"
      "sw ra,  0  * 4(sp)\n"
      "sw s0,  1  * 4(sp)\n"
      "sw s1,  2  * 4(sp)\n"
      "sw s2,  3  * 4(sp)\n"
      "sw s3,  4  * 4(sp)\n"
      "sw s4,  5  * 4(sp)\n"
      "sw s5,  6  * 4(sp)\n"
      "sw s6,  7  * 4(sp)\n"
      "sw s7,  8  * 4(sp)\n"
      "sw s8,  9  * 4(sp)\n"
      "sw s9,  10 * 4(sp)\n"
      "sw s10, 11 * 4(sp)\n"
      "sw s11, 12 * 4(sp)\n"

      // Switch the stack pointer.
      "sw sp, (a0)\n"
      "lw sp, (a1)\n"

      // Restore callee-saved registers from the next process's stack.
      "lw ra,  0  * 4(sp)\n"
      "lw s0,  1  * 4(sp)\n"
      "lw s1,  2  * 4(sp)\n"
      "lw s2,  3  * 4(sp)\n"
      "lw s3,  4  * 4(sp)\n"
      "lw s4,  5  * 4(sp)\n"
      "lw s5,  6  * 4(sp)\n"
      "lw s6,  7  * 4(sp)\n"
      "lw s7,  8  * 4(sp)\n"
      "lw s8,  9  * 4(sp)\n"
      "lw s9,  10 * 4(sp)\n"
      "lw s10, 11 * 4(sp)\n"
      "lw s11, 12 * 4(sp)\n"
      "addi sp, sp, 13 * 4\n"
      "ret\n");
}
#endif

void yield() {
  // Search for any runnable process
  struct Process *next = idle_proc;
  for (int i = 0; i < PROCS_MAX; ++i) {
    struct Process *proc = &procs[(current_proc->pid + i) % PROCS_MAX];
    if (proc->state == PROCSTATE_RUNNABLE && proc->pid > 0) {
      next = proc;
      break;
    }
  }

  // Didn't find one; carry on.
  if (next == current_proc) {
    return;
  }

  // Store a pointer to the kernel stack for the current process before
  // switching (see exception handler for details).
  reg_t satp_val = SATP_MODE | SATP_PPN(next->page_table);

  __asm__ __volatile__(
      "sfence.vma\n"
      "csrw satp, %[satp]\n"
      "sfence.vma\n"
      "csrw sscratch, %[sscratch]\n"
      :
      : [satp] "r"(satp_val), [sscratch] "r"((uintptr_t)&next->stack[sizeof(
                                  next->stack)]));

  // SWITCH!
  struct Process *prev = current_proc;
  current_proc = next;
  switch_context((vaddr_t *)&prev->sp, (vaddr_t *)&next->sp);
}

/*
 * Spawns a new process from a binary file in the filesystem.
 * remaps is an optional array of FdRemap pairs that redirect file descriptors
 * in the child (e.g. wire a pipe write-end to the child's stdout).
 * Returns the PID of the new process, or -1 on failure.
 */
int spawn_process(const char *cmdline, const struct FdRemap *remaps,
                  int remap_count) {
  kprintf("spawn_process '%s'\n", cmdline);
  int argc = 0;
  char *argv[16];
  char cmd[128];

  int len = strlen(cmdline);
  if (len >= 128)
    len = 127;
  memcpy(cmd, cmdline, len);
  cmd[len] = '\0';

  char *p = cmd;
  while (*p && argc < 16) {
    while (*p == ' ') {
      p++;
    }
    if (*p == '\0')
      break;

    argv[argc++] = p;

    while (*p && *p != ' ') {
      p++;
    }
    if (*p == ' ') {
      *p = '\0';
      p++;
    }
  }

  if (argc == 0) {
    return -1;
  }

  struct Process *proc = create_process(NULL, 0, argc, argv);
  if (proc == NULL) {
    return -1;
  }

  /* Apply fd remaps: copy parent's file ref into child's ofile slot. */
  struct Process *parent = get_current_process();
  for (int i = 0; i < remap_count; ++i) {
    int from = remaps[i].from;
    int to = remaps[i].to;
    if (from < 0 || from >= NUM_FILES_PER_PROCESS || to < 0 ||
        to >= NUM_FILES_PER_PROCESS) {
      continue;
    }
    struct File *src = parent->ofile[from];
    if (src == NULL) {
      continue;
    }
    /* Close whatever is currently in the child's target slot. */
    if (proc->ofile[to] != NULL) {
      file_close(proc->ofile[to]);
    }
    proc->ofile[to] = file_dup(src);
  }

  return proc->pid;
}

/*
 * Waits for a process to finish execution.
 * Blocks until the target process transitions to PROCSTATE_EXITED.
 * Returns 0 on success, or -1 if the PID does not exist.
 */
int wait_process(int pid) {
  bool found = false;
  for (int i = 0; i < PROCS_MAX; i++) {
    if (procs[i].state != PROCSTATE_UNUSED && procs[i].pid == pid) {
      found = true;
      break;
    }
  }
  if (!found) {
    return -1;
  }

  while (true) {
    struct Process *proc = NULL;
    for (int i = 0; i < PROCS_MAX; i++) {
      if (procs[i].state != PROCSTATE_UNUSED && procs[i].pid == pid) {
        proc = &procs[i];
        break;
      }
    }
    if (proc == NULL || proc->state == PROCSTATE_EXITED) {
      if (proc != NULL) {
        free_process_pages(proc);
        proc->state = PROCSTATE_UNUSED;
      }
      return 0;
    }
    sleep_on(proc, 0); // woken by exit_current_process()
  }
}

void sleep_until(const void *chan, uint64_t deadline) {
  if (deadline != 0 && uptime() >= deadline) {
    return;
  }
  current_proc->wait_chan = chan;
  current_proc->wake_time = deadline;
  current_proc->state = PROCSTATE_BLOCKED;
  if (deadline != 0) {
    timer_wake_at(deadline);
  }
  yield();
  current_proc->wait_chan = NULL;
  current_proc->wake_time = 0;
}

void sleep_on(const void *chan, uint32_t timeout_ms) {
  sleep_until(chan, timeout_ms > 0
                        ? uptime() + (uint64_t)timeout_ms * TICKS_PER_MS
                        : 0);
}

void wakeup(const void *chan) {
  if (chan == NULL) {
    return;
  }
  for (int i = 0; i < PROCS_MAX; ++i) {
    if (procs[i].state == PROCSTATE_BLOCKED && procs[i].wait_chan == chan) {
      procs[i].state = PROCSTATE_RUNNABLE;
    }
  }
}

bool wake_expired_sleepers(void) {
  uint64_t now = uptime();
  bool woke = false;
  for (int i = 0; i < PROCS_MAX; ++i) {
    if (procs[i].state == PROCSTATE_BLOCKED && procs[i].wake_time != 0 &&
        now >= procs[i].wake_time) {
      procs[i].state = PROCSTATE_RUNNABLE;
      woke = true;
    }
  }
  return woke;
}

uint64_t process_next_wake_time(void) {
  uint64_t earliest = UINT64_MAX;
  for (int i = 0; i < PROCS_MAX; ++i) {
    if (procs[i].state == PROCSTATE_BLOCKED && procs[i].wake_time != 0 &&
        procs[i].wake_time < earliest) {
      earliest = procs[i].wake_time;
    }
  }
  return earliest;
}

bool process_any_alive(void) {
  for (int i = 0; i < PROCS_MAX; ++i) {
    if (procs[i].pid > 0 && (procs[i].state == PROCSTATE_RUNNABLE ||
                             procs[i].state == PROCSTATE_BLOCKED)) {
      return true;
    }
  }
  return false;
}

struct Process *get_current_process(void) { return current_proc; }
