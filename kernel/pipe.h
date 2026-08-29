#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PIPE_BUF_SIZE 512
#define PIPE_MAX 8

struct Pipe {
  uint8_t buf[PIPE_BUF_SIZE];
  uint32_t read_pos;
  uint32_t write_pos;
  int ref;           // total open struct File refs across both ends
  bool write_closed; // set when the write-end File is closed
};

struct File; /* avoid circular include with file.h */

/* Called from file_init() — not for general use. */
void pipe_module_init(void);

/* Called from file.c — creates a wired read/write File pair. */
int file_pipe(struct File **rout, struct File **wout);

/* Called from fs_read / fs_write — not for general use. */
int pipe_read(struct Pipe *p, uint8_t *buf, int n);
int pipe_write(struct Pipe *p, const uint8_t *buf, int n);
