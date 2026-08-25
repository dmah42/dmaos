#include "ramfb.h"

#include "fb.h"
#include "kernel.h"
#include "page.h"
#include "stdlib.h"
#include "string.h"

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
typedef struct _packed {
  uint32_t size;
  uint16_t select;
  uint16_t reserved;
  char name[56];
} FWCfgFile;

typedef struct _packed {
  uint32_t control;
  uint32_t len;
  uint64_t addr;
} FWCfgDmaAccess;

typedef struct _packed {
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

static uint32_t fw_cfg_read_u32_str(uint16_t selector, uint32_t size) {
  if (!selector || size == 0) {
    return 0;
  }
  *FW_CFG_SELECTOR = bswap16(selector);
  char buf[32];
  uint32_t len = (size < sizeof(buf) - 1) ? size : (sizeof(buf) - 1);
  volatile uint8_t *data_ptr = (volatile uint8_t *)FW_CFG_DATA;
  for (uint32_t i = 0; i < len; ++i) {
    buf[i] = *data_ptr;
  }
  buf[len] = '\0';
  return (uint32_t)atoi(buf);
}

void ramfb_init(void) {
  // Find selectors in fw_cfg
  *FW_CFG_SELECTOR = bswap16(FW_CFG_FILE_DIR);

  // Read count of files in directory
  uint32_t file_count;
  file_count = bswap32(*(volatile uint32_t *)FW_CFG_DATA);

  uint16_t ramfb_select = 0;
  uint16_t width_select = 0;
  uint32_t width_size = 0;
  uint16_t height_select = 0;
  uint32_t height_size = 0;

  for (uint32_t i = 0; i < file_count; ++i) {
    FWCfgFile file;
    volatile uint8_t *data_ptr = (volatile uint8_t *)FW_CFG_DATA;
    uint8_t *out = (uint8_t *)&file;
    for (size_t b = 0; b < sizeof(FWCfgFile); b++) {
      out[b] = *data_ptr;
    }

    if (strcmp(file.name, "etc/ramfb") == 0) {
      ramfb_select = bswap16(file.select);
    } else if (strcmp(file.name, "opt/dmaos/width") == 0) {
      width_select = bswap16(file.select);
      width_size = bswap32(file.size);
    } else if (strcmp(file.name, "opt/dmaos/height") == 0) {
      height_select = bswap16(file.select);
      height_size = bswap32(file.size);
    }
  }

  if (!ramfb_select) {
    PANIC("'etc/ramfb' missing from fw_cfg! Check QEMU -device ramfb flag");
    return;
  }

  // Parse display resolution provided by host via fw_cfg
  uint32_t width = fw_cfg_read_u32_str(width_select, width_size);
  uint32_t height = fw_cfg_read_u32_str(height_select, height_size);

  if (width == 0 || height == 0) {
    PANIC("ramfb: resolution not provided in fw_cfg (opt/dmaos/width, opt/dmaos/height)");
    return;
  }

  // Dynamically allocate framebuffer memory
  size_t fb_bytes = (size_t)width * height * sizeof(uint32_t);
  size_t fb_pages = (fb_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
  uint32_t *framebuffer = (uint32_t *)alloc_pages(fb_pages);
  if (!framebuffer) {
    PANIC("ramfb: failed to allocate memory for framebuffer");
    return;
  }
  memset(framebuffer, 0, fb_bytes);

  // Fill out RamFBCfg in Big-Endian
  RamFBCfg cfg = {.addr = bswap64((uint64_t)(uintptr_t)framebuffer),
                  .fourcc = bswap32(DRM_FORMAT_XRGB8888),
                  .flags = 0,
                  .width = bswap32(width),
                  .height = bswap32(height),
                  .stride = bswap32(width * sizeof(uint32_t))};

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

  fb_register(framebuffer, width, height, width);
  kprintf("ramfb: qemu framebuffer initialized (%ux%u, 32bpp)\n", width,
          height);
}

void fb_init(void) { ramfb_init(); }