#ifndef P8SHIM_INPUT_H
#define P8SHIM_INPUT_H

// minarch's joypad (ipc->buttons) as the one controller PICO-8 sees. Same buttons as
// fake08's libretro map: B is O, A is X, Start opens the cart's pause menu.

#include <stdint.h>

// RETRO_DEVICE_ID_JOYPAD_* (libretro.h)
#define P8_RETRO_B 0
#define P8_RETRO_START 3
#define P8_RETRO_UP 4
#define P8_RETRO_DOWN 5
#define P8_RETRO_LEFT 6
#define P8_RETRO_RIGHT 7
#define P8_RETRO_A 8

// SDL_CONTROLLER_BUTTON_* (SDL_gamecontroller.h)
#define P8_PAD_A 0
#define P8_PAD_B 1
#define P8_PAD_START 6
#define P8_PAD_UP 11
#define P8_PAD_DOWN 12
#define P8_PAD_LEFT 13
#define P8_PAD_RIGHT 14

// 1 when SDL controller button sdl_button is held in the joypad bitmask buttons
static inline int input_pad_button(uint32_t buttons, int sdl_button) {
	int id;
	switch (sdl_button) {
	case P8_PAD_A: // south face button, PICO-8's O
		id = P8_RETRO_B;
		break;
	case P8_PAD_B: // east face button, PICO-8's X
		id = P8_RETRO_A;
		break;
	case P8_PAD_START:
		id = P8_RETRO_START;
		break;
	case P8_PAD_UP:
		id = P8_RETRO_UP;
		break;
	case P8_PAD_DOWN:
		id = P8_RETRO_DOWN;
		break;
	case P8_PAD_LEFT:
		id = P8_RETRO_LEFT;
		break;
	case P8_PAD_RIGHT:
		id = P8_RETRO_RIGHT;
		break;
	default:
		return 0;
	}
	return (buttons >> id) & 1;
}

#endif
