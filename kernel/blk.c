#include "blk.h"

#include "string.h"

static struct blk_ops g_blk_devs[MAX_BLK_DEVS];
static bool g_blk_dev_active[MAX_BLK_DEVS];

bool blk_register_device(uint32_t dev_id, const struct blk_ops *ops) {
  if (dev_id >= MAX_BLK_DEVS || !ops) {
    return false;
  }
  g_blk_devs[dev_id] = *ops;
  g_blk_dev_active[dev_id] = true;
  return true;
}

void read_device(uint32_t dev, void *buf, uint32_t sector) {
  if (dev < MAX_BLK_DEVS && g_blk_dev_active[dev] && g_blk_devs[dev].read) {
    if (g_blk_devs[dev].read(buf, sector)) {
      return;
    }
  }
  memset(buf, 0, SECTOR_SIZE);
}

void write_device(uint32_t dev, void *buf, uint32_t sector) {
  if (dev < MAX_BLK_DEVS && g_blk_dev_active[dev] && g_blk_devs[dev].write) {
    g_blk_devs[dev].write(buf, sector);
  }
}

uint32_t blk_get_sectors(uint32_t dev) {
  if (dev < MAX_BLK_DEVS && g_blk_dev_active[dev] &&
      g_blk_devs[dev].get_sectors) {
    return g_blk_devs[dev].get_sectors();
  }
  return 0;
}
