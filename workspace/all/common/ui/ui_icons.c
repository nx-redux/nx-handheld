#include "ui_icons.h"
#include "defines.h"
#include <SDL2/SDL_image.h>

static SDL_Surface* empty_icon = NULL;
static SDL_Surface* empty_icon_inv = NULL;
static bool empty_icon_loaded = false;

SDL_Surface* UI_invertIconSurface(SDL_Surface* src) {
	if (!src)
		return NULL;
	SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(
		0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!dst)
		return NULL;
	// reads 32-bit pixels row by row: anything else (an icon whose RGBA32
	// conversion failed) is converted here first
	SDL_Surface* conv = NULL;
	if (src->format->BytesPerPixel != 4) {
		conv = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
		if (!conv) {
			SDL_FreeSurface(dst);
			return NULL;
		}
		src = conv;
	}
	SDL_LockSurface(src);
	SDL_LockSurface(dst);
	for (int y = 0; y < src->h; y++) {
		const Uint32* src_row = (const Uint32*)((const Uint8*)src->pixels + y * src->pitch);
		Uint32* dst_row = (Uint32*)((Uint8*)dst->pixels + y * dst->pitch);
		for (int x = 0; x < src->w; x++) {
			Uint8 r, g, b, a;
			SDL_GetRGBA(src_row[x], src->format, &r, &g, &b, &a);
			dst_row[x] = SDL_MapRGBA(dst->format, 255 - r, 255 - g, 255 - b, a);
		}
	}
	SDL_UnlockSurface(dst);
	SDL_UnlockSurface(src);
	if (conv)
		SDL_FreeSurface(conv);
	return dst;
}

void UI_loadIconPair(const char* path, SDL_Surface** original, SDL_Surface** inverted) {
	*original = IMG_Load(path);
	if (*original) {
		// Convert to RGBA32 for consistent pixel access
		SDL_Surface* converted = SDL_ConvertSurfaceFormat(*original, SDL_PIXELFORMAT_RGBA32, 0);
		if (converted) {
			SDL_FreeSurface(*original);
			*original = converted;
		}
		*inverted = UI_invertIconSurface(*original);
	} else {
		*inverted = NULL;
	}
}

void UI_freeIconPair(SDL_Surface** original, SDL_Surface** inverted) {
	if (*original) {
		SDL_FreeSurface(*original);
		*original = NULL;
	}
	if (*inverted) {
		SDL_FreeSurface(*inverted);
		*inverted = NULL;
	}
}

void UI_initEmptyIcon(void) {
	if (empty_icon_loaded)
		return;
	char icon_empty_path[MAX_PATH];
	snprintf(icon_empty_path, sizeof(icon_empty_path), "%s/icon-empty.png", RES_PATH);
	UI_loadIconPair(icon_empty_path, &empty_icon, &empty_icon_inv);
	empty_icon_loaded = true;
}

void UI_quitEmptyIcon(void) {
	UI_freeIconPair(&empty_icon, &empty_icon_inv);
	empty_icon_loaded = false;
}

SDL_Surface* Icons_getEmpty(bool selected) {
	if (!empty_icon_loaded)
		UI_initEmptyIcon();
	return selected ? empty_icon : empty_icon_inv;
}
