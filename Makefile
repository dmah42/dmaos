cc := $(shell brew --prefix llvm)/bin/clang
cflags := -std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib
kflags := -Wl,-Tkernel.ld -Wl,-Map=kernel.map
qemu := $(shell brew --prefix)/bin/qemu-system-riscv32
qflags := -machine virt -bios default -nographic -serial mon:stdio --no-reboot


.PHONY: all clean run

all: kernel.elf

kernel.elf: kernel.c kernel.ld stdlib.c
	$(cc) $(cflags) $(kflags) -o $@ $(filter-out kernel.ld, $^)

run: kernel.elf
	$(qemu) $(qflags) -kernel kernel.elf

clean:
	rm -f kernel.map kernel.elf
