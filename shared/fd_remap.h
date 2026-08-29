#pragma once

/*
 * FdRemap — used by SYSCALL_SPAWN to wire file descriptors in a child process
 * before it starts running.  The kernel copies from[i] in the parent's fd table
 * to slot to[i] in the child's fd table.
 */
struct FdRemap {
  int from; /* source fd in the parent's ofile[] */
  int to;   /* destination fd slot in the child's ofile[] */
};
