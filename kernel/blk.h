#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SECTOR_SIZE 512
#define MAX_BLK_DEVS 4

struct blk_ops {
  const char *name;
  bool (*read)(void *buf, uint32_t sector);
  bool (*write)(const void *buf, uint32_t sector);
  uint32_t (*get_sectors)(void);
};

void blk_init(void);
bool blk_register_device(uint32_t dev_id, const struct blk_ops *ops);

void read_device(uint32_t dev, void *buf, uint32_t sector);
void write_device(uint32_t dev, void *buf, uint32_t sector);
uint32_t blk_get_sectors(uint32_t dev);
