#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------
// Framebuffer Console Public API
// -----------------------------------------------------------------------------
void fbcon_init(void);
void fbcon_putchar(char ch);
void fbcon_write(const char *buf, uint32_t len);
void fbcon_clear(void);
void fbcon_set_scale(uint32_t scale);
void fbcon_set_colors(uint32_t fg, uint32_t bg);
uint32_t fbcon_get_cols(void);
uint32_t fbcon_get_rows(void);
