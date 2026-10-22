#include "lrc_parse.h"
#include <stdlib.h>
#include <string.h>

// Most timestamps read from one LRC line
#define LRC_MAX_TAGS_PER_LINE 32

// Reads one "[mm:ss]" / "[mm:ss.f]" tag at p, ending before eol. Returns its
// time in ms with *after just past the ']', or -1 when p holds no timestamp.
static int lrc_tag_ms(const char* p, const char* eol, const char** after) {
	if (p >= eol || *p != '[')
		return -1;
	const char* q = p + 1;
	if (q >= eol || *q < '0' || *q > '9')
		return -1;
	long mm = 0;
	while (q < eol && *q >= '0' && *q <= '9' && mm < 100000)
		mm = mm * 10 + (*q++ - '0');
	if (q >= eol || *q != ':')
		return -1;
	q++;
	if (q >= eol || *q < '0' || *q > '9')
		return -1;
	long ss = 0;
	while (q < eol && *q >= '0' && *q <= '9' && ss < 100000)
		ss = ss * 10 + (*q++ - '0');
	long ms = 0;
	if (q < eol && (*q == '.' || *q == ':')) { // some files write mm:ss:xx
		q++;
		int digits = 0;
		while (q < eol && *q >= '0' && *q <= '9') {
			if (digits < 3) {
				ms = ms * 10 + (*q - '0');
				digits++;
			}
			q++;
		}
		for (; digits > 0 && digits < 3; digits++)
			ms *= 10;
	}
	// tolerate stray characters before the ']'
	while (q < eol && *q != ']')
		q++;
	if (q >= eol)
		return -1;
	*after = q + 1;
	long t = mm * 60000 + ss * 1000 + ms;
	return t > 0x7FFFFFFF ? 0x7FFFFFFF : (int)t;
}

int LrcParse_text(const char* lrc_text, LyricLine* lines, int max_lines) {
	int count = 0;
	const char* p = lrc_text;

	while (*p && count < max_lines) {
		// Skip whitespace/newlines
		while (*p == '\n' || *p == '\r' || *p == ' ')
			p++;
		if (!*p)
			break;
		const char* eol = p + strcspn(p, "\r\n");

		int times[LRC_MAX_TAGS_PER_LINE];
		int tag_count = 0;
		const char* text = p;
		const char* after;
		int t;
		while ((t = lrc_tag_ms(text, eol, &after)) >= 0) {
			if (tag_count < LRC_MAX_TAGS_PER_LINE)
				times[tag_count++] = t;
			text = after;
		}

		// Not a timestamp line (metadata like [ar:Artist]) or no lyric text
		size_t len = (size_t)(eol - text);
		if (tag_count > 0 && len > 0) {
			if (len > sizeof(lines[0].text) - 1) {
				len = sizeof(lines[0].text) - 1;
				// don't cut a UTF-8 character in half
				while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80)
					len--;
			}
			for (int i = 0; i < tag_count && count < max_lines; i++) {
				lines[count].time_ms = times[i];
				memcpy(lines[count].text, text, len);
				lines[count].text[len] = '\0';
				count++;
			}
		}
		p = eol;
	}

	// Lines with several timestamps come out of order: sort by time, keeping
	// the file order for equal times (insertion sort: stable, small arrays)
	for (int i = 1; i < count; i++) {
		if (lines[i].time_ms >= lines[i - 1].time_ms)
			continue;
		LyricLine tmp = lines[i];
		int j = i - 1;
		while (j >= 0 && lines[j].time_ms > tmp.time_ms) {
			lines[j + 1] = lines[j];
			j--;
		}
		lines[j + 1] = tmp;
	}
	return count;
}
