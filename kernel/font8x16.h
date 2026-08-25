#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------
// Raw 8x16 CP437 Bitmap Font (4096 bytes: 256 glyphs x 16 bytes/glyph)
// -----------------------------------------------------------------------------
// These linker symbols are automatically emitted by llvm-objcopy when packaging
// kernel/fonts/font8x16.bin into the kernel binary.
extern const char _binary_font8x16_bin_start[];
extern const char _binary_font8x16_bin_end[];

// Returns a pointer to the 16-byte scanline bitmap for the given CP437 glyph.
const uint8_t *font_get_glyph(uint8_t glyph);

// Decodes a UTF-8 character byte stream into a single CP437 glyph index.
// Advances the stream pointer by the number of consumed bytes.
uint8_t utf8_decode_to_cp437(const uint8_t **stream);
