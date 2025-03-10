#include "stdlib.h"

void putchar(char);

void* memcpy(void* dst, const void* src, size_t n) {
	uint8_t *d = (uint8_t *) dst;
	const uint8_t* s = (const uint8_t*) src;
	while (n--) {
		*d++ = *s++;
	}
	return dst;
}

void* memset(void* buf, char c, size_t n) {
	uint8_t *p = (uint8_t*) buf;
	while (n--) {
		*p++ = c;
	}
	return buf;
}

int strcmp(const char* s1, const char* s2) {
	while (*s1 && *s2) {
		if (*s1 != *s2)
			break;
		++s1;
		++s2;
	}
	return *(uint8_t*)s1 - *(uint8_t*)s2;
}

void printf(const char *fmt, ...) {
	va_list vargs;
	va_start(vargs, fmt);

	while (*fmt) {
		if (*fmt == '%') {
			++fmt;
			switch (*fmt) {
				case '\0':
					putchar('%');
					goto end;
				case '%':
					putchar('%');
					break;
				case 's': {
					const char *s = va_arg(vargs, const char*);
					while (*s) {
						putchar(*s);
						++s;
					}
					break;
				}
				case 'd': {
					int value = va_arg(vargs, int);
					unsigned mag = value;
					if (value < 0) {
						putchar('-');
						mag = -mag;
					}

					unsigned div = 1;
					while (mag / div > 9)
						div *= 10;

					while (div > 0) {
						putchar('0' + mag / div);
						mag %= div;
						div /= 10;
					}
					break;
				}
				case 'x': {
					unsigned val = va_arg(vargs, unsigned);
					for (int i = 7; i >=0; --i) {
						unsigned nibble = (val >> (i * 4)) & 0xf;
						putchar("0123456789abcdef"[nibble]);
					}
					break;
				}
			}
		} else {
			putchar(*fmt);
		}
		++fmt;
	}
end:
	va_end(vargs);
}
