#include "doomgeneric.h"

#include "doomkeys.h"
#include "user.h"
#include <unistd.h>

void DG_Init(void) {
  /* Framebuffer is already up; nothing special needed here */
}

void DG_DrawFrame(void) {
  /* Pass Doom's internal 320x200 32-bit pixel buffer to your kernel! */
  framebuffer_present(DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

void DG_SleepMs(uint32_t ms) { sleep_ms(ms); }

// Ticks per millisecond on QEMU riscv virt board (10 MHz / 1000)
#define QEMU_RISCV_TIMEBASE_FREQ 10000000ULL
#define TICKS_PER_MS (QEMU_RISCV_TIMEBASE_FREQ / 1000ULL)

uint32_t DG_GetTicksMs(void) { return (uint32_t)(uptime() / TICKS_PER_MS); }

// Linux input event codes (reported by virtio-keyboard)
#define INPUT_KEY_ESC 1
#define INPUT_KEY_ENTER 28
#define INPUT_KEY_SPACE 57
#define INPUT_KEY_LEFTCTRL 29
#define INPUT_KEY_A 30
#define INPUT_KEY_S 31
#define INPUT_KEY_D 32
#define INPUT_KEY_W 17
#define INPUT_KEY_Y 21
#define INPUT_KEY_UP 103
#define INPUT_KEY_LEFT 105
#define INPUT_KEY_RIGHT 106
#define INPUT_KEY_DOWN 108

unsigned char doom_map_keycode(uint16_t hw_code) {
  switch (hw_code) {
  case INPUT_KEY_W:
    return KEY_UPARROW;
  case INPUT_KEY_S:
    return KEY_DOWNARROW;
  case INPUT_KEY_A:
    return KEY_STRAFE_L;
  case INPUT_KEY_D:
    return KEY_STRAFE_R;
  case INPUT_KEY_Y:
    return 'y';
  case INPUT_KEY_SPACE:
    return KEY_USE;
  case INPUT_KEY_LEFTCTRL:
    return KEY_FIRE;
  case INPUT_KEY_UP:
    return KEY_UPARROW;
  case INPUT_KEY_DOWN:
    return KEY_DOWNARROW;
  case INPUT_KEY_LEFT:
    return KEY_LEFTARROW;
  case INPUT_KEY_RIGHT:
    return KEY_RIGHTARROW;
  case INPUT_KEY_ENTER:
    return KEY_ENTER;
  case INPUT_KEY_ESC:
    return KEY_ESCAPE;
  default:
    return 0;
  }
}

int DG_GetKey(int *pressed, unsigned char *key) {
  uint16_t hw_code;
  int is_pressed;

  // Pull raw hardware event from your kernel ring buffer via syscall
  if (input_poll(&hw_code, &is_pressed)) {
    unsigned char mapped_key = doom_map_keycode(hw_code);
    if (mapped_key != 0) {
      *pressed = is_pressed;
      *key = mapped_key;
      return 1;
    }
  }
  return 0;
}

void DG_SetWindowTitle(const char *title) { (void)title; }

// --- Sound & Music Variables ---
int snd_musicdevice = 0;
int snd_sfxdevice = 0;

// --- Sound Interface Stubs ---
void I_InitSound(void) {}
void I_UpdateSound(void) {}
void I_SubmitSound(void) {}
void I_ShutdownSound(void) {}
void I_SetChannels(void) {}
void I_BindSoundVariables(void) {}
void I_UpdateSoundParams(int handle, int vol, int sep, int pitch) {
  (void)handle;
  (void)vol;
  (void)sep;
  (void)pitch;
}

int I_GetSfxLumpNum(void *sfxinfo) {
  (void)sfxinfo;
  return 0;
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority) {
  (void)id;
  (void)vol;
  (void)sep;
  (void)pitch;
  (void)priority;
  return 0;
}

void I_StopSound(int handle) { (void)handle; }

int I_SoundIsPlaying(int handle) {
  (void)handle;
  return 0;
}

void I_PrecacheSounds(void) {}

// --- Music Interface Stubs ---
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) { (void)volume; }
void I_PauseSong(int handle) { (void)handle; }
void I_ResumeSong(int handle) { (void)handle; }
int I_RegisterSong(void *data, int len) {
  (void)data;
  (void)len;
  return 1;
}
void I_PlaySong(int handle, int looping) {
  (void)handle;
  (void)looping;
}
void I_StopSong(int handle) { (void)handle; }
void I_UnRegisterSong(int handle) { (void)handle; }

void doomgeneric_Create(int argc, char **argv);

int main(int argc, char **argv) {
  doomgeneric_Create(argc, argv);
  while (1) {
    doomgeneric_Tick();

    yield();
  }
  return 0;
}
