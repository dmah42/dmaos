#include "dw_mmc.h"

#include "blk.h"
#include "kernel.h"
#include "stdlib.h"

// -----------------------------------------------------------------------------
// MMIO Accessors
// -----------------------------------------------------------------------------
#define mmio_read32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define mmio_write32(addr, val)                                                \
  (*(volatile uint32_t *)(uintptr_t)(addr) = (uint32_t)(val))

// -----------------------------------------------------------------------------
// Synopsys DesignWare Mobile Storage (DW-MMC) Register Map
// -----------------------------------------------------------------------------
#define DWMCI_CTRL (DW_MMC_BASE + 0x00)
#define DWMCI_PWREN (DW_MMC_BASE + 0x04)
#define DWMCI_CLKDIV (DW_MMC_BASE + 0x08)
#define DWMCI_CLKSRC (DW_MMC_BASE + 0x0C)
#define DWMCI_CLKENA (DW_MMC_BASE + 0x10)
#define DWMCI_TMOUT (DW_MMC_BASE + 0x14)
#define DWMCI_CTYPE (DW_MMC_BASE + 0x18)
#define DWMCI_BLKSIZ (DW_MMC_BASE + 0x1C)
#define DWMCI_BYTCNT (DW_MMC_BASE + 0x20)
#define DWMCI_INTMASK (DW_MMC_BASE + 0x24)
#define DWMCI_CMDARG (DW_MMC_BASE + 0x28)
#define DWMCI_CMD (DW_MMC_BASE + 0x2C)
#define DWMCI_RESP0 (DW_MMC_BASE + 0x30)
#define DWMCI_RESP1 (DW_MMC_BASE + 0x34)
#define DWMCI_RESP2 (DW_MMC_BASE + 0x38)
#define DWMCI_RESP3 (DW_MMC_BASE + 0x3C)
#define DWMCI_MINTSTS (DW_MMC_BASE + 0x40)
#define DWMCI_RINTSTS (DW_MMC_BASE + 0x44)
#define DWMCI_STATUS (DW_MMC_BASE + 0x48)
#define DWMCI_FIFOTH (DW_MMC_BASE + 0x4C)
#define DWMCI_DATA (DW_MMC_BASE + 0x100)

// CTRL Register Flags
#define DWMCI_CTRL_RESET (1 << 0)
#define DWMCI_CTRL_FIFO_RESET (1 << 1)
#define DWMCI_CTRL_DMA_RESET (1 << 2)

// CMD Register Flags
#define DWMCI_CMD_START (1U << 31)
#define DWMCI_CMD_UPDATE_CLK (1 << 21)
#define DWMCI_CMD_SEND_INIT (1 << 15)
#define DWMCI_CMD_STOP_ABORT (1 << 14)
#define DWMCI_CMD_WRITE (1 << 10)
#define DWMCI_CMD_DATA_EXP (1 << 9)
#define DWMCI_CMD_CHECK_CRC (1 << 8)
#define DWMCI_CMD_RESP_LEN (1 << 7)
#define DWMCI_CMD_RESP_EXP (1 << 6)

// Interrupt Status Flags
#define DWMCI_INT_RE (1 << 1)
#define DWMCI_INT_CDONE (1 << 2)
#define DWMCI_INT_DTO (1 << 3)
#define DWMCI_INT_TXDR (1 << 4)
#define DWMCI_INT_RXDR (1 << 5)
#define DWMCI_INT_RTO (1 << 8)
#define DWMCI_INT_DCRC (1 << 9)

// STATUS Register Flags
#define DWMCI_STATUS_FIFO_EMPTY (1 << 2)
#define DWMCI_STATUS_FIFO_FULL (1 << 3)

// Standard SD Commands
#define SD_CMD0_GO_IDLE_STATE 0
#define SD_CMD2_ALL_SEND_CID 2
#define SD_CMD3_SEND_RELATIVE_ADDR 3
#define SD_CMD7_SELECT_CARD 7
#define SD_CMD8_SEND_IF_COND 8
#define SD_CMD9_SEND_CSD 9
#define SD_CMD16_SET_BLOCKLEN 16
#define SD_CMD17_READ_SINGLE_BLOCK 17
#define SD_CMD24_WRITE_SINGLE_BLOCK 24
#define SD_CMD55_APP_CMD 55
#define SD_ACMD6_SET_BUS_WIDTH 6
#define SD_ACMD41_SD_SEND_OP_COND 41

// Driver State
static uint32_t g_card_rca = 0;
static bool g_is_sdhc = false;
static bool g_mmc_initialized = false;
static uint32_t g_total_sectors = 0;

// Attached partition state
static uint64_t g_part_offset_sectors = 0;
static uint32_t g_part_num_sectors = 0;

// Hardware timer based microsecond delay using RISC-V timer
static void dw_delay_us(uint32_t us) {
  // 1 microsecond = TIMER_FREQ / 1,000,000 cycles (~10 cycles at 10 MHz clock)
  uint64_t start = uptime();
  uint64_t ticks = (uint64_t)us * 10;
  while (uptime() - start < ticks) {
    __asm__ __volatile__("nop");
  }
}

// Update clock state in DW-MMC
static bool dw_update_clock(void) {
  mmio_write32(DWMCI_CMD,
               DWMCI_CMD_START | DWMCI_CMD_UPDATE_CLK | DWMCI_CMD_STOP_ABORT);
  for (int i = 0; i < 100000; i++) {
    if ((mmio_read32(DWMCI_CMD) & DWMCI_CMD_START) == 0) {
      return true;
    }
    dw_delay_us(10);
  }
  return false;
}

// Set controller clock divider
static bool dw_set_clock(uint32_t div) {
  mmio_write32(DWMCI_CLKENA, 0);
  dw_update_clock();

  mmio_write32(DWMCI_CLKDIV, div);
  dw_update_clock();

  mmio_write32(DWMCI_CLKENA, 1 | (1 << 16));
  return dw_update_clock();
}

// Send SD command and poll for completion
static bool dw_send_cmd(uint32_t cmd_idx, uint32_t arg, uint32_t flags,
                        uint32_t *resp) {
  mmio_write32(DWMCI_RINTSTS, 0xFFFFFFFF);
  mmio_write32(DWMCI_CMDARG, arg);
  mmio_write32(DWMCI_CMD, DWMCI_CMD_START | (cmd_idx & 0x3F) | flags);

  uint32_t status = 0;
  for (int i = 0; i < 500000; i++) {
    status = mmio_read32(DWMCI_RINTSTS);
    if (status & (DWMCI_INT_CDONE | DWMCI_INT_RTO | DWMCI_INT_RE)) {
      break;
    }
    dw_delay_us(10);
  }

  if (!(status & DWMCI_INT_CDONE) || (status & DWMCI_INT_RTO)) {
    return false;
  }

  if (resp) {
    resp[0] = mmio_read32(DWMCI_RESP0);
    if (flags & DWMCI_CMD_RESP_LEN) {
      resp[1] = mmio_read32(DWMCI_RESP1);
      resp[2] = mmio_read32(DWMCI_RESP2);
      resp[3] = mmio_read32(DWMCI_RESP3);
    }
  }

  return true;
}

// Send Application-Specific Command (CMD55 prefix)
static bool dw_send_acmd(uint32_t acmd_idx, uint32_t arg, uint32_t flags,
                         uint32_t *resp) {
  uint32_t cmd55_resp = 0;
  if (!dw_send_cmd(SD_CMD55_APP_CMD, g_card_rca << 16,
                   DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, &cmd55_resp)) {
    return false;
  }
  return dw_send_cmd(acmd_idx, arg, flags, resp);
}

// Parse CSD register to calculate total sectors
static uint32_t dw_parse_csd_sectors(const uint32_t csd[4]) {
  // CSD structure version is in bits [127:126] (stored in csd[3] top bits)
  uint32_t csd_ver = (csd[3] >> 30) & 0x3;
  if (csd_ver == 1) { // CSD Version 2.0 (SDHC / SDXC)
    // C_SIZE is in bits [69:48]
    uint32_t c_size = ((csd[1] >> 16) & 0xFFFF) | ((csd[2] & 0x3F) << 16);
    // Capacity = (C_SIZE + 1) * 512KB = (C_SIZE + 1) * 1024 sectors
    return (c_size + 1) * 1024;
  } else { // CSD Version 1.0 (SDSC)
    uint32_t read_bl_len = (csd[2] >> 16) & 0xF;
    uint32_t c_size = ((csd[1] >> 30) & 0x3) | ((csd[2] & 0x3FF) << 2);
    uint32_t c_size_mult = (csd[1] >> 15) & 0x7;
    uint32_t mult = 1 << (c_size_mult + 2);
    uint32_t blocknr = (c_size + 1) * mult;
    uint32_t block_len = 1 << read_bl_len;
    return (uint32_t)(((uint64_t)blocknr * block_len) / SECTOR_SIZE);
  }
}

// Initialize SD card hardware
bool dw_mmc_init(void) {
  kprintf(
      "dw_mmc: initializing Synopsys DesignWare MMC controller at 0x%lx...\n",
      (uintptr_t)DW_MMC_BASE);

  // Reset controller, FIFO, and DMA
  mmio_write32(DWMCI_CTRL,
               DWMCI_CTRL_RESET | DWMCI_CTRL_FIFO_RESET | DWMCI_CTRL_DMA_RESET);
  for (int i = 0; i < 1000; i++) {
    if ((mmio_read32(DWMCI_CTRL) & (DWMCI_CTRL_RESET | DWMCI_CTRL_FIFO_RESET |
                                    DWMCI_CTRL_DMA_RESET)) == 0) {
      break;
    }
    dw_delay_us(10);
  }

  mmio_write32(DWMCI_PWREN, 1);
  dw_delay_us(1000);

  // Initial identification clock (~400 kHz)
  dw_set_clock(0x7F);
  mmio_write32(DWMCI_TMOUT, 0xFFFFFF40);
  mmio_write32(DWMCI_CTYPE, 0);

  // Send 74 clock cycles initialization sequence
  dw_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, DWMCI_CMD_SEND_INIT, NULL);
  dw_delay_us(2000);

  if (!dw_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, 0, NULL)) {
    kprintf("dw_mmc: CMD0 failed (no SD card responding)\n");
    return false;
  }
  dw_delay_us(1000);

  uint32_t resp8 = 0;
  bool v2_card = false;
  if (dw_send_cmd(SD_CMD8_SEND_IF_COND, 0x1AA,
                  DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, &resp8)) {
    if ((resp8 & 0xFF) == 0xAA) {
      v2_card = true;
    }
  }

  uint32_t ocr_arg = 0x00FF8000;
  if (v2_card) {
    ocr_arg |= (1U << 30);
  }

  uint32_t ocr_resp = 0;
  bool card_ready = false;
  for (int retry = 0; retry < 200; retry++) {
    if (dw_send_acmd(SD_ACMD41_SD_SEND_OP_COND, ocr_arg, DWMCI_CMD_RESP_EXP,
                     &ocr_resp)) {
      if (ocr_resp & (1U << 31)) {
        card_ready = true;
        g_is_sdhc = (ocr_resp & (1U << 30)) != 0;
        break;
      }
    }
    dw_delay_us(10000);
  }

  if (!card_ready) {
    kprintf("dw_mmc: ACMD41 timeout (card not ready)\n");
    return false;
  }

  uint32_t cid[4] = {0};
  if (!dw_send_cmd(
          SD_CMD2_ALL_SEND_CID, 0,
          DWMCI_CMD_RESP_EXP | DWMCI_CMD_RESP_LEN | DWMCI_CMD_CHECK_CRC, cid)) {
    return false;
  }

  uint32_t rca_resp = 0;
  if (!dw_send_cmd(SD_CMD3_SEND_RELATIVE_ADDR, 0,
                   DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, &rca_resp)) {
    return false;
  }
  g_card_rca = (rca_resp >> 16) & 0xFFFF;

  // CMD9: Read CSD to determine card capacity
  uint32_t csd[4] = {0};
  if (dw_send_cmd(SD_CMD9_SEND_CSD, g_card_rca << 16,
                  DWMCI_CMD_RESP_EXP | DWMCI_CMD_RESP_LEN | DWMCI_CMD_CHECK_CRC,
                  csd)) {
    g_total_sectors = dw_parse_csd_sectors(csd);
  }

  // CMD7: Select Card
  uint32_t resp7 = 0;
  if (!dw_send_cmd(SD_CMD7_SELECT_CARD, g_card_rca << 16,
                   DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, &resp7)) {
    return false;
  }

  dw_send_cmd(SD_CMD16_SET_BLOCKLEN, SECTOR_SIZE,
              DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, NULL);

  if (dw_send_acmd(SD_ACMD6_SET_BUS_WIDTH, 2,
                   DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, NULL)) {
    mmio_write32(DWMCI_CTYPE, 1); // 4-bit bus mode
  }

  dw_set_clock(0); // 50 MHz full speed
  g_mmc_initialized = true;

  kprintf("dw_mmc: SD card online (%s, RCA=0x%x, %u MB)\n",
          g_is_sdhc ? "SDHC/SDXC" : "SDSC", g_card_rca,
          (g_total_sectors * SECTOR_SIZE) / (1024 * 1024));
  return true;
}

bool dw_mmc_read_sector(uint64_t sector, void *buf) {
  if (!g_mmc_initialized || !buf) {
    return false;
  }

  uint32_t arg =
      g_is_sdhc ? (uint32_t)sector : (uint32_t)(sector * SECTOR_SIZE);

  mmio_write32(DWMCI_BLKSIZ, SECTOR_SIZE);
  mmio_write32(DWMCI_BYTCNT, SECTOR_SIZE);

  uint32_t flags =
      DWMCI_CMD_DATA_EXP | DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC;

  if (!dw_send_cmd(SD_CMD17_READ_SINGLE_BLOCK, arg, flags, NULL)) {
    return false;
  }

  uint32_t *dest = (uint32_t *)buf;
  for (int words = 0; words < SECTOR_SIZE / 4;) {
    if (!(mmio_read32(DWMCI_STATUS) & DWMCI_STATUS_FIFO_EMPTY)) {
      dest[words++] = mmio_read32(DWMCI_DATA);
    }
  }

  for (int i = 0; i < 500000; i++) {
    if (mmio_read32(DWMCI_RINTSTS) & DWMCI_INT_DTO) {
      break;
    }
    dw_delay_us(5);
  }

  return true;
}

bool dw_mmc_write_sector(uint64_t sector, const void *buf) {
  if (!g_mmc_initialized || !buf) {
    return false;
  }

  uint32_t arg =
      g_is_sdhc ? (uint32_t)sector : (uint32_t)(sector * SECTOR_SIZE);

  mmio_write32(DWMCI_BLKSIZ, SECTOR_SIZE);
  mmio_write32(DWMCI_BYTCNT, SECTOR_SIZE);

  uint32_t flags = DWMCI_CMD_DATA_EXP | DWMCI_CMD_WRITE | DWMCI_CMD_RESP_EXP |
                   DWMCI_CMD_CHECK_CRC;

  if (!dw_send_cmd(SD_CMD24_WRITE_SINGLE_BLOCK, arg, flags, NULL)) {
    return false;
  }

  const uint32_t *src = (const uint32_t *)buf;
  for (int words = 0; words < SECTOR_SIZE / 4;) {
    if (!(mmio_read32(DWMCI_STATUS) & DWMCI_STATUS_FIFO_FULL)) {
      mmio_write32(DWMCI_DATA, src[words++]);
    }
  }

  for (int i = 0; i < 500000; i++) {
    if (mmio_read32(DWMCI_RINTSTS) & DWMCI_INT_DTO) {
      break;
    }
    dw_delay_us(5);
  }

  return true;
}

uint32_t dw_mmc_get_total_sectors(void) { return g_total_sectors; }

// -----------------------------------------------------------------------------
// Block Device Partition Adapter
// -----------------------------------------------------------------------------
static bool dw_part_read(void *buf, uint32_t sector) {
  if (sector >= g_part_num_sectors) {
    return false;
  }
  return dw_mmc_read_sector(g_part_offset_sectors + sector, buf);
}

static bool dw_part_write(const void *buf, uint32_t sector) {
  if (sector >= g_part_num_sectors) {
    return false;
  }
  return dw_mmc_write_sector(g_part_offset_sectors + sector, buf);
}

static uint32_t dw_part_get_sectors(void) { return g_part_num_sectors; }

static struct blk_ops g_dw_part_ops = {
    .read = dw_part_read,
    .write = dw_part_write,
    .get_sectors = dw_part_get_sectors,
};

bool dw_mmc_attach_partition(uint32_t dev_id, const char *name,
                             uint64_t start_sector, uint32_t num_sectors) {
  g_dw_part_ops.name = name;
  g_part_offset_sectors = start_sector;
  g_part_num_sectors = num_sectors;
  return blk_register_device(dev_id, &g_dw_part_ops);
}
