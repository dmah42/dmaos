#pragma once

#include "stdlib.h"

#define SECTOR_SIZE 512
#define VIRTIO_BLK0_PADDR 0x10001000
#define VIRTIO_BLK1_PADDR 0x10002000

void virtio_blk_init();
void read_device(uint32_t dev, void *buf, uint32_t sector);
void write_device(uint32_t dev, void *buf, uint32_t sector);
uint32_t virtio_blk_sectors(uint32_t dev);