#pragma once

#include "stdint.h"

#define FB_WIDTH (1280)
#define FB_HEIGHT (800)

void ramfb_init();
uint32_t *ramfb_get_buffer();