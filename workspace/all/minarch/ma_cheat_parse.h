#pragma once

#include <stdio.h>
#include "ma_cheats.h"

// RetroArch .cht parsing: plain C, no SDL, host-tested by tests/test_cheat_parse.c.
// ma_cheats.c owns the file lookup and the loaded cheats.

// A logger with api.h's LOG_note signature (NULL: silent); the levels are
// api.h's LOG_WARN and LOG_ERROR.
typedef void (*CheatParseLog)(int level, const char* fmt, ...);
#define CHEAT_PARSE_LOG_WARN 2
#define CHEAT_PARSE_LOG_ERROR 3

// Parses one open .cht file into out (out->cheats allocated here). `path` only
// names the file in log lines. Returns 1 on success, 0 on failure (out is then
// left empty and freed).
int CheatParse_file(FILE* file, struct Cheats* out, const char* path, CheatParseLog log);

// Frees one standalone struct Cheats and leaves it empty.
void CheatParse_free(struct Cheats* c);

// Glob pattern for `name` that ignores case ("Of" -> "[oO][fF]") and matches
// glob metacharacters literally. Truncated at whole pieces to fit out_len.
void CheatParse_caseFoldPattern(const char* name, char* out, size_t out_len);
