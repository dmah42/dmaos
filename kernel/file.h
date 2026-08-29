#pragma once

#include "fs.h"
#include "net.h"
#include "pipe.h"

#include <stdbool.h>
#include <stdint.h>

#define NUM_FILES_PER_PROCESS 16
#define GLOBAL_OPEN_FILE_LIMIT 64

enum FileDescType {
  FD_NONE = 0,
  FD_INODE = 1,
  FD_CONSOLE = 2,
  FD_SOCKET = 3,
  FD_PIPE = 4,
};

struct File {
  int ref;
  enum FileDescType type;
  bool readable;
  bool writable;
  union {
    struct inode *ip;
    struct socket *sock;
    struct Pipe *pipe;
  };
  uint32_t off;
};

void file_init(void);
struct File *file_alloc(void);
struct File *file_create(enum FileDescType type, int flags);
struct File *file_dup(struct File *f);
void file_close(struct File *f);
int file_pipe(struct File **rout, struct File **wout);
