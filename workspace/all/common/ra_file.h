#ifndef __RA_FILE_H__
#define __RA_FILE_H__

// Whole-file read and atomic write shared by the RetroAchievements modules
// (ra_offline.c, ra_hash_cache.c, ra_badge_path.c). Header-only so each
// host test keeps compiling its module on its own. No SDL.

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Read all of path into a malloc'd, NUL-terminated buffer (*out, caller
// frees; *out_len gets the size when out_len is non-NULL). False for a
// missing or unreadable file, one larger than max bytes, or a short read.
static inline bool RA_File_read(const char* path, size_t max, char** out, size_t* out_len) {
	FILE* f = fopen(path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0 || (size_t)sz > max) {
		fclose(f);
		return false;
	}
	char* buf = (char*)malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return false;
	}
	size_t rd = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	if (rd != (size_t)sz) {
		free(buf);
		return false;
	}
	buf[sz] = '\0';
	*out = buf;
	if (out_len)
		*out_len = (size_t)sz;
	return true;
}

// Write via temp file + fsync + rename so a power cut never leaves a torn
// file. The temp name is per thread: concurrent writers to the same path
// each stage their own complete copy and the last rename wins.
static inline bool RA_File_writeAtomic(const char* path, const void* data, size_t len) {
	char tmp[1152];
	int n = snprintf(tmp, sizeof(tmp), "%s.%lu.tmp", path, (unsigned long)pthread_self());
	if (n < 0 || (size_t)n >= sizeof(tmp))
		return false;
	FILE* f = fopen(tmp, "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, len, f) == len && fflush(f) == 0;
	fsync(fileno(f));
	if (fclose(f) != 0)
		ok = false;
	if (!ok || rename(tmp, path) != 0) {
		remove(tmp);
		return false;
	}
	return true;
}

#endif // __RA_FILE_H__
