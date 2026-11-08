#pragma once

#include <stdint.h>

// Length of a scheduler time slice.
#define SCHED_TICK_MS 10

// Probes the SBI timer interface and arms the first tick. Call once at boot,
// before enabling the supervisor timer interrupt.
void timer_init(void);

// Handles a supervisor timer interrupt: wakes sleepers whose deadline has
// passed, runs the scheduler tick's kernel work if it is due, re-arms the timer
// and switches process if the time slice is over or a sleeper woke. Only called
// from handle_trap() for a trap taken in U-mode.
void timer_interrupt(void);

// Makes sure the timer fires no later than deadline (in uptime() units).
// Called by sleep_until().
void timer_wake_at(uint64_t deadline);

// Idles the hart until the next timer deadline, then does the same work as
// timer_interrupt() without switching processes. Called by the idle process
// when nothing is runnable.
void timer_idle_wait(void);

// Number of scheduler ticks since boot.
uint64_t timer_ticks(void);
