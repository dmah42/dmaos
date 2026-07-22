#pragma once

#include "stdlib.h"

#define SECTOR_SIZE 512
#define VIRTIO_BLK_PADDR 0x10001000

void virtio_blk_init();
void read_write_device(void *buf, uint32_t sector, bool is_write);
uint32_t virtio_blk_sectors(void);