cc := $(shell brew --prefix llvm)/bin/clang
objcopy := $(shell brew --prefix llvm)/bin/llvm-objcopy
qemu := $(shell brew --prefix)/bin/qemu-system-riscv32

cflags := -std=c11 -O2 -g3 -Wall -Wextra -Werror \
          --target=riscv32-unknown-elf -nostdlib \
          -fno-stack-protector -ffreestanding -fno-builtin
ldflags := -fuse-ld=lld

SYSROOT := third_party/newlib
user_cflags := $(cflags) -isystem $(SYSROOT)/include \
               -include user/lib/user.h \
							 -ffunction-sections -fdata-sections -flto \
               -D_POSIX_C_SOURCE=200809L
user_ldflags := $(ldflags) -Wl,--gc-sections -s -flto

host_cflags := -std=c11 -O2 -Wall -Wextra -Werror

qflags := -machine virt -bios default -nographic \
          -serial mon:stdio --no-reboot \
          -d unimp,guest_errors,cpu_reset -D qemu.log \
          -drive id=drive0,file=disk.img,format=raw,if=none \
          -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
          -drive id=drive1,file=data.img,format=raw,if=none \
          -device virtio-blk-device,drive=drive1,bus=virtio-mmio-bus.1

# Directories
KERNEL_DIR := kernel
USER_DIR := user
SHARED_DIR := shared
TOOLS_DIR := tools
BUILD_DIR := build

# Guest binaries & assets
uprogs := cat hello ls snake mkdir write rm memtest kilo
utxts := hello.txt lorem.txt meow.txt
uconfigs := dmash.cfg

.PHONY: all clean run
.SECONDARY:

all: kernel.elf disk.img data.img

# ----------------- Kernel -----------------

KERNEL_OBJS := $(addprefix $(BUILD_DIR)/kernel/, kernel.o fs.o page.o process.o virtio.o file.o stdlib.o)

kernel.elf: $(KERNEL_OBJS) $(KERNEL_DIR)/kernel.ld $(BUILD_DIR)/user/sh/shell.bin.o
	$(cc) $(cflags) $(ldflags) -Wl,-T$(KERNEL_DIR)/kernel.ld -Wl,-Map=$(BUILD_DIR)/kernel.map -o $@ $(KERNEL_OBJS) $(BUILD_DIR)/user/sh/shell.bin.o

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(dir $@)
	$(cc) $(cflags) -I$(KERNEL_DIR) -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

# ----------------- User Library -----------------

USER_LIB_OBJS := $(addprefix $(BUILD_DIR)/user/lib/, user.o)

$(BUILD_DIR)/user/lib/%.o: $(USER_DIR)/lib/%.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

# ----------------- Shell -----------------

$(BUILD_DIR)/user/sh/shell.o: $(USER_DIR)/sh/shell.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/sh/shell.elf: $(BUILD_DIR)/user/sh/shell.o $(USER_LIB_OBJS) $(USER_DIR)/lib/user.ld
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(user_ldflags) -Wl,-T$(USER_DIR)/lib/user.ld -Wl,-Map=$(BUILD_DIR)/user/sh/shell.map -o $@ $(BUILD_DIR)/user/sh/shell.o $(USER_LIB_OBJS) -L$(SYSROOT)/lib -lc -lm -lgcc

$(BUILD_DIR)/user/sh/shell.bin: $(BUILD_DIR)/user/sh/shell.elf
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary $< $@

$(BUILD_DIR)/user/sh/shell.bin.o: $(BUILD_DIR)/user/sh/shell.bin
	cd $(BUILD_DIR)/user/sh && $(objcopy) -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o

# ----------------- User Programs -----------------

$(BUILD_DIR)/user/bin/kilo.o: third_party/kilo/kilo.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -Wno-char-subscripts -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/bin/%.o: $(USER_DIR)/bin/%.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/elf/%.elf: $(BUILD_DIR)/user/bin/%.o $(USER_LIB_OBJS) $(USER_DIR)/lib/user.ld
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(user_ldflags) -Wl,-T$(USER_DIR)/lib/user.ld -Wl,-Map=$(BUILD_DIR)/user/elf/$*.map -o $@ $(filter %.o, $^) -L$(SYSROOT)/lib -lc -lm -lgcc

$(BUILD_DIR)/root/bin/%: $(BUILD_DIR)/user/elf/%.elf
	@mkdir -p $(dir $@)
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary $< $@

# ----------------- Host Tools -----------------

bin/mkfs: $(TOOLS_DIR)/mkfs.c $(SHARED_DIR)/fs_shared.h
	@mkdir -p bin
	$(cc) $(host_cflags) -iquote$(SHARED_DIR) -o $@ $<

# ----------------- Disk Images & Running -----------------

disk.img: bin/mkfs $(addprefix $(BUILD_DIR)/root/, $(utxts)) $(addprefix $(BUILD_DIR)/root/cfg/, $(uconfigs)) $(addprefix $(BUILD_DIR)/root/bin/, $(uprogs))
	@mkdir -p $(BUILD_DIR)/root/home
	bin/mkfs $@ $(BUILD_DIR)/root

data.img:
	$(MAKE) bin/mkfs
	bin/mkfs $@

$(BUILD_DIR)/root/cfg/%: disk/%
	@mkdir -p $(dir $@)
	@cp $< $@

$(BUILD_DIR)/root/%: disk/%
	@mkdir -p $(dir $@)
	@cp $< $@

run: all
	$(qemu) $(qflags) -kernel kernel.elf

clean:
	rm -f kernel.elf disk.img
	rm -rf bin $(BUILD_DIR)

-include $(shell find $(BUILD_DIR) -name "*.d" 2>/dev/null)