#include "xtras_meta.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// --- meta.txt: flat key=value, unknown keys ignored ---------------------
static void meta_set(AddonEntry* e, const char* key, const char* val) {
	if (!strcmp(key, "name"))
		snprintf(e->name, sizeof(e->name), "%s", val);
	else if (!strcmp(key, "category"))
		snprintf(e->category, sizeof(e->category), "%s", val);
	else if (!strcmp(key, "desc"))
		snprintf(e->desc, sizeof(e->desc), "%s", val);
	else if (!strcmp(key, "repo"))
		snprintf(e->repo, sizeof(e->repo), "%s", val);
	else if (!strcmp(key, "size_mb"))
		e->size_mb = atoi(val);
	else if (!strcmp(key, "done_msg"))
		snprintf(e->done_msg, sizeof(e->done_msg), "%s", val);
	else if (!strcmp(key, "platforms"))
		snprintf(e->platforms, sizeof(e->platforms), "%s", val);
	else if (!strcmp(key, "version"))
		snprintf(e->version, sizeof(e->version), "%s", val);
	else if (!strcmp(key, "version_source"))
		e->version_internal = (strcmp(val, "internal") == 0);
	// "asset" is consumed only by the entry's own install.sh; ignored here.
}

bool xtras_meta_parse(const char* path, AddonEntry* e) {
	FILE* f = fopen(path, "r");
	if (!f)
		return false;
	char line[1200];
	while (fgets(line, sizeof(line), f)) {
		char* nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		char* eq = strchr(line, '=');
		if (!eq || eq == line)
			continue;
		*eq = '\0';
		meta_set(e, line, eq + 1);
	}
	fclose(f);
	return e->name[0] && e->category[0];
}

int xtras_entry_cmp(const void* a, const void* b) {
	const AddonEntry *ea = a, *eb = b;
	int c = strcmp(ea->category, eb->category); // GAME before TOOL
	return c ? c : strcasecmp(ea->name, eb->name);
}

// Catalog progress-hint convention (Task 11): a line of the form "@NN status
// text" - NN a bare decimal (no sign), then a space, then non-empty free
// text - sets the progress bar to NN% (clamped to 100, in case a script
// ever sends more) and shows "status text" as the big centered status line.
// Any other line is ordinary human-readable output and is shown as the
// smaller detail line below the bar instead (see the caller's read loop).
// Scripts emit hints as their OWN line, never glued onto an existing
// message line, so existing message strings (checksum failures, "kept",
// etc.) stay byte-for-byte intact for anything that greps them - see
// install.sh's header for the full contract text.
//
// Hardening (round-1 review): a line is only ever treated as hint SYNTAX -
// and so never shown to the user verbatim - once it's unambiguously
// digit-leading (line[1] is a decimal digit; this also rejects a '-' right
// after '@', so a negative number is never mistaken for a hint - it just
// reads as ordinary text instead, same as any other line that merely
// starts with '@', e.g. an email address or an "@channel"-style mention).
// Once that gate is passed, strtol() only ever walks digits it was already
// promised were there, so it can't wander onto leading whitespace the way
// it would if called on arbitrary input. A digit-leading line that ISN'T a
// well-formed hint (no space, or a space with nothing after it) is still
// malformed hint syntax, not ordinary text - `*stripped` is set to
// whatever remains after the "@<digits>" token (possibly empty) so the
// caller can route THAT to the detail line instead of the raw "@NN..."
// text; the caller drops the line entirely when `*stripped` comes back
// empty.
//
// Returns true (with *pct/*status_text set) for a well-formed hint. Returns
// false otherwise; *stripped is then NULL for ordinary (non-digit-leading)
// text - use `line` itself unmodified - or a pointer to the hint-token-
// stripped remainder for malformed digit-leading syntax.
bool xtras_parse_hint(const char* line, int* pct, const char** status_text, const char** stripped) {
	*stripped = NULL;
	if (line[0] != '@' || !isdigit((unsigned char)line[1]))
		return false;

	char* end;
	long n = strtol(line + 1, &end, 10);
	if (*end == ' ' && end[1] != '\0') {
		*pct = (n > 100) ? 100 : (int)n;
		*status_text = end + 1;
		return true;
	}
	*stripped = (*end == ' ') ? end + 1 : end;
	return false;
}
