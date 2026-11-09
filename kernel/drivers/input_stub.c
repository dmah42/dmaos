#include "input_driver.h"

// Placeholder for boards without a keyboard driver yet: it reports no key
// events, so console input comes only from serial.

void input_driver_init(void) {}

bool input_driver_poll_event(uint16_t *code, int *pressed) {
  (void)code;
  (void)pressed;
  return false;
}
