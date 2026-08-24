#!/bin/bash
set -e

VERSION=${1:-"14.2.0-3"}
HOST_OS=$(uname -s | tr '[:upper:]' '[:lower:]')
HOST_ARCH=$(uname -m)

if [ "$HOST_OS" = "darwin" ]; then
    if [ "$HOST_ARCH" = "arm64" ]; then
        ARCH_SUFFIX="darwin-arm64"
    else
        ARCH_SUFFIX="darwin-x64"
    fi
elif [ "$HOST_OS" = "linux" ]; then
    if [ "$HOST_ARCH" = "aarch64" ] || [ "$HOST_ARCH" = "arm64" ]; then
        ARCH_SUFFIX="linux-arm64"
    else
        ARCH_SUFFIX="linux-x64"
    fi
else
    echo "Unsupported OS: $HOST_OS"
    exit 1
fi

URL="https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v$VERSION/xpack-riscv-none-elf-gcc-$VERSION-$ARCH_SUFFIX.tar.gz"
TEMP_DIR="third_party/temp_toolchain"
TARGET_DIR="third_party/newlib"

echo "Downloading xPack RISC-V GCC v$VERSION for $HOST_ARCH..."
mkdir -p third_party
curl -L "$URL" -o "third_party/toolchain.tar.gz"

echo "Extracting..."
rm -rf "$TEMP_DIR"
mkdir -p "$TEMP_DIR"
tar -zxf "third_party/toolchain.tar.gz" -C "$TEMP_DIR"

echo "Creating newlib structure under $TARGET_DIR..."
rm -rf "$TARGET_DIR"
mkdir -p "$TARGET_DIR/lib/rv32"
mkdir -p "$TARGET_DIR/lib/rv64"

EXTRACTED_FOLDER=$(ls "$TEMP_DIR")
SRC_DIR="$TEMP_DIR/$EXTRACTED_FOLDER"

# Copy headers
cp -R "$SRC_DIR/riscv-none-elf/include" "$TARGET_DIR/include"

# Copy 32-bit newlib libraries (rv32im/ilp32)
cp "$SRC_DIR/riscv-none-elf/lib/rv32im/ilp32/libc.a" "$TARGET_DIR/lib/rv32/libc.a"
cp "$SRC_DIR/riscv-none-elf/lib/rv32im/ilp32/libm.a" "$TARGET_DIR/lib/rv32/libm.a"
LIBGCC_32=$(find "$SRC_DIR/lib/gcc/riscv-none-elf" -path "*/rv32im/ilp32/libgcc.a" | head -n 1)
if [ -n "$LIBGCC_32" ]; then
    cp "$LIBGCC_32" "$TARGET_DIR/lib/rv32/libgcc.a"
fi

# Copy 64-bit newlib libraries (rv64imac/lp64 or rv64gc/lp64 or rv64imafdc/lp64d)
RV64_DIR=$(find "$SRC_DIR/riscv-none-elf/lib" -path "*/rv64imac/lp64" -o -path "*/rv64gc/lp64" -o -path "*/rv64imafdc/lp64d" -o -path "*/rv64i/lp64" | head -n 1)
if [ -n "$RV64_DIR" ]; then
    echo "Found RV64 newlib libraries at $RV64_DIR"
    cp "$RV64_DIR/libc.a" "$TARGET_DIR/lib/rv64/libc.a"
    cp "$RV64_DIR/libm.a" "$TARGET_DIR/lib/rv64/libm.a"
fi

LIBGCC_64=$(find "$SRC_DIR/lib/gcc/riscv-none-elf" -path "*/rv64imac/lp64/libgcc.a" -o -path "*/rv64gc/lp64/libgcc.a" -o -path "*/rv64imafdc/lp64d/libgcc.a" -o -path "*/rv64i/lp64/libgcc.a" | head -n 1)
if [ -n "$LIBGCC_64" ]; then
    echo "Found RV64 libgcc at $LIBGCC_64"
    cp "$LIBGCC_64" "$TARGET_DIR/lib/rv64/libgcc.a"
fi

echo "Cleaning up..."
rm -f "third_party/toolchain.tar.gz"
rm -rf "$TEMP_DIR"

echo "Done! RV32 and RV64 standard libraries and headers populated at $TARGET_DIR"
