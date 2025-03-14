cc := $(shell brew --prefix llvm)/bin/clang
objcopy := $(shell brew --prefix llvm)/bin/llvm-objcopy
cflags := -std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib
kflags := -Wl,-Tkernel.ld -Wl,-Map=kernel.map
uflags := -Wl,-Tuser.ld
qemu := $(shell brew --prefix)/bin/qemu-system-riscv32
qflags := -machine virt -bios default -nographic -serial mon:stdio --no-reboot -d unimp,guest_errors,int,cpu_reset -D qemu.log

ksources := kernel.c memory.c process.c stdlib.c
usources := shell.c stdlib.c user.c

.PHONY: all clean run

all: kernel.elf

kernel.elf: $(ksources) kernel.ld shell.bin.o
	$(cc) $(cflags) $(kflags) -o $@ $(ksources) shell.bin.o

run: kernel.elf
	$(qemu) $(qflags) -kernel kernel.elf

clean:
	rm -f kernel.map kernel.elf shell.map shell.elf shell.bin.o shell.bin

# TODO: generalize these...
shell.elf: $(usources) user.ld
	$(cc) $(cflags) $(uflags) -Wl,-Map=shell.map -o $@ $(usources)

shell.bin: shell.elf
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin

shell.bin.o: shell.bin
	$(objcopy) -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o