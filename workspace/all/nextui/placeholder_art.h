// placeholder_art.h — an abstract picture for a game without a screenshot. SDL-free.
//
// Seeded by the game's name, so each game gets its own and keeps it: a muted two-hue diagonal gradient, two soft
// glows, faint diagonal lines and a vignette. Dark enough that the white captions drawn over it read.
#ifndef PLACEHOLDER_ART_H
#define PLACEHOLDER_ART_H

#include <stdint.h>

// 32-bit FNV-1a of `seed` (NULL = ""): what places the shapes, and the name of a placeholder saved to disk.
uint32_t PlaceholderArt_seedHash(const char* seed);
// Fill w×h opaque ARGB8888 pixels (`pitch` pixels per row) for `seed` (any string; NULL = "").
void PlaceholderArt_render(uint32_t* px, int w, int h, int pitch, const char* seed);
// The same shapes (placed by `seed`) in one colour: rgb's (0xRRGGBB) hue, its saturation scaling the picture's (a grey
// or white rgb gives a neutral one), the same dark values. Home's "Pick a game" card in the accent.
void PlaceholderArt_renderRgb(uint32_t* px, int w, int h, int pitch, const char* seed, uint32_t rgb);

#endif
