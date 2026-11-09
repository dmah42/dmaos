#pragma once

#include <stdbool.h>
#include <stdint.h>

// Implemented by the board's keyboard driver. Only kernel/input.c calls these.

void input_driver_init(void);

// Returns the next key event (Linux key code, pressed or released) from the
// device, or false if there is none.
bool input_driver_poll_event(uint16_t *code, int *pressed);
