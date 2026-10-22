#ifndef LINE_REWRITE_H
#define LINE_REWRITE_H
// Pure line filter behind the map.txt and collection edits in gamelist.c, kept
// free of SDL and file I/O so tests/test_line_rewrite.c can cover it.
#include <stdbool.h>
#include <stddef.h>

// Filters `data` (len bytes, any line endings) into a new malloc'd buffer.
// A line matches when it equals `key` (map_key false) or when the part before
// its first tab equals `key` (map_key true; map.txt is key<TAB>alias); a
// trailing '\r' is ignored. Matching lines are dropped, or replaced by
// `replacement` (a full line, newline included) when it is non-NULL;
// append_if_absent adds `replacement` when no line matched. Other lines are
// kept byte for byte. *changed reports whether anything matched or was
// appended. Returns NULL only when out of memory.
char* LineRewrite_filter(const char* data, size_t len, const char* key, bool map_key,
						 const char* replacement, bool append_if_absent, size_t* out_len, bool* changed);
#endif
