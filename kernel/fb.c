#include "fb.h"
#include "string.h"

static struct Framebuffer g_fb = {NULL, 0, 0, 0};

void fb_register(uint32_t *base, uint32_t width, uint32_t height,
                 uint32_t stride) {
  g_fb.base = base;
  g_fb.width = width;
  g_fb.height = height;
  g_fb.stride = (stride > 0) ? stride : width;

  if (g_fb.base && g_fb.width > 0 && g_fb.height > 0) {
    memset(g_fb.base, 0, g_fb.stride * g_fb.height * sizeof(uint32_t));
  }
}

uint32_t *fb_get_buffer(void) { return g_fb.base; }
uint32_t fb_get_width(void) { return g_fb.width; }
uint32_t fb_get_height(void) { return g_fb.height; }
uint32_t fb_get_stride(void) { return g_fb.stride; }

int fb_present(uint32_t *src, uint32_t w, uint32_t h) {
  if (!src || !g_fb.base || w == 0 || h == 0 || g_fb.width == 0 ||
      g_fb.height == 0) {
    return -1;
  }

  uint32_t step_x = (w << 16) / g_fb.width;
  uint32_t step_y = (h << 16) / g_fb.height;
  uint32_t curr_y_fp = 0;

  for (uint32_t dst_y = 0; dst_y < g_fb.height; ++dst_y) {
    uint32_t src_y = curr_y_fp >> 16;
    if (src_y >= h) {
      src_y = h - 1;
    }

    const uint32_t *src_line = &src[src_y * w];
    uint32_t *dst_line = &g_fb.base[dst_y * g_fb.stride];
    uint32_t curr_x_fp = 0;

    for (uint32_t dst_x = 0; dst_x < g_fb.width; ++dst_x) {
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
