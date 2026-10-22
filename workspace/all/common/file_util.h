#ifndef FILE_UTIL_H
#define FILE_UTIL_H

// Lightweight file helpers with no SDL or platform dependencies, shared by the
// small daemons (screenshot, screenrecorder), the pure modules (emu_shortcuts,
// emu_overlay_cfg) and utils.c (whose utils.h includes this). Header-only so
// the ~25 binaries that link utils.c need no extra source file, and the pure
// modules' host tests no utils.c.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

// mkdir -p: create `path` and any missing parents with `mode` (errors, such as
// an existing directory, are ignored). Paths over 511 bytes are truncated.
static inline void mkdir_p_mode(const char* path, mode_t mode) {
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", path);
	if (!tmp[0])
		return;
	for (char* p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, mode);
			*p = '/';
		}
	}
	mkdir(tmp, mode);
}

// Create the directory part of file path `path` (mkdir_p_mode on everything
// before its last '/'). No-op when `path` has no '/' or only a leading one.
static inline void mkdir_parent_mode(const char* path, mode_t mode) {
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	char* slash = strrchr(dir, '/');
	if (slash && slash != dir) {
		*slash = '\0';
		mkdir_p_mode(dir, mode);
	}
}

// Read a whole file into a new NUL-terminated buffer (caller frees). NULL when
// the file can't be opened or sized, is larger than max_size bytes (a cap for
// files from the network or the user), or memory runs out. A short read keeps
// what was read; *out_len (optional) gets its length.
static inline char* allocFileCapped(const char* path, long max_size, long* out_len) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size < 0 || size > max_size) {
		fclose(f);
		return NULL;
	}
	char* buf = malloc((size_t)size + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t got = fread(buf, 1, (size_t)size, f);
	fclose(f);
	buf[got] = '\0';
	if (out_len)
		*out_len = (long)got;
	return buf;
}

#endif // FILE_UTIL_H
