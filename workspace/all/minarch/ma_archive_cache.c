#include "ma_archive_cache.h"
#include <dirent.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// FNV-1a, 64-bit
static uint64_t hash_bytes(uint64_t h, const void* data, size_t len) {
	const unsigned char* p = data;
	for (size_t i = 0; i < len; i++) {
		h ^= p[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

int ArchiveCache_dir(char* out, size_t size, const char* root, const char* archive_path) {
	struct stat st;
	if (stat(archive_path, &st) != 0)
		return -1;

	// size and mtime too, so a replaced archive at the same path is unpacked again
	int64_t stamp[2] = {(int64_t)st.st_size, (int64_t)st.st_mtime};
	uint64_t h = hash_bytes(0xcbf29ce484222325ULL, archive_path, strlen(archive_path));
	h = hash_bytes(h, stamp, sizeof(stamp));

	int n = snprintf(out, size, "%s/%016llx", root, (unsigned long long)h);
	return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

int ArchiveCache_hasExtension(const char* name, char** extensions) {
	size_t len = strlen(name);
	for (int e = 0; extensions[e]; e++) {
		size_t ext_len = strlen(extensions[e]);
		if (len > ext_len + 1 && name[len - ext_len - 1] == '.' && !strcasecmp(name + len - ext_len, extensions[e]))
			return 1;
	}
	return 0;
}

int ArchiveCache_find(const char* dir, char** extensions, char* out, size_t size) {
	DIR* d = opendir(dir);
	if (!d)
		return 0;

	int found = 0;
	struct dirent* entry;
	while (!found && (entry = readdir(d))) {
		if (entry->d_name[0] == '.' || !ArchiveCache_hasExtension(entry->d_name, extensions))
			continue;
		int n = snprintf(out, size, "%s/%s", dir, entry->d_name);
		if (n < 0 || (size_t)n >= size)
			continue;
		struct stat st;
		found = stat(out, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
	}
	closedir(d);
	if (!found && size)
		out[0] = '\0';
	return found;
}

// rm -rf for the small trees under /tmp/nextarch (no symlinks are followed)
static void remove_tree(const char* path) {
	struct stat st;
	if (lstat(path, &st) != 0)
		return;
	if (S_ISDIR(st.st_mode)) {
		DIR* d = opendir(path);
		if (d) {
			struct dirent* entry;
			char child[1024];
			while ((entry = readdir(d))) {
				if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
					continue;
				int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
				if (n > 0 && (size_t)n < sizeof(child))
					remove_tree(child);
			}
			closedir(d);
		}
		rmdir(path);
	} else {
		unlink(path);
	}
}

int ArchiveCache_evictOthers(const char* root, const char* keep) {
	DIR* tags = opendir(root);
	if (!tags)
		return 0;

	int removed = 0;
	struct dirent* tag;
	char tag_dir[1024];
	char path[1024];
	while ((tag = readdir(tags))) {
		if (!strcmp(tag->d_name, ".") || !strcmp(tag->d_name, ".."))
			continue;
		int n = snprintf(tag_dir, sizeof(tag_dir), "%s/%s", root, tag->d_name);
		if (n < 0 || (size_t)n >= sizeof(tag_dir))
			continue;
		struct stat st;
		if (lstat(tag_dir, &st) != 0)
			continue;
		if (!S_ISDIR(st.st_mode)) {
			unlink(tag_dir);
			removed++;
			continue;
		}
		DIR* d = opendir(tag_dir);
		if (!d)
			continue;
		struct dirent* entry;
		while ((entry = readdir(d))) {
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;
			n = snprintf(path, sizeof(path), "%s/%s", tag_dir, entry->d_name);
			if (n < 0 || (size_t)n >= sizeof(path) || (keep && !strcmp(path, keep)))
				continue;
			remove_tree(path);
			removed++;
		}
		closedir(d);
	}
	closedir(tags);
	return removed;
}

int ArchiveCache_fits(unsigned long long available, unsigned long long needed) {
	return available >= ARCHIVE_CACHE_RESERVE && available - ARCHIVE_CACHE_RESERVE >= needed;
}

unsigned long long ArchiveCache_available(const char* path) {
	struct statvfs sv;
	if (statvfs(path, &sv) != 0)
		return 0;
	return (unsigned long long)sv.f_bavail * (unsigned long long)sv.f_frsize;
}

long long ArchiveCache_sum7zListing(FILE* listing) {
	char line[1024];
	long long total = 0;
	int found = 0;
	while (fgets(line, sizeof(line), listing)) {
		// "Physical Size = " / "Packed Size = " lines don't start with "Size"
		if (strncmp(line, "Size = ", 7) != 0)
			continue;
		char* end;
		long long v = strtoll(line + 7, &end, 10);
		if (end == line + 7 || v < 0)
			continue;
		total += v;
		found = 1;
	}
	return found ? total : -1;
}
