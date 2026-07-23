cc := $(shell brew --prefix llvm)/bin/clang
objcopy := $(shell brew --prefix llvm)/bin/llvm-objcopy
cflags := -std=c11 -O2 -g3 -Wall -Wextra -Werror \
          --target=riscv32-unknown-elf -nostdlib \
					-fno-stack-protector -ffreestanding -fno-builtin -fuse-ld=lld
kflags := -Wl,-Tkernel.ld -Wl,-Map=kernel.map
uflags := -Wl,-Tuser.ld
qemu := $(shell brew --prefix)/bin/qemu-system-riscv32
qflags := -machine virt -bios default -nographic \
					-serial mon:stdio --no-reboot \
					-d unimp,guest_errors,cpu_reset -D qemu.log \
					-drive id=drive0,file=disk.img,format=raw,if=none \
					-device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0

ksources := kernel.c fs.c memory.c process.c stdlib.c virtio.c
kheaders := kernel.h fs.h memory.h process.h syscall.h virtio.h stdlib.h
ssources := sh/shell.c stdlib.c user.c
uheaders := user.h fs.h stdlib.h
utxts    := hello.txt lorem.txt meow.txt
uconfigs := dmash.cfg
uprogs   := cat hello ls snake

.PHONY: all clean run
.PRECIOUS: build/bin/%.elf

all: kernel.elf disk.img

kernel.elf: $(ksources) $(kheaders) kernel.ld sh/shell.bin.o
	$(cc) $(cflags) $(kflags) -o $@ $(ksources) sh/shell.bin.o

run: kernel.elf disk.img
	$(qemu) $(qflags) -kernel kernel.elf

clean:
	@rm -f kernel.map kernel.elf sh/shell.map sh/shell.elf sh/shell.bin.o sh/shell.bin disk.img
	@rm -rf bin build

sh/shell.elf: $(ssources) $(uheaders) user.ld
	$(cc) $(cflags) $(uflags) -I. -Wl,-Map=sh/shell.map -o $@ $(ssources)

sh/shell.bin: sh/shell.elf
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary sh/shell.elf sh/shell.bin

sh/shell.bin.o: sh/shell.bin
	cd sh && $(objcopy) -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

bin/mkfs: tools/mkfs.c
	@mkdir -p bin
	$(cc) -Wall -Wextra -O2 -o $@ $< -DHOST_BUILD

disk.img: bin/mkfs $(addprefix build/, $(utxts)) $(addprefix build/cfg/, $(uconfigs)) $(addprefix build/bin/, $(uprogs))
	bin/mkfs $@ $(addprefix build/, $(utxts)) $(addprefix build/cfg/, $(uconfigs)) $(addprefix build/bin/, $(uprogs))

build/cfg/%: disk/%
	@mkdir -p build/cfg
	@cp $< $@

build/%: disk/%
	@mkdir -p build/
	@cp $< $@

build/bin/%.elf: usr/%.c stdlib.c user.c $(uheaders) user.ld
	@mkdir -p build/bin
	$(cc) $(cflags) $(uflags) -I. -Wl,-Map=build/bin/$*.map -o $@ $(filter %.c, $^)

build/bin/%: build/bin/%.elf
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary $< $@