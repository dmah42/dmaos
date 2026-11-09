#pragma once

#include <stdint.h>

#define SCHED_TICK_MS 10

// Call once at boot, before enabling the timer interrupt.
void timer_init(void);

void timer_interrupt(void);

// Makes the timer fire no later than deadline (in uptime() units).
void timer_wake_at(uint64_t deadline);

// Waits for the next timer deadline. Called by the idle process.
void timer_idle_wait(void);

uint64_t timer_ticks(void);
