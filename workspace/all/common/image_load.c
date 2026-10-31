// IMG_Load plus WebP (image_load.h).
#include "image_load.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <formats/image.h>
#include <formats/rwebp.h>

// Whether the file starts RIFF....WEBP.
static bool isWebp(FILE* f) {
	unsigned char head[12];
	return fread(head, 1, sizeof(head), f) == sizeof(head) && memcmp(head, "RIFF", 4) == 0 &&
		   memcmp(head + 8, "WEBP", 4) == 0;
}

// The whole file read into memory; NULL (and *len 0) on a read failure.
static uint8_t* readAll(FILE* f, size_t* len) {
	*len = 0;
	if (fseek(f, 0, SEEK_END) != 0)
		return NULL;
	long size = ftell(f);
	if (size <= 0 || fseek(f, 0, SEEK_SET) != 0)
		return NULL;
	uint8_t* data = malloc((size_t)size);
	if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
		free(data);
		return NULL;
	}
	if (data)
		*len = (size_t)size;
	return data;
}

// rwebp decodes in slices: called until it ends, it leaves 0xAARRGGBB pixels (supports_rgba false), w x h packed.
static SDL_Surface* decodeWebp(uint8_t* data, size_t len) {
	rwebp_t* dec = rwebp_alloc();
	if (!dec)
		return NULL;
	SDL_Surface* out = NULL;
	void* pixels = NULL;
	unsigned w = 0, h = 0;
	int ret = IMAGE_PROCESS_NEXT;
	if (rwebp_set_buf_ptr(dec, data, len)) {
		do
			ret = rwebp_process_image(dec, &pixels, len, &w, &h, false);
		while (ret == IMAGE_PROCESS_NEXT);
	}
	if (ret == IMAGE_PROCESS_END && pixels && w > 0 && h > 0) {
		out = SDL_CreateRGBSurfaceWithFormat(0, (int)w, (int)h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (out)
			for (unsigned y = 0; y < h; y++)
				memcpy((uint8_t*)out->pixels + y * out->pitch, (uint32_t*)pixels + y * w, w * 4);
	}
	free(pixels); // the decoder hands its output over (rwebp_free doesn't free it)
	rwebp_free(dec);
	return out;
}

SDL_Surface* Image_load(const char* path) {
	FILE* f = path ? fopen(path, "rb") : NULL;
	if (!f)
		return NULL;
	if (!isWebp(f)) {
		fclose(f);
		return IMG_Load(path);
	}
	size_t len;
	uint8_t* data = readAll(f, &len);
	fclose(f);
	SDL_Surface* img = data ? decodeWebp(data, len) : NULL;
	free(data);
	return img;
}
