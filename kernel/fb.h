#pragma once

#include <stdint.h>

#define FB_WIDTH (1280)
#define FB_HEIGHT (800)

void fb_init(void);
uint32_t *fb_get_buffer(void);
int fb_present(uint32_t *buffer, uint32_t width, uint32_t height);
