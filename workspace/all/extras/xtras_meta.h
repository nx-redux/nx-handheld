#ifndef XTRAS_META_H
#define XTRAS_META_H
#include <stdbool.h>

// Pure parts of the Xtras catalog (extras.c): meta.txt parsing, catalog ordering and the
// install-script progress hint. No SDL: host-testable.

#define META_STR 256

typedef struct {
	char id[64]; // catalog folder name
	char name[META_STR];
	char category[16]; // "GAME" | "TOOL"
	char desc[1024];
	char repo[128];		   // GitHub owner/name whose releases drive the update check ("" = untracked)
	char version[64];	   // our catalog version string (internal entries)
	bool version_internal; // true = update via our version=, false = upstream repo tag
	char latest[64];	   // latest upstream release tag, from the <id>.latest cache ("" = unknown)
	int size_mb;
	char installed[64]; // "" = not installed, else the installed release tag
	char done_msg[128]; // optional: install-success subtitle override, for
						// entries whose payload doesn't land in the default
						// category folder (e.g. an emulator pak in Emus/<plat>/, not
						// Tools/, so "Find it in Tools." would mislead)
	char platforms[64]; // meta.txt "platforms=" verbatim ("" = compatible everywhere)
	bool compatible;	// computed in catalog_load via xtras_platform_compatible
} AddonEntry;

// meta.txt: flat key=value, unknown keys ignored. Fills e (callers zero it first); true when the
// entry has both a name and a category.
bool xtras_meta_parse(const char* path, AddonEntry* e);

// qsort order of the catalog: by category (GAME before TOOL), then case-insensitive name.
int xtras_entry_cmp(const void* a, const void* b);

// "@NN status text" progress hint from an install/uninstall script line: see xtras_meta.c.
bool xtras_parse_hint(const char* line, int* pct, const char** status_text, const char** stripped);

#endif
