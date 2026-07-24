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
					-device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
					-drive id=drive1,file=data.img,format=raw,if=none \
					-device virtio-blk-device,drive=drive1,bus=virtio-mmio-bus.1

ksources := kernel.c fs.c page.c process.c stdlib.c virtio.c file.c
kheaders := kernel.h fs.h page.h process.h syscall.h virtio.h stdlib.h errno.h file.h
ssources := sh/shell.c stdlib.c user.c memory.c
uheaders := user.h fs.h stdlib.h errno.h memory.h
utxts    := hello.txt lorem.txt meow.txt
uconfigs := dmash.cfg
uprogs   := cat hello ls snake mkdir write rm memtest

.PHONY: all clean run
.PRECIOUS: build/elf/%.elf

all: kernel.elf disk.img data.img

kernel.elf: $(ksources) $(kheaders) kernel.ld sh/shell.bin.o
	$(cc) $(cflags) $(kflags) -o $@ $(ksources) sh/shell.bin.o

run: kernel.elf disk.img data.img
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
	$(cc) -Wall -Wextra -O2 -o $@ $<

disk.img: bin/mkfs $(addprefix build/root/, $(utxts)) $(addprefix build/root/cfg/, $(uconfigs)) $(addprefix build/root/bin/, $(uprogs))
	@mkdir -p build/root/home
	bin/mkfs $@ build/root

data.img: bin/mkfs
	bin/mkfs $@

build/root/cfg/%: disk/%
	@mkdir -p build/root/cfg
	@cp $< $@

build/root/%: disk/%
	@mkdir -p build/root/
	@cp $< $@

build/root/bin/%: build/elf/%.elf
	@mkdir -p build/root/bin
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary $< $@

build/elf/%.elf: usr/%.c memory.c stdlib.c user.c $(uheaders) user.ld
	@mkdir -p build/elf
	$(cc) $(cflags) $(uflags) -I. -Wl,-Map=build/elf/$*.map -o $@ $(filter %.c, $^)