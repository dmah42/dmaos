#pragma once

#include <stdint.h>

struct Framebuffer {
  uint32_t *base;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
};

// Core Display Subsystem Interface
void fb_init(void);
void fb_register(uint32_t *base, uint32_t width, uint32_t height,
                 uint32_t stride);

uint32_t *fb_get_buffer(void);
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
uint32_t fb_get_stride(void);

int fb_present(uint32_t *buffer, uint32_t width, uint32_t height);
