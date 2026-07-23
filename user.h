#pragma once

#include "fs.h"

__attribute__((noreturn)) void exit(void);

void putchar(char ch);
int getchar(void);
int getchar_nonblock(void);

int read_file(const char *name, char *buf, int offset);
int write_file(const char *name, const char *buf, int len, int offset);
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