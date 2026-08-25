#pragma once

#include <stdbool.h>
#include <stdint.h>

void input_init(void);
bool input_poll_event(uint16_t *code, int *pressed);
int input_getchar(void);
