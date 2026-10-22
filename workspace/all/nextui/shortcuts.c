#include "shortcuts.h"
#include "api.h"
#include "defines.h"
#include "menutabs_model.h"
#include "path_list.h"
#include "utils.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

///////////////////////////////////////
// Internal types

typedef struct Shortcut {
	char* path; // without SDCARD_PATH prefix
	char* name; // display name
} Shortcut;

///////////////////////////////////////
// Shortcut functions

static void Shortcut_free(Shortcut* self) {
	free(self->path);
	if (self->name)
		free(self->name);
	free(self);
}

static Shortcut* Shortcut_new(char* path, char* name) {
	Shortcut* self = malloc(sizeof(Shortcut));
	if (!self)
		return NULL;
	self->path = strdup(path);
	self->name = name ? strdup(name) : NULL;
	if (!self->path || (name && !self->name)) {
		Shortcut_free(self);
		return NULL;
	}
	return self;
}

// Appends a fresh Shortcut_new result; false (freeing it) when it's NULL or the array can't grow.
static bool ShortcutArray_push(Array* self, Shortcut* shortcut) {
	if (!shortcut)
		return false;
	if (Array_push(self, shortcut))
		return true;
	Shortcut_free(shortcut);
	return false;
}

static int ShortcutArray_indexOf(Array* self, const char* path) {
	for (int i = 0; i < self->count; i++) {
		Shortcut* shortcut = self->items[i];
		if (exactMatch(shortcut->path, path))
			return i;
	}
	return -1;
}

static void ShortcutArray_free(Array* self) {
	for (int i = 0; i < self->count; i++) {
		Shortcut_free(self->items[i]);
	}
	Array_free(self);
}

///////////////////////////////////////
// Global state

static Array* shortcuts = NULL;

///////////////////////////////////////
// Save/Load functions

static void Shortcut_get(void* item, const char** path, const char** label) {
	Shortcut* shortcut = item;
	*path = shortcut->path;
	*label = shortcut->name;
}

static void saveShortcuts(void) {
	if (!PathList_save(SHORTCUTS_PATH, shortcuts, Shortcut_get))
		LOG_warn("Shortcuts: couldn't save %s\n", SHORTCUTS_PATH);
}

static int loadShortcuts(void) {
	if (shortcuts) {
		ShortcutArray_free(shortcuts);
	}
	shortcuts = Array_new();
	bool removed_any = false;

	FILE* file = fopen(SHORTCUTS_PATH, "r");
	if (file) {
		char line[MAX_PATH * 2]; // "path\tname": each up to a path's length
		char *path, *name;
		while (PathList_nextLine(file, line, sizeof(line), &path, &name, NULL)) {
			if (strlen(path) >= MAX_PATH)
				continue; // no such path on the card

			// Validate that the tool still exists
			char sd_path[MAX_PATH];
			snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, path);

			if (exists(sd_path)) {
				ShortcutArray_push(shortcuts, Shortcut_new(path, name));
			} else {
				removed_any = true;
			}
		}
		fclose(file);
	}

	// Kept in stored (pin) order: no sorting.

	// Auto-clean: re-save if any were removed
	if (removed_any)
		saveShortcuts();

	return shortcuts->count > 0;
}

///////////////////////////////////////
// Public API

void Shortcuts_init(void) {
	loadShortcuts();
}

void Shortcuts_quit(void) {
	if (shortcuts) {
		ShortcutArray_free(shortcuts);
		shortcuts = NULL;
	}
}

int Shortcuts_exists(const char* path) {
	if (!shortcuts)
		return 0;
	return ShortcutArray_indexOf(shortcuts, path) != -1;
}

bool Shortcuts_canAdd(Entry* entry) {
	if (!shortcuts || !entry || shortcuts->count >= MAX_SHORTCUTS)
		return false;
	if (entry->type != ENTRY_PAK)
		return true;
	int tools = 0, games = 0;
	for (int i = 0; i < shortcuts->count; i++) {
		Shortcut* shortcut = shortcuts->items[i];
		if (suffixMatch(".pak", shortcut->path))
			tools++;
		else
			games++;
	}
	return tools < (games > 0 ? MAX_PINNED_TOOLS : MAX_PINNED_TOOLS_NO_GAMES);
}

void Shortcuts_add(Entry* entry) {
	if (!shortcuts || !entry)
		return;
	if (!MenuTabs_underPath(entry->path, SDCARD_PATH))
		return;

	char* path = entry->path + strlen(SDCARD_PATH);
	if (Shortcuts_exists(path))
		return;

	if (!Shortcuts_canAdd(entry)) {
		// refuse rather than silently evict a pin the user placed
		LOG_warn("Shortcuts_add: pin limit reached, not adding %s\n", path);
		return;
	}
	if (!ShortcutArray_push(shortcuts, Shortcut_new(path, entry->name))) // appended: pins keep the order they were added
		return;
	saveShortcuts();
}

void Shortcuts_remove(Entry* entry) {
	if (!shortcuts || !entry)
		return;
	if (!MenuTabs_underPath(entry->path, SDCARD_PATH))
		return;

	char* path = entry->path + strlen(SDCARD_PATH);
	int idx = ShortcutArray_indexOf(shortcuts, path);
	if (idx != -1) {
		Shortcut* shortcut = shortcuts->items[idx];
		Array_remove(shortcuts, shortcut);
		Shortcut_free(shortcut);
		saveShortcuts();
	}
}

bool Shortcuts_replacePath(const char* old_path, const char* new_path, const char* new_name) {
	if (!shortcuts || !old_path || !new_path)
		return false;
	int idx = ShortcutArray_indexOf(shortcuts, old_path);
	if (idx == -1)
		return false;
	Shortcut* shortcut = shortcuts->items[idx];
	// both copies first: out of memory leaves the pin as it was (a NULL path would crash every later lookup)
	char* path = strdup(new_path);
	char* name = new_name ? strdup(new_name) : NULL;
	if (!path || (new_name && !name)) {
		free(path);
		free(name);
		return false;
	}
	free(shortcut->path);
	shortcut->path = path; // same slot: the pin keeps its place
	if (name) {
		free(shortcut->name);
		shortcut->name = name;
	}
	saveShortcuts();
	return true;
}

int Shortcuts_isInToolsFolder(const char* path) {
	char paks_tools_path[MAX_PATH];
	snprintf(paks_tools_path, sizeof(paks_tools_path), "%s/Tools", PAKS_PATH);
	return MenuTabs_underPath(path, TOOLS_PATH) || MenuTabs_underPath(path, paks_tools_path);
}

int Shortcuts_getCount(void) {
	return shortcuts ? shortcuts->count : 0;
}

char* Shortcuts_getPath(int index) {
	if (!shortcuts || index < 0 || index >= shortcuts->count)
		return NULL;
	Shortcut* shortcut = shortcuts->items[index];
	return shortcut->path;
}

char* Shortcuts_getName(int index) {
	if (!shortcuts || index < 0 || index >= shortcuts->count)
		return NULL;
	Shortcut* shortcut = shortcuts->items[index];
	return shortcut->name;
}

int Shortcuts_validate(void) {
	if (!shortcuts)
		return 0;

	bool needs_save = false;
	for (int i = shortcuts->count - 1; i >= 0; i--) {
		Shortcut* shortcut = shortcuts->items[i];
		char sd_path[MAX_PATH];
		snprintf(sd_path, sizeof(sd_path), "%s%s", SDCARD_PATH, shortcut->path);

		if (!exists(sd_path)) {
			Array_remove(shortcuts, shortcut);
			Shortcut_free(shortcut);
			needs_save = true;
		}
	}

	if (needs_save)
		saveShortcuts();
	return needs_save;
}

char* Shortcuts_getPakBasename(const char* path) {
	static char basename[STR_MAX];

	// Extract filename from path
	const char* pakname = strrchr(path, '/');
	pakname = pakname ? pakname + 1 : path;

	// Copy and remove .pak extension
	strncpy(basename, pakname, sizeof(basename) - 1);
	basename[sizeof(basename) - 1] = '\0';
	char* dot = strrchr(basename, '.');
	if (dot)
		*dot = '\0';

	return basename;
}
