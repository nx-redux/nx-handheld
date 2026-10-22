// content.c's and the Game Switcher's naming rules, SDL-free so they can be host-tested
// (common/tests/test_entry_names.c).

#include "entry_names.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "defines.h"
#include "utils.h"

int EntryNames_indexChar(const char* name) {
	int c = tolower((unsigned char)name[0]);
	if (c >= 'a' && c <= 'z')
		return (c - 'a') + 1;
	return c > 'z' ? 27 : 0;
}

int EntryNames_alphaStep(IntArray* alphas, int* alpha, int* index, int row, const char* name) {
	int a = EntryNames_indexChar(name);
	if (a != *alpha) {
		if (alphas->count < INT_ARRAY_MAX) {
			*index = alphas->count;
			IntArray_push(alphas, row);
		}
		*alpha = a;
	}
	return *index;
}

void EntryNames_fileStem(const char* path, char* out) {
	const char* base = baseName(path);
	snprintf(out, MAX_PATH, "%s", base);
	char* tmp;
	while ((tmp = strrchr(out, '.')) != NULL) {
		int len = strlen(tmp);
		if (len > 2 && len <= 5)
			tmp[0] = '\0'; // 1-4 letter extension plus dot
		else
			break;
	}
	if (out[0] == '\0')
		snprintf(out, MAX_PATH, "%s", base); // stripping ate everything; restore the basename
}

Hash* EntryNames_readMap(const char* map_path) {
	FILE* file = fopen(map_path, "r");
	if (!file)
		return NULL;
	Hash* map = Hash_new();
	char line[MAX_PATH];
	while (fgets(line, sizeof(line), file) != NULL) {
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (strlen(line) == 0)
			continue; // skip empty lines

		char* tmp = strchr(line, '\t');
		if (tmp) {
			tmp[0] = '\0';
			char* key = line;
			char* value = tmp + 1;
			// the name ends at the next tab (a stray third column) and loses trailing blanks and a lone CR: the
			// caches are tab-separated, and a tab in a name made the Consoles cache unreadable (a rescan every boot)
			value[strcspn(value, "\t")] = '\0';
			size_t len = strlen(value);
			while (len > 0 && isspace((unsigned char)value[len - 1]))
				value[--len] = '\0';
			if (len > 0) // an empty name would show as a blank row
				Hash_set(map, key, value);
		}
	}
	fclose(file);
	return map;
}

EntryNamesRun EntryNames_runInfo(Array* entries, int i, int j) {
	EntryNamesRun run = {true, false, false};
	Entry* entry = entries->items[i];
	char first_tag[MAX_PATH];
	getEmuName(entry->path, first_tag);
	const char* first_file = baseName(entry->path);
	for (int k = i + 1; k < j; k++) {
		Entry* e = entries->items[k];
		if (!exactMatch(baseName(e->path), first_file))
			run.same_file = false;
		char tag[MAX_PATH];
		getEmuName(e->path, tag);
		if (!exactMatch(tag, first_tag))
			run.tags_differ = true;
	}

	// Prefer the extensionless stem for the "files differ" labels, but only if every stem in the run is still
	// distinct; if two collide (e.g. "Tetris.gb" vs "Tetris.gbc") the run keeps full filenames. Runs are short, so
	// an O(n^2) pairwise compare is fine.
	if (!run.same_file) {
		run.stems_unique = true;
		for (int k = i; k < j && run.stems_unique; k++) {
			char stem_k[MAX_PATH];
			EntryNames_fileStem(((Entry*)entries->items[k])->path, stem_k);
			for (int m = k + 1; m < j; m++) {
				char stem_m[MAX_PATH];
				EntryNames_fileStem(((Entry*)entries->items[m])->path, stem_m);
				if (exactMatch(stem_k, stem_m)) {
					run.stems_unique = false;
					break;
				}
			}
		}
	}
	return run;
}

// "<name> (<tag>)": a per-core copy of one rom
static void uniqueName(Entry* entry, char* out_name) {
	char* slash = strrchr(entry->path, '/');
	if (!slash)
		return;
	char emu_tag[MAX_PATH];
	getEmuName(entry->path, emu_tag);
	snprintf(out_name, MAX_PATH, "%s (%s)", entry->name, emu_tag);
}

void EntryNames_labelRun(Array* entries, int i, int j, EntryNamesRun run) {
	for (int k = i; k < j; k++) {
		Entry* e = entries->items[k];
		free(e->unique);
		e->unique = NULL;
		char buf[MAX_PATH] = {0};
		if (run.same_file) {
			uniqueName(e, buf);
		} else {
			char stem[MAX_PATH];
			EntryNames_fileStem(e->path, stem);
			const char* label = run.stems_unique ? stem : baseName(e->path);
			if (run.tags_differ) {
				char tag[MAX_PATH];
				getEmuName(e->path, tag);
				snprintf(buf, sizeof(buf), "%s (%s)", label, tag);
			} else {
				snprintf(buf, sizeof(buf), "%s", label);
			}
		}
		e->unique = strdup(buf);
	}
}

void EntryNames_labelSwitcher(Array* entries) {
	for (int i = 0; i < entries->count; i++) {
		Entry* e = entries->items[i];
		bool dup = false, same_tag = false;
		char tag[MAX_PATH];
		getEmuName(e->path, tag);
		for (int j = 0; j < entries->count; j++) {
			Entry* o = entries->items[j];
			if (j == i || !exactMatch(e->name, o->name))
				continue;
			dup = true;
			char other[MAX_PATH];
			getEmuName(o->path, other);
			if (exactMatch(tag, other))
				same_tag = true;
		}
		if (!dup)
			continue;
		char label[MAX_PATH], buf[MAX_PATH * 2 + 4];
		if (same_tag)
			snprintf(label, sizeof(label), "%s", baseName(e->path));
		else
			snprintf(label, sizeof(label), "%s", tag);
		snprintf(buf, sizeof(buf), "%s (%s)", e->name, label);
		free(e->unique);
		e->unique = strdup(buf);
	}
}
