#pragma once

#include "file.h"
#include "fs.h"
#include "stdlib.h"

#define PROCS_MAX (8) // maximum number of processes

enum ProcState { PROCSTATE_UNUSED, PROCSTATE_RUNNABLE, PROCSTATE_EXITED };

struct Process {
  int pid;
  enum ProcState state;
  vaddr_t sp;
  uint32_t *page_table;
  struct inode *cwd;
  uint32_t heap_start;
  uint32_t heap_end;
  struct File *ofile[NUM_FILES_PER_PROCESS];
  char name[16];
  char cwd_path[MAX_PATH];
  uint8_t stack[8192] __attribute__((aligned(16)));
};

void process_init();

struct Process *create_process(const void *image, size_t image_size, int argc,
                               char **argv);
struct Process *get_current_process(void);
void exit_current_process();
void yield();
int spawn_process(const char *filename);
int wait_process(int pid);