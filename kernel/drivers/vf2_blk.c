#include "blk.h"
#include "dw_mmc.h"
#include "initrd.h"
#include "kernel.h"

// Standard VisionFive 2 GPT Partition 4 offset (72 MB / LBA 147456)
#define VF2_HOME_PART_START 147456ULL
#define VF2_HOME_PART_SECTORS (8 * 1024 * 1024 / SECTOR_SIZE)

void blk_init(void) {
  // Device 0: In-memory Initrd root
  initrd_init();

  // Device 1: Physical SD card persistent /home partition
  if (dw_mmc_init()) {
    dw_mmc_attach_partition(1, "sd_home", VF2_HOME_PART_START,
                            VF2_HOME_PART_SECTORS);
  } else {
    kprintf("vf2_blk: SD card offline, running with read-only root\n");
  }
}
