#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "colors.h"
#include "user.h"

// Test buffer dimensions (e.g. 320x200 for fast rendering / scaling test)
#define WIDTH 320
#define HEIGHT 200

// static uint32_t g_test_buffer[WIDTH * HEIGHT];

// Helper to combine RGB components into a 32-bit XRGB pixel
static inline uint32_t make_color(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  printf("Starting Graphics Test (%dx%d)...\n", WIDTH, HEIGHT);

  uint32_t *buffer = (uint32_t *)malloc(WIDTH * HEIGHT * sizeof(uint32_t));

  // Square properties for animation
  int sq_size = 30;
  int sq_x = 20, sq_y = 20;
  int vel_x = 3, vel_y = 2;

  // Render loop (~300 frames)
  for (int frame = 0; frame < 300; ++frame) {
    // Clear background with an animated color gradient
    for (int y = 0; y < HEIGHT; ++y) {
      for (int x = 0; x < WIDTH; ++x) {
        uint8_t r = (x + frame * 2) % 256;
        uint8_t g = (y + frame * 3) % 256;
        uint8_t b = (frame * 4) % 256;

        buffer[y * WIDTH + x] = make_color(r, g, b);
      }
    }

    // Update bouncing square position
    sq_x += vel_x;
    sq_y += vel_y;

    // Bounce off left/right edges
    if (sq_x <= 0 || sq_x + sq_size >= WIDTH) {
      vel_x = -vel_x;
    }
    // Bounce off top/bottom edges
    if (sq_y <= 0 || sq_y + sq_size >= HEIGHT) {
      vel_y = -vel_y;
    }

    // Draw a white box with a black border at the bouncing position
    for (int y = sq_y; y < sq_y + sq_size; ++y) {
      for (int x = sq_x; x < sq_x + sq_size; ++x) {
        // Keep within bounds safety check
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
          // Border vs Fill
          if (x == sq_x || x == sq_x + sq_size - 1 || y == sq_y ||
              y == sq_y + sq_size - 1) {
            buffer[y * WIDTH + x] = make_color(0, 0, 0); /* Black border */
          } else {
            buffer[y * WIDTH + x] = make_color(255, 255, 255); /* White fill */
          }
        }
      }
    }

    // Present the frame to the kernel
    framebuffer_present(buffer, WIDTH, HEIGHT);

    // Tiny sleep so it doesn't spin at 1000 FPS
    sleep_ms(16); /* ~60 FPS target */
  }

  printf(GREEN "Graphics test complete!\n" DEFAULT);
  free(buffer);
  return 0;
}