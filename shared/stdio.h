#pragma once

#define va_list __builtin_va_list

void printf(const char *fmt, ...);
void vprintf(void (*putc)(char), const char *fmt, va_list vargs);
