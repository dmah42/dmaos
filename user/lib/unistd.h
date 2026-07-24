#pragma once

#include <stdint.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int read(int fd, void *buf, int n);
int write(int fd, const void *buf, int n);
int close(int fd);
int isatty(int fd);
int ftruncate(int fd, int length);
