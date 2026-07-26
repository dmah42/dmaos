#include "file.h"
#include "k_fcntl.h"

#include "fs.h"
#include "kernel.h"

struct File file_table[GLOBAL_OPEN_FILE_LIMIT];

void file_init(void) { memset(file_table, 0, sizeof(file_table)); }

struct File *file_alloc(void) {
  for (int i = 0; i < GLOBAL_OPEN_FILE_LIMIT; ++i) {
    if (file_table[i].ref == 0) {
      file_table[i].ref = 1;
      file_table[i].type = FD_NONE;
      file_table[i].off = 0;
      file_table[i].readable = false;
      file_table[i].writable = false;
      file_table[i].ip = NULL;
      return &file_table[i];
    }
  }
  return NULL;
}

struct File *file_create(enum FileDescType type, int flags) {
  struct File *f = file_alloc();
  if (f == NULL) {
    return NULL;
  }
  f->type = type;
  f->readable = (flags & FILECTRL_READONLY) != 0;
  f->writable = (flags & FILECTRL_WRITEONLY) != 0;
  f->ip = NULL;
  f->off = 0;
  return f;
}

struct File *file_dup(struct File *f) {
  if (f == NULL) {
    return NULL;
  }
  if (f->ref <= 0) {
    PANIC("file_dup: duplicate a free file slot");
  }
  ++f->ref;
  return f;
}

void file_close(struct File *f) {
  if (f == NULL) {
    return;
  }
  if (f->ref <= 0) {
    PANIC("file_close: closing already free file slot");
  }
  if (--f->ref == 0) {
    if (f->ip != NULL) {
      iput(f->ip);
      f->ip = NULL;
    }
    f->type = FD_NONE;
  }
}
