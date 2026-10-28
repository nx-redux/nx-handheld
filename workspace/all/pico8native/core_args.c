#include "core_args.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int core_build_argv(const char* bin, const char* home, const char* cart, int splore, char** argv, int max) {
	if (!bin || !home || (!splore && !cart))
		return -1;
	// No window/size flags: under the offscreen driver PICO-8's default fullscreen-desktop
	// window and software renderer work (Task 5 spike), and the shim never forwards rendering.
	const char* v[6];
	int n = 0;
	v[n++] = bin;
	v[n++] = "-home";
	v[n++] = home;
	if (splore)
		v[n++] = "-splore";
	else {
		v[n++] = "-run";
		v[n++] = cart;
	}
	if (n + 1 > max)
		return -1;
	for (int i = 0; i < n; i++)
		argv[i] = (char*)v[i];
	argv[n] = NULL;
	return n;
}

// length of NAME in "NAME=value" (the whole string if it has no '=')
static size_t name_len(const char* kv) {
	const char* eq = strchr(kv, '=');
	return eq ? (size_t)(eq - kv) : strlen(kv);
}

int core_build_env(char* const* base, const char* const* set, char** out, int max) {
	int n = 0;
	for (; base && base[n]; n++) {
		if (n + 1 > max)
			return -1;
		out[n] = base[n];
	}
	for (int s = 0; set && set[s]; s++) {
		size_t len = name_len(set[s]);
		int i = 0;
		for (; i < n; i++)
			if (name_len(out[i]) == len && strncmp(out[i], set[s], len) == 0)
				break;
		if (i == n) {
			if (n + 1 > max)
				return -1;
			n++;
		}
		out[i] = (char*)set[s];
	}
	if (n + 1 > max)
		return -1;
	out[n] = NULL;
	return n;
}

static int has_runtime(const char* dir) {
	char bin[1024], dat[1024];
	if (snprintf(bin, sizeof(bin), "%s/pico8_64", dir) >= (int)sizeof(bin) ||
		snprintf(dat, sizeof(dat), "%s/pico8.dat", dir) >= (int)sizeof(dat))
		return 0;
	return access(bin, F_OK) == 0 && access(dat, F_OK) == 0;
}

int core_find_runtime(const char* system_dir, char* dir_out, size_t size) {
	if (!system_dir || !*system_dir || !dir_out || !size)
		return -1;
	// Bios/P8 (minarch's system dir for the tag), then the sibling Bios/PICO:
	// the same order as PICO8_findRuntime in common/utils.c
	char dirs[2][1024];
	size_t len = strlen(system_dir);
	while (len > 1 && system_dir[len - 1] == '/')
		len--;
	if (len >= sizeof(dirs[0]))
		return -1;
	memcpy(dirs[0], system_dir, len);
	dirs[0][len] = '\0';
	char* slash = strrchr(dirs[0], '/');
	int ndirs = 1;
	if (slash && snprintf(dirs[1], sizeof(dirs[1]), "%.*s/PICO", (int)(slash - dirs[0]), dirs[0]) < (int)sizeof(dirs[1]))
		ndirs = 2;
	for (int i = 0; i < ndirs; i++) {
		if (!has_runtime(dirs[i]))
			continue;
		if (strlen(dirs[i]) >= size)
			return -1;
		strcpy(dir_out, dirs[i]);
		return 0;
	}
	return -1;
}

#define FRAMERATE_KEY "host_framerate_control"

int core_force_framerate_control(const char* config) {
	FILE* f = fopen(config, "rb");
	if (!f)
		return 0;
	char buf[16384];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	int big = !feof(f);
	fclose(f);
	if (big) // PICO-8's config.txt is ~2 KB; anything this size isn't one to edit
		return -1;
	buf[n] = '\0';

	// the line "host_framerate_control <value>", at the start of a line
	const size_t klen = strlen(FRAMERATE_KEY);
	char* line = NULL;
	for (char* p = buf; p;) {
		if (strncmp(p, FRAMERATE_KEY, klen) == 0 && p[klen] == ' ') {
			line = p;
			break;
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	// no line: added at the end. Else its value is replaced by 1.
	char *val, *rest;
	const char* put;
	if (!line) {
		val = rest = buf + n;
		put = n && buf[n - 1] != '\n' ? "\n" FRAMERATE_KEY " 1\n" : FRAMERATE_KEY " 1\n";
	} else {
		val = line + klen + 1;
		size_t vlen = strcspn(val, "\r\n");
		if (vlen == 1 && val[0] == '1')
			return 0;
		rest = val + vlen;
		put = "1";
	}

	char tmp[1100];
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", config) >= (int)sizeof(tmp))
		return -1;
	FILE* o = fopen(tmp, "wb");
	if (!o)
		return -1;
	size_t head = (size_t)(val - buf);
	int ok = fwrite(buf, 1, head, o) == head && fputs(put, o) != EOF && fwrite(rest, 1, strlen(rest), o) == strlen(rest);
	ok = (fclose(o) == 0) && ok;
	if (!ok || rename(tmp, config) != 0) {
		unlink(tmp);
		return -1;
	}
	return 1;
}
