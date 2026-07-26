#include "ramfb.h"

#include "kernel.h"
#include "stdlib.h"

/* RISC-V Virt Board MMIO Addresses */
#define FW_CFG_BASE 0x10100000ULL
#define FW_CFG_SELECTOR ((volatile uint16_t *)(FW_CFG_BASE + 0x08))
#define FW_CFG_DATA ((volatile uint64_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_DMA ((volatile uint64_t *)(FW_CFG_BASE + 0x10))

/* Selectors */
#define FW_CFG_SIGNATURE 0x0000
#define FW_CFG_FILE_DIR 0x0019

/* DMA Control Flags */
#define QEMU_CFG_DMA_CTL_SELECT 0x08
#define QEMU_CFG_DMA_CTL_WRITE 0x10

/* DRM FOURCC format for 32-bit XRGB (0x34325258 -> "XR24") */
#define DRM_FORMAT_XRGB8888 0x34325258

/* Packed structures matching QEMU specs */
typedef struct __attribute__((packed)) {
  uint32_t size;
  uint16_t select;
  uint16_t reserved;
  char name[56];
} FWCfgFile;

typedef struct __attribute__((packed)) {
  uint32_t control;
  uint32_t len;
  uint64_t addr;
} FWCfgDmaAccess;

typedef struct __attribute__((packed)) {
  uint64_t addr;   /* Physical address of RAM buffer */
  uint32_t fourcc; /* DRM FOURCC pixel format */
  uint32_t flags;  /* Flags (usually 0) */
  uint32_t width;  /* Width in pixels */
  uint32_t height; /* Height in pixels */
  uint32_t stride; /* Bytes per line (0 = width * bytes_per_pixel) */
} RamFBCfg;

/* Helper macros for Big-Endian conversion required by QEMU DMA */
#define bswap16(x) __builtin_bswap16(x)
#define bswap32(x) __builtin_bswap32(x)
#define bswap64(x) __builtin_bswap64(x)

// Global Framebuffer Info
uint32_t g_framebuffer[FB_WIDTH * FB_HEIGHT];

void ramfb_init() {
  memset(g_framebuffer, 0, FB_WIDTH * FB_HEIGHT);

  // Find the selector for "etc/ramfb"
  *FW_CFG_SELECTOR = bswap16(FW_CFG_FILE_DIR);

  // Read count of files in directory
  uint32_t file_count;
  file_count = bswap32(*(volatile uint32_t *)FW_CFG_DATA);

  uint16_t ramfb_select = 0;
  for (uint32_t i = 0; i < file_count; ++i) {
    FWCfgFile file;
    volatile uint8_t *data_ptr = (volatile uint8_t *)FW_CFG_DATA;
    uint8_t *out = (uint8_t *)&file;
    for (size_t b = 0; b < sizeof(FWCfgFile); b++) {
      out[b] = *data_ptr;
    }

    if (strcmp(file.name, "etc/ramfb") == 0) {
      ramfb_select = bswap16(file.select);
      break;
    }
  }

  if (!ramfb_select) {
    PANIC("'etc/ramfb' missing from fw_cfg! Check QEMU -device ramfb flag");
    return;
  }

  // Fill out RamFBCfg in Big-Endian
  RamFBCfg cfg = {.addr = bswap64((uint64_t)(uintptr_t)g_framebuffer),
                  .fourcc = bswap32(DRM_FORMAT_XRGB8888),
                  .flags = 0,
                  .width = bswap32(FB_WIDTH),
                  .height = bswap32(FB_HEIGHT),
                  .stride = bswap32(FB_WIDTH * sizeof(uint32_t))};

  // Execute DMA Transfer to tell QEMU where the buffer is
  FWCfgDmaAccess dma = {.control = bswap32((ramfb_select << 16) |
                                           QEMU_CFG_DMA_CTL_SELECT |
                                           QEMU_CFG_DMA_CTL_WRITE),
                        .len = bswap32(sizeof(RamFBCfg)),
                        .addr = bswap64((uint64_t)(uintptr_t)&cfg)};

  /* Write physical address of the DMA struct to FW_CFG_DMA register */
  *FW_CFG_DMA = bswap64((uint64_t)(uintptr_t)&dma);

  /* Wait for DMA transfer to complete (control becomes 0) */
  volatile FWCfgDmaAccess *volatile_dma = (volatile FWCfgDmaAccess *)&dma;
  while (volatile_dma->control != 0) {
    __asm__ __volatile__("nop");
  }
}

uint32_t *ramfb_get_buffer() { return g_framebuffer; }