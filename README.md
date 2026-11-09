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
- **System Calls**: Parameter-specific assembly syscall routines routing user space traps (`ecall`) to kernel handlers. Returns self-documenting negative error codes (`enum Errno` from `shared/k_errno.h`) using Linux errno numbering, translated by the user library to Newlib `errno` values for `strerror`.
- **Dynamic Memory Allocation**: Standard heap allocator (`malloc`, `free`, `calloc`, `realloc`) backed by the `sbrk` system call which dynamically maps physical pages into the user address space.

### 2. Block Drivers & Mounting
- **VirtIO MMIO**: Support for two concurrent VirtIO block device controllers mapped to separate MMIO slots (`0x10001000` and `0x10002000`) and distinct QEMU buses.
- **Dual Partition Layout**:
  - **Device 0 (`disk.img`)**: System drive containing read-only system executables, configurations, and assets.
  - **Device 1 (`data.img`)**: Persistent data drive dynamically mounted at `/home`.

### 3. Filesystem Layer & Pipes
- **Staging-Based Image Builds**: The build system compiles and stages the entire filesystem tree under `build/$(ARCH)/root/` on the host, and the filesystem image utility `mkfs` recursively walks this staging root to produce raw disk images.
- **Directories & Path Resolution**: Dynamic path resolution (`namei`/`namex`) with cross-boundary mount traversal and normalized path tracking.
- **Stateful File Descriptors**: Process-level open file descriptor tracking (`proc->ofile` up to `NUM_FILES_PER_PROCESS`) mapped to a global descriptor table (`GLOBAL_OPEN_FILE_LIMIT`), with automatic cleanup and console warning prints on memory leak detection during process exit.
- **Anonymous Pipes**: In-kernel ring buffer pipes (`FD_PIPE`) allocated via the `pipe()` system call with cooperative blocking and broken pipe detection (`ERR_BROKEN_PIPE`).
- **File Descriptor Remapping**: `spawn` syscall supports descriptor remapping (`struct FdRemap`) to redirect child standard streams (`stdin`, `stdout`, `stderr`) to pipes or files.
- **Write Protections**: Writes, folder creations, and deletions are prohibited on Device 0 and allowed only under the persistent `/home` directory (Device 1).
- **Leak-Safe Overwriting**: Automatic block deallocation and file truncation (`itrunc`) when files are overwritten at offset 0.

### 4. Networking & BSD Sockets (lwIP)
- **VirtIO MMIO Network Device**: VirtIO network adapter driver operating on MMIO bus 3 with ring buffer transmit/receive queues.
- **lwIP TCP/IP Stack**: Full IPv4 network stack integrated in kernel space (`NO_SYS 1`) with Ethernet frame processing, dynamic ARP resolution, ICMP, UDP, and TCP.
- **BSD Socket Subsystem**: In-kernel socket abstraction and POSIX system calls (`socket`, `bind`, `listen`, `accept`, `connect`, `sendto`, `recvfrom`). TCP `connect` blocks with a 10 s timeout and reports `ECONNREFUSED` / `ETIMEDOUT`.
- **Unified VFS File Descriptors**: Network sockets are first-class file descriptors (`FD_SOCKET`) fully integrated with `read()`, `write()`, and `close()`.
- **POSIX Userland Headers**: Standard networking headers provided in user space: `<sys/socket.h>`, `<netinet/in.h>`, and `<arpa/inet.h>`.

### 5. Graphics & Framebuffer Text Console
- **Unified Framebuffer Subsystem**: Core display manager (`kernel/fb.c` / `kernel/fb.h`) abstracting physical framebuffer state, runtime dimension accessors (`fb_get_width`, `fb_get_height`, `fb_get_stride`), and a shared fixed-point bilinear/nearest-neighbor scaling engine (`fb_present`) with zero board `#ifdef`s.
- **Dynamic QEMU fw_cfg Resolution**: QEMU display resolution configured at launch via `fw_cfg` (`-fw_cfg name=opt/dmaos/width/height`) and dynamically allocated at boot via `alloc_pages()`, allowing instant resolution switching (e.g. `make FB_WIDTH=1600 FB_HEIGHT=1200 run`) without kernel recompilation.
- **Gruvbox Dark ANSI Terminal**: In-kernel framebuffer text console (`kernel/fbcon.c`) featuring a 16-color Gruvbox Dark palette, dynamic grid scaling (e.g. 80x25 @ 2x or 3x on high-res displays), smooth scanline scrolling, and ANSI escape sequence parsing (SGR colors, cursor movements, and line clearing).
- **CP437 & Real-Time UTF-8 Decoding**: Full Code Page 437 bitmap font with real-time multi-byte UTF-8 decoding supporting custom prompts (e.g. `∅`), status banners, and international symbols.
- **Reproducible Font Generator**: Standalone font tooling (`tools/generate_font.py`) generating raw binary bitmaps packaged directly into object files at build time without source-tree bloat.
- **Strict Stream Isolation**:
  - **Kernel Logs (`kprintf` / `kputc`)**: Routed to OpenSBI UART and the in-kernel `kmesg` ring buffer only.
  - **Userland Streams (`stdout` / `stderr`)**: Rendered directly on the graphical framebuffer console screen.
- **Window Keyboard Input**: VirtIO keyboard driver (`kernel/drivers/virtio.c`) translating raw evdev hardware scancodes and Shift modifier state into ASCII characters and ANSI escape sequences (arrow keys, home, end, etc.) with transparent fallback to serial UART.

### 6. Hardware Board Support (VisionFive 2)
- **StarFive JH7110 (VisionFive 2)**: 64-bit multi-core hardware platform port (`ARCH=riscv64 BOARD=visionfive2`).
- **Memory Architecture**: Execution from physical RAM base `0x40200000` (`RAM_BASE=0x40000000`) with custom linker map and U-Boot `boot.scr` script.
- **Storage Subsystem**: Dedicated DesignWare Mobile Storage Host Controller (DW-MMC / SDIO) driver (`kernel/drivers/dw_mmc.c`) interacting directly with physical SD cards.
- **Display Driver**: Linear simple-framebuffer driver (`kernel/drivers/simplefb.c`) binding runtime video memory parameters passed from U-Boot / Device Tree into the core framebuffer subsystem.
- **Packaging & Deployment**: Automated image generation script (`tools/vf2/create_sdcard.sh`) preparing raw bootable GPT/MBR SD cards with SPL, U-Boot, and dmaOS partitions.

### 7. Shell & Command Utilities
- **dmash Shell**: Standard shell with configuration parsing (`/cfg/dmash.cfg`), command history traversal via arrow keys, path navigation, command pipelines (`cmd1 | cmd2`), and built-ins (`cd`, `clear`, `exit`, `kmesg`).
- **User Library**:
  - Powered by the Newlib user-space standard library with dedicated 32-bit and 64-bit multilib configurations.
- **User Utilities**:
  - `cat` - read and display file contents (supports reading from `stdin` when no filename is specified).
  - `less` - terminal pager utility with interactive scrolling and pipeline support.
  - `ls` - list directory structures.
  - `mkdir` - create persistent directories (restricted to `/home`).
  - `write` - write/append text strings or stream `stdin` to a file (restricted to `/home`).
  - `rm` - remove files and empty directories (restricted to `/home`).
  - `kilo` - externally developed interactive terminal text editor using ANSI escape sequences.
  - `snake` - classic snake game.
  - `hello` - simple greeting program.
  - `gfxtest` - framebuffer test that renders frames to a user buffer and presents them via `framebuffer_present` (exercises scaling).
  - `memtest` - heap allocator self-test covering `malloc`/`free`, heap expansion via `sbrk`, `calloc`, and `realloc`.
  - `nettest` - network diagnostic test utility (supports both UDP and TCP echo server modes on port 9999).
  - `nslookup` - DNS query utility resolving domain hostnames to IPv4 addresses.
  - `wget` - HTTP/1.0 file downloader (`wget [-O <file>] [-q] http://host[:port]/path`) following redirects; saves to the current directory (writable only under `/home`) or to `stdout` with `-O -`.
  - `doom` - classic DOOM (shareware episode 1) via `doomgeneric`.
  - `lua` - standalone Lua 5.4 scripting interpreter and interactive REPL (`/bin/lua`).
    - *Features*: Full core language and standard libraries (`math`, `string`, `table`, `io`, `os`, `coroutine`, `utf8`, `debug`).
    - *Sockets*: Built-in `socket` module (`user/lua/socket.c`) providing TCP `bind`/`accept`, `connect`, `receive` (`*l`, `*a`, or a byte count), `send`, and `gettime`, modelled on a minimal subset of LuaSocket.
    - *Sample Scripts*: `/test.lua`, and `/httpd.lua`, a static file HTTP server on port 8080 serving `/www` by default (`lua /httpd.lua [root_dir] [port]`) (forwarded from the host by QEMU, so `curl 127.0.0.1:8080` works from the host).
    - *Constraints*: Statically compiled pure ANSI C runtime without dynamic library loading (`.so` / `dlopen`). External modules must be pure Lua scripts or statically compiled into the binary. On 32-bit builds Lua uses 32-bit integers and floats (`LUA_32BITS`).

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

#### 32-Bit Target (QEMU Virt)
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

#### 64-Bit Target (QEMU Virt)
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

#### StarFive VisionFive 2 (Hardware Target)
- **Compile kernel binary and root filesystem image**:
  ```bash
  make ARCH=riscv64 BOARD=visionfive2 all
  ```
- **Generate bootable SD card image**:
  ```bash
  ./tools/vf2/create_sdcard.sh build/rv64-vf2/sdcard.img
  ```
- **Clean VisionFive 2 build artifacts**:
  ```bash
  make ARCH=riscv64 BOARD=visionfive2 clean
  ```

### Tests
`make test` boots the system headless in QEMU and checks preemption, pipes, sleeps, `Ctrl+C`, a TCP connection from the guest, serving HTTP to the host, DOOM (skipped without `doom1.wad`) and power off. It runs on a copy of the root disk with `tests/disk/` added and a scratch data disk, so your `data.img` is left alone. Serial logs are written to `build/<arch>/test/`.
```bash
make ARCH=riscv32 test
make ARCH=riscv64 test
make ARCH=riscv64 test TESTS="core httpd"   # a subset: core, signals, httpd, doom
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

- [ ] **Framebuffer Console Scrollback History**:
  - Add an in-memory scrollback ring buffer with keyboard shortcuts (e.g. `Shift + PageUp` / `Shift + PageDown`) to inspect previous terminal output.

---

## Long-Term Architectural Goals

- [x] **Preemptive Multitasking**:
  - Set up hardware timer interrupts using the RISC-V SBI timer interface.
  - Implement a preemptive scheduler to prevent infinite loops in user space from freezing the system.
- [ ] **VisionFive 2 USB Keyboard**:
  - Driver for the JH7110's USB host controller (Cadence USB 3 in xHCI mode), with device enumeration and the HID boot keyboard protocol.
  - Implements `input_driver_init()` and `input_driver_poll_event()` from `kernel/input_driver.h` in place of `input_stub.c`; translation, buffering and `Ctrl+C` come from the shared input layer.
- [ ] **Virtual Console Multiplexing (Multi-TTY)**:
  - Add virtual console session buffers and a keyboard router (switching active shell sessions via `Alt + F1/F2/F3`).
  - Replace the interim `Ctrl+C` rule (signal every descendant of the console's first shell) with POSIX process groups and sessions: `setpgid`, a controlling terminal per console, and `tcsetpgrp` so each console's shell sets its own foreground job.
  - Move termios and line editing into the kernel per console, replacing the user library's copy and the interim `ISIG` syscall.
  - Job control: `Ctrl+Z` (`SIGTSTP`), a stopped process state, and `fg`, `bg` and `jobs` in the shell.
- [ ] **Process Signals**:
  - Done: `SIGINT` and `SIGQUIT` from `Ctrl+C` and `Ctrl+\`, ending the foreground job.
  - Remaining: `SIGKILL`, a `kill` command, and user handlers (`signal()`, `sigaction`).
- [x] **Loopback Networking (`localhost`)**:
  - The lwIP loopback interface (`LWIP_NETIF_LOOPBACK`, polled from `net_poll`) delivers `127.0.0.1` locally.
  - `localhost` resolves to `LOOPBACK=` in `/cfg/net.cfg` without querying the DNS server.
- [ ] **Network Utilities**:
  - `ping` - ICMP Echo Request/Reply utility to check host reachability.
  - `traceroute` - TTL-incrementing path analyzer.
  - `telnet` - clear-text interactive remote terminal client.
  - `netstat` - track and display active network interface statistics.

