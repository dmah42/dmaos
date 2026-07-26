#include "doomgeneric.h"
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

int DG_GetKey(int *pressed, unsigned char *key) {
  /* Return 0 for now (no key pressed) until keyboard driver is ready */
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
  /* Initialize and hand off control to Doom! */
  doomgeneric_Create(argc, argv);
  return 0;
}
