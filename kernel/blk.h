#pragma once

#include <stdint.h>

#define SECTOR_SIZE 512

void blk_init(void);
void read_device(uint32_t dev, void *buf, uint32_t sector);
void write_device(uint32_t dev, void *buf, uint32_t sector);
uint32_t blk_get_sectors(uint32_t dev);
