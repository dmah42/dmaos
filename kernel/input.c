#include "input.h"

#include "input_driver.h"
#include "kernel.h"
#include "process.h"

// Linux key codes
#define KEY_BACKSLASH 43
#define KEY_C 46
#define KEY_LEFTCTRL 29
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTSHIFT 54

// Characters that signal when typed on the serial console
#define CHAR_INTR 0x03 // Ctrl+C
#define CHAR_QUIT 0x1c // Ctrl+backslash

#define SBI_LEGACY_CONSOLE_GETCHAR 2

#define RAW_EVENTS_MAX 128
#define TEXT_MAX 128

struct raw_event {
  uint16_t code;
  uint8_t pressed;
};

// Keyboard events, in arrival order. Free-running indices.
static struct raw_event raw_events[RAW_EVENTS_MAX];
static uint32_t raw_head = 0;
static uint32_t raw_tail = 0;

// Serial console bytes. Processes blocked in input_wait() sleep on this.
static char text[TEXT_MAX];
static uint32_t text_head = 0;
static uint32_t text_tail = 0;

// The rest of a multi-byte sequence (e.g. an arrow key) being read.
static char pending_seq[8];
static uint32_t pending_head = 0;
static uint32_t pending_tail = 0;

// Whether Ctrl is held, tracked as events arrive (for signals) and as they are
// translated to text (for control characters); the two can differ while events
// wait in the buffer.
static bool arrival_ctrl = false;
static bool read_ctrl = false;
static bool read_shift = false;

static bool isig = true;
static int isig_off_pid = 0;   // the process that turned ISIG off, or 0
static int keyboard_owner = 0; // the process reading raw events, or 0

static const char *const scancode_map_lower[128] = {
    [1] = "\033",
    [2] = "1",
    [3] = "2",
    [4] = "3",
    [5] = "4",
    [6] = "5",
    [7] = "6",
    [8] = "7",
    [9] = "8",
    [10] = "9",
    [11] = "0",
    [12] = "-",
    [13] = "=",
    [14] = "\b",
    [15] = "\t",
    [16] = "q",
    [17] = "w",
    [18] = "e",
    [19] = "r",
    [20] = "t",
    [21] = "y",
    [22] = "u",
    [23] = "i",
    [24] = "o",
    [25] = "p",
    [26] = "[",
    [27] = "]",
    [28] = "\n",
    [30] = "a",
    [31] = "s",
    [32] = "d",
    [33] = "f",
    [34] = "g",
    [35] = "h",
    [36] = "j",
    [37] = "k",
    [38] = "l",
    [39] = ";",
    [40] = "'",
    [41] = "`",
    [43] = "\\",
    [44] = "z",
    [45] = "x",
    [46] = "c",
    [47] = "v",
    [48] = "b",
    [49] = "n",
    [50] = "m",
    [51] = ",",
    [52] = ".",
    [53] = "/",
    [57] = " ",
    // Extended keys (ANSI sequences)
    [102] = "\033[H",  /* Home */
    [103] = "\033[A",  /* Up */
    [104] = "\033[5~", /* PageUp */
    [105] = "\033[D",  /* Left */
    [106] = "\033[C",  /* Right */
    [107] = "\033[F",  /* End */
    [108] = "\033[B",  /* Down */
    [109] = "\033[6~", /* PageDown */
    [110] = "\033[2~", /* Insert */
    [111] = "\033[3~", /* Delete */
};

static const char *const scancode_map_upper[128] = {
    [1] = "\033",
    [2] = "!",
    [3] = "@",
    [4] = "#",
    [5] = "$",
    [6] = "%",
    [7] = "^",
    [8] = "&",
    [9] = "*",
    [10] = "(",
    [11] = ")",
    [12] = "_",
    [13] = "+",
    [14] = "\b",
    [15] = "\t",
    [16] = "Q",
    [17] = "W",
    [18] = "E",
    [19] = "R",
    [20] = "T",
    [21] = "Y",
    [22] = "U",
    [23] = "I",
    [24] = "O",
    [25] = "P",
    [26] = "{",
    [27] = "}",
    [28] = "\n",
    [30] = "A",
    [31] = "S",
    [32] = "D",
    [33] = "F",
    [34] = "G",
    [35] = "H",
    [36] = "J",
    [37] = "K",
    [38] = "L",
    [39] = ":",
    [40] = "\"",
    [41] = "~",
    [43] = "|",
    [44] = "Z",
    [45] = "X",
    [46] = "C",
    [47] = "V",
    [48] = "B",
    [49] = "N",
    [50] = "M",
    [51] = "<",
    [52] = ">",
    [53] = "?",
    [57] = " ",
    // Extended keys (ANSI sequences)
    [102] = "\033[H",  /* Home */
    [103] = "\033[A",  /* Up */
    [104] = "\033[5~", /* PageUp */
    [105] = "\033[D",  /* Left */
    [106] = "\033[C",  /* Right */
    [107] = "\033[F",  /* End */
    [108] = "\033[B",  /* Down */
    [109] = "\033[6~", /* PageDown */
    [110] = "\033[2~", /* Insert */
    [111] = "\033[3~", /* Delete */
};

void input_init(void) { input_driver_init(); }

static void raw_push(uint16_t code, int pressed) {
  if (raw_tail - raw_head < RAW_EVENTS_MAX) {
    raw_events[raw_tail % RAW_EVENTS_MAX] =
        (struct raw_event){.code = code, .pressed = pressed != 0};
    ++raw_tail;
  }
}

static void text_push(char c) {
  if (text_tail - text_head < TEXT_MAX) {
    text[text_tail % TEXT_MAX] = c;
    ++text_tail;
  }
}

// Sends sig to the foreground job if the console is set up for it. Returns
// true if the key was used as a signal, false if it should be delivered as
// input.
static bool signal_key(int sig, bool from_keyboard) {
  if (!isig) {
    return false;
  }
  // A program reading raw events owns the keyboard, as DOOM does on Linux by
  // grabbing the input device; the serial console can still interrupt it.
  if (from_keyboard && keyboard_owner != 0) {
    return false;
  }
  return signal_foreground(sig);
}

void input_poll_devices(void) {
  bool arrived = false;

  uint16_t code;
  int pressed;
  while (input_driver_poll_event(&code, &pressed)) {
    if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
      arrival_ctrl = pressed != 0;
    } else if (pressed && arrival_ctrl) {
      int sig = code == KEY_C ? SIGINT : code == KEY_BACKSLASH ? SIGQUIT : 0;
      if (sig != 0 && signal_key(sig, true)) {
        continue;
      }
    }
    raw_push(code, pressed);
    arrived = true;
  }

  for (;;) {
    struct sbiret ret =
        sbi_call(0, 0, 0, 0, 0, 0, 0, SBI_LEGACY_CONSOLE_GETCHAR);
    if (ret.error < 0) {
      break;
    }
    char c = (char)ret.error;
    int sig = c == CHAR_INTR ? SIGINT : c == CHAR_QUIT ? SIGQUIT : 0;
    if (sig != 0 && signal_key(sig, false)) {
      continue;
    }
    text_push(c);
    arrived = true;
  }

  if (arrived) {
    wakeup(text);
  }
}

bool input_poll_event(uint16_t *code, int *pressed) {
  keyboard_owner = get_current_process()->pid;
  input_poll_devices();
  if (raw_head == raw_tail) {
    return false;
  }
  struct raw_event ev = raw_events[raw_head % RAW_EVENTS_MAX];
  ++raw_head;
  *code = ev.code;
  *pressed = ev.pressed;
  return true;
}

// Translates a key event to text. Returns the first character, queueing the
// rest of a multi-byte sequence, or -1 if the event produces no text.
static int translate(struct raw_event ev) {
  if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
    read_shift = ev.pressed;
    return -1;
  }
  if (ev.code == KEY_LEFTCTRL || ev.code == KEY_RIGHTCTRL) {
    read_ctrl = ev.pressed;
    return -1;
  }
  if (!ev.pressed || ev.code >= 128) {
    return -1;
  }

  const char *seq =
      read_shift ? scancode_map_upper[ev.code] : scancode_map_lower[ev.code];
  if (seq == NULL || seq[0] == '\0') {
    return -1;
  }

  // Ctrl with a letter or one of [ \ ] gives the control character.
  const char *base = scancode_map_lower[ev.code];
  if (read_ctrl && base[1] == '\0' &&
      ((base[0] >= 'a' && base[0] <= 'z') ||
       (base[0] >= '[' && base[0] <= ']'))) {
    return base[0] & 0x1f;
  }

  for (int i = 1; seq[i] != '\0'; ++i) {
    pending_seq[pending_tail % sizeof(pending_seq)] = seq[i];
    ++pending_tail;
  }
  return (unsigned char)seq[0];
}

int input_getchar(void) {
  input_poll_devices();

  if (pending_head != pending_tail) {
    return (unsigned char)pending_seq[pending_head++ % sizeof(pending_seq)];
  }
  if (text_head != text_tail) {
    return (unsigned char)text[text_head++ % TEXT_MAX];
  }
  while (raw_head != raw_tail) {
    struct raw_event ev = raw_events[raw_head % RAW_EVENTS_MAX];
    ++raw_head;
    int c = translate(ev);
    if (c >= 0) {
      return c;
    }
  }
  return -1;
}

int input_wait(void) { return sleep_on(text, 0); }

void input_set_isig(bool on) {
  isig = on;
  isig_off_pid = on ? 0 : get_current_process()->pid;
}

void input_process_exited(int pid) {
  if (keyboard_owner == pid) {
    keyboard_owner = 0;
    // The owner consumed the modifier events; start text translation afresh.
    read_ctrl = false;
    read_shift = false;
  }
  // A raw-mode program that crashes mustn't leave Ctrl+C disabled.
  if (isig_off_pid == pid) {
    isig = true;
    isig_off_pid = 0;
  }
}
