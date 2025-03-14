#include "process.h"

#include "kernel.h"
#include "memory.h"

struct Process procs[PROCS_MAX];
struct Process *current_proc = NULL;
struct Process *idle_proc = NULL;

extern char __kernel_base[], __free_ram_end[];

void process_init() {
  idle_proc = create_process(0);
  idle_proc->pid = 0;
  current_proc = idle_proc;
}

struct Process *create_process(uint32_t pc) {
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

  // Stack callee-saved registers. These register values will be restored in
  // the first context switch in switch_context.
  uint32_t *sp = (uint32_t *)&proc->stack[sizeof(proc->stack)];
  *--sp = 0;            // s11
  *--sp = 0;            // s10
  *--sp = 0;            // s9
  *--sp = 0;            // s8
  *--sp = 0;            // s7
  *--sp = 0;            // s6
  *--sp = 0;            // s5
  *--sp = 0;            // s4
  *--sp = 0;            // s3
  *--sp = 0;            // s2
  *--sp = 0;            // s1
  *--sp = 0;            // s0
  *--sp = (uint32_t)pc; // ra

  // Map kernel pages.
  uint32_t *page_table = (uint32_t *)alloc_pages(1);
  for (paddr_t paddr = (paddr_t)__kernel_base; paddr < (paddr_t)__free_ram_end;
       paddr += PAGE_SIZE) {
    map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
  }

  // Initialize Process
  proc->pid = i + 1;
  proc->state = PROCSTATE_RUNNABLE;
  proc->page_table = page_table;
  proc->sp = (uint32_t)sp;
  return proc;
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
  struct Process *next = current_proc;
  for (int i = 0; i < PROCS_MAX; ++i) {
    struct Process *proc = &procs[(next->pid + i) % PROCS_MAX];
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