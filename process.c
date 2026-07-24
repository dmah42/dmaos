#include "process.h"

#include "fs.h"
#include "kernel.h"
#include "page.h"
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
  uint32_t *sp = (uint32_t *)&proc->stack[sizeof(proc->stack)];

  // Map kernel pages.
  uint32_t *page_table = (uint32_t *)alloc_pages(1);
  for (paddr_t paddr = (paddr_t)__kernel_base; paddr < (paddr_t)__free_ram_end;
       paddr += PAGE_SIZE) {
    map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
  }

  // Map MMIO
  map_page(page_table, VIRTIO_BLK0_PADDR, VIRTIO_BLK0_PADDR, PAGE_R | PAGE_W);
  map_page(page_table, VIRTIO_BLK1_PADDR, VIRTIO_BLK1_PADDR, PAGE_R | PAGE_W);

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
  for (uint32_t off = 0; off < size; off += PAGE_SIZE) {
    paddr_t page = alloc_pages(1);

    size_t remain = size - off;
    size_t copy_size = PAGE_SIZE <= remain ? PAGE_SIZE : remain;

    if (image != NULL) {
      memcpy((void *)page, image + off, copy_size);
    } else if (ip != NULL) {
      readi(ip, (char *)page, off, copy_size);
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
  uint32_t user_sp = USER_BASE + size;
  if (argc > 0 && last_page != 0) {
    uint32_t stack_top_offset = size % PAGE_SIZE;
    if (stack_top_offset == 0) {
      stack_top_offset = PAGE_SIZE;
    }

    uint8_t *kernel_stack_top = (uint8_t *)(last_page + stack_top_offset);
    uint8_t *kernel_sp = kernel_stack_top;

    // Copy argument strings
    uint32_t user_argv_va[16];
    for (int j = argc - 1; j >= 0; --j) {
      size_t len = strlen(argv[j]) + 1;
      kernel_sp -= len;
      memcpy(kernel_sp, argv[j], len);

      uint32_t offset_from_top = kernel_stack_top - kernel_sp;
      user_argv_va[j] = (USER_BASE + size) - offset_from_top;
    }

    // Align stack pointer to 16-byte boundary
    kernel_sp = (uint8_t *)((uint32_t)kernel_sp & ~0xF);

    // Write argv array of pointers
    kernel_sp -= (argc + 1) * sizeof(uint32_t);
    uint32_t *argv_array = (uint32_t *)kernel_sp;
    for (int j = 0; j < argc; ++j) {
      argv_array[j] = user_argv_va[j];
    }
    argv_array[argc] = 0; // NULL terminator

    uint32_t offset_from_top = kernel_stack_top - kernel_sp;
    user_sp = (USER_BASE + size) - offset_from_top;
  }

  // Initialize process register state
  *--sp = 0;       // s11
  *--sp = 0;       // s10
  *--sp = 0;       // s9
  *--sp = 0;       // s8
  *--sp = 0;       // s7
  *--sp = 0;       // s6
  *--sp = 0;       // s5
  *--sp = 0;       // s4
  *--sp = 0;       // s3
  *--sp = user_sp; // s2 (will be loaded into a1 / argv)
  *--sp = argc;    // s1 (will be loaded into a0 / argc)
  *--sp = user_sp; // s0 (will be loaded into sp / user stack pointer)
  *--sp = (uint32_t)user_entry; // ra

  // Initialize Process
  proc->pid = i + 1;
  proc->state = PROCSTATE_RUNNABLE;
  proc->page_table = page_table;
  proc->sp = (uint32_t)sp;
  proc->heap_start = USER_BASE + align_up(size, PAGE_SIZE);
  proc->heap_end = proc->heap_start;
  if (argv != NULL && argc > 0) {
    strncpy(proc->name, argv[0], sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    kprintf(YELLOW "Process created: PID=%d, name=%s, image_size=%d\n" DEFAULT,
            proc->pid, argv[0], size);
  } else {
    strncpy(proc->name, "idle/init", sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';
    kprintf(YELLOW "Process created: PID=%d, idle/init\n" DEFAULT, proc->pid);
  }
  return proc;
}

void exit_current_process() {
  kprintf(YELLOW "Process exited: PID=%d\n" DEFAULT, current_proc->pid);
  current_proc->state = PROCSTATE_EXITED;
}

void free_process_pages(struct Process *proc) {
  if (proc->cwd != NULL) {
    iput(proc->cwd);
    proc->cwd = NULL;
  }

  for (int fd = 0; fd < NUM_FILES_PER_PROCESS; ++fd) {
    if (proc->ofile[fd] != NULL) {
      kprintf(RED "process exit: PID %d leaked file descriptor %d!\n" DEFAULT,
              proc->pid, fd);
      file_close(proc->ofile[fd]);
      proc->ofile[fd] = NULL;
    }
  }

  if (!proc->page_table)
    return;

  uint32_t *t1 = proc->page_table;
  for (uint32_t vpn1 = 0; vpn1 < 1024; vpn1++) {
    uint32_t entry1 = t1[vpn1];
    if (entry1 & PAGE_V) {
      paddr_t pt0_paddr = (entry1 >> 10) * PAGE_SIZE;
      uint32_t *t0 = (uint32_t *)pt0_paddr;

      if (vpn1 < 512) {
        for (uint32_t vpn0 = 0; vpn0 < 1024; vpn0++) {
          uint32_t entry0 = t0[vpn0];
          if (entry0 & PAGE_V) {
            paddr_t page_paddr = (entry0 >> 10) * PAGE_SIZE;
            if (page_paddr >= (paddr_t)__free_ram &&
                page_paddr < (paddr_t)__free_ram_end) {
              free_pages(page_paddr, 1);
            }
          }
        }
      }

      free_pages(pt0_paddr, 1);
    }
  }

  free_pages((paddr_t)t1, 1);
  proc->page_table = NULL;
}

// TODO: consider benefits of storing the context in the Process.
__attribute__((naked)) void switch_context(uint32_t *prev_sp,
                                           uint32_t *next_sp) {
  __asm__ __volatile__(
      // Save callee-saved registers onto the current process's stack.
      "addi sp, sp, -13 * 4\n" // Allocate stack space for 13 4-byte registers
      "sw ra,  0  * 4(sp)\n"   // Save callee-saved registers only
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
      "sw sp, (a0)\n" // *prev_sp = sp;
      "lw sp, (a1)\n" // Switch stack pointer (sp) here

      // Restore callee-saved registers from the next process's stack.
      "lw ra,  0  * 4(sp)\n" // Restore callee-saved registers only
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
      "addi sp, sp, 13 * 4\n" // We've popped 13 4-byte registers from the stack
      "ret\n");
}

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
  __asm__ __volatile__(
      "sfence.vma\n"
      "csrw satp, %[satp]\n"
      "sfence.vma\n"
      "csrw sscratch, %[sscratch]\n"
      :
      : [satp] "r"(SATP_SV32 | PAGE_TABLE_TO_INDEX(next->page_table)),
        [sscratch] "r"((uint32_t)&next->stack[sizeof(next->stack)]));

  // SWITCH!
  struct Process *prev = current_proc;
  current_proc = next;
  switch_context(&prev->sp, &next->sp);
}

/*
 * Spawns a new process from a binary file in the filesystem.
 * Returns the PID of the new process, or -1 if the file does not exist.
 */
int spawn_process(const char *cmdline) {
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
  return proc->pid;
}

/*
 * Waits for a process to finish execution.
 * Yields CPU slices until the target process transitions to PROCSTATE_EXITED.
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
    yield();
  }
}

struct Process *get_current_process(void) { return current_proc; }
