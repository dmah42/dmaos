#include "stdlib.h"
#include "user.h"

#define WIDTH 32
#define HEIGHT 16

struct Point {
  int x;
  int y;
};

struct Point snake[100];
int snake_len = 3;
struct Point food;
int dx = 1;
int dy = 0;
bool game_over = false;
int score = 0;

void draw_borders() {
  printf("\033[H\033[J"); // Clear screen and move to home
  printf("\033[?25l");    // Hide cursor

  // Top border
  printf(" ");
  for (int x = 0; x < WIDTH; x++) {
    printf("-");
  }
  printf("\n");

  for (int y = 0; y < HEIGHT; y++) {
    printf("|");
    for (int x = 0; x < WIDTH; x++) {
      printf(" ");
    }
    printf("|\n");
  }

  // Bottom border
  printf(" ");
  for (int x = 0; x < WIDTH; x++) {
    printf("-");
  }
  printf("\n");
}

void place_food() {
  while (1) {
    food.x = (rand() % WIDTH);
    food.y = (rand() % HEIGHT);

    // Make sure food is not on snake
    bool on_snake = false;
    for (int i = 0; i < snake_len; i++) {
      if (snake[i].x == food.x && snake[i].y == food.y) {
        on_snake = true;
        break;
      }
    }
    if (!on_snake) {
      break;
    }
  }

  // Draw food (VT100 uses 1-based indexing, borders add 1 or 2 offsets)
  // Our game coordinates are 0 to WIDTH-1, 0 to HEIGHT-1.
  // Visual positions:
  // x-coordinate on screen: food.x + 2 (since column 1 is the left border '|')
  // y-coordinate on screen: food.y + 2 (since row 1 is the top border '-')
  printf("\033[%d;%dH\033[31m*\033[0m", food.y + 2, food.x + 2);
}

void init_game() {
  // Seed the random number generator using the hardware timer
  srand((uint32_t)uptime());

  snake_len = 3;
  // Initialize snake in the middle moving right
  snake[0].x = WIDTH / 2;
  snake[0].y = HEIGHT / 2;
  snake[1].x = WIDTH / 2 - 1;
  snake[1].y = HEIGHT / 2;
  snake[2].x = WIDTH / 2 - 2;
  snake[2].y = HEIGHT / 2;

  dx = 1;
  dy = 0;
  game_over = false;
  score = 0;

  draw_borders();
  place_food();

  // Draw initial snake
  for (int i = 0; i < snake_len; i++) {
    printf("\033[%d;%dH\033[32mO\033[0m", snake[i].y + 2, snake[i].x + 2);
  }
}

void update_game() {
  // Calculate new head position
  struct Point new_head;
  new_head.x = snake[0].x + dx;
  new_head.y = snake[0].y + dy;

  // Collision with borders
  if (new_head.x < 0 || new_head.x >= WIDTH || new_head.y < 0 ||
      new_head.y >= HEIGHT) {
    game_over = true;
    return;
  }

  // Collision with self
  for (int i = 0; i < snake_len; i++) {
    if (snake[i].x == new_head.x && snake[i].y == new_head.y) {
      game_over = true;
      return;
    }
  }

  // Check if we eat food
  if (new_head.x == food.x && new_head.y == food.y) {
    score += 10;
    if (snake_len < 100) {
      // Grow snake
      snake_len++;
      // Shift elements down
    } else {
      // Limit reached: eat food but don't grow.
      struct Point tail = snake[snake_len - 1];
      printf("\033[%d;%dH ", tail.y + 2, tail.x + 2);
    }
    for (int i = snake_len - 1; i > 0; --i) {
      snake[i] = snake[i - 1];
    }
    snake[0] = new_head;

    // Draw new head
    printf("\033[%d;%dH\033[32mO\033[0m", snake[0].y + 2, snake[0].x + 2);

    place_food();
  } else {
    // Clear tail from screen
    struct Point tail = snake[snake_len - 1];
    printf("\033[%d;%dH ", tail.y + 2, tail.x + 2);

    // Move snake
    for (int i = snake_len - 1; i > 0; --i) {
      snake[i] = snake[i - 1];
    }
    snake[0] = new_head;

    // Draw new head
    printf("\033[%d;%dH\033[32mO\033[0m", snake[0].y + 2, snake[0].x + 2);
  }

  // Print score at the bottom
  printf("\033[%d;1HScore: %d", HEIGHT + 3, score);
}

int get_input(void) {
  int ch = getchar_nonblock();
  if (ch == 27) { // Escape
    int next1 = -1;
    for (int i = 0; i < 10 && next1 < 0; i++) {
      next1 = getchar_nonblock();
      if (next1 < 0) {
        yield();
      }
    }
    if (next1 == '[') {
      int next2 = -1;
      for (int i = 0; i < 10 && next2 < 0; i++) {
        next2 = getchar_nonblock();
        if (next2 < 0) {
          yield();
        }
      }
      if (next2 == 'A')
        return 'w'; // Up
      if (next2 == 'B')
        return 's'; // Down
      if (next2 == 'C')
        return 'd'; // Right
      if (next2 == 'D')
        return 'a'; // Left
    }
  }
  return ch;
}

int main(void) {
  init_game();

  while (!game_over) {
    int ch = get_input();
    if (ch >= 0) {
      // Handle direction change (cannot reverse directly into self)
      if ((ch == 'w' || ch == 'W') && dy == 0) {
        dx = 0;
        dy = -1;
      } else if ((ch == 's' || ch == 'S') && dy == 0) {
        dx = 0;
        dy = 1;
      } else if ((ch == 'a' || ch == 'A') && dx == 0) {
        dx = -1;
        dy = 0;
      } else if ((ch == 'd' || ch == 'D') && dx == 0) {
        dx = 1;
        dy = 0;
      } else if (ch == 'q' || ch == 'Q') {
        game_over = true;
        break;
      }
    }

    update_game();
    sleep_ms(150); // Delay between frames
  }

  // Restore cursor and print final screen
  printf("\033[%d;1H\033[?25h", HEIGHT + 3);
  printf("\nGame Over! Final Score: %d\n", score);
  return 0;
}
