#ifndef __FFPLAY_PARSE_H__
#define __FFPLAY_PARSE_H__

// Pure text helpers of ffplay_engine.c, split out so they can be unit-tested
// on the host: see tests/test_ffplay_parse.c.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Escape a value for inclusion inside single quotes in an ffmpeg filtergraph.
// Inside '...' only the closing quote is special, so a literal ' must be written
// as '\'' (close-quote, backslash-quote, reopen-quote). Without this, a subtitle
// path containing an apostrophe (e.g. "my'movie.srt") ends the quoted value early
// and the rest of the filename is parsed as filtergraph syntax, breaking playback.
static inline void FfplayParse_escapeFilterSquote(const char* in, char* out, size_t out_size) {
	if (out_size == 0)
		return;
	size_t j = 0;
	for (size_t i = 0; in[i] && j + 4 < out_size; i++) {
		if (in[i] == '\'') {
			out[j++] = '\'';
			out[j++] = '\\';
			out[j++] = '\'';
			out[j++] = '\'';
		} else {
			out[j++] = in[i];
		}
	}
	out[j] = '\0';
}

// ffplay/ffmpeg exits 0 even when it fails to OPEN the input (e.g. "Invalid
// data found when processing input", "Server returned 403 Forbidden"). On a
// fatal open failure it prints a line beginning with the input URL followed by
// ':'. True for such a line of the captured stderr — distinct from benign
// mid-stream warnings like "[https @ ...] Stream ends prematurely" which a
// working stream may print.
static inline bool FfplayParse_isFatalLine(const char* line, const char* url) {
	size_t urllen = strlen(url);
	return urllen > 0 && strncmp(line, url, urllen) == 0 && line[urllen] == ':';
}

#endif
