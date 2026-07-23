# dmaOS

dmaOS is a lightweight, custom 32-bit RISC-V operating system designed to run on the QEMU `virt` machine. It features a page-mapped kernel, a cooperative scheduler, a multi-device virtual filesystem with persistence, and a custom user shell.

---

## Current Project Status

### 1. Kernel & Memory Management
- **CPU Target**: 32-bit RISC-V (`riscv32-unknown-elf`).
- **Memory Mapping**: SV32 paging system with kernel pages mapped directly and user pages mapped dynamically.
- **Multitasking**: Cooperative scheduler with context switching, process spawning, and parent-child execution waiting.
- **System Calls**: Parameter-specific assembly syscall routines (`syscall1` to `syscall4`) routing user space traps (`ecall`) to kernel handlers.

### 2. Block Drivers & Mounting
- **VirtIO MMIO**: Support for two concurrent VirtIO block device controllers mapped to separate MMIO slots (`0x10001000` and `0x10002000`) and distinct QEMU buses.
- **Dual Partition Layout**:
  - **Device 0 (`disk.img`)**: System drive containing read-only system executables, configurations, and assets.
  - **Device 1 (`data.img`)**: Persistent data drive dynamically mounted at `/home`.

### 3. Filesystem Layer
- **Staging-Based Image Builds**: The build system compiles and stages the entire filesystem tree under `build/root/` on the host, and the filesystem image utility `mkfs` recursively walks this staging root to produce the raw disk images.
- **Directories & Path Resolution**: Dynamic path resolution (`namei`/`namex`) with cross-boundary mount traversal and normalized path tracking.
- **Write Protections**: Writes, folder creations, and deletions are prohibited on Device 0 and allowed only under the persistent `/home` directory (Device 1).
- **Leak-Safe Overwriting**: Automatic block deallocation and file truncation (`itrunc`) when files are overwritten at offset 0.

### 4. Shell & Command Utilities
- **dmash Shell**: Standard shell with configurations parsing for path, prompt, etc.
- **User Utilities**:
  - `cat` - read and display file contents.
  - `ls` - list directory structures.
  - `mkdir` - create persistent directories (restricted to `/home`).
  - `write` - write/overwrite text strings to a file (restricted to `/home`).
  - `rm` - remove files and empty directories (restricted to `/home`).
  - `snake` - classic snake game.
  - `hello` - simple greeting program.

---

## Build & Run Instructions

### Prerequisites
Install the LLVM toolchain (clang, lld, llvm-objcopy) and QEMU:
```bash
brew install llvm qemu
```

### Build & Execution Commands
- **Clean builds**:
  ```bash
  make clean
  ```
- **Compile all binaries and format disk images**:
  ```bash
  make
  ```
- **Run the OS inside QEMU**:
  ```bash
  make run
  ```

---

## Future Roadmap (TODOs)

- [ ] **Detailed Syscall Error Codes**:
  - Transition system call returns from simple `-1`/`0` to detailed POSIX-like error codes (e.g., `ENOENT` for missing files, `ENOTEMPTY` for non-empty directories, `EACCES` for read-only partitions).
  - Update user utilities to print context-rich error messages.

- [ ] **Stateful File Descriptors**:
  - Implement a stateful file descriptor table in the kernel and associated syscalls (`open`, `read`, `write`, `close`).
  - Support read/write seek offsets and automatic atomic file appending.
