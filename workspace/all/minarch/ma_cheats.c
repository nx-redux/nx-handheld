#include "ma_internal.h"
#include "utils.h"
#include "config.h"
#include "ma_cheats.h"
#include "ma_cheat_match.h"
#include <glob.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include "ma_menu.h"
#include "ra_integration.h"
#include <stddef.h>

// cheat types live in ma_cheats.h
struct Cheats cheatcodes;

static size_t parse_count(FILE* file) {
	// The count may be quoted: every libretro PSP .cht (and many DC, SNES, MSX,
	// ...) has `cheats = "15"`, which the unquoted form read as 0 (#257). Files
	// saved by RetroArch's cheat search put the line last, not first.
	unsigned long count = 0;
	char* line = NULL;
	size_t cap = 0;
	while (getline(&line, &cap, file) >= 0) {
		const char* s = line;
		while (isspace((unsigned char)*s))
			s++;
		if (strncmp(s, "cheats", 6) || !(isspace((unsigned char)s[6]) || s[6] == '='))
			continue;
		if (sscanf(s, "cheats = \"%lu\"", &count) != 1)
			sscanf(s, "cheats = %lu", &count);
		break;
	}
	free(line);
	rewind(file);
	return count;
}

// A number value, quoted or not (RetroArch writes `cheat0_value = "7"`); 0x hex
// is accepted as RetroArch's strtoul(.., 0) does.
static int parse_uint(const char* ptr, unsigned* out) {
	char* end;
	if (*ptr == '"')
		ptr++;
	if (!isdigit((unsigned char)*ptr))
		return -1;
	*out = (unsigned)strtoul(ptr, &end, 0);
	return 0;
}

// RetroArch writes `true`/`false` quoted and also accepts 1/0.
static int parse_flag(const char* ptr) {
	if (*ptr == '"')
		ptr++;
	return !strncasecmp(ptr, "true", 4) || *ptr == '1';
}

// "cheat12_address_bit_position = ..." -> does the key after "cheat12_" equal `key`?
static int key_is(const char* line, const char* key) {
	const char* s = line + 5; // past "cheat"
	while (isdigit((unsigned char)*s))
		s++;
	if (*s++ != '_')
		return 0;
	size_t n = strlen(key);
	return !strncmp(s, key, n) && (isspace((unsigned char)s[n]) || s[n] == '=');
}

static const char* find_val(const char* start) {
	// skip the key token, then whitespace, expect '=', then skip whitespace;
	// every loop stops at NUL so a line without spaces can't scan out of bounds
	while (*start && !isspace((unsigned char)*start))
		start++;
	while (*start && isspace((unsigned char)*start))
		start++;

	if (*start != '=')
		return NULL;
	start++;

	while (*start && isspace((unsigned char)*start))
		start++;

	return start;
}

static int parse_bool(const char* ptr, int* out) {
	if (!strncasecmp(ptr, "true", 4)) {
		*out = 1;
	} else if (!strncasecmp(ptr, "false", 5)) {
		*out = 0;
	} else {
		return -1;
	}
	return 0;
}

// The entities the libretro database uses, each decoding to no more bytes than
// it takes up. Advances *ptr past the entity and appends it; 0 if not one.
static int decode_entity(const char** ptr, char* buf, int* index) {
	static const struct {
		const char* entity;
		const char* text;
	} entities[] = {
		{"&quot;", "\""},
		{"&amp;", "&"},
		{"&apos;", "'"},
		{"&lt;", "<"},
		{"&gt;", ">"},
		{"&nbsp;", " "},
		{"&eacute;", "\xC3\xA9"}, // é
	};
	for (size_t i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
		size_t n = strlen(entities[i].entity);
		if (strncmp(*ptr, entities[i].entity, n))
			continue;
		for (const char* t = entities[i].text; *t; t++)
			buf[(*index)++] = *t;
		*ptr += n;
		return 1;
	}
	return 0;
}

static int parse_string(const char* ptr, char* buf, size_t len) {
	int index = 0;
	size_t input_len = strlen(ptr);

	buf[0] = '\0';

	if (*ptr++ != '"')
		return -1;

	while (*ptr != '\0' && *ptr != '"' && index < len - 1) {
		if (*ptr == '\\' && *(ptr + 1) != '\0') {
			ptr++;
			buf[index++] = *ptr++;
		} else if (*ptr == '&' && decode_entity(&ptr, buf, &index)) {
			// HTML entities in database descriptions ("P1 &amp; P2")
		} else {
			buf[index++] = *ptr++;
		}
	}

	if (*ptr != '"') {
		buf[0] = '\0';
		return -1;
	}

	buf[index] = '\0';
	return 0;
}

static int parse_cheats(struct Cheats* cheats, FILE* file) {
	int ret = -1;
	// Lines have no fixed limit: database codes run to 15,000 chars (PSP), and
	// a 512-byte line buffer dropped every longer cheat. buf grows with line.
	char* line = NULL;
	size_t line_cap = 0;
	char* buf = NULL;
	size_t buf_cap = 0;
	const char* ptr;

	do {
		ssize_t got = getline(&line, &line_cap, file);
		if (got < 0) {
			ret = 0;
			break;
		}

		size_t line_len = strlen(line);
		if (line_len == 0)
			continue; // line starting with NUL (binary garbage)

		if (buf_cap < line_len + 1) {
			char* grown = realloc(buf, line_len + 1);
			if (!grown)
				goto finish;
			buf = grown;
			buf_cap = line_len + 1;
		}

		if ((ptr = strstr(line, "cheat"))) {
			int index = -1;
			struct Cheat* cheat;
			size_t len;
			sscanf(ptr, "cheat%d", &index);

			if (index < 0 || index >= cheats->count)
				continue;
			cheat = &cheats->cheats[index];

			unsigned num;
			struct CheatMemPatch* mem = &cheat->mem;
			if (key_is(ptr, "desc")) {
				ptr = find_val(ptr);
				if (!ptr || parse_string(ptr, buf, buf_cap)) {
					LOG_warn("Couldn't parse cheat %d description\n", index);
					continue;
				}

				len = strlen(buf);
				if (len == 0)
					continue;

				// duplicate keys in the .cht overwrite the previous value
				free((char*)cheat->name);
				free((char*)cheat->info);
				cheat->info = NULL;
				cheat->name = calloc(len + 1, sizeof(char));
				if (!cheat->name)
					goto finish;

				strncpy((char*)cheat->name, buf, len);
				truncateString((char*)cheat->name, CHEAT_MAX_DESC_LEN);

				if (len >= CHEAT_MAX_DESC_LEN) {
					cheat->info = calloc(len + 1, sizeof(char));
					if (!cheat->info)
						goto finish;

					strncpy((char*)cheat->info, buf, len);
					wrapString((char*)cheat->info, CHEAT_MAX_LINE_LEN, CHEAT_MAX_LINES);
				}
			} else if (key_is(ptr, "code")) {
				ptr = find_val(ptr);
				if (!ptr || parse_string(ptr, buf, buf_cap)) {
					LOG_warn("Couldn't parse cheat %d code\n", index);
					continue;
				}

				len = strlen(buf);
				if (len == 0)
					continue;

				free((char*)cheat->code);
				cheat->code = calloc(len + 1, sizeof(char));
				if (!cheat->code)
					goto finish;

				strncpy((char*)cheat->code, buf, len);
			} else if (key_is(ptr, "enable")) {
				ptr = find_val(ptr);
				// A quoted value is RetroArch's own format, which the database
				// ships with "true" on many cheats: ignored, so downloading the
				// database never turns cheats on by itself.
				if (ptr && *ptr == '"')
					continue;
				if (!ptr || parse_bool(ptr, &cheat->enabled)) {
					LOG_warn("Couldn't parse cheat %d enabled\n", index);
					continue;
				}
			} else if (key_is(ptr, "big_endian")) {
				if ((ptr = find_val(ptr)))
					mem->big_endian = parse_flag(ptr);
			} else {
				// the RAM-patch fields (handler 1, see ma_cheat_mem.h)
				static const struct {
					const char* key;
					size_t offset;
				} fields[] = {
					{"handler", offsetof(struct CheatMemPatch, memory)},
					{"address", offsetof(struct CheatMemPatch, address)},
					{"value", offsetof(struct CheatMemPatch, value)},
					{"memory_search_size", offsetof(struct CheatMemPatch, size)},
					{"cheat_type", offsetof(struct CheatMemPatch, type)},
					{"address_bit_position", offsetof(struct CheatMemPatch, bit_mask)},
					{"repeat_count", offsetof(struct CheatMemPatch, repeat_count)},
					{"repeat_add_to_value", offsetof(struct CheatMemPatch, repeat_add_to_value)},
					{"repeat_add_to_address", offsetof(struct CheatMemPatch, repeat_add_to_address)},
				};
				for (size_t f = 0; f < sizeof(fields) / sizeof(fields[0]); f++) {
					if (!key_is(ptr, fields[f].key))
						continue;
					const char* val = find_val(ptr);
					if (val && !parse_uint(val, &num)) {
						if (fields[f].offset == offsetof(struct CheatMemPatch, memory))
							mem->memory = num == 1;
						else
							*(unsigned*)((char*)mem + fields[f].offset) = num;
					}
					break;
				}
			}
		}
	} while (1);

finish:
	free(line);
	free(buf);
	return ret;
}

// Glob pattern for `name` that ignores case ("Of" -> "[oO][fF]") and matches
// glob metacharacters literally: ROM and cheat names differ in title case
// ("Skies of Deception" vs "Skies Of Deception") and glob compares exactly.
static void caseFoldPattern(const char* name, char* out, size_t out_len) {
	size_t o = 0;
	for (const char* s = name; *s; s++) {
		unsigned char c = (unsigned char)*s;
		char piece[8];
		if (isalpha(c))
			snprintf(piece, sizeof(piece), "[%c%c]", tolower(c), toupper(c));
		else if (c == '[' || c == ']' || c == '*' || c == '?' || c == '\\')
			snprintf(piece, sizeof(piece), "\\%c", c);
		else
			snprintf(piece, sizeof(piece), "%c", c);
		size_t len = strlen(piece);
		if (o + len >= out_len)
			break;
		memcpy(out + o, piece, len);
		o += len;
	}
	out[o] = '\0';
}

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
				if (*count >= CHEAT_MAX_PATHS - 1) {
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
		caseFoldPattern(rom_name, folded, room < sizeof(folded) ? room : 1);
		snprintf(paths[(*count)++], MAX_PATH, "%s/%s*.cht", core.cheats_dir, folded); // /mnt/SDCARD/Cheats/GB/[sS][uU][pP]...*.cht
	}
}

void Cheats_free() {
	if (cheatcodes.cheats) {
		size_t i;
		for (i = 0; i < cheatcodes.count; i++) {
			struct Cheat* cheat = &cheatcodes.cheats[i];
			free((char*)cheat->name);
			free((char*)cheat->info);
			free((char*)cheat->code);
		}
		free(cheatcodes.cheats);
		cheatcodes.cheats = NULL;
	}
	cheatcodes.count = 0;
}

// getDisplayName-based stem for the current game, for matcher comparison.
static const char* game_display_for_cheats(void) {
	static char disp[MAX_PATH];
	getDisplayName(game.alt_name, disp);
	getAlias(game.path, disp);
	return disp;
}

// Free one standalone struct Cheats (used when an append fails mid-way).
static void Cheats_free_one(struct Cheats* c) {
	if (!c->cheats)
		return;
	for (size_t i = 0; i < c->count; i++) {
		free((char*)c->cheats[i].name);
		free((char*)c->cheats[i].info);
		free((char*)c->cheats[i].code);
	}
	free(c->cheats);
	c->cheats = NULL;
	c->count = 0;
}

// Parse exactly one .cht file into `out` (out->cheats allocated here).
// Returns 1 on success, 0 on failure (out left empty/freed by the caller).
static int load_one_file(const char* path, struct Cheats* out) {
	out->count = 0;
	out->cheats = NULL;

	FILE* file = fopen(path, "r");
	if (!file) {
		LOG_error("Couldn't open cheat file: %s\n", path);
		return 0;
	}

	out->count = parse_count(file);
	if (out->count <= 0) {
		LOG_error("Couldn't read cheat count\n");
		fclose(file);
		return 0;
	}

	out->cheats = calloc(out->count, sizeof(struct Cheat));
	if (!out->cheats) {
		LOG_error("Couldn't allocate memory for cheats\n");
		out->count = 0;
		fclose(file);
		return 0;
	}
	for (size_t i = 0; i < out->count; i++)
		CheatMemPatch_init(&out->cheats[i].mem);

	if (parse_cheats(out, file)) {
		LOG_error("Error parsing cheat file: %s\n", path);
		fclose(file);
		return 0;
	}

	// Some database entries have no (or an empty) description, and the Cheats
	// menu needs a name for every entry: "Cheat 60" instead of a NULL it would crash on.
	for (size_t i = 0; i < out->count; i++) {
		struct Cheat* c = &out->cheats[i];
		if (c->name)
			continue;
		char label[32];
		snprintf(label, sizeof(label), "Cheat %zu", i + 1);
		c->name = strdup(label);
		if (!c->name) {
			fclose(file);
			return 0;
		}
	}

	fclose(file);
	return 1;
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

			if (gc > 0) {
				int sel[512];
				int nsel = CheatMatch_select(game_display_for_cheats(),
											 game.alt_name,
											 baseptrs, gc, sel, gc);
				if (nsel <= 0) {
					// No confident match: legacy behaviour, load the first hit.
					sel[0] = 0;
					nsel = 1;
				}
				for (int s = 0; s < nsel; s++) {
					struct Cheats one;
					if (load_one_file(fulls[sel[s]], &one)) {
						char src[64] = {0};
						if (nsel > 1)
							CheatMatch_sourceLabel(bases[sel[s]], src, sizeof(src));
						if (cheats_append(&one, src)) {
							success = 1;
						} else {
							Cheats_free_one(&one);
						}
					}
				}
			}
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
					Cheats_free_one(&one);
			}
			if (success)
				break;
		}
	}

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
