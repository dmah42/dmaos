#pragma once

#include <stdint.h>

#define FB_WIDTH (1280)
#define FB_HEIGHT (800)

void ramfb_init(void);
uint32_t *ramfb_get_buffer(void);
int ramfb_present(uint32_t *buffer, uint32_t width, uint32_t height);