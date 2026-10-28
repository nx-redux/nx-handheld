#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "paths.h"

int (*paths_mkdir)(const char* path, mode_t mode) = mkdir;

// mkdir -p; a dir that cannot be made shows up as PICO-8 failing to save, nothing worse
static void make_dirs(const char* dir) {
	char tmp[512];
	if (snprintf(tmp, sizeof(tmp), "%s", dir) >= (int)sizeof(tmp))
		return;
	for (char* p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		paths_mkdir(tmp, 0755);
		*p = '/';
	}
	if (paths_mkdir(tmp, 0755) != 0 && errno != EEXIST)
		fprintf(stderr, "pico8shim: could not create %s\n", tmp);
}

const char* paths_redirect(const char* path, char* buf, size_t size) {
	static int made;
	const char* home = getenv("P8NATIVE_HOME");
	const char* cdata = getenv("P8NATIVE_CDATA");
	if (!path || !home || !*home || !cdata || !*cdata)
		return path;

	size_t hl = strlen(home);
	while (hl > 1 && home[hl - 1] == '/')
		hl--;
	if (strncmp(path, home, hl) != 0 || path[hl] != '/')
		return path;
	const char* p = path + hl;
	while (*p == '/')
		p++;
	if (strncmp(p, "cdata", 5) != 0 || (p[5] != '\0' && p[5] != '/'))
		return path;
	p += 5;
	while (*p == '/')
		p++;

	size_t cl = strlen(cdata);
	while (cl > 1 && cdata[cl - 1] == '/')
		cl--;
	int n = *p ? snprintf(buf, size, "%.*s/%s", (int)cl, cdata, p) : snprintf(buf, size, "%.*s", (int)cl, cdata);
	if (n < 0 || (size_t)n >= size)
		return path;

	if (!made) {
		made = 1;
		char dir[512];
		if (snprintf(dir, sizeof(dir), "%.*s", (int)cl, cdata) < (int)sizeof(dir))
			make_dirs(dir);
	}
	return buf;
}
