# dmaOS

dmaOS is a lightweight, custom 32-bit RISC-V operating system designed to run on the QEMU `virt` machine. It features a page-mapped kernel, a cooperative scheduler, a multi-device virtual filesystem with persistence, and a custom user shell.

---

## Current Project Status

### 1. Kernel & Memory Management
- **CPU Target**: 32-bit RISC-V (`riscv32-unknown-elf`).
- **Memory Mapping**: SV32 paging system with kernel pages mapped directly and user pages mapped dynamically.
- **Multitasking**: Cooperative scheduler with context switching, process spawning, and parent-child execution waiting.
- **System Calls**: Parameter-specific assembly syscall routines (`syscall1` to `syscall4`) routing user space traps (`ecall`) to kernel handlers. Returns detailed self-documenting negative error codes (`enum Errno` from `errno.h`) translated via `strerror` in user space.
- **Dynamic Memory Allocation**: Standard heap allocator (`malloc`, `free`, `calloc`, `realloc`) using a first-fit boundary-tag (header/footer) design in the user-space standard library, backed by the `sbrk` system call which dynamically maps physical pages into the user address space.

### 2. Block Drivers & Mounting
- **VirtIO MMIO**: Support for two concurrent VirtIO block device controllers mapped to separate MMIO slots (`0x10001000` and `0x10002000`) and distinct QEMU buses.
- **Dual Partition Layout**:
  - **Device 0 (`disk.img`)**: System drive containing read-only system executables, configurations, and assets.
  - **Device 1 (`data.img`)**: Persistent data drive dynamically mounted at `/home`.

### 3. Filesystem Layer
- **Staging-Based Image Builds**: The build system compiles and stages the entire filesystem tree under `build/root/` on the host, and the filesystem image utility `mkfs` recursively walks this staging root to produce the raw disk images.
- **Directories & Path Resolution**: Dynamic path resolution (`namei`/`namex`) with cross-boundary mount traversal and normalized path tracking.
- **Stateful File Descriptors**: Process-level open file descriptor tracking (`proc->ofile` up to `NUM_FILES_PER_PROCESS`) mapped to a global descriptor table (`GLOBAL_OPEN_FILE_LIMIT`), with automatic cleanup and console warning prints on memory leak detection during process exit.
- **Write Protections**: Writes, folder creations, and deletions are prohibited on Device 0 and allowed only under the persistent `/home` directory (Device 1).
- **Leak-Safe Overwriting**: Automatic block deallocation and file truncation (`itrunc`) when files are overwritten at offset 0.

### 4. Shell & Command Utilities
- **dmash Shell**: Standard shell with configurations parsing for path, prompt, etc.
- **User Library**:
  - Relying on newlib user-space standard library.
- **User Utilities**:
  - `cat` - read and display file contents.
  - `ls` - list directory structures.
  - `mkdir` - create persistent directories (restricted to `/home`).
  - `write` - write/append text strings to a file (always appends; restricted to `/home`).
  - `rm` - remove files and empty directories (restricted to `/home`).
  - `kilo` - externally developed interactive terminal text editor using ANSI escape sequences.
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

## IDE Integration (Autocomplete & Diagnostics)

To configure VS Code and C/C++ language servers (like `clangd` or the Microsoft C/C++ extension) to accurately parse compiler paths, definitions, and targets for both the host tools and guest RISC-V targets:

1. **Set up a Python Virtual Environment**:
   ```bash
   python3 -m venv .venv
   . .venv/bin/activate
   ```
2. **Install the development dependencies**:
   ```bash
   pip install .
   ```
3. **Generate the compilation database**:
   ```bash
   compiledb make
   ```
This generates a `compile_commands.json` database at the workspace root, which the editor automatically loads to configure autocomplete and diagnostics. Run `compiledb make -n` (or `compiledb make` during a build) whenever directories, target flags, or C files are added or modified.

---

## Future Roadmap (TODOs)

- [ ] **Standard C Library Integration (Newlib)**:
  - Replace the custom, homegrown user-space standard library (`user/lib`) with standard **Newlib**.
  - Map Newlib's standard system call stubs (like `_read`, `_write`, `_open`, `_close`, `_sbrk`, and `_stat`) to our kernel ABI using a defensive translation layer in `user/lib` to maintain decoupling.

- [ ] **Standard Streams & Shell Piping**:
  - Introduce file descriptor tracking for standard streams (`stdin`, `stdout`, `stderr`) and implement kernel-supported pipe buffers to enable shell command pipelines (e.g., `cat file.txt | grep query`).

---

## Long-Term Architectural Goals

- [ ] **Preemptive Multitasking**:
  - Set up hardware timer interrupts using the RISC-V SBI timer interface.
  - Implement a preemptive scheduler to prevent infinite loops in user space from freezing the system.
- [ ] **Virtual Console Multiplexing (Multi-TTY)**:
  - Add virtual console session buffers and a keyboard router (switching active shell sessions via `Alt + F1/F2/F3`).
- [ ] **Process Signals**:
  - Implement basic UNIX-like signals (e.g., `SIGINT`, `SIGKILL`) to support terminal execution interrupts (`Ctrl+C`).
- [ ] **Network Stack & Utilities (VirtIO Network Card)**:
  - Implement a `virtio-net` network interface driver and a basic network stack (ARP, IP, ICMP, UDP, TCP).
  - Create standard command-line networking utilities:
    - `ping` - ICMP Echo Request/Reply utility to check host reachability.
    - `traceroute` - TTL-incrementing path analyzer.
    - `telnet` - clear-text interactive remote terminal client.
    - `wget` - retrieve files over HTTP directly to the persistent `/home` drive.
    - `netstat` - track and display active network interface statistics.

