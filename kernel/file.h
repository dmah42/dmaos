#pragma once

#include <stdint.h>
#include <stdlib.h>

#define NUM_FILES_PER_PROCESS 16
#define GLOBAL_OPEN_FILE_LIMIT 64

struct inode;

enum FileDescType {
  FD_NONE = 0,
  FD_INODE = 1,
  FD_CONSOLE = 2,
};

struct File {
  int ref;
  enum FileDescType type;
  bool readable;
  bool writable;
  struct inode *ip;
  uint32_t off;
};

void file_init(void);
struct File *file_alloc(void);
struct File *file_create(enum FileDescType type, int flags);
struct File *file_dup(struct File *f);
void file_close(struct File *f);
