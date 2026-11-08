#pragma once

#include "fd_remap.h"
#include "file.h"
#include "fs.h"
#include "page.h"
#include "stdlib.h"

#define PROCS_MAX (8) // maximum number of processes

enum ProcState {
  PROCSTATE_UNUSED,
  PROCSTATE_RUNNABLE,
  PROCSTATE_BLOCKED, // waiting in sleep_on(); not scheduled until woken
  PROCSTATE_EXITED
};

struct Process {
  int pid;
  enum ProcState state;
  int exit_code;
  const void *wait_chan; // what a BLOCKED process waits on, or NULL
  uint64_t wake_time;    // uptime() deadline for a BLOCKED process, or 0
  vaddr_t sp;
  pte_t *page_table;
  struct inode *cwd;
  vaddr_t heap_start;
  vaddr_t heap_end;
  struct File *ofile[NUM_FILES_PER_PROCESS];
  char name[16];
  char cwd_path[MAX_PATH];
  uint8_t stack[8192] __attribute__((aligned(16)));
};

void process_init();

struct Process *create_process(const void *image, size_t image_size, int argc,
                               char **argv);
struct Process *get_current_process(void);
void exit_current_process(int exit_code);
void yield();
int spawn_process(const char *cmdline, const struct FdRemap *remaps,
                  int remap_count);
int wait_process(int pid);

// Blocks the current process until wakeup(chan) is called or uptime() reaches
// deadline, whichever comes first. A NULL chan sleeps until the deadline only,
// and a deadline of 0 waits for wakeup() only. Wakeups can be spurious, so
// callers re-check their condition in a loop.
void sleep_until(const void *chan, uint64_t deadline);

// sleep_until() with a deadline timeout_ms from now (0: no timeout).
void sleep_on(const void *chan, uint32_t timeout_ms);

// Makes every process blocked on chan runnable.
void wakeup(const void *chan);

// Makes every process whose sleep deadline has passed runnable. Returns true if
// any process was woken.
bool wake_expired_sleepers(void);

// Returns the earliest sleep deadline of any blocked process, or UINT64_MAX.
uint64_t process_next_wake_time(void);

// Returns true while any user process is runnable or blocked.
bool process_any_alive(void);