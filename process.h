#pragma once

#include "stdlib.h"

#define PROCS_MAX (8) // maximum number of processes

enum ProcState { PROCSTATE_UNUSED, PROCSTATE_RUNNABLE, PROCSTATE_EXITED };

struct Process {
  int pid;
  enum ProcState state;
  vaddr_t sp;
  uint32_t *page_table;
  char name[16];
  uint8_t stack[8192];
};

void process_init();

struct Process *create_process(const void *image, size_t image_size, int argc,
                               char **argv);
struct Process *get_current_process(void);
void exit_current_process();
void yield();
int spawn_process(const char *filename);
int wait_process(int pid);