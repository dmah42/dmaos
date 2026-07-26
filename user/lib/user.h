#pragma once

#include "sys/types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

int spawn(const char *name);
void yield(void);
int wait(int pid);
int kmesg(char *buf, int buf_len);
int getchar_nonblock(void);
uint64_t uptime(void);
void sleep_ms(uint32_t ms);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);
int framebuffer_present(uint32_t *framebuffer, uint32_t width, uint32_t height);