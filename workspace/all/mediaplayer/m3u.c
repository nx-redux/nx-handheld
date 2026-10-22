#define _GNU_SOURCE
#include "m3u.h"
#include "../common/str_trim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Extract attr="value" from an #EXTINF line into out. Returns 1 if found.
static int extinf_attr(const char* line, const char* attr, char* out, int out_sz) {
	char needle[64];
	snprintf(needle, sizeof(needle), "%s=\"", attr);
	const char* p = strstr(line, needle);
	if (!p)
		return 0;
	p += strlen(needle);
	const char* end = strchr(p, '"');
	if (!end)
		return 0;
	int n = (int)(end - p);
	if (n >= out_sz)
		n = out_sz - 1;
	memcpy(out, p, n);
	out[n] = '\0';
	return 1;
}

// Display name = text after the last comma on the #EXTINF line, with the
// surrounding whitespace dropped (a leading space would otherwise sort the
// channel to the top of the list as its own first-letter group).
static void extinf_name(const char* line, char* out, int out_sz) {
	const char* comma = strrchr(line, ',');
	const char* name = comma ? comma + 1 : line;
	while (*name == ' ' || *name == '\t')
		name++;
	snprintf(out, out_sz, "%s", name);
	strTrimRight(out, "\n\r ");
}

// fgets that also swallows the rest of an over-long line, so a line longer
// than the buffer is consumed as ONE (truncated) line. Without this the tail
// of a huge #EXTINF attribute list came back as a separate line and, not
// starting with '#', was taken as the channel URL. Both parser passes use it
// so their line counts agree.
static char* read_line(char* buf, int sz, FILE* f) {
	if (!fgets(buf, sz, f))
		return NULL;
	size_t len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n')
		return buf; // complete line
	if (len < (size_t)(sz - 1))
		return buf; // short final line without a newline (EOF)
	int c;
	while ((c = fgetc(f)) != EOF && c != '\n') {
	}
	return buf;
}

int M3U_parseFile(const char* path, CuratedTVChannel** out, const char* country_code) {
	*out = NULL;

	FILE* f = fopen(path, "r");
	if (!f)
		return -1;

	// First pass: an #EXTINF per channel is the upper bound (some are orphaned
	// -- no URL follows -- so the real count can be lower, never higher).
	char line[2048];
	int max = 0;
	while (read_line(line, sizeof(line), f)) {
		if (strncmp(line, "#EXTINF", 7) == 0)
			max++;
	}
	if (max == 0) {
		fclose(f);
		return 0;
	}

	CuratedTVChannel* channels = malloc(sizeof(CuratedTVChannel) * max);
	if (!channels) {
		fclose(f);
		return -1;
	}

	rewind(f);
	int count = 0;
	bool have_meta = false;
	CuratedTVChannel cur;
	memset(&cur, 0, sizeof(cur));

	while (count < max && read_line(line, sizeof(line), f)) {
		if (strncmp(line, "#EXTINF", 7) == 0) {
			memset(&cur, 0, sizeof(cur));
			char buf[512];
			if (extinf_attr(line, "tvg-logo", buf, sizeof(buf)))
				snprintf(cur.logo, IPTV_MAX_LOGO, "%s", buf);
			if (extinf_attr(line, "group-title", buf, sizeof(buf)))
				snprintf(cur.category, IPTV_MAX_GROUP, "%s", buf);
			extinf_name(line, buf, sizeof(buf));
			snprintf(cur.name, IPTV_MAX_NAME, "%s", buf);
			snprintf(cur.country_code, sizeof(cur.country_code), "%s", country_code ? country_code : "");
			have_meta = true;
		} else if (line[0] == '#') {
			continue; // other directives (#EXTM3U, #EXTVLCOPT, ...)
		} else {
			strTrimRight(line, "\n\r ");
			if (line[0] == '\0')
				continue; // blank
			if (!have_meta)
				continue; // URL with no preceding #EXTINF
			snprintf(cur.url, IPTV_MAX_URL, "%s", line);
			channels[count++] = cur;
			have_meta = false;
		}
	}

	fclose(f);
	if (count == 0) {
		free(channels);
		return 0;
	}
	*out = channels;
	return count;
}
