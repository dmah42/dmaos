#include "timer.h"

#include "k_time.h"
#include "kernel.h"
#include "net.h"
#include "process.h"
#include "stdlib.h"

#include <stdbool.h>

#define SBI_EXT_LEGACY_SET_TIMER 0x00
#define SBI_EXT_BASE 0x10
#define SBI_EXT_TIME 0x54494D45

#define SBI_BASE_PROBE_EXTENSION 3
#define SBI_TIME_SET_TIMER 0

#define TICK_INTERVAL ((uint64_t)SCHED_TICK_MS * TICKS_PER_MS)

#define SIP_STIP (1 << 5)

static bool use_time_ext = false;
static uint64_t ticks = 0;

// Programs the next timer interrupt for the absolute time `deadline` (in
// `time` CSR units). This also clears any pending supervisor timer interrupt.
static void sbi_set_timer(uint64_t deadline) {
#if __riscv_xlen == 64
  long lo = (long)deadline;
  long hi = 0;
#else
  long lo = (long)(uint32_t)deadline;
  long hi = (long)(uint32_t)(deadline >> 32);
#endif
  if (use_time_ext) {
    sbi_call(lo, hi, 0, 0, 0, 0, SBI_TIME_SET_TIMER, SBI_EXT_TIME);
  } else {
    sbi_call(lo, hi, 0, 0, 0, 0, 0, SBI_EXT_LEGACY_SET_TIMER);
  }
}

static uint64_t next_deadline = 0;

// Arms the timer one interval after the previous deadline, so handling latency
// doesn't make the tick drift. If that deadline has already passed (a tick was
// delayed by a long kernel operation), the next one is measured from now
// instead, so there is no burst of catch-up interrupts.
static void timer_arm(void) {
  uint64_t now = uptime();
  next_deadline += TICK_INTERVAL;
  if (next_deadline <= now) {
    next_deadline = now + TICK_INTERVAL;
  }
  sbi_set_timer(next_deadline);
}

void timer_init(void) {
  struct sbiret ret = sbi_call(SBI_EXT_TIME, 0, 0, 0, 0, 0,
                               SBI_BASE_PROBE_EXTENSION, SBI_EXT_BASE);
  use_time_ext = ret.error == 0 && ret.value != 0;
  kprintf("timer: %d ms tick via SBI %s\n", SCHED_TICK_MS,
          use_time_ext ? "TIME extension" : "legacy set_timer");
  timer_arm();
}

// Periodic kernel work, run once per tick. The kernel is never interrupted
// (interrupts are only taken in U-mode), so non-reentrant subsystems are safe
// to call here.
static void timer_service(void) {
  timer_arm();
  ticks++;
  wake_expired_sleepers();

  // Polling the network every tick keeps lwIP's timers running when no process
  // is using a socket, and runs the lwIP callbacks that wake blocked sockets.
  net_poll();
}

void timer_tick(void) {
  timer_service();
  yield();
}

void timer_idle_wait(void) {
  // wfi resumes on an interrupt that is pending and enabled in sie even though
  // sstatus.SIE is clear, so no trap is taken in S-mode. It may also resume
  // spuriously, which is why STIP is checked.
  __asm__ __volatile__("wfi");
  if (READ_CSR(sip) & SIP_STIP) {
    timer_service();
  }
}

uint64_t timer_ticks(void) { return ticks; }
