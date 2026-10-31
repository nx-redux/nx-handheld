#ifndef IMAGE_LOAD_H
#define IMAGE_LOAD_H
// IMG_Load plus WebP. The devices' SDL2_image is built without WebP (and there is no libwebp on them), so a WebP
// file (RIFF....WEBP, whatever its name) is decoded with libretro-common's self-contained rwebp: lossy, lossless and
// alpha; an animated one gives its first frame. Every other file goes to IMG_Load. NULL on failure, the caller frees.
#include "sdl.h"

SDL_Surface* Image_load(const char* path);
#endif
