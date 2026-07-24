#pragma once

#include "stdlib.h"

#define NUM_FILES_PER_PROCESS 16
#define GLOBAL_OPEN_FILE_LIMIT 64

struct inode;

struct File {
  int ref;
  char readable;
  char writable;
  struct inode *ip;
  uint32_t off;
};

void file_init(void);
struct File *file_alloc(void);
struct File *file_dup(struct File *f);
void file_close(struct File *f);
