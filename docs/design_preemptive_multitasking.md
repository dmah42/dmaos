# Technical Design: Preemptive Multitasking

## 1. Overview & Goals

dmaOS currently runs a purely cooperative scheduler: a process gives up the CPU only when it (or the kernel on its behalf) calls `yield()`. A user program that loops without making a syscall, such as `lua -e "while true do end"`, owns the CPU forever, and nothing (not even the shell) runs again.

This document specifies how to add **timer-driven preemption** so that every runnable process gets the CPU regardless of how it behaves. It is also the foundation for three roadmap items that depend on processes genuinely running side by side:

- **Process signals / `Ctrl+C`**: the kernel must regain control from a spinning process in order to deliver a signal to it.
- **Virtual console multiplexing (Multi-TTY)**: multiple interactive sessions need a fair share of the CPU.
- **Loopback networking (`localhost`)**: a local server and client must make progress concurrently.

### 1.1 Goals
- A runaway user process cannot freeze the system.
- No new locking discipline is needed in existing kernel code for the first phase.
- Works identically on QEMU `virt` (RV32 and RV64) and the VisionFive 2.
- The existing syscall ABI and user programs are unchanged.

### 1.2 Non-Goals
- **Kernel preemption**: code running in S-mode is not interrupted (see §3.1).
- **SMP**: dmaOS runs on a single hart; the VisionFive 2's other U74 cores remain parked.
- **Priority scheduling**: plain round-robin is kept.

---

## 2. Current State

### 2.1 Process Model (`kernel/process.c`, `kernel/process.h`)
- A fixed table of `PROCS_MAX` (8) `struct Process` entries. States are `UNUSED`, `RUNNABLE` and `EXITED`; there is no blocked or sleeping state.
- Each process has an 8 KiB kernel stack embedded in its `struct Process` (`stack[8192]`) and its own page table. The kernel is identity-mapped into every page table.
- `idle_proc` (PID 0) is the `kmain` context running on the boot stack.

### 2.2 Context Switching
- `yield()` (`kernel/process.c:324`) picks the next `RUNNABLE` process round-robin, switches `satp`, points `sscratch` at the next process's kernel stack top, and calls `switch_context()`, which saves and restores callee-saved registers on the kernel stack.
- If nothing other than the current process is runnable, the scheduler falls back to `idle_proc`. Control then returns to `kmain` after its single `yield()`, and **the system powers off**. That's how the machine shuts down today when the shell exits.

### 2.3 Trap Path (`kernel/kernel.c`)
- `kentry` swaps `sp` with `sscratch` (`csrrw sp, sscratch, sp`) to move onto the current process's kernel stack, saves all GPRs into a `struct trap_frame`, and calls `handle_trap()` (`kernel/kernel.c:1001`).
- This entry sequence **assumes every trap comes from U-mode**: `sscratch` must hold the kernel stack top when the trap is taken. A trap taken while already in S-mode would corrupt the stack.
- `handle_trap()` handles `ecall` (syscalls). Any other trap from U-mode is treated as a crash and kills the process, and any trap from S-mode panics.
- `sepc` is read into a local and written back at the end of `handle_trap()`. It survives a `yield()` inside a syscall because it lives on the process's kernel stack. `sstatus` is not saved, which is safe only because every trap currently enters from U-mode (`SPP = 0`).
- `user_entry` sets `sstatus = SPIE | SUM` before the first `sret`, so U-mode would accept interrupts **if any were enabled in `sie`**. Nothing enables `sie` today, so no interrupts are ever taken.

### 2.4 Waiting Is Busy Polling
Every blocking operation is a loop around `yield()`:

| Operation | Location | Loop |
|---|---|---|
| `wait()` for a child | `kernel/process.c:434` | yields until the child is `EXITED` |
| Console read | `kernel/fs.c:1037` | yields until `getchar()` returns a character |
| Pipe read and write | `kernel/pipe.c:83`, `kernel/pipe.c:105` | yields until data or space is available |
| TCP connect, DNS lookup | `kernel/net.c:394`, `kernel/net.c:793` | `net_poll()` plus `yield()` with an `uptime_ms()` timeout |
| Socket `read` / `accept` | user space (`user/lua/socket.c`, `wget`) | `ENOENT` and `yield()` retry loops |

A waiting process is always `RUNNABLE`, so it is scheduled and immediately yields again. This works, but it means the idle path is never reached while anything is waiting.

### 2.5 Timekeeping
- `uptime()` reads the `time` CSR; `shared/k_time.h` provides `TICKS_PER_SEC` (10 MHz on QEMU, 4 MHz on the VisionFive 2).
- The SBI firmware (OpenSBI) is already used for console I/O and shutdown via `sbi_call()`.
- lwIP timers (`sys_check_timeouts()`) run only when something calls `net_poll()`. With no process touching the network, TCP retransmits and ACKs stall.

---

## 3. Phase 1: User-Mode Preemption

### 3.1 Scope: Interrupts Only in U-Mode
The timer interrupt is enabled (`sie.STIE`) but **`sstatus.SIE` stays 0 whenever the CPU is in S-mode**. That is already the case: hardware clears `SIE` on trap entry, and the kernel never sets it. In U-mode, supervisor interrupts are taken regardless of `sstatus.SIE`.

A timer that fires while the kernel is running stays pending, and is taken the instant the kernel `sret`s back to U-mode. Consequences:

- **No kernel code is ever interrupted.** lwIP, the filesystem, the framebuffer console, `kmesg`, the pipe ring buffers and the page allocator keep exactly the guarantees they have today. No locks or interrupt masking need to be added.
- **`kentry` keeps working unchanged.** Every trap still arrives from U-mode with `sscratch` holding the kernel stack.
- **The preemption point is a quiescent kernel state.** The timer handler runs with no other kernel activity in flight, so it may safely call into non-reentrant subsystems like `net_poll()`.
- **Latency cost:** a long-running kernel operation (e.g. a large disk read) delays preemption until it finishes. That's acceptable for a single-user OS and can be revisited with kernel preemption later.
- **No ticks while everything waits in the kernel.** The busy-wait loops of §2.4 run in S-mode, so while every process is waiting (e.g. the shell blocked on console input), no tick is taken and the periodic kernel work in the tick doesn't run. That's no worse than today, and Phase 2's idle loop (§4.3) removes it.

### 3.2 Timer Source
Program the next tick through the **SBI TIME extension** (`EID 0x54494D45`, `FID 0`, `sbi_set_timer(stime_value)`):

- It works on QEMU (OpenSBI) and on the VisionFive 2. The U74 cores do not implement the `Sstc` extension, so writing `stimecmp` directly is not portable.
- Setting a new deadline also clears the pending supervisor timer interrupt (`sip.STIP`), so no extra acknowledgement is needed.
- If the TIME extension is absent (very old SBI), fall back to the legacy `set_timer` call (`EID 0x00`). `sbi_probe_extension` (`EID 0x10`, `FID 3`) can decide at boot.

The tick length is `SCHED_TICK_MS` milliseconds, converted with `TICKS_PER_MS` from `shared/k_time.h`, so it is correct on both boards. The default is **10 ms** (100 Hz).

### 3.3 Boot Changes (`kmain`)
After `process_init()` and before the first `yield()`:
1. `timer_init()`: program the first deadline at `uptime() + SCHED_TICK_MS * TICKS_PER_MS`.
2. Set `sie.STIE` (bit 5).

### 3.4 Trap Handling Changes (`handle_trap`)
- Check the interrupt bit (the MSB of `scause`) **first**. Today, any non-`ecall` trap from U-mode is treated as a crash, so an unhandled interrupt would kill the running process.
- For `scause == (INTERRUPT | 5)` (supervisor timer):
  1. Program the next deadline.
  2. Increment a global `ticks` counter (useful for diagnostics and future sleep timeouts).
  3. Run periodic kernel work: `net_poll()`, so lwIP timers advance even when no process is using the network.
  4. Call `yield()` to switch to the next runnable process.
- **Do not advance `sepc`** for interrupts. The `+4` applies only to `ecall`.
- Any other interrupt cause is logged and ignored (not fatal).

`yield()` from the timer path is the same operation as `yield()` from `SYSCALL_YIELD`, which is already exercised constantly. The interrupted process resumes later through the normal `handle_trap()` return and `sret`, with its `sepc` preserved on its kernel stack.

### 3.5 Scheduler Policy
- Round-robin over `RUNNABLE` processes, unchanged.
- A process that yields voluntarily forfeits the rest of its slice. The next timer interrupt may arrive early in the next process's slice; that's acceptable at 10 ms granularity. If it matters later, a per-switch "slice start" timestamp can let the timer handler skip a switch when the slice is not used up.

### 3.6 Idle and Shutdown
Today, reaching `idle_proc` means "nothing left, power off". In Phase 1 every waiting process is still `RUNNABLE` (§2.4), so idle is still only reached when no processes remain, and the existing behaviour can stay. Phase 2 changes that (§4.3).

---

## 4. Phase 2: Blocking Instead of Busy Polling

Preemption makes spinning processes fair, but busy-wait loops still burn their whole slice doing nothing. Phase 2 lets waiting processes stop being scheduled.

### 4.1 New Process States
Add `PROCSTATE_BLOCKED` with a wake condition stored in the process:
- `wait_chan`: an opaque pointer naming what the process waits on (a pipe, a socket, a child process, the console).
- `wake_time`: an optional `uptime()` deadline for timed waits, checked on every tick.

Primitives (single hart, kernel non-preemptible, so no locks are needed):
- `sleep_on(chan, timeout_ms)`: mark the current process `BLOCKED` on `chan`, then `yield()`. A `NULL` `chan` sleeps for the timeout only.
- `wakeup(chan)`: mark every process blocked on `chan` `RUNNABLE`.
- The timer handler wakes processes whose `wake_time` has passed.

### 4.2 Converting the Busy Loops
| Wait | Wakes on |
|---|---|
| `wait_process()` | `exit_current_process()` calling `wakeup(child)` |
| Pipe read and write | the other end writing, reading or closing (`kernel/pipe.c`) |
| Console read | a one-tick timeout: the reader sleeps for `SCHED_TICK_MS` and polls again. Input is polled, not interrupt driven, so nothing could wake it (see §5.1 for why the tick doesn't gather input yet). The user library's blocking TTY read (`VMIN > 0`) now uses `SYSCALL_READ`, which reaches this path, instead of spinning on `SYSCALL_GETCHAR`. |
| Socket connect (kernel-internal wait) | lwIP callbacks (`tcp_recv_callback`, `tcp_accept_callback`, `tcp_connected_callback`, `tcp_err_callback`) calling `wakeup(socket)`. These run inside `net_poll()`, which the timer handler now calls every tick. |
| `sleep_ms` | `wake_time`, once a sleep syscall exists (§6, Phase 2 step 4) |

The socket `read()` contract (`ENOENT` when no data is available) is part of today's user ABI, and `user/lua/socket.c` and `wget` retry on it. **Socket reads and `accept` stay non-blocking**, so that contract is unchanged. Only waits that are already internal to the kernel (`connect`, DNS lookup) are converted to `sleep_on()`.

### 4.3 Idle Loop
Once processes can block, the scheduler can find nothing runnable while processes still exist. `kmain` changes from "yield once, then power off" to:

```c
for (;;) {
  yield();
  if (no process is in use) {
    shutdown();
  }
  __asm__ __volatile__("wfi"); // wakes on the pending timer even with sstatus.SIE = 0
}
```

`wfi` resumes when an interrupt is pending and enabled in `sie`, even though `sstatus.SIE = 0`. So the idle loop never takes a trap in S-mode, and the U-mode-only invariant of §3.1 holds. After waking, idle runs the same tick work as the trap path (deadline, wake timed sleepers, `net_poll()`) before `yield()`ing.

Deadlines are scheduled one interval after the previous deadline rather than one interval from now, so wake-up latency doesn't make the tick drift; a deadline that has already passed is re-based on now.

---

## 5. Phase 3: Foundations for Signals and `Ctrl+C`

This phase isn't designed in full here; it's listed to show what Phases 1 and 2 must leave room for.

### 5.1 Kernel Input Buffer
Keyboard input is currently consumed only by whoever calls `getchar()`. To notice `Ctrl+C` while a process spins, the timer handler polls `input_getchar()` and the SBI console into a kernel ring buffer. Console reads then drain the buffer instead. The tick handler sees every byte as it arrives.

The virtio keyboard has a single event queue shared by `input_getchar()` (console text) and `input_poll_event()` (raw key presses and releases, used by DOOM). A tick that drains the queue into a text buffer would steal raw events, so the buffer has to sit below both: queue raw events, and translate to text only for console readers.

### 5.2 Foreground Process and Delivery
- The console tracks a **foreground process**, set by the shell when it starts a job.
- On `0x03`, the kernel marks the foreground process's pending signal set (`SIGINT`).
- Pending signals are checked on **every return to U-mode** (end of `handle_trap()`), which now happens at least every tick even for a spinning process. The default `SIGINT` action terminates the process with status `128 + 2`. User handlers (`signal()`/`sigaction`) are a later extension.
- A blocked process with a pending signal is woken so the signal can be acted on.

---

## 6. Implementation Roadmap

### Phase 1: Timer Preemption
1. Add `sbi_set_timer()` (TIME extension with legacy fallback) and `timer_init()`; define `SCHED_TICK_MS`.
2. Enable `sie.STIE` in `kmain` before the first `yield()`.
3. Handle interrupts first in `handle_trap()`: timer → re-arm, count, `net_poll()`, `yield()`; other interrupts → log; no `sepc` advance.
4. Expose the tick count in a boot or `kmesg` diagnostic for verification.

### Phase 2: Blocking
1. `PROCSTATE_BLOCKED`, `wait_chan`, `wake_tick`, `sleep_on()` and `wakeup()`.
2. Convert `wait_process()`, then pipes, then sockets, then console reads (after the input buffer).
3. Idle `wfi` loop and the revised shutdown rule.
4. Optional: a `sleep_ms` syscall, replacing the user-space `uptime()` and `yield()` spin in `user/lib/user.c`.

### Phase 3: Signals
1. Kernel input ring buffer filled from the tick.
2. Foreground process tracking and the shell integration.
3. Pending-signal check on return to user; `SIGINT` default action.

### 6.1 Verification
- **Fairness:** `lua -e "while true do end" | lua -e "for i = 1, 5 do print(i); local t = os.clock() + 1; while os.clock() < t do end end"`. Today the spinning producer starves the consumer and nothing prints. With Phase 1 the consumer prints `1`–`5` about a second apart.
- **Network timers:** start `lua /httpd.lua`, and let a TCP connection from the host sit idle. Phase 1's `net_poll()` per tick keeps lwIP's timers running, because `httpd.lua`'s accept loop returns to U-mode between retries.
- **Both boards:** confirm the tick rate on QEMU (10 MHz timebase) and the VisionFive 2 (4 MHz), e.g. by logging `ticks` against `uptime()` after a fixed interval.
- **Regression:** DOOM, `less`, pipelines, `wget` and `httpd.lua` behave as before.

---

## 7. Open Decisions

| # | Decision | Recommendation |
|---|---|---|
| 1 | Tick length | 10 ms (100 Hz). It's responsive enough for interactive use, with negligible overhead at QEMU and U74 speeds. |
| 2 | Timer mechanism | SBI TIME extension with legacy fallback; `stimecmp` (`Sstc`) is not available on the U74. |
| 3 | Kernel preemption | Not now. U-mode-only preemption avoids auditing every kernel subsystem for reentrancy. |
| 4 | Periodic kernel work in the tick | `net_poll()` in Phase 1; input polling once the input buffer exists (Phase 3). |
| 5 | Socket read semantics in Phase 2 | **Decided:** keep today's non-blocking `ENOENT` contract for socket `read` and `accept`. |
| 6 | `PROCS_MAX` | 8 is tight once pipelines, multiple TTYs and servers run concurrently; revisit with Multi-TTY. |
| 7 | Process kernel stack | 8 KiB embedded in `struct Process`; unchanged, since no nesting is introduced (one trap frame per process at a time). |
