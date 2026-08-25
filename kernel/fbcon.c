#include "fbcon.h"

#include <stdbool.h>

#include "fb.h"
#include "font8x16.h"
#include "string.h"

#define MAX_ESC_ARGS 8
#define UTF8_BUF_SIZE 4

// -----------------------------------------------------------------------------
// Gruvbox Dark Palette Colors (32-bit XRGB8888)
// -----------------------------------------------------------------------------
#define GRUVBOX_BG 0x00282828 // dark0
#define GRUVBOX_FG 0x00EBDBB2 // light1

#define GRUVBOX_BLACK 0x00282828
#define GRUVBOX_RED 0x00CC241D
#define GRUVBOX_GREEN 0x0098971A
#define GRUVBOX_YELLOW 0x00D79921
#define GRUVBOX_BLUE 0x00458588
#define GRUVBOX_MAGENTA 0x00B16286
#define GRUVBOX_CYAN 0x00689D6A
#define GRUVBOX_WHITE 0x00A89984

#define GRUVBOX_BR_BLACK 0x00928374 // gray
#define GRUVBOX_BR_RED 0x00FB4934
#define GRUVBOX_BR_GREEN 0x00B8BB26
#define GRUVBOX_BR_YELLOW 0x00FABD2F
#define GRUVBOX_BR_BLUE 0x0083A598
#define GRUVBOX_BR_MAGENTA 0x00D3869B
#define GRUVBOX_BR_CYAN 0x008EC07C
#define GRUVBOX_BR_WHITE 0x00EBDBB2

// ANSI Palette mapping
static const uint32_t g_ansi_palette[16] = {
    GRUVBOX_BLACK,    GRUVBOX_RED,        GRUVBOX_GREEN,    GRUVBOX_YELLOW,
    GRUVBOX_BLUE,     GRUVBOX_MAGENTA,    GRUVBOX_CYAN,     GRUVBOX_WHITE,
    GRUVBOX_BR_BLACK, GRUVBOX_BR_RED,     GRUVBOX_BR_GREEN, GRUVBOX_BR_YELLOW,
    GRUVBOX_BR_BLUE,  GRUVBOX_BR_MAGENTA, GRUVBOX_BR_CYAN,  GRUVBOX_BR_WHITE,
};

// Console State
static uint32_t g_scale = 3;
static uint32_t g_cols = 0;
static uint32_t g_rows = 0;
static uint32_t g_cursor_col = 0;
static uint32_t g_cursor_row = 0;

static uint32_t g_fg_color = GRUVBOX_FG;
static uint32_t g_bg_color = GRUVBOX_BG;
static bool g_bold = false;

// ANSI Parser State
enum AnsiState {
  STATE_NORMAL,
  STATE_ESC,
  STATE_CSI,
};

static enum AnsiState g_state = STATE_NORMAL;
static int g_esc_args[MAX_ESC_ARGS];
static int g_esc_argc = 0;
static bool g_is_private = false;

// Multi-byte UTF-8 Buffer State
static uint8_t g_utf8_buf[UTF8_BUF_SIZE];
static int g_utf8_len = 0;
static int g_utf8_expected = 0;

static uint32_t min_u32(uint32_t a, uint32_t b) { return (a < b) ? a : b; }

// Draw a single 8x16 character glyph on the framebuffer
static void fbcon_draw_glyph(uint32_t col, uint32_t row, uint8_t glyph,
                             uint32_t fg, uint32_t bg) {
  uint32_t *fb = fb_get_buffer();
  if (!fb || col >= g_cols || row >= g_rows) {
    return;
  }

  uint32_t width = fb_get_width();
  uint32_t pixel_x = col * 8 * g_scale;
  uint32_t pixel_y = row * 16 * g_scale;
  const uint8_t *bitmap = font_get_glyph(glyph);

  if (g_scale == 1) {
    for (uint32_t y = 0; y < 16; ++y) {
      uint8_t line = bitmap[y];
      uint32_t *dst = &fb[(pixel_y + y) * width + pixel_x];
      for (uint32_t x = 0; x < 8; ++x) {
        dst[x] = (line & (0x80 >> x)) ? fg : bg;
      }
    }
  } else {
    // Scaled rendering for 2x/high DPI
    for (uint32_t y = 0; y < 16; ++y) {
      uint8_t line = bitmap[y];
      for (uint32_t sy = 0; sy < g_scale; ++sy) {
        uint32_t *dst = &fb[(pixel_y + y * g_scale + sy) * width + pixel_x];
        for (uint32_t x = 0; x < 8; ++x) {
          uint32_t color = (line & (0x80 >> x)) ? fg : bg;
          for (uint32_t sx = 0; sx < g_scale; ++sx) {
            dst[x * g_scale + sx] = color;
          }
        }
      }
    }
  }
}

// Scroll framebuffer text lines upward by one row
static void fbcon_scroll_up(void) {
  uint32_t *fb = fb_get_buffer();
  if (!fb) {
    return;
  }

  uint32_t width = fb_get_width();
  uint32_t row_height = 16 * g_scale;
  uint32_t scanline_pixels = width * row_height;
  uint32_t total_scroll_pixels = scanline_pixels * (g_rows - 1);

  // Move upper screen buffer up by one text row
  memmove(fb, fb + scanline_pixels, total_scroll_pixels * sizeof(uint32_t));

  // Clear bottom row with current background color
  uint32_t *bottom = fb + total_scroll_pixels;
  for (uint32_t i = 0; i < scanline_pixels; ++i) {
    bottom[i] = g_bg_color;
  }
}

// Clear a specific range of columns on a row
static void fbcon_clear_line_range(uint32_t row, uint32_t start_col,
                                   uint32_t end_col) {
  for (uint32_t col = start_col; col <= end_col && col < g_cols; ++col) {
    fbcon_draw_glyph(col, row, ' ', g_fg_color, g_bg_color);
  }
}

// Execute SGR (Select Graphic Rendition) color command
static void fbcon_handle_sgr(void) {
  if (g_esc_argc == 0 && g_esc_args[0] == 0) {
    // Reset to defaults
    g_fg_color = GRUVBOX_FG;
    g_bg_color = GRUVBOX_BG;
    g_bold = false;
    return;
  }

  for (int i = 0; i <= g_esc_argc; ++i) {
    int code = g_esc_args[i];
    if (code == 0) {
      g_fg_color = GRUVBOX_FG;
      g_bg_color = GRUVBOX_BG;
      g_bold = false;
    } else if (code == 1) {
      g_bold = true;
    } else if (code == 22) {
      g_bold = false;
    } else if (code >= 30 && code <= 37) {
      g_fg_color = g_ansi_palette[(code - 30) + (g_bold ? 8 : 0)];
    } else if (code == 39) {
      g_fg_color = GRUVBOX_FG;
    } else if (code >= 40 && code <= 47) {
      g_bg_color = g_ansi_palette[code - 40];
    } else if (code == 49) {
      g_bg_color = GRUVBOX_BG;
    } else if (code >= 90 && code <= 97) {
      g_fg_color = g_ansi_palette[(code - 90) + 8];
    } else if (code >= 100 && code <= 107) {
      g_bg_color = g_ansi_palette[(code - 100) + 8];
    }
  }
}

// Execute complete CSI escape sequence
static void fbcon_handle_csi(char cmd) {
  switch (cmd) {
  case 'm':
    fbcon_handle_sgr();
    break;
  case 'H':
  case 'f': {
    uint32_t r = (g_esc_args[0] > 0) ? (uint32_t)(g_esc_args[0] - 1) : 0;
    uint32_t c = (g_esc_argc > 0 && g_esc_args[1] > 0)
                     ? (uint32_t)(g_esc_args[1] - 1)
                     : 0;
    g_cursor_row = min_u32(r, g_rows - 1);
    g_cursor_col = min_u32(c, g_cols - 1);
    break;
  }
  case 'C': {
    // Cursor Forward (supports RALIGN)
    uint32_t count = (g_esc_args[0] > 0) ? (uint32_t)g_esc_args[0] : 1;
    g_cursor_col = min_u32(g_cursor_col + count, g_cols - 1);
    break;
  }
  case 'D': {
    // Cursor Backward
    uint32_t count = (g_esc_args[0] > 0) ? (uint32_t)g_esc_args[0] : 1;
    g_cursor_col = (g_cursor_col >= count) ? (g_cursor_col - count) : 0;
    break;
  }
  case 'J':
    // Clear display
    fbcon_clear();
    g_cursor_col = 0;
    g_cursor_row = 0;
    break;
  case 'K':
    // Clear from cursor to end of line
    fbcon_clear_line_range(g_cursor_row, g_cursor_col, g_cols - 1);
    break;
  case 'h':
  case 'l':
    // Private mode flag handler (e.g. ?25h, ?25l)
    break;
  default:
    break;
  }
}

// Output a single printable glyph and advance cursor
static void fbcon_put_glyph(uint8_t glyph) {
  fbcon_draw_glyph(g_cursor_col, g_cursor_row, glyph, g_fg_color, g_bg_color);
  ++g_cursor_col;
  if (g_cursor_col >= g_cols) {
    g_cursor_col = 0;
    ++g_cursor_row;
    if (g_cursor_row >= g_rows) {
      fbcon_scroll_up();
      g_cursor_row = g_rows - 1;
    }
  }
}

// Public API Implementation

void fbcon_init(void) {
  g_cols = fb_get_width() / (8 * g_scale);
  g_rows = fb_get_height() / (16 * g_scale);
  g_cursor_col = 0;
  g_cursor_row = 0;
  g_state = STATE_NORMAL;
  g_utf8_len = 0;
  g_utf8_expected = 0;
  g_fg_color = GRUVBOX_FG;
  g_bg_color = GRUVBOX_BG;
  g_bold = false;

  fbcon_clear();
}

void fbcon_clear(void) {
  uint32_t *fb = fb_get_buffer();
  if (!fb) {
    return;
  }
  uint32_t total_pixels = fb_get_width() * fb_get_height();
  for (uint32_t i = 0; i < total_pixels; ++i) {
    fb[i] = g_bg_color;
  }
}

void fbcon_set_scale(uint32_t scale) {
  if (scale < 1) {
    scale = 1;
  }
  g_scale = scale;
  g_cols = fb_get_width() / (8 * g_scale);
  g_rows = fb_get_height() / (16 * g_scale);
  if (g_cursor_col >= g_cols) {
    g_cursor_col = g_cols - 1;
  }
  if (g_cursor_row >= g_rows) {
    g_cursor_row = g_rows - 1;
  }
}

void fbcon_set_colors(uint32_t fg, uint32_t bg) {
  g_fg_color = fg;
  g_bg_color = bg;
}

uint32_t fbcon_get_cols(void) { return g_cols; }
uint32_t fbcon_get_rows(void) { return g_rows; }

void fbcon_putchar(char ch) {
  uint8_t byte = (uint8_t)ch;

  // Multi-byte UTF-8 decoding accumulator
  if (g_utf8_expected > 0) {
    g_utf8_buf[g_utf8_len++] = byte;
    if (g_utf8_len >= g_utf8_expected) {
      const uint8_t *stream = g_utf8_buf;
      uint8_t glyph = utf8_decode_to_cp437(&stream);
      fbcon_put_glyph(glyph);
      g_utf8_len = 0;
      g_utf8_expected = 0;
    }
    return;
  }

  // Check start of UTF-8 multi-byte sequence
  if (byte >= 0xC0 && byte <= 0xDF) {
    g_utf8_buf[0] = byte;
    g_utf8_len = 1;
    g_utf8_expected = 2;
    return;
  } else if (byte >= 0xE0 && byte <= 0xEF) {
    g_utf8_buf[0] = byte;
    g_utf8_len = 1;
    g_utf8_expected = 3;
    return;
  } else if (byte >= 0xF0 && byte <= 0xF7) {
    g_utf8_buf[0] = byte;
    g_utf8_len = 1;
    g_utf8_expected = 4;
    return;
  }

  // ANSI Escape State Machine
  switch (g_state) {
  case STATE_NORMAL:
    if (byte == '\033') {
      g_state = STATE_ESC;
    } else if (byte == '\n') {
      g_cursor_col = 0;
      g_cursor_row++;
      if (g_cursor_row >= g_rows) {
        fbcon_scroll_up();
        g_cursor_row = g_rows - 1;
      }
    } else if (byte == '\r') {
      g_cursor_col = 0;
    } else if (byte == '\t') {
      g_cursor_col = (g_cursor_col + 8) & ~7;
      if (g_cursor_col >= g_cols) {
        g_cursor_col = 0;
        g_cursor_row++;
        if (g_cursor_row >= g_rows) {
          fbcon_scroll_up();
          g_cursor_row = g_rows - 1;
        }
      }
    } else if (byte == '\b') {
      if (g_cursor_col > 0) {
        g_cursor_col--;
        fbcon_draw_glyph(g_cursor_col, g_cursor_row, ' ', g_fg_color,
                         g_bg_color);
      }
    } else if (byte >= 0x20 || byte == 0xED) {
      fbcon_put_glyph(byte);
    }
    break;

  case STATE_ESC:
    if (byte == '[') {
      g_state = STATE_CSI;
      g_esc_argc = 0;
      g_esc_args[0] = 0;
      g_is_private = false;
    } else {
      g_state = STATE_NORMAL;
    }
    break;

  case STATE_CSI:
    if (byte >= '0' && byte <= '9') {
      g_esc_args[g_esc_argc] = g_esc_args[g_esc_argc] * 10 + (byte - '0');
    } else if (byte == ';') {
      if (g_esc_argc < MAX_ESC_ARGS - 1) {
        g_esc_argc++;
        g_esc_args[g_esc_argc] = 0;
      }
    } else if (byte == '?') {
      g_is_private = true;
    } else if (byte >= 0x40 && byte <= 0x7E) {
      fbcon_handle_csi((char)byte);
      g_state = STATE_NORMAL;
    } else {
      g_state = STATE_NORMAL;
    }
    break;
  }
}

void fbcon_write(const char *buf, uint32_t len) {
  if (!buf) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    fbcon_putchar(buf[i]);
  }
}
