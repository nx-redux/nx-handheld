#ifndef HOMEART_MODEL_H
#define HOMEART_MODEL_H

// SDL-free picture math for the Home tab (host-tested).

#include <stdbool.h>
#include <stddef.h>

// pixels as 0xAARRGGBB words, row-major
typedef struct {
	int x, y, w, h;
} HomeArtRect;

// Trim symmetric pure-black letterbox bars (every channel <= 4). Rows trimmed only when the top and bottom bars match
// within max(2 px, 1% of the height); columns likewise. Nothing trimmed when the result would be under a quarter of
// the frame in either axis. Returns the kept rect (the full frame when nothing is trimmed).
HomeArtRect HomeArt_trimLetterbox(const unsigned* px, int w, int h, int pitch_words);

// Whether a frame shows nothing: at least 98% of the pixels in `keep` (the trimmed rect, inside the frame) are black,
// every channel <= 4. An empty rect or no pixels count as blank. A blank resume frame (a black save-state preview) is
// treated as missing, so Continue falls back to the screenshot.
bool HomeArt_isBlankFrame(const unsigned* px, HomeArtRect keep, int pitch_words);

// Where a crop-to-fill frames a w×h picture (0 = its top, 1 = its bottom): a picture taller than wide (a Nintendo DS
// screenshot stacks its two screens, 256×384) shows its top, so the top screen fills the tile; any other keeps dflt.
float HomeArt_frameY(int w, int h, float dflt);

// 2x2 box-filter halving of `keep` (inside px, pitch spitch words) into dst (keep.w/2 x keep.h/2, pitch dpitch words;
// an odd last row or column is left out): each channel the rounded mean of its four, so a bilinear pass after it
// never skips source pixels.
void HomeArt_halve(const unsigned* px, int spitch, HomeArtRect keep, unsigned* dst, int dpitch);

// The crop-to-fill scale of a keep_w×keep_h picture covering w×h, times zoom.
float HomeArt_coverScale(int keep_w, int keep_h, int w, int h, float zoom);

// Where the w×h window sits in the picture scaled by s: centred across, frame_y of the way down the vertical
// overflow. With frame_y = 0.4 the source's 40% line lands on the window's 40% line at any scale.
void HomeArt_coverOrigin(int keep_w, int keep_h, int w, int h, float s, float frame_y, float* ox, float* oy);

// The Home List's background pictures travel through the thumbnail loader as paths that are never a file's
// ("\x01home:" leads): "cont\n<preview>\n<rom>" (a resume frame) or "abs\n<rom>" (the abstract picture).
#define HOMEART_LIST_PREFIX "\x01home:"

// Encode one into out; false (out = "") when it doesn't fit size.
bool HomeArt_listPathCont(const char* preview, const char* rom, char* out, size_t size);
bool HomeArt_listPathAbs(const char* rom, char* out, size_t size);

typedef enum {
	HOMEART_LIST_NONE = 0, // not a list path, or a malformed one
	HOMEART_LIST_CONT,	   // preview (cut to preview_size) and *rom set
	HOMEART_LIST_ABS,	   // *rom set
} HomeArtListKind;

HomeArtListKind HomeArt_parseListPath(const char* path, char* preview, size_t preview_size, const char** rom);

// An unselected game's picture (nx-mobile's "drained" look): each pixel of the w×h block (pitch_words a row) in place
// to its Rec. 709 luminance times level/256 (256: grey at full brightness; 64: grey at 25%, as a 25% picture over
// black), alpha kept. A premultiplied pixel stays premultiplied (the grey of a premultiplied colour is the
// premultiplied grey).
#define HOMEART_DRAIN_LEVEL 64 // an unselected game's picture: grey at 25% (nx-mobile's DRAIN_PICTURE_ALPHA)
#define HOMEART_GREY_LEVEL 256 // a Backdrop's unselected box art: grey at full brightness (backdropSaturation)
void HomeArt_drain(unsigned* px, int w, int h, int pitch_words, int level);

#endif // HOMEART_MODEL_H
