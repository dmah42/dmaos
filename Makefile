ARCH ?= riscv32
BOARD ?= qemu

ifeq ($(ARCH),riscv64)
  TARGET_TRIPLE      := riscv64-unknown-elf
  ARCH_FLAGS         := -march=rv64imac -mabi=lp64
  QEMU_NAME          := qemu-system-riscv64
  OBJCOPY_ELF_TARGET := elf64-littleriscv
  NEWLIB_SUBDIR      := rv64
else
  TARGET_TRIPLE      := riscv32-unknown-elf
  ARCH_FLAGS         := -march=rv32im -mabi=ilp32
  QEMU_NAME          := qemu-system-riscv32
  OBJCOPY_ELF_TARGET := elf32-littleriscv
  NEWLIB_SUBDIR      := rv32
endif

ifeq ($(BOARD),visionfive2)
  KERNEL_BASE   := 0x40200000
  RAM_BASE      := 0x40000000
  BOARD_CFLAGS  := -DBOARD_VISIONFIVE2 -DKERNEL_BASE=$(KERNEL_BASE) -DRAM_BASE=$(RAM_BASE)
  BUILD_DIR     := build/$(NEWLIB_SUBDIR)-vf2
else
  KERNEL_BASE   := 0x80200000
  RAM_BASE      := 0x80000000
  BOARD_CFLAGS  := -DBOARD_QEMU -DKERNEL_BASE=$(KERNEL_BASE) -DRAM_BASE=$(RAM_BASE)
  BUILD_DIR     := build/$(NEWLIB_SUBDIR)
endif

# Toolchain Auto-Detection (macOS Homebrew vs WSL/Linux System PATH)
BREW_LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null)
BREW_PREFIX := $(shell brew --prefix 2>/dev/null)

ifneq ($(BREW_LLVM_PREFIX),)
    DEFAULT_CC := $(BREW_LLVM_PREFIX)/bin/clang
    DEFAULT_OBJCOPY := $(BREW_LLVM_PREFIX)/bin/llvm-objcopy
else
    DEFAULT_CC := clang
    DEFAULT_OBJCOPY := llvm-objcopy
endif

ifneq ($(BREW_PREFIX),)
    DEFAULT_QEMU := $(BREW_PREFIX)/bin/$(QEMU_NAME)
else
    DEFAULT_QEMU := $(QEMU_NAME)
endif

# Query package managers (dpkg / rpm / pacman) for installed 32-bit OpenSBI firmware
OPENSBI_PKG_PATH := $(shell \
    dpkg -L opensbi 2>/dev/null | grep -m1 'riscv32.*fw_dynamic\.bin$$' || \
    rpm -ql opensbi 2>/dev/null | grep -m1 'riscv32.*fw_dynamic\.bin$$' || \
    pacman -Ql opensbi 2>/dev/null | grep -m1 'riscv32.*fw_dynamic\.bin$$' \
)

SYSTEM_OPENSBI_32 := $(firstword $(OPENSBI_PKG_PATH) $(wildcard \
    /usr/lib/riscv32*/opensbi/*/fw_dynamic.bin \
    /usr/share/*/opensbi*riscv32*.bin \
    /usr/share/qemu/opensbi-riscv32*.bin \
))

DEFAULT_BIOS := $(if $(filter riscv32,$(ARCH)),$(if $(SYSTEM_OPENSBI_32),$(SYSTEM_OPENSBI_32),default),default)

cc ?= $(DEFAULT_CC)
objcopy ?= $(DEFAULT_OBJCOPY)
qemu ?= $(DEFAULT_QEMU)
bios ?= $(DEFAULT_BIOS)

$(info [dmaOS Build] Target Arch : $(ARCH) ($(TARGET_TRIPLE)))
$(info [dmaOS Build] Target Board: $(BOARD) (Base: $(KERNEL_BASE)))
$(info [dmaOS Build] Compiler   : $(cc))
$(info [dmaOS Build] Objcopy    : $(objcopy))
$(info [dmaOS Build] QEMU       : $(qemu))
$(info [dmaOS Build] BIOS       : $(bios))

cflags := -std=c11 -O2 -g3 -Wall -Wextra -Werror \
          --target=$(TARGET_TRIPLE) $(ARCH_FLAGS) -mcmodel=medany $(BOARD_CFLAGS) -nostdlib \
          -fno-stack-protector -ffreestanding -fno-builtin
ldflags := -fuse-ld=lld

SYSROOT := third_party/newlib
NEWLIB_LIBDIR := $(SYSROOT)/lib/$(NEWLIB_SUBDIR)
NEWLIB_LIBS := $(NEWLIB_LIBDIR)/libc.a $(NEWLIB_LIBDIR)/libm.a $(NEWLIB_LIBDIR)/libgcc.a

.DEFAULT_GOAL := all

user_cflags := $(cflags) -isystem $(SYSROOT)/include \
               -include user/lib/user.h \
							 -ffunction-sections -fdata-sections -flto \
               -D_POSIX_C_SOURCE=200809L
user_ldflags := $(ldflags) -Wl,--gc-sections -s -flto

host_cflags := -std=c11 -O2 -Wall -Wextra -Werror

qflags := -machine virt -bios $(bios) -nographic \
          -serial mon:stdio --no-reboot \
          -d unimp,guest_errors,cpu_reset -D qemu.log \
          -drive id=drive0,file=$(BUILD_DIR)/disk.img,format=raw,if=none \
          -device virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0 \
          -drive id=drive1,file=data.img,format=raw,if=none \
          -device virtio-blk-device,drive=drive1,bus=virtio-mmio-bus.1 \
					-device ramfb -display default \
					-device virtio-keyboard-device,bus=virtio-mmio-bus.2 \
					-netdev user,id=net0,hostfwd=udp::9999-:9999,hostfwd=tcp::9999-:9999 \
					-device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.3

# Directories
KERNEL_DIR := kernel
USER_DIR := user
SHARED_DIR := shared
TOOLS_DIR := tools

# Guest binaries & assets
uprogs := cat doom gfxtest hello kilo ls memtest mkdir nettest nslookup rm snake write
utxts := hello.txt lorem.txt meow.txt doom1.wad #doom2.wad doom3.wad
uconfigs := dmash.cfg net.cfg

.PHONY: all clean run
.SECONDARY:

all: $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/disk.img data.img

# ----------------- Kernel -----------------

KERNEL_INCLUDES := -I$(KERNEL_DIR) -I$(KERNEL_DIR)/lwip -I$(SHARED_DIR) -isystem third_party/lwip/src/include

include $(KERNEL_DIR)/lwip/lwip.mk

kobjs := kernel.o file.o fs.o net.o page.o process.o ramfb.o stdlib.o string.o virtio.o
KERNEL_OBJS := $(addprefix $(BUILD_DIR)/kernel/, $(kobjs)) $(LWIP_OBJS) $(LWIP_PORT_OBJS)

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJS) $(KERNEL_DIR)/kernel.ld $(BUILD_DIR)/user/sh/shell.bin.o
	$(cc) $(cflags) $(ldflags) -Wl,-T$(KERNEL_DIR)/kernel.ld -Wl,--defsym=KERNEL_BASE=$(KERNEL_BASE) -Wl,-Map=$(BUILD_DIR)/kernel.map \
	      -o $@ $(KERNEL_OBJS) $(BUILD_DIR)/user/sh/shell.bin.o -L$(NEWLIB_LIBDIR) -lgcc

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(objcopy) -O binary $< $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(KERNEL_INCLUDES) -MMD -MP -c -o $@ $<

# ----------------- User Library -----------------

$(NEWLIB_LIBS):
	@echo "[dmaOS Build] Newlib libraries missing. Auto-fetching toolchain..."
	tools/update_newlib.sh

USER_LIB_OBJS := $(addprefix $(BUILD_DIR)/user/lib/, user.o)

$(BUILD_DIR)/user/lib/%.o: $(USER_DIR)/lib/%.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

# ----------------- Shell -----------------

$(BUILD_DIR)/user/sh/shell.o: $(USER_DIR)/sh/shell.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/sh/shell.elf: $(BUILD_DIR)/user/sh/shell.o $(USER_LIB_OBJS) $(USER_DIR)/lib/user.ld $(NEWLIB_LIBS)
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(user_ldflags) -Wl,-T$(USER_DIR)/lib/user.ld -Wl,-Map=$(BUILD_DIR)/user/sh/shell.map \
	      -o $@ $(filter %.o, $^) -L$(NEWLIB_LIBDIR) -lc -lm -lgcc

$(BUILD_DIR)/user/sh/shell.bin: $(BUILD_DIR)/user/sh/shell.elf
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary $< $@

$(BUILD_DIR)/user/sh/shell.bin.o: $(BUILD_DIR)/user/sh/shell.bin
	cd $(BUILD_DIR)/user/sh && $(objcopy) -Ibinary -O$(OBJCOPY_ELF_TARGET) shell.bin shell.bin.o

# ----------------- User Programs -----------------

$(BUILD_DIR)/user/bin/kilo.o: third_party/kilo/kilo.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -Wno-char-subscripts -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

DOOM_DIR := third_party/doomgeneric/doomgeneric
DOOM_EXCLUDE_SRCS := $(DOOM_DIR)/doomgeneric_%.c \
                $(DOOM_DIR)/i_allegromusic.c \
                $(DOOM_DIR)/i_allegrosound.c \
                $(DOOM_DIR)/i_sdlmusic.c \
                $(DOOM_DIR)/i_sdlsound.c \
                $(DOOM_DIR)/i_winmusic.c \
                $(DOOM_DIR)/i_cdmus.c \
                $(DOOM_DIR)/i_sound.c

DOOM_SRCS := $(filter-out $(DOOM_EXCLUDE_SRCS), $(wildcard $(DOOM_DIR)/*.c))
DOOM_SRCS += user/lib/doomgeneric_dmaos.c
DOOM_CFLAGS := $(user_cflags) -I$(DOOM_DIR) -Wno-error

DOOM_OBJS := $(patsubst $(DOOM_DIR)/%.c, $(BUILD_DIR)/user/bin/doom/%.o, $(filter $(DOOM_DIR)/%.c, $(DOOM_SRCS)))
DOOM_OBJS += $(BUILD_DIR)/user/lib/doomgeneric_dmaos.o

$(BUILD_DIR)/user/bin/doom/%.o: $(DOOM_DIR)/%.c
	@mkdir -p $(dir $@)
	$(cc) $(DOOM_CFLAGS) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/lib/doomgeneric_dmaos.o: $(USER_DIR)/lib/doomgeneric_dmaos.c
	@mkdir -p $(dir $@)
	$(cc) $(DOOM_CFLAGS) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/elf/doom.elf: $(DOOM_OBJS) $(USER_LIB_OBJS) $(USER_DIR)/lib/user.ld $(NEWLIB_LIBS)
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(user_ldflags) -Wl,-T$(USER_DIR)/lib/user.ld -Wl,-Map=$(BUILD_DIR)/user/elf/doom.map \
	      -o $@ $(filter %.o, $^) -L$(NEWLIB_LIBDIR) -lc -lm -lgcc

$(BUILD_DIR)/user/bin/%.o: $(USER_DIR)/bin/%.c
	@mkdir -p $(dir $@)
	$(cc) $(user_cflags) -I$(USER_DIR)/lib -I$(SHARED_DIR) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/user/elf/%.elf: $(BUILD_DIR)/user/bin/%.o $(USER_LIB_OBJS) $(USER_DIR)/lib/user.ld $(NEWLIB_LIBS)
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(user_ldflags) -Wl,-T$(USER_DIR)/lib/user.ld -Wl,-Map=$(BUILD_DIR)/user/elf/$*.map \
	      -o $@ $(filter %.o, $^) -L$(NEWLIB_LIBDIR) -lc -lm -lgcc

$(BUILD_DIR)/root/bin/%: $(BUILD_DIR)/user/elf/%.elf
	@mkdir -p $(dir $@)
	$(objcopy) --set-section-flags .bss=alloc,contents -O binary $< $@

# ----------------- Host Tools -----------------

bin/mkfs: $(TOOLS_DIR)/mkfs.c $(SHARED_DIR)/fs_shared.h
	@mkdir -p bin
	$(cc) $(host_cflags) -iquote$(SHARED_DIR) -o $@ $<

# ----------------- Disk Images & Running -----------------

$(BUILD_DIR)/disk.img: bin/mkfs $(addprefix $(BUILD_DIR)/root/, $(utxts)) $(addprefix $(BUILD_DIR)/root/cfg/, $(uconfigs)) $(addprefix $(BUILD_DIR)/root/bin/, $(uprogs))
	@mkdir -p $(BUILD_DIR)/root/home
	bin/mkfs $@ $(BUILD_DIR)/root

data.img: bin/mkfs
	bin/mkfs $@

$(BUILD_DIR)/root/cfg/%: disk/%
	@mkdir -p $(dir $@)
	@cp $< $@

$(BUILD_DIR)/root/%: disk/%
	@mkdir -p $(dir $@)
	@cp $< $@

run: all
	$(qemu) $(qflags) -kernel $(BUILD_DIR)/kernel.elf

clean:
	rm -rf bin $(BUILD_DIR)

-include $(shell find $(BUILD_DIR) -name "*.d" 2>/dev/null)
