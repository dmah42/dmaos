#include "kernel.h"

#include "stdlib.h"

extern char __bss[], __bss_end[], __stack_top[];

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
		       long arg5, long fid, long eid) {
	register long a0 __asm__("a0") = arg0;
	register long a1 __asm__("a1") = arg1;
	register long a2 __asm__("a2") = arg2;
	register long a3 __asm__("a3") = arg3;
	register long a4 __asm__("a4") = arg4;
	register long a5 __asm__("a5") = arg5;
	register long a6 __asm__("a6") = fid;
	register long a7 __asm__("a7") = eid;

	__asm__ __volatile__(
		"ecall"
		: "=r"(a0), "=r"(a1)
		: "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6),
		  "r"(a7)
		: "memory");
	return (struct sbiret){.error = a0, .value = a1};
}

void putchar(const char ch) {
	sbi_call((long) ch, 0, 0, 0, 0, 0, 0, 1 /* putchar */);
}

void kmain(void) {
	memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

	printf("1 + 2 = %d, %x\n", 1 + 2, 0x00c0ffee);

	for(;;) {
		__asm__ __volatile__("wfi");
	}
}

__attribute__((section(".text.boot")))
__attribute__((naked))
void boot(void) {
	__asm__ __volatile__(
		"mv sp, %[stack_top]\n"
		"j kmain\n"
		:
		: [stack_top] "r" (__stack_top)
	);
}
