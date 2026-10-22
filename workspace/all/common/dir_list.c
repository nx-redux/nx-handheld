#include "dir_list.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

// Directories first, then case-insensitive by name.
static int compare_entries(const void* a, const void* b) {
	const DirListEntry* ea = (const DirListEntry*)a;
	const DirListEntry* eb = (const DirListEntry*)b;
	if (ea->is_dir != eb->is_dir)
		return ea->is_dir ? -1 : 1;
	return strcasecmp(ea->name, eb->name);
}

// Stat a listed name; false for hidden names, too-long paths and stat failures.
static bool stat_entry(const char* dir, const char* name, char* full_path, size_t full_sz, struct stat* st) {
	if (name[0] == '.')
		return false;
	int len = snprintf(full_path, full_sz, "%s/%s", dir, name);
	if (len < 0 || len >= (int)full_sz)
		return false;
	return stat(full_path, st) == 0;
}

DirListEntry* DirList_load(const char* path, const char* root, DirListFilter accept, int* count) {
	*count = 0;
	DIR* dir = opendir(path);
	if (!dir)
		return NULL;

	// First pass: count the rows.
	int total = 0;
	struct dirent* ent;
	char full_path[1024];
	struct stat st;
	while ((ent = readdir(dir)) != NULL) {
		if (!stat_entry(path, ent->d_name, full_path, sizeof(full_path), &st))
			continue;
		if (S_ISDIR(st.st_mode) || accept(ent->d_name))
			total++;
	}

	bool has_parent = strcmp(path, root) != 0;
	if (has_parent)
		total++;

	DirListEntry* entries = malloc(sizeof(DirListEntry) * (total > 0 ? total : 1));
	if (!entries) {
		closedir(dir);
		return NULL;
	}

	int idx = 0;
	if (has_parent) {
		DirListEntry* up = &entries[idx++];
		snprintf(up->name, sizeof(up->name), "..");
		// The parent of `path` as the browser keeps it (its 512-byte copy).
		char current[sizeof(up->path)];
		snprintf(current, sizeof(current), "%s", path);
		char* last_slash = strrchr(current, '/');
		if (last_slash) {
			*last_slash = '\0';
			snprintf(up->path, sizeof(up->path), "%s", current);
		} else {
			snprintf(up->path, sizeof(up->path), "%s", root);
		}
		up->is_dir = true;
		up->size_bytes = 0;
	}

	// Second pass: fill the rows. Bounded by the first pass's count: a file
	// added (or a directory made) in between must not write past entries[].
	rewinddir(dir);
	while (idx < total && (ent = readdir(dir)) != NULL) {
		if (!stat_entry(path, ent->d_name, full_path, sizeof(full_path), &st))
			continue;
		bool is_dir = S_ISDIR(st.st_mode);
		if (!is_dir && !accept(ent->d_name))
			continue;
		DirListEntry* e = &entries[idx++];
		snprintf(e->name, sizeof(e->name), "%s", ent->d_name);
		snprintf(e->path, sizeof(e->path), "%s", full_path);
		e->is_dir = is_dir;
		e->size_bytes = is_dir ? 0 : (long)st.st_size;
	}
	closedir(dir);

	// Sort, keeping ".." on top.
	int sort_start = has_parent ? 1 : 0;
	if (idx > sort_start + 1)
		qsort(&entries[sort_start], idx - sort_start, sizeof(DirListEntry), compare_entries);

	*count = idx;
	return entries;
}
