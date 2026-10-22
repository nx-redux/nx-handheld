#ifndef __NEXT_CMD_H__
#define __NEXT_CMD_H__

// Pull the ROM path back out of the launch command nextui queues in /tmp/next.
//
// launcher.c writes `'<pak>/launch.sh' '<rom>'` with both words single-quoted
// and every embedded apostrophe escaped as '\'' (escapeSingleQuotes). The old
// reader here took the text between the last two quote characters, which for
// "Link's Awakening.gb" is the tail of that escape sequence, so the tracker
// looked up "s Awakening.gb" and never matched the game. Parse the line the
// way the shell will instead: single-quoted spans are literal, a backslash
// outside quotes escapes the next byte, unquoted whitespace splits words.
// Header-only so the host test (common/tests/test_next_cmd.c) needs no sqlite.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The launcher's command buffer (launcher.c: char cmd[MAX_PATH * 8]). /tmp/next
// can be this long, so it must never be read into a STR_MAX buffer: a cut
// command loses the end of the ROM path and the tracker can't find the game.
#define NEXT_CMD_MAX (512 * 8)

// Copies the last word of cmd into out (out_size bytes). Returns 1 when a
// word was found, 0 for an empty or whitespace-only command.
static int NextCmd_lastWord(const char* cmd, char* out, size_t out_size) {
	if (!cmd || !out || out_size == 0)
		return 0;
	size_t len = 0;	 // bytes of the word currently being built
	int in_word = 0; // a word has started (so '' still counts as a word)
	int in_quote = 0;
	int found = 0;
	out[0] = '\0';
	for (const char* p = cmd; *p; p++) {
		char c = *p;
		if (in_quote) {
			if (c == '\'') {
				in_quote = 0;
				continue;
			}
		} else if (c == '\'') {
			if (!in_word) {
				in_word = 1;
				len = 0;
			}
			in_quote = 1;
			continue;
		} else if (c == '\\' && p[1]) {
			if (!in_word) {
				in_word = 1;
				len = 0;
			}
			c = *++p;
		} else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			if (in_word) {
				out[len] = '\0';
				found = 1;
				in_word = 0;
			}
			continue;
		} else if (!in_word) {
			in_word = 1;
			len = 0;
		}
		if (len + 1 < out_size)
			out[len++] = c;
	}
	if (in_word) {
		out[len] = '\0';
		found = 1;
	}
	return found;
}


// Reads the launch command in cmd_path and copies its ROM path (last word)
// into out. Returns 1 on success, 0 when the file is missing or empty, longer
// than NEXT_CMD_MAX, or the path does not fit out_size (a cut path would name
// another file).
static int NextCmd_readRomPath(const char* cmd_path, char* out, size_t out_size) {
	if (!out || out_size == 0)
		return 0;
	out[0] = '\0';
	FILE* f = fopen(cmd_path, "r");
	if (!f)
		return 0;
	char* cmd = malloc(NEXT_CMD_MAX + 2);
	char* word = malloc(NEXT_CMD_MAX + 1);
	int ok = 0;
	if (cmd && word) {
		size_t n = fread(cmd, 1, NEXT_CMD_MAX + 1, f);
		cmd[n] = '\0';
		if (n <= NEXT_CMD_MAX && NextCmd_lastWord(cmd, word, NEXT_CMD_MAX + 1) && strlen(word) < out_size) {
			memcpy(out, word, strlen(word) + 1);
			ok = 1;
		}
	}
	free(cmd);
	free(word);
	fclose(f);
	return ok;
}

#endif
