#pragma once

#include <stdint.h>

// Length of a scheduler time slice.
#define SCHED_TICK_MS 10

// Probes the SBI timer interface and arms the first tick. Call once at boot,
// before enabling the supervisor timer interrupt.
void timer_init(void);

// Handles a supervisor timer interrupt: re-arms the timer, runs periodic
// kernel work and switches to the next runnable process. Only called from
// handle_trap() for a trap taken in U-mode.
void timer_tick(void);

// Idles the hart until the next tick, then runs the same periodic kernel work
// as timer_tick() without switching processes. Called by the idle process when
// nothing is runnable.
void timer_idle_wait(void);

// Number of timer interrupts handled since boot.
uint64_t timer_ticks(void);
