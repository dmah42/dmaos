#include "fb.h"
#include "kernel.h"
#include "string.h"

static uint32_t g_framebuffer[FB_WIDTH * FB_HEIGHT];

void fb_init(void) {
  memset(g_framebuffer, 0, sizeof(g_framebuffer));
  kprintf("fb: simple memory framebuffer initialized (%dx%d, 32bpp)\n",
          FB_WIDTH, FB_HEIGHT);
}

uint32_t *fb_get_buffer(void) { return g_framebuffer; }

int fb_present(uint32_t *fb, uint32_t w, uint32_t h) {
  if (!fb || w == 0 || h == 0) {
    return -1;
  }

  uint32_t step_x = (w << 16) / FB_WIDTH;
  uint32_t step_y = (h << 16) / FB_HEIGHT;
  uint32_t curr_y_fp = 0;

  uint32_t *dst = fb_get_buffer();

  for (uint32_t dst_y = 0; dst_y < FB_HEIGHT; ++dst_y) {
    uint32_t src_y = curr_y_fp >> 16;
    if (src_y >= h) {
      src_y = h - 1;
    }

    const uint32_t *src_line = &fb[src_y * w];
    uint32_t *dst_line = &dst[dst_y * FB_WIDTH];
    uint32_t curr_x_fp = 0;

    for (uint32_t dst_x = 0; dst_x < FB_WIDTH; ++dst_x) {
      uint32_t src_x = curr_x_fp >> 16;
      if (src_x >= w) {
        src_x = w - 1;
      }
      dst_line[dst_x] = src_line[src_x];
      curr_x_fp += step_x;
    }

    curr_y_fp += step_y;
  }

  return 0;
}
