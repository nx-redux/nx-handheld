#include <unistd.h>
#include "utils.h"
#include "recents.h"
#include "path_list.h"
#include "api.h"
#include "menutabs_model.h"

///////////////////////////////////////
// Internal state

static Array* recents = NULL;
static char* recent_alias = NULL;
static HasEmuFunc _hasEmu = NULL;
static HasM3uFunc _hasM3u = NULL;

///////////////////////////////////////
// Lifecycle

void Recents_init(void) {
	recents = Array_new();
}
void Recents_quit(void) {
	if (recents) {
		RecentArray_free(recents);
		recents = NULL;
	}
	free(recent_alias);
	recent_alias = NULL;
}

void Recents_setHasEmu(HasEmuFunc func) {
	_hasEmu = func;
}
void Recents_setHasM3u(HasM3uFunc func) {
	_hasM3u = func;
}

///////////////////////////////////////
// Recent struct methods

static Recent* Recent_new(char* path, char* alias) {
	Recent* self = malloc(sizeof(Recent));
	if (!self)
		return NULL;

	char sd_path[MAX_PATH]; // only need to get emu name
	snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, path);

	char emu_name[MAX_PATH];
	getEmuName(sd_path, emu_name);

	self->path = strdup(path);
	if (!self->path) {
		free(self);
		return NULL;
	}
	self->alias = alias ? strdup(alias) : NULL;
	self->available = _hasEmu ? _hasEmu(emu_name) : 0;
	return self;
}
static void Recent_free(Recent* self) {
	free(self->path);
	if (self->alias)
		free(self->alias);
	free(self);
}

static int RecentArray_indexOf(Array* self, char* str) {
	for (int i = 0; i < self->count; i++) {
		Recent* item = self->items[i];
		if (exactMatch(item->path, str))
			return i;
	}
	return -1;
}
void RecentArray_free(Array* self) {
	for (int i = 0; i < self->count; i++) {
		Recent_free(self->items[i]);
	}
	Array_free(self);
}

///////////////////////////////////////
// Core API

static void Recent_get(void* item, const char** path, const char** label) {
	Recent* recent = item;
	*path = recent->path;
	*label = recent->alias;
}

// Runs on every launch: written atomically, as the pins are
static void Recents_save(void) {
	if (!PathList_save(RECENT_PATH, recents, Recent_get))
		LOG_warn("Recents: couldn't save %s\n", RECENT_PATH);
}

void Recents_add(char* path, char* alias) {
	if (!MenuTabs_underPath(path, SDCARD_PATH)) // only SD paths can be made platform-agnostic
		return;
	path += strlen(SDCARD_PATH); // makes paths platform agnostic
	int id = RecentArray_indexOf(recents, path);
	if (id == -1) { // add
		Recent* recent = Recent_new(path, alias);
		if (!recent)
			return;
		if (recents->count >= MAX_RECENTS) {
			Recent_free(Array_pop(recents));
		}
		if (!Array_unshift(recents, recent)) {
			Recent_free(recent);
			return;
		}
	} else { // refresh the alias even when already at the top (id == 0)
		Recent* existing = recents->items[id];
		if (alias) {
			char* new_alias = strdup(alias);
			if (new_alias) {
				free(existing->alias);
				existing->alias = new_alias;
			}
		}
		for (int i = id; i > 0; i--) { // bump to top
			void* tmp = recents->items[i - 1];
			recents->items[i - 1] = recents->items[i];
			recents->items[i] = tmp;
		}
	}
	Recents_save();
}

int Recents_load(void) {
	LOG_info("hasRecents %s\n", RECENT_PATH);
	int has = 0;
	bool changed = false;
	RecentArray_free(recents);
	recents = Array_new();

	Array* parent_paths = Array_new();
	if (exists(CHANGE_DISC_PATH)) {
		changed = true;
		char sd_path[MAX_PATH];
		getFile(CHANGE_DISC_PATH, sd_path, MAX_PATH);
		if (exists(sd_path) && MenuTabs_underPath(sd_path, SDCARD_PATH)) {
			char* disc_path = sd_path + strlen(SDCARD_PATH); // makes path platform agnostic
			Recent* recent = Recent_new(disc_path, NULL);
			if (!recent)
				goto done_disc;
			if (!Array_push(recents, recent)) {
				Recent_free(recent);
				goto done_disc;
			}
			if (recent->available)
				has += 1;

			char parent_path[MAX_PATH];
			strcpy(parent_path, disc_path);
			char* tmp = strrchr(parent_path, '/');
			if (tmp) {
				tmp[1] = '\0';
				char* dup = strdup(parent_path);
				if (dup && !Array_push(parent_paths, dup))
					free(dup);
			}
		}
	done_disc:
		unlink(CHANGE_DISC_PATH);
	}

	FILE* file = fopen(RECENT_PATH, "r"); // newest at top
	if (file) {
		char line[MAX_PATH * 2]; // "path\talias": each up to a path's length
		char *path, *alias;
		int long_lines = 0;
		while (PathList_nextLine(file, line, sizeof(line), &path, &alias, &long_lines)) {
			if (strlen(path) >= MAX_PATH) {
				changed = true; // no such path on the card (and too long for the buffers below)
				continue;
			}

			char sd_path[MAX_PATH];
			snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, path);
			if (!exists(sd_path)) {
				changed = true;
				continue;
			}
			if (recents->count >= MAX_RECENTS) {
				changed = true;
				continue;
			}
			// this logic replaces an existing disc from a multi-disc game with the last used
			char m3u_path[MAX_PATH];
			if (_hasM3u && _hasM3u(sd_path, m3u_path)) { // TODO: this might tank launch speed
				char parent_path[MAX_PATH];
				strcpy(parent_path, path);
				char* tmp = strrchr(parent_path, '/');
				if (!tmp)
					goto skip_m3u;
				tmp[1] = '\0';

				bool found = false;
				for (int i = 0; i < parent_paths->count; i++) {
					char* parent = parent_paths->items[i];
					if (prefixMatch(parent, parent_path)) {
						found = true;
						break;
					}
				}
				if (found) {
					changed = true;
					continue;
				}

				char* dup = strdup(parent_path);
				if (dup && !Array_push(parent_paths, dup))
					free(dup);
			}

		skip_m3u:;
			Recent* recent = Recent_new(path, alias);
			if (!recent)
				continue;
			if (!Array_push(recents, recent)) {
				Recent_free(recent);
				continue;
			}
			if (recent->available)
				has += 1;
		}
		fclose(file);
		if (long_lines)
			changed = true;
	}

	if (changed)
		Recents_save();

	StringArray_free(parent_paths);
	return has > 0;
}

///////////////////////////////////////
// Access

int Recents_count(void) {
	return recents ? recents->count : 0;
}

Recent* Recents_at(int index) {
	if (!recents || index < 0 || index >= recents->count)
		return NULL;
	return recents->items[index];
}

void Recents_removeAt(int index) {
	if (!recents || index < 0 || index >= recents->count)
		return;
	Recent* recent = recents->items[index];
	Array_remove(recents, recent);
	Recent_free(recent);
	Recents_save();
}

// A rename (a new map.txt alias): replace the entry's stored alias snapshot
// so Recently Played shows the new name before the game's next launch.
void Recents_updateAlias(char* path, char* alias) {
	if (!recents)
		return;
	int index = RecentArray_indexOf(recents, path);
	if (index < 0)
		return;
	Recent* recent = recents->items[index];
	char* dup = strdup(alias);
	if (!dup)
		return;
	free(recent->alias);
	recent->alias = dup;
	Recents_save();
}

///////////////////////////////////////
// Entry conversion

Entry* Recents_entryFromRecent(Recent* recent) {
	if (!recent || !recent->available)
		return NULL;

	char sd_path[MAX_PATH];
	snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, recent->path);
	int type = suffixMatch(".pak", sd_path) ? ENTRY_PAK : ENTRY_ROM; // ???
	return recent->alias ? Entry_newNamed(sd_path, type, recent->alias) : Entry_new(sd_path, type);
}

Array* Recents_getEntries(void) {
	Array* entries = Array_new();
	if (!recents)
		return entries;
	for (int i = 0; i < recents->count; i++) {
		Recent* recent = recents->items[i];
		EntryArray_push(entries, Recents_entryFromRecent(recent));
	}
	return entries;
}

// The newest recent the Continue card can resume: the first available one that is not a .pak (a tool), built with
// the same constructor Recents_getEntries uses. NULL when there is none. The caller owns the Entry.
Entry* Recents_firstRom(void) {
	if (!recents)
		return NULL;
	for (int i = 0; i < recents->count; i++) {
		Recent* recent = recents->items[i];
		if (!recent || !recent->available || suffixMatch(".pak", recent->path)) // Recents_entryFromRecent's .pak test
			continue;
		return Recents_entryFromRecent(recent);
	}
	return NULL;
}

///////////////////////////////////////
// Alias management

void Recents_setAlias(char* alias) {
	// own a copy: callers pass Entry names whose lifetime we don't control
	free(recent_alias);
	recent_alias = alias ? strdup(alias) : NULL;
}
char* Recents_getAlias(void) {
	return recent_alias;
}
