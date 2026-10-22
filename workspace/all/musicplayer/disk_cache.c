#include "disk_cache.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
	char name[256];
	time_t mtime;
	long long size;
} CacheFile;

static int by_mtime(const void* a, const void* b) {
	const CacheFile* x = a;
	const CacheFile* y = b;
	if (x->mtime != y->mtime)
		return x->mtime < y->mtime ? -1 : 1;
	return strcmp(x->name, y->name);
}

int DiskCache_trim(const char* dir, int max_files, long long max_bytes) {
	DIR* d = opendir(dir);
	if (!d)
		return 0;

	CacheFile* files = NULL;
	int count = 0, cap = 0;
	long long total = 0;
	char path[1024];
	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.' || strlen(ent->d_name) >= sizeof(files[0].name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		if (count == cap) {
			int ncap = cap ? cap * 2 : 256;
			CacheFile* grown = realloc(files, (size_t)ncap * sizeof(*grown));
			if (!grown)
				break;
			files = grown;
			cap = ncap;
		}
		snprintf(files[count].name, sizeof(files[count].name), "%s", ent->d_name);
		files[count].mtime = st.st_mtime;
		files[count].size = (long long)st.st_size;
		total += files[count].size;
		count++;
	}
	closedir(d);

	int removed = 0;
	if (count > max_files || total > max_bytes) {
		qsort(files, (size_t)count, sizeof(*files), by_mtime);
		for (int i = 0; i < count && (count - removed > max_files || total > max_bytes); i++) {
			snprintf(path, sizeof(path), "%s/%s", dir, files[i].name);
			if (unlink(path) == 0) {
				removed++;
				total -= files[i].size;
			}
		}
	}
	free(files);
	return removed;
}

long DiskCache_size(const char* dir) {
	DIR* d = opendir(dir);
	if (!d)
		return 0;
	long total = 0;
	char path[1024];
	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		struct stat st;
		if (stat(path, &st) == 0)
			total += st.st_size;
	}
	closedir(d);
	return total;
}

void DiskCache_clear(const char* dir) {
	DIR* d = opendir(dir);
	if (!d)
		return;
	char path[1024];
	struct dirent* ent;
	while ((ent = readdir(d))) {
		if (ent->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		unlink(path);
	}
	closedir(d);
}
