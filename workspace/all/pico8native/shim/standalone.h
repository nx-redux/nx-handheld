#ifndef P8SHIM_STANDALONE_H
#define P8SHIM_STANDALONE_H

// Standalone mode (P8SHIM_STANDALONE=1, set only by Splore's launcher): PICO-8 runs on the
// device's own SDL with no core, and every SDL call goes straight through, but for the
// sensor flag (shim.c) and Menu, which opens a "Quit Splore?" dialog (dialog.h) drawn
// over the game from SDL_RenderPresent. shim.c's hooks call these in standalone mode.

#include <stdint.h>

// Whether this run is standalone; set once at load
extern int standalone;

void st_present(void* renderer);	// SDL_RenderPresent: may run the dialog first
int st_poll_event(void* event);		// SDL_PollEvent: keeps Menu and hidden buttons from PICO-8
uint32_t st_ticks(void);			// SDL_GetTicks: less the time the dialog was open
void st_audio_opened(uint32_t dev); // the device PICO-8 plays through, paused by the dialog
void st_pad_opened(void* pad);
void st_pad_closed(void* pad);
void st_joystick_opened(void* joy);
uint8_t st_pad_button(void* pad, int button); // SDL_GameControllerGetButton
uint8_t st_joy_button(void* joy, int button); // SDL_JoystickGetButton

#endif
