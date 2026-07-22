#pragma once

__attribute__((noreturn)) void exit(void);

void putchar(char ch);
char getchar();
int read_file(const char *name, char *buf, int offset);
int get_file_name(int index, char *buf, int buf_len);
int get_file_size(int index);