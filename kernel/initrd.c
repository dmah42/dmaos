#include "blk.h"
#include "kernel.h"
#include "string.h"

#define INITRD_PADDR 0x46000000ULL
#define INITRD_SECTORS (8 * 1024 * 1024 / SECTOR_SIZE)

void blk_init(void) {
  kprintf("initrd: in-memory root filesystem at 0x%lx (%d KB)\n",
          (uintptr_t)INITRD_PADDR, (int)(INITRD_SECTORS * SECTOR_SIZE / 1024));
}

void read_device(uint32_t dev, void *buf, uint32_t sector) {
  if (dev == 0) {
    if (sector >= INITRD_SECTORS) {
      memset(buf, 0, SECTOR_SIZE);
      return;
    }
    memcpy(buf, (const void *)(INITRD_PADDR + (uint64_t)sector * SECTOR_SIZE),
           SECTOR_SIZE);
    return;
  }
  // Device 1: Placeholder for DW-MMC physical SD card driver (Phase 4)
  memset(buf, 0, SECTOR_SIZE);
}

void write_device(uint32_t dev, void *buf, uint32_t sector) {
  (void)buf;
  (void)sector;
  if (dev == 0) {
    // Device 0 is read-only in-memory Initrd
    return;
  }
  // Device 1: Placeholder for DW-MMC physical SD card driver (Phase 4)
}

uint32_t blk_get_sectors(uint32_t dev) {
  if (dev == 0) {
    return INITRD_SECTORS;
  }
  return 0;
}
