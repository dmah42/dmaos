#pragma once

#include "fd_remap.h"
#include "file.h"
#include "fs.h"
#include "page.h"
#include "stdlib.h"

#define PROCS_MAX (8) // maximum number of processes

// Signals (Linux numbering)
#define SIGINT 2
#define SIGQUIT 3

enum ProcState {
  PROCSTATE_UNUSED,
  PROCSTATE_RUNNABLE,
  PROCSTATE_BLOCKED, // in sleep_until()
  PROCSTATE_EXITED
};

struct Process {
  int pid;
  int ppid;
  enum ProcState state;
  int exit_code;
  const void *wait_chan;    // BLOCKED: what it waits on, or NULL
  uint64_t wake_time;       // BLOCKED: uptime() deadline, or 0
  uint32_t pending_signals; // bit n: signal n
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

// Sleeps until wakeup(chan) or until uptime() reaches deadline (NULL chan or
// 0 deadline: no such wakeup). Wakeups can be spurious, so callers re-check in
// a loop. Returns ERR_INTERRUPTED if a signal is pending; callers return it.
int sleep_until(const void *chan, uint64_t deadline);

// sleep_until() with a deadline timeout_ms from now (0: none).
int sleep_on(const void *chan, uint32_t timeout_ms);

void wakeup(const void *chan);

// Returns true if any process was woken.
bool wake_expired_sleepers(void);

// Returns UINT64_MAX if no process is sleeping with a deadline.
uint64_t process_next_wake_time(void);

bool process_any_alive(void);

// Signals the descendants of the console's first process. Returns false if
// there are none.
bool signal_foreground(int sig);

// Called on every return to U-mode.
void signal_deliver(void);