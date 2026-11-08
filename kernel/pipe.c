/*
 * kernel/pipe.c — anonymous pipe implementation.
 *
 * Each pipe is a fixed-size ring buffer backed by a static pool.  Both the
 * read-end and write-end are represented as ordinary struct File entries with
 * FD_PIPE type; they share a pointer to the same struct Pipe.
 *
 * A blocked reader or writer sleeps on the struct Pipe. Each end wakes the
 * other after moving data, and file_close() wakes both when an end closes.
 */

#include "file.h"
#include "k_errno.h"
#include "k_fcntl.h"
#include "process.h"
#include "stdlib.h"
#include "string.h"

static struct Pipe pipe_pool[PIPE_MAX];

static void pipe_pool_init(void) { memset(pipe_pool, 0, sizeof(pipe_pool)); }

static struct Pipe *pipe_alloc(void) {
  for (int i = 0; i < PIPE_MAX; ++i) {
    if (pipe_pool[i].ref == 0) {
      pipe_pool[i].read_pos = 0;
      pipe_pool[i].write_pos = 0;
      pipe_pool[i].write_closed = false;
      return &pipe_pool[i];
    }
  }
  return NULL;
}

/*
 * Allocates a pipe and returns two wired-up struct File entries.
 * *rout receives the readable end (fd 0 / stdin side).
 * *wout receives the writable end (fd 1 / stdout side).
 * Returns 0 on success, negative error code on failure.
 */
int file_pipe(struct File **rout, struct File **wout) {
  struct Pipe *p = pipe_alloc();
  if (p == NULL)
    return ERR_NO_SPACE;

  struct File *rfile = file_create(FD_PIPE, FILECTRL_READONLY);
  struct File *wfile = file_create(FD_PIPE, FILECTRL_WRITEONLY);
  if (rfile == NULL || wfile == NULL) {
    if (rfile) {
      file_close(rfile);
    }
    if (wfile) {
      file_close(wfile);
    }
    return ERR_NO_SPACE;
  }

  rfile->pipe = p;
  wfile->pipe = p;
  p->ref = 2; /* one for each file */

  *rout = rfile;
  *wout = wfile;
  return 0;
}

/*
 * Called from file_init() to zero the pool.
 * Not exposed in any header — file.c calls it directly.
 */
void pipe_module_init(void) { pipe_pool_init(); }

/*
 * Read up to n bytes from the pipe into buf.
 * Blocks until data is available or the write end has been closed.
 * Returns number of bytes read, or 0 on EOF.
 */
int pipe_read(struct Pipe *p, uint8_t *buf, int n) {
  while (p->read_pos == p->write_pos) {
    if (p->write_closed) {
      return 0; /* EOF */
    }
    sleep_on(p, 0);
  }

  int count = 0;
  while (count < n && p->read_pos != p->write_pos) {
    buf[count++] = p->buf[p->read_pos & (PIPE_BUF_SIZE - 1)];
    ++p->read_pos;
  }
  wakeup(p); // a writer waiting for space
  return count;
}

// Write n bytes from buf into the pipe.
// Blocks until there is space in the ring buffer.
// Returns n on success, or ERR_BROKEN_PIPE if no readers remain.
int pipe_write(struct Pipe *p, const uint8_t *buf, int n) {
  int written = 0;
  while (written < n) {
    /* Block while ring buffer is full, letting the reader drain it. */
    while ((p->write_pos - p->read_pos) >= PIPE_BUF_SIZE) {
      if (p->ref <= 1) {
        return ERR_BROKEN_PIPE;
      }
      wakeup(p);
      sleep_on(p, 0);
    }
    // No readers left — only the writer holds a ref.
    if (p->ref <= 1) {
      return ERR_BROKEN_PIPE;
    }
    p->buf[p->write_pos & (PIPE_BUF_SIZE - 1)] = buf[written++];
    ++p->write_pos;
  }
  wakeup(p); // a reader waiting for data
  return written;
}
