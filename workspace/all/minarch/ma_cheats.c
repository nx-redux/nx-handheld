#include "ma_internal.h"
#include "utils.h"
#include "config.h"
#include "ma_cheats.h"
#include "ma_cheat_match.h"
#include "ma_cheat_parse.h"
#include <dirent.h>
#include <glob.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include "ma_menu.h"
#include "ra_integration.h"
#include <stddef.h>

// cheat types live in ma_cheats.h
struct Cheats cheatcodes;

_Static_assert(CHEAT_PARSE_LOG_WARN == LOG_WARN && CHEAT_PARSE_LOG_ERROR == LOG_ERROR, "CheatParse log levels follow api.h");

// return variations with/without extensions and other cruft
void Cheat_getPaths(char paths[CHEAT_MAX_PATHS][MAX_PATH], int* count) {
	// Generate possible paths, ordered by most likely to be used (pre v6.2.3 style first)
	snprintf(paths[(*count)++], MAX_PATH, "%s/%s.cht", core.cheats_dir, game.name); // /mnt/SDCARD/Cheats/GB/Super Example World.<ext>.cht
	if (CFG_getUseExtractedFileName())
		snprintf(paths[(*count)++], MAX_PATH, "%s/%s.cht", core.cheats_dir, game.alt_name); // /mnt/SDCARD/Cheats/GB/Super Example World (USA).<ext>.cht

	// game.alt_name, but with all extension-like stuff removed (apart from .cht)
	// eg. Super Example World (USA).zip -> Super Example World (USA).cht
	{
		int i = 0;
		char* ext;
		char exts[128];
		// skip only the extension-based variants when extensions are unusable;
		// the sanitized/alias/wildcard paths below don't depend on them
		if (strlen(core.extensions) < sizeof(exts)) {
			strcpy(exts, core.extensions);
			while ((ext = strtok(i ? NULL : exts, "|"))) {
				// keep 3 slots for the sanitized, alias and wildcard paths below: a core
				// with many extensions (PicoDrive has 16) used the wildcard's slot, so
				// its games never reached the ranked match
				if (*count >= CHEAT_MAX_PATHS - 3) {
					break;
				}

				char rom_name[MAX_PATH];
				if (strlen(game.alt_name) >= MAX_PATH) {
					i++;
					continue;
				}

				strcpy(rom_name, game.alt_name);
				char* tmp = strrchr(rom_name, '.');
				if (tmp != NULL && strlen(tmp) > 2 && strlen(tmp) <= 5) {
					tmp[0] = '\0';

					// Add length check before sprintf to prevent buffer overflow
					int needed_len = strlen(core.cheats_dir) + strlen(rom_name) + strlen(ext) + 10; // +10 for "/", ".", ".cht", etc.
					if (needed_len < MAX_PATH) {
						snprintf(paths[(*count)++], MAX_PATH, "%s/%s.%s.cht", core.cheats_dir, rom_name, ext);
					}
				}
				i++;
			}
		}
	}

	// Sanitized: remove extension and other cruft
	// eg. Super Example World (USA).zip -> Super Example World
	//     Super Example World (USA) [!].7z -> Super Example World
	//     Super Example World (USA) (Rev 1).rar -> Super Example World
	char rom_name[MAX_PATH];
	getDisplayName(game.alt_name, rom_name);
	if (*count < CHEAT_MAX_PATHS)
		snprintf(paths[(*count)++], MAX_PATH, "%s/%s.cht", core.cheats_dir, rom_name); // /mnt/SDCARD/Cheats/GB/Super Example World.cht
	// Respect map.txt: use alias if available
	// eg. 1941.zip	-> 1941: Counter Attack
	if (getAlias(game.path, rom_name) && *count < CHEAT_MAX_PATHS)
		snprintf(paths[(*count)++], MAX_PATH, "%s/%s.cht", core.cheats_dir, rom_name); // /mnt/SDCARD/Cheats/GB/Super Example World.cht

	// Santitized alias, ignoring all extra cruft - including Cheat specifics like "(Game Breaker)" etc.
	// This is a wildcard that may match something unexpected, but also may find something when nothing else does.
	getDisplayName(game.alt_name, rom_name);
	getAlias(game.path, rom_name);
	if (*count < CHEAT_MAX_PATHS) {
		char folded[MAX_PATH];
		// leave room for the folder and "*.cht" so the wildcard isn't cut off
		size_t room = sizeof(folded) - strlen(core.cheats_dir) - 8;
		CheatParse_caseFoldPattern(rom_name, folded, room < sizeof(folded) ? room : 1);
		snprintf(paths[(*count)++], MAX_PATH, "%s/%s*.cht", core.cheats_dir, folded); // /mnt/SDCARD/Cheats/GB/[sS][uU][pP]...*.cht
	}
}

void Cheats_free() {
	CheatParse_free(&cheatcodes);
	cheatcodes.count = 0;
}

// getDisplayName-based stem for the current game, for matcher comparison.
static const char* game_display_for_cheats(void) {
	static char disp[MAX_PATH];
	getDisplayName(game.alt_name, disp);
	getAlias(game.path, disp);
	return disp;
}

// Parse exactly one .cht file into `out` (out->cheats allocated here).
// Returns 1 on success, 0 on failure (out is then left empty and freed).
static int load_one_file(const char* path, struct Cheats* out) {
	out->count = 0;
	out->cheats = NULL;

	FILE* file = fopen(path, "r");
	if (!file) {
		LOG_error("Couldn't open cheat file: %s\n", path);
		return 0;
	}
	int ok = CheatParse_file(file, out, path, LOG_note);
	fclose(file);
	return ok;
}

// Append every cheat in `src` onto the global cheatcodes array, taking
// ownership of src's allocated strings (src's array itself is freed, its
// entries are moved). When `source` is non-empty (a merged multi-variant
// load), prefix each cheat's detail (info) line with "(<source>) " so the
// Y-detail view shows which cheat system it came from.
static int cheats_append(struct Cheats* src, const char* source) {
	if (src->count <= 0)
		return 1;
	size_t new_count = cheatcodes.count + src->count;
	struct Cheat* grown = realloc(cheatcodes.cheats, new_count * sizeof(struct Cheat));
	if (!grown)
		return 0;
	cheatcodes.cheats = grown;

	for (size_t i = 0; i < src->count; i++) {
		struct Cheat* c = &src->cheats[i];
		if (source && source[0]) {
			// Build "(source) " + existing info-or-name into a fresh info line.
			const char* base = c->info ? c->info : (c->name ? c->name : "");
			size_t len = strlen(source) + strlen(base) + 4; // "(" ") " NUL
			char* tagged = calloc(len, sizeof(char));
			if (tagged) {
				snprintf(tagged, len, "(%s) %s", source, base);
				free((char*)c->info);
				c->info = tagged;
			}
		}
		cheatcodes.cheats[cheatcodes.count + i] = *c; // move pointers
	}
	cheatcodes.count = new_count;
	free(src->cheats); // entries moved; free only the array
	src->cheats = NULL;
	src->count = 0;
	return 1;
}

// Ranks the candidate files (full paths + their basenames) for this game and
// loads the winning group, merging same-region variants. With first_hit, no
// confident match still loads the first candidate (the wildcard's legacy
// behaviour). Returns 1 if anything was loaded.
static int load_ranked(char (*fulls)[MAX_PATH], const char** bases, int n, int first_hit) {
	int* sel = calloc((size_t)n, sizeof(int));
	if (!sel)
		return 0;
	int nsel = CheatMatch_select(game_display_for_cheats(), game.alt_name, bases, n, sel, n);
	if (nsel <= 0 && first_hit) {
		sel[0] = 0;
		nsel = 1;
	}
	int loaded = 0;
	for (int s = 0; s < nsel; s++) {
		struct Cheats one;
		if (load_one_file(fulls[sel[s]], &one)) {
			char src[64] = {0};
			if (nsel > 1)
				CheatMatch_sourceLabel(bases[sel[s]], src, sizeof(src));
			if (cheats_append(&one, src)) {
				LOG_info("Cheats: loaded %s\n", bases[sel[s]]);
				loaded = 1;
			} else {
				CheatParse_free(&one);
			}
		}
	}
	free(sel);
	return loaded;
}

// Last resort when no path found the game: every .cht in the cheats folder
// whose stem has the same letters and digits as the ROM's ("Soulcalibur" ->
// "Soul Calibur.cht"), ranked like a wildcard hit but never falling back to an
// unrelated first file.
static int load_loose_matches(void) {
	char want[MAX_PATH];
	CheatMatch_looseStem(game_display_for_cheats(), want, sizeof(want));
	if (!want[0])
		return 0;
	DIR* dir = opendir(core.cheats_dir);
	if (!dir)
		return 0;
	enum { MAX_LOOSE = 32 };
	char (*fulls)[MAX_PATH] = calloc(MAX_LOOSE, sizeof(*fulls));
	char (*bases)[MAX_PATH] = calloc(MAX_LOOSE, sizeof(*bases));
	const char* baseptrs[MAX_LOOSE];
	int n = 0;
	struct dirent* ent;
	while (fulls && bases && n < MAX_LOOSE && (ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.' || !suffixMatch(".cht", ent->d_name))
			continue;
		char loose[MAX_PATH];
		CheatMatch_looseStem(ent->d_name, loose, sizeof(loose));
		if (strcmp(loose, want) != 0)
			continue;
		if (snprintf(fulls[n], MAX_PATH, "%s/%s", core.cheats_dir, ent->d_name) >= MAX_PATH)
			continue;
		snprintf(bases[n], MAX_PATH, "%s", ent->d_name);
		baseptrs[n] = bases[n];
		n++;
	}
	closedir(dir);
	int loaded = n > 0 && load_ranked(fulls, baseptrs, n, 0);
	if (loaded)
		LOG_info("Cheats: no exact name match, loaded the files matching \"%s\" by letters and digits\n", want);
	free(fulls);
	free(bases);
	return loaded;
}

bool Cheats_load() {
	int success = 0;
	size_t i;

	// we get our paths frrom Cheat_getPaths, some might be wildcards
	char paths[CHEAT_MAX_PATHS][MAX_PATH];
	int path_count = 0;
	Cheat_getPaths(paths, &path_count);

	for (i = 0; i < path_count; i++) {
		if (strchr(paths[i], '*')) {
			// Wildcard: glob, then rank + merge the matches.
			glob_t g;
			memset(&g, 0, sizeof(g));
			if (glob(paths[i], 0, NULL, &g) != 0 || g.gl_pathc == 0) {
				globfree(&g);
				continue;
			}

			// Keep only real .cht matches; collect full paths + basenames.
			int gc = 0;
			int max = (int)g.gl_pathc;
			if (max > 512)
				max = 512; // sane cap for a single game's glob
			char (*fulls)[MAX_PATH] = calloc((size_t)max, sizeof(*fulls));
			char (*bases)[MAX_PATH] = calloc((size_t)max, sizeof(*bases));
			const char** baseptrs = calloc((size_t)max, sizeof(char*));
			if (!fulls || !bases || !baseptrs) {
				free(fulls);
				free(bases);
				free(baseptrs);
				globfree(&g);
				continue;
			}
			for (int gi = 0; gi < max; gi++) {
				if (!suffixMatch(".cht", g.gl_pathv[gi]))
					continue;
				if (!exists(g.gl_pathv[gi]))
					continue;
				snprintf(fulls[gc], MAX_PATH, "%s", g.gl_pathv[gi]);
				char tmp[MAX_PATH];
				snprintf(tmp, sizeof(tmp), "%s", g.gl_pathv[gi]);
				snprintf(bases[gc], MAX_PATH, "%s", basename(tmp));
				baseptrs[gc] = bases[gc];
				gc++;
			}
			globfree(&g);

			if (gc > 0 && load_ranked(fulls, baseptrs, gc, 1))
				success = 1;
			free(fulls);
			free(bases);
			free(baseptrs);
			if (success)
				break;
			continue;
		} else {
			// Exact candidate: unchanged single-file behaviour.
			if (!exists(paths[i]))
				continue;
			struct Cheats one;
			if (load_one_file(paths[i], &one)) {
				if (cheats_append(&one, NULL))
					success = 1;
				else
					CheatParse_free(&one);
			}
			if (success)
				break;
		}
	}

	if (!success)
		success = load_loose_matches();
	if (!success)
		Cheats_free();
	return success;
}

///////////////////////////////
// RAM-patch cheats (handler 1): applied by the frontend every frame, like
// RetroArch, to the core's system RAM. That is the SYSTEM_RAM regions of the
// core's memory map in order, or else RETRO_MEMORY_SYSTEM_RAM; the database's
// addresses are offsets into that.

static struct {
	void* ptr;
	size_t len;
} mem_map[CHEAT_MEM_MAX_REGIONS];
static int mem_map_count;
static CheatMem cheat_mem;
static int cheat_mem_ready;
static int cheat_mem_logged;

void Cheats_setMemoryMap(const struct retro_memory_map* mmap) {
	mem_map_count = 0;
	cheat_mem_ready = 0;
	cheat_mem_logged = 0;
	if (!mmap)
		return;
	for (unsigned i = 0; i < mmap->num_descriptors && mem_map_count < CHEAT_MEM_MAX_REGIONS; i++) {
		const struct retro_memory_descriptor* d = &mmap->descriptors[i];
		if ((d->flags & RETRO_MEMDESC_SYSTEM_RAM) && d->ptr && d->len) {
			mem_map[mem_map_count].ptr = d->ptr;
			mem_map[mem_map_count].len = d->len;
			mem_map_count++;
		}
	}
}

static void init_cheat_mem(void) {
	CheatMem_reset(&cheat_mem);
	for (int i = 0; i < mem_map_count; i++)
		CheatMem_addRegion(&cheat_mem, mem_map[i].ptr, mem_map[i].len);
	if (!cheat_mem.count && core.get_memory_data && core.get_memory_size)
		CheatMem_addRegion(&cheat_mem, core.get_memory_data(RETRO_MEMORY_SYSTEM_RAM),
						   core.get_memory_size(RETRO_MEMORY_SYSTEM_RAM));
	if (!cheat_mem_logged) {
		if (!cheat_mem.count)
			LOG_warn("Cheats: the core exposes no system RAM, RAM cheats can't apply\n");
		else
			LOG_info("Cheats: RAM cheats apply to %d region(s), %zu bytes (%s)\n", cheat_mem.count, cheat_mem.total,
					 mem_map_count ? "memory map" : "RETRO_MEMORY_SYSTEM_RAM");
		cheat_mem_logged = 1;
	}
	// A memory map's regions stay put; the RETRO_MEMORY_SYSTEM_RAM pointer is
	// asked for again every frame, in case the core moves its RAM.
	cheat_mem_ready = mem_map_count > 0;
}

void Cheats_applyMemory(void) {
	static const struct CheatMemPatch** list;
	static size_t list_cap;
	int n = 0;

	if (!cheatcodes.count || RA_isHardcoreModeActive())
		return;
	for (size_t i = 0; i < cheatcodes.count; i++) {
		struct Cheat* c = &cheatcodes.cheats[i];
		if (!c->enabled || !c->mem.memory)
			continue;
		if ((size_t)n >= list_cap) {
			size_t cap = list_cap ? list_cap * 2 : 16;
			const struct CheatMemPatch** grown = realloc(list, cap * sizeof(*list));
			if (!grown)
				return;
			list = grown;
			list_cap = cap;
		}
		list[n++] = &c->mem;
	}
	if (!n)
		return;
	if (!cheat_mem_ready)
		init_cheat_mem();
	CheatMem_apply(&cheat_mem, list, n);
}
