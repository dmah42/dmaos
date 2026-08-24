#include "initrd.h"

#include "blk.h"
#include "kernel.h"
#include "string.h"

#define INITRD_SECTORS (INITRD_SIZE / SECTOR_SIZE)

static bool initrd_read_sector(void *buf, uint32_t sector) {
  if (!buf || sector >= INITRD_SECTORS) {
    return false;
  }
  memcpy(buf, (const void *)(INITRD_PADDR + (uint64_t)sector * SECTOR_SIZE),
         SECTOR_SIZE);
  return true;
}

static bool initrd_write_sector(const void *buf, uint32_t sector) {
  (void)buf;
  (void)sector;
  // Initrd is immutable read-only storage
  return false;
}

static uint32_t initrd_get_sectors(void) {
  return INITRD_SECTORS;
}

static const struct blk_ops g_initrd_blk_ops = {
    .name = "initrd",
    .read = initrd_read_sector,
    .write = initrd_write_sector,
    .get_sectors = initrd_get_sectors,
};

bool initrd_init(void) {
  kprintf("initrd: mounting in-memory root filesystem at 0x%lx (%d KB)\n",
          (uintptr_t)INITRD_PADDR, (int)(INITRD_SIZE / 1024));
  return blk_register_device(0, &g_initrd_blk_ops);
}
