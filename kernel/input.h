#pragma once

#include <stdbool.h>
#include <stdint.h>

// Console input from the keyboard and the serial console.

void input_init(void);

// Moves pending input from the devices into the kernel's buffers. Called on
// every timer tick.
void input_poll_devices(void);

// Raw key events. The calling process owns the keyboard until it exits.
bool input_poll_event(uint16_t *code, int *pressed);

// Returns the next character, or -1 if there is none.
int input_getchar(void);

// Blocks until input arrives. Returns ERR_INTERRUPTED if a signal is pending.
int input_wait(void);

// Sets termios ISIG for the console.
void input_set_isig(bool on);

void input_process_exited(int pid);
