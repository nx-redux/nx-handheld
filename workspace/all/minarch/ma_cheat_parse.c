#include "ma_cheat_parse.h"
#include "ma_cheat_mem.h"
#include "utils.h"
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

#define CHEAT_LOG(log, level, ...)   \
	do {                             \
		if (log)                     \
			log(level, __VA_ARGS__); \
	} while (0)

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

static int parse_cheats(struct Cheats* cheats, FILE* file, CheatParseLog log) {
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
					CHEAT_LOG(log, CHEAT_PARSE_LOG_WARN, "Couldn't parse cheat %d description\n", index);
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
					CHEAT_LOG(log, CHEAT_PARSE_LOG_WARN, "Couldn't parse cheat %d code\n", index);
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
					CHEAT_LOG(log, CHEAT_PARSE_LOG_WARN, "Couldn't parse cheat %d enabled\n", index);
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

// ROM and cheat names differ in title case ("Skies of Deception" vs "Skies Of
// Deception") and glob compares exactly.
void CheatParse_caseFoldPattern(const char* name, char* out, size_t out_len) {
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

void CheatParse_free(struct Cheats* c) {
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

int CheatParse_file(FILE* file, struct Cheats* out, const char* path, CheatParseLog log) {
	out->count = 0;
	out->cheats = NULL;

	size_t declared = parse_count(file);
	if (!CheatFile_countOk(declared)) {
		CHEAT_LOG(log, CHEAT_PARSE_LOG_ERROR, "Couldn't read cheat count (%zu) in %s\n", declared, path);
		return 0;
	}
	out->count = declared;

	out->cheats = calloc(out->count, sizeof(struct Cheat));
	if (!out->cheats) {
		CHEAT_LOG(log, CHEAT_PARSE_LOG_ERROR, "Couldn't allocate memory for cheats\n");
		out->count = 0;
		return 0;
	}
	for (size_t i = 0; i < out->count; i++)
		CheatMemPatch_init(&out->cheats[i].mem);

	if (parse_cheats(out, file, log)) {
		CHEAT_LOG(log, CHEAT_PARSE_LOG_ERROR, "Error parsing cheat file: %s\n", path);
		CheatParse_free(out);
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
			CheatParse_free(out);
			return 0;
		}
	}
	return 1;
}
