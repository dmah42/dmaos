#pragma once

#include "stdlib.h"

#define PROCS_MAX (8) // maximum number of processes

enum ProcState { PROCSTATE_UNUSED, PROCSTATE_RUNNABLE };

struct Process {
  int pid;
  enum ProcState state;
  vaddr_t sp;
  uint32_t *page_table;
  // NOTE: context could be stored in a thread, or outside of the process...
  uint8_t stack[8192];
};

void process_init();

struct Process *create_process(uint32_t pc);

void yield();