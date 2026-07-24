#pragma once

#include "stdlib.h"
#include "sys/types.h"
#include <stdarg.h>

typedef struct _FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
int fprintf(FILE *stream, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int sscanf(const char *str, const char *fmt, ...);
void perror(const char *s);
