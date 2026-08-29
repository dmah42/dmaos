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

FB_WIDTH  ?= 1600
FB_HEIGHT ?= 1200

$(info [dmaOS Build] Target Arch : $(ARCH) ($(TARGET_TRIPLE)))
$(info [dmaOS Build] Target Board: $(BOARD) (Base: $(KERNEL_BASE)))
$(info [dmaOS Build] Display Res : $(FB_WIDTH)x$(FB_HEIGHT))
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
          -fw_cfg name=opt/dmaos/width,string=$(FB_WIDTH) \
          -fw_cfg name=opt/dmaos/height,string=$(FB_HEIGHT) \
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

.PHONY: all clean run
.SECONDARY:

all: $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/disk.img data.img

# ----------------- Kernel -----------------

KERNEL_INCLUDES := -I$(KERNEL_DIR) -I$(KERNEL_DIR)/drivers -I$(KERNEL_DIR)/lwip -I$(SHARED_DIR) -isystem third_party/lwip/src/include

include $(KERNEL_DIR)/lwip/lwip.mk

ifeq ($(BOARD),visionfive2)
  BOARD_KOBJS := drivers/initrd.o drivers/dw_mmc.o drivers/vf2_blk.o drivers/simplefb.o drivers/input_stub.o
else
  BOARD_KOBJS := drivers/virtio.o drivers/ramfb.o
endif

kobjs := kernel.o blk.o fb.o fbcon.o font8x16.o file.o fs.o net.o page.o pipe.o process.o stdlib.o string.o $(BOARD_KOBJS)
KERNEL_OBJS := $(addprefix $(BUILD_DIR)/kernel/, $(kobjs)) $(LWIP_OBJS) $(LWIP_PORT_OBJS)

$(BUILD_DIR)/kernel/fonts/font8x16.bin: $(TOOLS_DIR)/generate_font.py
	@mkdir -p $(dir $@)
	python3 $< $@

$(BUILD_DIR)/kernel/fonts/font8x16.bin.o: $(BUILD_DIR)/kernel/fonts/font8x16.bin
	@mkdir -p $(dir $@)
	cd $(dir $<) && $(objcopy) -Ibinary -O$(OBJCOPY_ELF_TARGET) $(notdir $<) $(abspath $@)

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJS) $(KERNEL_DIR)/kernel.ld $(BUILD_DIR)/user/sh/shell.bin.o $(BUILD_DIR)/kernel/fonts/font8x16.bin.o
	$(cc) $(cflags) $(ldflags) -Wl,-T$(KERNEL_DIR)/kernel.ld -Wl,--defsym=KERNEL_BASE=$(KERNEL_BASE) -Wl,-Map=$(BUILD_DIR)/kernel.map \
	      -o $@ $(KERNEL_OBJS) $(BUILD_DIR)/user/sh/shell.bin.o $(BUILD_DIR)/kernel/fonts/font8x16.bin.o -L$(NEWLIB_LIBDIR) -lgcc

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(objcopy) -O binary $< $@

$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c Makefile
	@mkdir -p $(dir $@)
	$(cc) $(cflags) $(KERNEL_INCLUDES) -MMD -MP -c -o $@ $<


$(NEWLIB_LIBS):
	@echo "[dmaOS Build] Newlib libraries missing. Auto-fetching toolchain..."
	tools/update_newlib.sh
# ----------------- Userland -----------------

include $(USER_DIR)/user.mk

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
