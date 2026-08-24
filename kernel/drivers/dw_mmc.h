#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DW_MMC_BASE 0x16010000ULL

// Initialize DW-MMC hardware controller
bool dw_mmc_init(void);

// Raw LBA sector read/write (sector 0 to card_sectors - 1)
bool dw_mmc_read_sector(uint64_t sector, void *buf);
bool dw_mmc_write_sector(uint64_t sector, const void *buf);
uint32_t dw_mmc_get_total_sectors(void);

// Attach a partition as a registered block device in the kernel block subsystem
bool dw_mmc_attach_partition(uint32_t dev_id, const char *name,
                             uint64_t start_sector, uint32_t num_sectors);
