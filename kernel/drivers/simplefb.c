#include "simplefb.h"
#include "fb.h"
#include "kernel.h"

// Runtime simple-framebuffer state (passed from U-Boot / FDT)
static uint32_t *g_simplefb_base = NULL;
static uint32_t g_simplefb_width = 0;
static uint32_t g_simplefb_height = 0;
static uint32_t g_simplefb_stride = 0;

void simplefb_setup(uint32_t *base, uint32_t width, uint32_t height,
                    uint32_t stride) {
  g_simplefb_base = base;
  g_simplefb_width = width;
  g_simplefb_height = height;
  g_simplefb_stride = (stride > 0) ? stride : width;

  if (g_simplefb_base && g_simplefb_width > 0 && g_simplefb_height > 0) {
    fb_register(g_simplefb_base, g_simplefb_width, g_simplefb_height,
                g_simplefb_stride);
    kprintf("simplefb: simple-framebuffer registered (%ux%u, stride=%u)\n",
            g_simplefb_width, g_simplefb_height, g_simplefb_stride);
  }
}

void simplefb_init(void) {
  if (!g_simplefb_base || g_simplefb_width == 0 || g_simplefb_height == 0) {
    PANIC("simplefb: no framebuffer provided by bootloader/FDT");
    return;
  }

  fb_register(g_simplefb_base, g_simplefb_width, g_simplefb_height,
              g_simplefb_stride);
  kprintf("simplefb: simple-framebuffer initialized (%ux%u)\n",
          g_simplefb_width, g_simplefb_height);
}

void fb_init(void) { simplefb_init(); }
