#pragma once

#include <stdint.h>

void simplefb_init(void);
void simplefb_setup(uint32_t *base, uint32_t width, uint32_t height,
                    uint32_t stride);
