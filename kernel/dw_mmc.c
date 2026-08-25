#include "dw_mmc.h"

#include "kernel.h"
#include "stdlib.h"

// -----------------------------------------------------------------------------
// Synopsys DesignWare Mobile Storage (DW-MMC) Register Offsets
// -----------------------------------------------------------------------------
#define DWMCI_CTRL (0x00)
#define DWMCI_PWREN (0x04)
#define DWMCI_CLKDIV (0x08)
#define DWMCI_CLKSRC (0x0C)
#define DWMCI_CLKENA (0x10)
#define DWMCI_TMOUT (0x14)
#define DWMCI_CTYPE (0x18)
#define DWMCI_BLKSIZ (0x1C)
#define DWMCI_BYTCNT (0x20)
#define DWMCI_INTMASK (0x24)
#define DWMCI_CMDARG (0x28)
#define DWMCI_CMD (0x2C)
#define DWMCI_RESP0 (0x30)
#define DWMCI_RESP1 (0x34)
#define DWMCI_RESP2 (0x38)
#define DWMCI_RESP3 (0x3C)
#define DWMCI_MINTSTS (0x40)
#define DWMCI_RINTSTS (0x44)
#define DWMCI_STATUS (0x48)
#define DWMCI_FIFOTH (0x4C)
#define DWMCI_DATA (0x100)

// CTRL Register Bits
#define DWMCI_CTRL_RESET (1 << 0)
#define DWMCI_CTRL_FIFO_RESET (1 << 1)
#define DWMCI_CTRL_DMA_RESET (1 << 2)
#define DWMCI_CTRL_INT_ENABLE (1 << 4)
#define DWMCI_CTRL_DMA_ENABLE (1 << 5)

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

// RINTSTS / MINTSTS Register Flags
#define DWMCI_INT_CD (1 << 0)    // Card detect
#define DWMCI_INT_RE (1 << 1)    // Response error
#define DWMCI_INT_CDONE (1 << 2) // Command done
#define DWMCI_INT_DTO (1 << 3)   // Data transfer over
#define DWMCI_INT_TXDR (1 << 4)  // Transmit FIFO data request
#define DWMCI_INT_RXDR (1 << 5)  // Receive FIFO data request
#define DWMCI_INT_RTO (1 << 8)   // Response timeout
#define DWMCI_INT_DCRC (1 << 9)  // Data CRC error
#define DWMCI_INT_RCRC (1 << 6)  // Response CRC error
#define DWMCI_INT_HTO (1 << 10)  // Data starvation timeout
#define DWMCI_INT_DRTO (1 << 9)  // Data read timeout

// STATUS Register Flags
#define DWMCI_STATUS_FIFO_EMPTY (1 << 2)
#define DWMCI_STATUS_FIFO_FULL (1 << 3)
#define DWMCI_STATUS_DATA_BUSY (1 << 9)

// Standard SD Commands
#define SD_CMD0_GO_IDLE_STATE 0
#define SD_CMD2_ALL_SEND_CID 2
#define SD_CMD3_SEND_RELATIVE_ADDR 3
#define SD_CMD6_SWITCH_FUNC 6
#define SD_CMD7_SELECT_CARD 7
#define SD_CMD8_SEND_IF_COND 8
#define SD_CMD9_SEND_CSD 9
#define SD_CMD13_SEND_STATUS 13
#define SD_CMD16_SET_BLOCKLEN 16
#define SD_CMD17_READ_SINGLE_BLOCK 17
#define SD_CMD24_WRITE_SINGLE_BLOCK 24
#define SD_CMD55_APP_CMD 55
#define SD_ACMD6_SET_BUS_WIDTH 6
#define SD_ACMD41_SD_SEND_OP_COND 41

// Driver State
static volatile uint32_t *const dw_regs = (volatile uint32_t *)DW_MMC_BASE;
static uint32_t card_rca = 0;
static bool is_sdhc = false;
static bool mmc_initialized = false;

static inline uint32_t dw_read32(uint32_t offset) {
  return dw_regs[offset / 4];
}

static inline void dw_write32(uint32_t offset, uint32_t val) {
  dw_regs[offset / 4] = val;
}

static void dw_delay_us(uint32_t us) {
  uint32_t count = us * 50;
  for (volatile uint32_t i = 0; i < count; ++i) {
    __asm__ __volatile__("nop");
  }
}

// Update the DW-MMC clock register state
static bool dw_update_clock(void) {
  dw_write32(DWMCI_CMD,
             DWMCI_CMD_START | DWMCI_CMD_UPDATE_CLK | DWMCI_CMD_STOP_ABORT);
  for (int i = 0; i < 100000; i++) {
    if ((dw_read32(DWMCI_CMD) & DWMCI_CMD_START) == 0) {
      return true;
    }
    dw_delay_us(10);
  }
  return false;
}

// Set DW-MMC clock divider
static bool dw_set_clock(uint32_t div) {
  // Disable clock before changing frequency
  dw_write32(DWMCI_CLKENA, 0);
  dw_update_clock();

  dw_write32(DWMCI_CLKDIV, div);
  dw_update_clock();

  // Enable clock
  dw_write32(DWMCI_CLKENA, 1 | (1 << 16)); // Enable clock and low power mode
  return dw_update_clock();
}

// Send SD command and poll for completion
static bool dw_send_cmd(uint32_t cmd_idx, uint32_t arg, uint32_t flags,
                        uint32_t *resp) {
  // Clear interrupt status flags
  dw_write32(DWMCI_RINTSTS, 0xFFFFFFFF);

  dw_write32(DWMCI_CMDARG, arg);
  dw_write32(DWMCI_CMD, DWMCI_CMD_START | (cmd_idx & 0x3F) | flags);

  // Poll for command done or error
  uint32_t status = 0;
  for (int i = 0; i < 500000; i++) {
    status = dw_read32(DWMCI_RINTSTS);
    if (status & (DWMCI_INT_CDONE | DWMCI_INT_RTO | DWMCI_INT_RE)) {
      break;
    }
    dw_delay_us(10);
  }

  if (!(status & DWMCI_INT_CDONE) || (status & DWMCI_INT_RTO)) {
    return false;
  }

  if (resp) {
    resp[0] = dw_read32(DWMCI_RESP0);
    if (flags & DWMCI_CMD_RESP_LEN) { // 136-bit long response
      resp[1] = dw_read32(DWMCI_RESP1);
      resp[2] = dw_read32(DWMCI_RESP2);
      resp[3] = dw_read32(DWMCI_RESP3);
    }
  }

  return true;
}

// Send Application-Specific Command (CMD55 prefix)
static bool dw_send_acmd(uint32_t acmd_idx, uint32_t arg, uint32_t flags,
                         uint32_t *resp) {
  uint32_t cmd55_resp = 0;
  if (!dw_send_cmd(SD_CMD55_APP_CMD, card_rca << 16,
                   DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, &cmd55_resp)) {
    return false;
  }
  return dw_send_cmd(acmd_idx, arg, flags, resp);
}

// Initialize SD card hardware
bool dw_mmc_init(void) {
  kprintf(
      "dw_mmc: initializing Synopsys DesignWare MMC controller at 0x%lx...\n",
      (uintptr_t)DW_MMC_BASE);

  // Reset controller, FIFO, and DMA
  dw_write32(DWMCI_CTRL,
             DWMCI_CTRL_RESET | DWMCI_CTRL_FIFO_RESET | DWMCI_CTRL_DMA_RESET);
  for (int i = 0; i < 1000; i++) {
    if ((dw_read32(DWMCI_CTRL) & (DWMCI_CTRL_RESET | DWMCI_CTRL_FIFO_RESET |
                                  DWMCI_CTRL_DMA_RESET)) == 0) {
      break;
    }
    dw_delay_us(10);
  }

  // Turn on power to SD bus
  dw_write32(DWMCI_PWREN, 1);
  dw_delay_us(1000);

  // Set initial identification clock (~400 kHz)
  dw_set_clock(0x7F);

  // Set timeouts (data timeout: 0xFFFFFF, response timeout: 0xFF)
  dw_write32(DWMCI_TMOUT, 0xFFFFFF40);

  // Reset 1-bit bus width
  dw_write32(DWMCI_CTYPE, 0);

  // Send 74 clock cycles initialization sequence
  dw_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, DWMCI_CMD_SEND_INIT, NULL);
  dw_delay_us(2000);

  // CMD0: Reset card into Idle state
  if (!dw_send_cmd(SD_CMD0_GO_IDLE_STATE, 0, 0, NULL)) {
    kprintf("dw_mmc: CMD0 failed (no SD card responding)\n");
    return false;
  }
  dw_delay_us(1000);

  // CMD8: Send Interface Condition (3.3V, check pattern 0xAA)
  uint32_t resp8 = 0;
  bool v2_card = false;
  if (dw_send_cmd(SD_CMD8_SEND_IF_COND, 0x1AA,
                  DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, &resp8)) {
    if ((resp8 & 0xFF) == 0xAA) {
      v2_card = true;
    }
  }

  // ACMD41: Initialize card and wait for ready
  uint32_t ocr_arg = 0x00FF8000; // 3.3V window
  if (v2_card) {
    ocr_arg |= (1U << 30); // HCS (High Capacity Support)
  }

  uint32_t ocr_resp = 0;
  bool card_ready = false;
  for (int retry = 0; retry < 200; retry++) {
    if (dw_send_acmd(SD_ACMD41_SD_SEND_OP_COND, ocr_arg, DWMCI_CMD_RESP_EXP,
                     &ocr_resp)) {
      if (ocr_resp & (1U << 31)) { // Card powered up (ready)
        card_ready = true;
        is_sdhc = (ocr_resp & (1U << 30)) != 0;
        break;
      }
    }
    dw_delay_us(10000);
  }

  if (!card_ready) {
    kprintf("dw_mmc: ACMD41 timeout (card not ready)\n");
    return false;
  }

  // CMD2: Read CID (Card Identification)
  uint32_t cid[4] = {0};
  if (!dw_send_cmd(
          SD_CMD2_ALL_SEND_CID, 0,
          DWMCI_CMD_RESP_EXP | DWMCI_CMD_RESP_LEN | DWMCI_CMD_CHECK_CRC, cid)) {
    kprintf("dw_mmc: CMD2 (read CID) failed\n");
    return false;
  }

  // CMD3: Read RCA (Relative Card Address)
  uint32_t rca_resp = 0;
  if (!dw_send_cmd(SD_CMD3_SEND_RELATIVE_ADDR, 0,
                   DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, &rca_resp)) {
    kprintf("dw_mmc: CMD3 (get RCA) failed\n");
    return false;
  }
  card_rca = (rca_resp >> 16) & 0xFFFF;

  // CMD7: Select Card (transition to Transfer State)
  uint32_t resp7 = 0;
  if (!dw_send_cmd(SD_CMD7_SELECT_CARD, card_rca << 16,
                   DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, &resp7)) {
    kprintf("dw_mmc: CMD7 (select card) failed\n");
    return false;
  }

  // Set block size to 512 bytes
  dw_send_cmd(SD_CMD16_SET_BLOCKLEN, DW_MMC_SECTOR_SIZE,
              DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, NULL);

  // Switch to 4-bit bus mode (ACMD6 with arg=2)
  if (dw_send_acmd(SD_ACMD6_SET_BUS_WIDTH, 2,
                   DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC, NULL)) {
    dw_write32(DWMCI_CTYPE, 1); // 4-bit mode in controller
  }

  // Switch to high speed clock (50 MHz)
  dw_set_clock(0); // 0 divider = maximum bus speed

  mmc_initialized = true;
  kprintf("dw_mmc: SD card initialized (%s, RCA=0x%x, 4-bit 50MHz)\n",
          is_sdhc ? "SDHC/SDXC" : "SDSC", card_rca);
  return true;
}

// Read single 512-byte sector from SD card
bool dw_mmc_read_sector(uint64_t sector, void *buf) {
  if (!mmc_initialized || !buf) {
    return false;
  }

  uint32_t arg =
      is_sdhc ? (uint32_t)sector : (uint32_t)(sector * DW_MMC_SECTOR_SIZE);

  dw_write32(DWMCI_BLKSIZ, DW_MMC_SECTOR_SIZE);
  dw_write32(DWMCI_BYTCNT, DW_MMC_SECTOR_SIZE);

  uint32_t flags =
      DWMCI_CMD_DATA_EXP | DWMCI_CMD_RESP_EXP | DWMCI_CMD_CHECK_CRC;

  if (!dw_send_cmd(SD_CMD17_READ_SINGLE_BLOCK, arg, flags, NULL)) {
    return false;
  }

  uint32_t *dest = (uint32_t *)buf;
  for (int words = 0; words < DW_MMC_SECTOR_SIZE / 4;) {
    if (!(dw_read32(DWMCI_STATUS) & DWMCI_STATUS_FIFO_EMPTY)) {
      dest[words++] = dw_read32(DWMCI_DATA);
    }
  }

  // Wait for data transfer over
  for (int i = 0; i < 500000; i++) {
    if (dw_read32(DWMCI_RINTSTS) & DWMCI_INT_DTO) {
      break;
    }
    dw_delay_us(5);
  }

  return true;
}

// Write single 512-byte sector to SD card
bool dw_mmc_write_sector(uint64_t sector, const void *buf) {
  if (!mmc_initialized || !buf) {
    return false;
  }

  uint32_t arg =
      is_sdhc ? (uint32_t)sector : (uint32_t)(sector * DW_MMC_SECTOR_SIZE);

  dw_write32(DWMCI_BLKSIZ, DW_MMC_SECTOR_SIZE);
  dw_write32(DWMCI_BYTCNT, DW_MMC_SECTOR_SIZE);

  uint32_t flags = DWMCI_CMD_DATA_EXP | DWMCI_CMD_WRITE | DWMCI_CMD_RESP_EXP |
                   DWMCI_CMD_CHECK_CRC;

  if (!dw_send_cmd(SD_CMD24_WRITE_SINGLE_BLOCK, arg, flags, NULL)) {
    return false;
  }

  const uint32_t *src = (const uint32_t *)buf;
  for (int words = 0; words < DW_MMC_SECTOR_SIZE / 4;) {
    if (!(dw_read32(DWMCI_STATUS) & DWMCI_STATUS_FIFO_FULL)) {
      dw_write32(DWMCI_DATA, src[words++]);
    }
  }

  // Wait for data transfer over
  for (int i = 0; i < 500000; i++) {
    if (dw_read32(DWMCI_RINTSTS) & DWMCI_INT_DTO) {
      break;
    }
    dw_delay_us(5);
  }

  return true;
}

uint32_t dw_mmc_get_sectors(void) {
  // Default 8MB partition size (16384 sectors) if not querying CSD
  return 16384;
}
