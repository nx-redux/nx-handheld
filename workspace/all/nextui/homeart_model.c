#include "homeart_model.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAR_MAX 4 // a channel at or under this counts as black

static bool isBlack(unsigned p) {
	return ((p >> 16) & 0xFF) <= BAR_MAX && ((p >> 8) & 0xFF) <= BAR_MAX && (p & 0xFF) <= BAR_MAX;
}

static bool rowBlack(const unsigned* px, int w, int pitch, int y) {
	const unsigned* row = px + (long)y * pitch;
	for (int x = 0; x < w; x++)
		if (!isBlack(row[x]))
			return false;
	return true;
}

static bool colBlack(const unsigned* px, int h, int pitch, int x) {
	for (int y = 0; y < h; y++)
		if (!isBlack(px[(long)y * pitch + x]))
			return false;
	return true;
}

// Accept a lead/trail bar pair along an axis of `len` px: symmetric within max(2, 1%) and leaving at least a quarter.
static bool barsAccepted(int lead, int trail, int len) {
	int tol = len / 100 > 2 ? len / 100 : 2;
	return abs(lead - trail) <= tol && len - lead - trail >= len / 4;
}

HomeArtRect HomeArt_trimLetterbox(const unsigned* px, int w, int h, int pitch_words) {
	HomeArtRect r = {0, 0, w, h};
	if (!px || w <= 0 || h <= 0)
		return r;

	int top = 0, bottom = 0;
	while (top < h && rowBlack(px, w, pitch_words, top))
		top++;
	while (bottom < h - top && rowBlack(px, w, pitch_words, h - 1 - bottom))
		bottom++;
	if (top < h && (top || bottom) && barsAccepted(top, bottom, h)) {
		r.y = top;
		r.h = h - top - bottom;
	}

	int left = 0, right = 0;
	while (left < w && colBlack(px, h, pitch_words, left))
		left++;
	while (right < w - left && colBlack(px, h, pitch_words, w - 1 - right))
		right++;
	if (left < w && (left || right) && barsAccepted(left, right, w)) {
		r.x = left;
		r.w = w - left - right;
	}
	return r;
}

bool HomeArt_isBlankFrame(const unsigned* px, HomeArtRect keep, int pitch_words) {
	if (!px || keep.w <= 0 || keep.h <= 0)
		return true;
	long total = (long)keep.w * keep.h;
	long allowed = total / 50; // up to 2% lit
	long lit = 0;
	for (int y = keep.y; y < keep.y + keep.h; y++) {
		const unsigned* row = px + (long)y * pitch_words;
		for (int x = keep.x; x < keep.x + keep.w; x++)
			if (!isBlack(row[x]) && ++lit > allowed)
				return false;
	}
	return true;
}

float HomeArt_frameY(int w, int h, float dflt) {
	return h > w ? 0.0f : dflt;
}

void HomeArt_halve(const unsigned* px, int spitch, HomeArtRect keep, unsigned* dst, int dpitch) {
	int w = keep.w / 2, h = keep.h / 2;
	for (int y = 0; y < h; y++) {
		const unsigned* r0 = px + (long)(keep.y + 2 * y) * spitch + keep.x;
		const unsigned* r1 = r0 + spitch;
		for (int x = 0; x < w; x++) {
			unsigned a = r0[2 * x], b = r0[2 * x + 1], c = r1[2 * x], d = r1[2 * x + 1];
			// two channels a word (0x00RR00BB, 0x00AA00GG): a lane's sum of four <= 1022 never carries over
			unsigned rb = (a & 0x00FF00FFu) + (b & 0x00FF00FFu) + (c & 0x00FF00FFu) + (d & 0x00FF00FFu) + 0x00020002u;
			unsigned ag = ((a >> 8) & 0x00FF00FFu) + ((b >> 8) & 0x00FF00FFu) + ((c >> 8) & 0x00FF00FFu) +
						  ((d >> 8) & 0x00FF00FFu) + 0x00020002u;
			dst[(long)y * dpitch + x] = ((rb >> 2) & 0x00FF00FFu) | (((ag >> 2) & 0x00FF00FFu) << 8);
		}
	}
}

float HomeArt_coverScale(int keep_w, int keep_h, int w, int h, float zoom) {
	float sx = (float)w / keep_w, sy = (float)h / keep_h;
	return (sx > sy ? sx : sy) * zoom;
}

void HomeArt_coverOrigin(int keep_w, int keep_h, int w, int h, float s, float frame_y, float* ox, float* oy) {
	*ox = 0.5f * (keep_w * s - w);
	*oy = frame_y * (keep_h * s - h);
}

bool HomeArt_listPathCont(const char* preview, const char* rom, char* out, size_t size) {
	int n = snprintf(out, size, HOMEART_LIST_PREFIX "cont\n%s\n%s", preview, rom);
	if (n < 0 || (size_t)n >= size) {
		out[0] = '\0';
		return false;
	}
	return true;
}

bool HomeArt_listPathAbs(const char* rom, char* out, size_t size) {
	int n = snprintf(out, size, HOMEART_LIST_PREFIX "abs\n%s", rom);
	if (n < 0 || (size_t)n >= size) {
		out[0] = '\0';
		return false;
	}
	return true;
}

HomeArtListKind HomeArt_parseListPath(const char* path, char* preview, size_t preview_size, const char** rom) {
	if (!path || strncmp(path, HOMEART_LIST_PREFIX, sizeof(HOMEART_LIST_PREFIX) - 1) != 0)
		return HOMEART_LIST_NONE;
	const char* p = path + sizeof(HOMEART_LIST_PREFIX) - 1;
	if (strncmp(p, "abs\n", 4) == 0) {
		*rom = p + 4;
		return HOMEART_LIST_ABS;
	}
	if (strncmp(p, "cont\n", 5) != 0)
		return HOMEART_LIST_NONE;
	p += 5;
	const char* nl = strchr(p, '\n');
	if (!nl)
		return HOMEART_LIST_NONE;
	snprintf(preview, preview_size, "%.*s", (int)(nl - p), p);
	*rom = nl + 1;
	return HOMEART_LIST_CONT;
}

void HomeArt_drain(unsigned* px, int w, int h, int pitch_words, int level) {
	if (!px || w <= 0 || h <= 0)
		return;
	if (level < 0)
		level = 0;
	if (level > 256)
		level = 256;
	for (int y = 0; y < h; y++) {
		unsigned* row = px + (size_t)y * pitch_words;
		for (int x = 0; x < w; x++) {
			unsigned c = row[x];
			// 0.2126, 0.7152, 0.0722 in 256ths (54 + 183 + 19 = 256: white stays white)
			unsigned l = (54u * ((c >> 16) & 0xFFu) + 183u * ((c >> 8) & 0xFFu) + 19u * (c & 0xFFu) + 128u) >> 8;
			l = (l * (unsigned)level + 128u) >> 8;
			row[x] = (c & 0xFF000000u) | (l << 16) | (l << 8) | l;
		}
	}
}
