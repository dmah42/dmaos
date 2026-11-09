#pragma once

// Kernel error codes are negated Linux errno numbers. The user library
// translates them to newlib's numbering, which differs above 34.
enum Errno {
  ERR_NOT_PERMITTED = -1,
  ERR_NOT_FOUND = -2,
  ERR_INTERRUPTED = -4,
  ERR_IO = -5,
  ERR_BAD_FILE = -9,
  ERR_NO_MEMORY = -12,
  ERR_PERMISSION_DENIED = -13,
  ERR_ALREADY_EXISTS = -17,
  ERR_NOT_A_DIRECTORY = -20,
  ERR_IS_A_DIRECTORY = -21,
  ERR_INVALID_ARGUMENT = -22,
  ERR_NO_TTY = -25,
  ERR_NO_SPACE = -28,
  ERR_BROKEN_PIPE = -32,
  ERR_NOT_IMPLEMENTED = -38,
  ERR_DIRECTORY_NOT_EMPTY = -39,
  ERR_TIMED_OUT = -110,
  ERR_CONNECTION_REFUSED = -111,
};

extern int errno;

#define ENOENT (-ERR_NOT_FOUND)
#define ENOTTY (-ERR_NO_TTY)
