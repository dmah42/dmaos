# dmaOS

dmaOS is a lightweight, custom 32-bit and 64-bit RISC-V operating system designed to run on the QEMU `virt` machine. It features a page-mapped kernel (SV32 / SV39), a cooperative scheduler, a multi-device virtual filesystem with persistence, an integrated lwIP network stack with BSD sockets, and a custom user shell.

oh, and it runs DOOM.

---

## Current Project Status

### 1. Kernel & Memory Management
- **CPU Targets**: 
  - 32-bit RISC-V: `riscv32-unknown-elf` (`rv32im`, `ilp32`).
  - 64-bit RISC-V: `riscv64-unknown-elf` (`rv64imac`, `lp64`).
- **Memory Mapping**: Universal multi-level paging system supporting **SV32** (2-level) on RV32 and **SV39** (3-level) on RV64 with dynamic user memory mapping and isolated page tables.
- **Multitasking**: Cooperative scheduler with context switching, process spawning, and parent-child execution waiting.
- **System Calls**: Parameter-specific assembly syscall routines routing user space traps (`ecall`) to kernel handlers. Returns self-documenting negative error codes (`enum Errno` from `errno.h`) translated via `strerror` in user space.
- **Dynamic Memory Allocation**: Standard heap allocator (`malloc`, `free`, `calloc`, `realloc`) backed by the `sbrk` system call which dynamically maps physical pages into the user address space.

### 2. Block Drivers & Mounting
- **VirtIO MMIO**: Support for two concurrent VirtIO block device controllers mapped to separate MMIO slots (`0x10001000` and `0x10002000`) and distinct QEMU buses.
- **Dual Partition Layout**:
  - **Device 0 (`disk.img`)**: System drive containing read-only system executables, configurations, and assets.
  - **Device 1 (`data.img`)**: Persistent data drive dynamically mounted at `/home`.

### 3. Filesystem Layer
- **Staging-Based Image Builds**: The build system compiles and stages the entire filesystem tree under `build/$(ARCH)/root/` on the host, and the filesystem image utility `mkfs` recursively walks this staging root to produce raw disk images.
- **Directories & Path Resolution**: Dynamic path resolution (`namei`/`namex`) with cross-boundary mount traversal and normalized path tracking.
- **Stateful File Descriptors**: Process-level open file descriptor tracking (`proc->ofile` up to `NUM_FILES_PER_PROCESS`) mapped to a global descriptor table (`GLOBAL_OPEN_FILE_LIMIT`), with automatic cleanup and console warning prints on memory leak detection during process exit.
- **Write Protections**: Writes, folder creations, and deletions are prohibited on Device 0 and allowed only under the persistent `/home` directory (Device 1).
- **Leak-Safe Overwriting**: Automatic block deallocation and file truncation (`itrunc`) when files are overwritten at offset 0.

### 4. Networking & BSD Sockets (lwIP)
- **VirtIO MMIO Network Device**: VirtIO network adapter driver operating on MMIO bus 3 with ring buffer transmit/receive queues.
- **lwIP TCP/IP Stack**: Full IPv4 network stack integrated in kernel space (`NO_SYS 1`) with Ethernet frame processing, dynamic ARP resolution, ICMP, UDP, and TCP.
- **BSD Socket Subsystem**: In-kernel socket abstraction and POSIX system calls (`socket`, `bind`, `listen`, `accept`, `connect`, `sendto`, `recvfrom`).
- **Unified VFS File Descriptors**: Network sockets are first-class file descriptors (`FD_SOCKET`) fully integrated with `read()`, `write()`, and `close()`.
- **POSIX Userland Headers**: Standard networking headers provided in user space: `<sys/socket.h>`, `<netinet/in.h>`, and `<arpa/inet.h>`.

### 5. Shell & Command Utilities
- **dmash Shell**: Standard shell with configuration parsing for path, prompt, etc.
- **User Library**:
  - Powered by the Newlib user-space standard library with dedicated 32-bit and 64-bit multilib configurations.
- **User Utilities**:
  - `cat` - read and display file contents.
  - `ls` - list directory structures.
  - `mkdir` - create persistent directories (restricted to `/home`).
  - `write` - write/append text strings to a file (always appends; restricted to `/home`).
  - `rm` - remove files and empty directories (restricted to `/home`).
  - `kilo` - externally developed interactive terminal text editor using ANSI escape sequences.
  - `snake` - classic snake game.
  - `hello` - simple greeting program.
  - `nettest` - network diagnostic test utility (supports both UDP and TCP echo server modes on port 9999).
  - `doom` - classic DOOM (shareware episode 1) via `doomgeneric`.

---

## Build & Run Instructions

### Prerequisites
Install the LLVM toolchain (`clang`, `lld`, `llvm-objcopy`) and QEMU (`qemu-system-riscv32`, `qemu-system-riscv64`):

- **macOS (Homebrew)**:
  ```bash
  brew install llvm qemu
  ```

- **Linux / WSL (Ubuntu/Debian)**:
  ```bash
  sudo apt update && sudo apt install -y clang lld llvm qemu-system-misc make opensbi
  ```

- **DOOM WAD Asset**:
  Obtain `doom1.wad` (Shareware DOOM WAD) and place it in the `disk/` directory (`disk/doom1.wad`).

### Build & Execution Commands

#### 32-Bit Target (Default)
- **Compile all binaries and build disk image**:
  ```bash
  make ARCH=riscv32 all   # or simply: make
  ```
- **Run in 32-bit QEMU**:
  ```bash
  make ARCH=riscv32 run   # or simply: make run
  ```
- **Clean 32-bit build artifacts**:
  ```bash
  make ARCH=riscv32 clean # or simply: make clean
  ```

#### 64-Bit Target
- **Compile all binaries and build disk image**:
  ```bash
  make ARCH=riscv64 all
  ```
- **Run in 64-bit QEMU**:
  ```bash
  make ARCH=riscv64 run
  ```
- **Clean 64-bit build artifacts**:
  ```bash
  make ARCH=riscv64 clean
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

- [x] **64-Bit RISC-V Architecture Port (RV64 / SV39)**:
  - Ported kernel, userland, paging, drivers, and newlib to `riscv64`.
- [x] **Standard C Library Integration (Newlib)**:
  - Replaced the homegrown standard library with standard **Newlib** (supporting both RV32 and RV64 multilibs).
  - Mapped Newlib system call stubs (`_read`, `_write`, `_open`, `_close`, `_sbrk`, `_stat`) to our kernel ABI.
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
- [x] **Network Stack & BSD Sockets (VirtIO Network Card & lwIP)**:
  - Implemented `virtio-net` MMIO network device driver and polling adapter.
  - Integrated lwIP TCP/IP stack with BSD socket syscalls and VFS file descriptor routing.
  - Implemented `nettest` utility for dynamic ARP, UDP, and TCP echo testing.
- [ ] **Network Utilities**:
  - `ping` - ICMP Echo Request/Reply utility to check host reachability.
  - `traceroute` - TTL-incrementing path analyzer.
  - `telnet` - clear-text interactive remote terminal client.
  - `wget` - retrieve files over HTTP directly to the persistent `/home` drive.
  - `netstat` - track and display active network interface statistics.

