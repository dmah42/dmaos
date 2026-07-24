#pragma once

#define O_RDONLY 0x001
#define O_WRONLY 0x002
#define O_RDWR (O_RDONLY | O_WRONLY)
#define O_CREAT 0x004
#define O_TRUNC 0x008
#define O_APPEND 0x010

int open(const char *path, int flags, ... /* int mode */);