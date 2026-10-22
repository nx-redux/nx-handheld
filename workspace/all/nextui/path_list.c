#include "path_list.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

bool PathList_save(const char* file, Array* items, PathListGetFn get) {
	size_t cap = 1, len = 0;
	for (int i = 0; i < items->count; i++) {
		const char *path, *label;
		get(items->items[i], &path, &label);
		cap += strlen(path) + 2 + (label ? strlen(label) : 0);
	}
	char* buf = malloc(cap);
	if (!buf)
		return false;
	for (int i = 0; i < items->count; i++) {
		const char *path, *label;
		get(items->items[i], &path, &label);
		len += (size_t)snprintf(buf + len, cap - len, "%s%s%s\n", path, label ? "\t" : "", label ? label : "");
	}
	buf[len] = '\0';
	bool ok = writeFileAtomic(file, buf, len);
	free(buf);
	return ok;
}

bool PathList_nextLine(FILE* f, char* line, size_t size, char** path, char** label, int* skipped) {
	while (fgets(line, (int)size, f) != NULL) {
		size_t len = strlen(line);
		if (len == size - 1 && line[len - 1] != '\n') {
			int c;
			while ((c = fgetc(f)) != EOF && c != '\n')
				;
			if (skipped)
				(*skipped)++;
			continue;
		}
		normalizeNewline(line);
		trimTrailingNewlines(line);
		if (line[0] == '\0')
			continue;
		*path = line;
		*label = NULL;
		char* tab = strchr(line, '\t');
		if (tab) {
			*tab = '\0';
			*label = tab + 1;
		}
		return true;
	}
	return false;
}
