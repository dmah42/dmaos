#pragma once

#include "fs.h"

__attribute__((noreturn)) void exit(void);

void putchar(char ch);
int getchar(void);
int getchar_nonblock(void);

int open(const char *path, int flags);
int read(int fd, void *buf, int n);
int write(int fd, const void *buf, int n);
int close(int fd);
int rm(const char *path);

int get_file_name(int index, char *buf, int buf_len);
int get_file_size(int index);
int stat(const char *name, struct stat *st);

int spawn(const char *name);
void yield(void);
int wait(int pid);
int kmesg(char *buf, int buf_len);

int getcwd(char *buf, int size);
int chdir(const char *path);
int mkdir(const char *path);

void *sbrk(int increment);