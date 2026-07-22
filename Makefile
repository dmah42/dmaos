cc := $(shell brew --prefix llvm)/bin/clang
objcopy := $(shell brew --prefix llvm)/bin/llvm-objcopy
cflags := -std=c11 -O2 -g3 -Wall -Wextra \
          --target=riscv32-unknown-elf -nostdlib \
					-fno-stack-protector -ffreestanding -fno-builtin -fuse-ld=lld
kflags := -Wl,-Tkernel.ld -Wl,-Map=kernel.map
uflags := -Wl,-Tuser.ld
qemu := $(shell brew --prefix)/bin/qemu-system-riscv32
qflags := -machine virt -bios default -nographic \
					-serial mon:stdio --no-reboot \
					-d unimp,guest_errors,int,cpu_reset -D qemu.log \
					-drive id=drive0,file=disk.tar,format=raw,if=none \
					-device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0

ksources := kernel.c fs.c memory.c process.c stdlib.c virtio.c
usources := shell.c stdlib.c user.c
utxts    := hello.txt lorem.txt meow.txt
uprogs   := cat hello ls snake

.PHONY: all clean run
.PRECIOUS: build/%.elf

all: kernel.elf disk.tar

kernel.elf: $(ksources) kernel.ld shell.bin.o $(addprefix build/, $(uprogs))
	$(cc) $(cflags) $(kflags) -o $@ $(ksources) shell.bin.o

run: kernel.elf disk.tar
	$(qemu) $(qflags) -kernel kernel.elf

clean:
	rm -f kernel.map kernel.elf shell.map shell.elf shell.bin.o shell.bin disk.tar
	rm -rf build

shell.elf: $(usources) user.ld
	$(cc) $(cflags) $(uflags) -Wl,-Map=shell.map -o $@ $(usources)

shell.bin: shell.elf
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin

shell.bin.o: shell.bin
	$(objcopy) -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

disk.tar: $(addprefix disk/, $(utxts)) $(addprefix build/, $(uprogs))
	COPYFILE_DISABLE=1 tar -cf $@ --format=ustar -C disk $(utxts) -C ../build $(uprogs)

build/%.elf: usr/%.c stdlib.c user.c user.ld
	@mkdir -p build
	$(cc) $(cflags) $(uflags) -I. -Wl,-Map=build/$*.map -o $@ $(filter %.c, $^)

build/%: build/%.elf
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary $< $@