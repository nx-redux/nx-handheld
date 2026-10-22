#include "ratools_scan.h"
#include "../common/folder_tag.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

// "Game Boy (GB)" -> "GB"
bool rat_dir_tag(const char* dirname, char* tag, size_t n) {
	return folderTag(dirname, tag, n, FOLDER_TAG_TRUNCATE);
}

bool rat_skip_extension(const char* name) {
	const char* ext = strrchr(name, '.');
	if (!ext)
		return true;
	ext++;
	// raw/sub are only ever disc tracks (CD-DA / subchannel), never a game
	static const char* deny[] = {"txt", "dat", "png", "jpg", "jpeg", "bmp", "xml",
								 "db", "sav", "srm", "st", "cfg", "log", "bak",
								 "raw", "sub", NULL};
	for (int i = 0; deny[i]; i++)
		if (strcasecmp(ext, deny[i]) == 0)
			return true;
	return false;
}

// true if `name` is a raw disc track (bin/img/raw/sub) that belongs to a
// cue/gdi/ccd/toc index rather than being a game on its own.
bool rat_is_track_file(const char* name) {
	const char* ext = strrchr(name, '.');
	if (!ext)
		return false;
	ext++;
	static const char* tracks[] = {"bin", "img", "raw", "sub", NULL};
	for (int i = 0; tracks[i]; i++)
		if (strcasecmp(ext, tracks[i]) == 0)
			return true;
	return false;
}

bool rat_is_archive(const char* name) {
	const char* ext = strrchr(name, '.');
	return ext && (!strcasecmp(ext, ".zip") || !strcasecmp(ext, ".7z"));
}

bool rat_entry_is_rom(const char* name, size_t n) {
	size_t len = strlen(name);
	if (!len || name[len - 1] == '/' || len >= n)
		return false;
	const char* base = strrchr(name, '/');
	base = base ? base + 1 : name;
	return !rat_skip_extension(base) && !rat_is_archive(base);
}

bool rat_7z_pick_entry(FILE* listing, char* entry, size_t n, long long* size) {
	bool found = false;
	char line[512], name[256] = {0};
	long long sz = 0;
	bool dir = false;
	// the trailing "" flushes the last entry
	while (!found) {
		bool more = fgets(line, sizeof(line), listing) != NULL;
		line[strcspn(line, "\r\n")] = '\0';
		if (!more || !line[0]) {
			if (!dir && rat_entry_is_rom(name, n)) {
				snprintf(entry, n, "%s", name);
				*size = sz;
				found = true;
			}
			name[0] = '\0';
			sz = 0;
			dir = false;
			if (!more)
				break;
		} else if (!strncmp(line, "Path = ", 7)) {
			if (strlen(line + 7) < sizeof(name) && strlen(line + 7) < n)
				snprintf(name, sizeof(name), "%s", line + 7);
		} else if (!strncmp(line, "Size = ", 7)) {
			sz = atoll(line + 7);
		} else if (!strncmp(line, "Attributes = ", 13)) {
			dir = strchr(line + 13, 'D') != NULL;
		}
	}
	return found;
}

void rat_launch_core(const char* launch_path, char* out, size_t n) {
	out[0] = '\0';
	FILE* f = fopen(launch_path, "r");
	if (!f)
		return;
	char line[512];
	char fallback[64] = {0};
	while (fgets(line, sizeof(line), f)) {
		const char* p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, "EMU_EXE=", 8) == 0) {
			p += 8;
			if (*p == '"' || *p == '\'')
				p++;
			size_t len = strcspn(p, "\"' \t\r\n;");
			if (len && len < n && !memchr(p, '$', len)) {
				memcpy(out, p, len);
				out[len] = '\0';
				break;
			}
		}
		const char* so = strstr(line, "_libretro.so");
		if (so && !fallback[0]) {
			const char* b = so;
			while (b > line && b[-1] != '/' && b[-1] != '"' && b[-1] != '\'' && b[-1] != ' ')
				b--;
			size_t len = (size_t)(so - b);
			if (len && len < sizeof(fallback) && !memchr(b, '$', len)) {
				memcpy(fallback, b, len);
				fallback[len] = '\0';
			}
		}
	}
	fclose(f);
	if (!out[0] && fallback[0])
		snprintf(out, n, "%s", fallback);
}
