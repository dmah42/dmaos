#!/bin/bash
set -xue

QEMU=$(brew --prefix)/bin/qemu-system-riscv32

CC=$(brew --prefix llvm)/bin/clang
CFLAGS="-std=c11 -O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fno-stack-protector -ffreestanding -nostdlib"

# Build
$CC $CFLAGS -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf kernel.c

# Run
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot -kernel kernel.elf

