#include "ma_alias.h"
#include "defines.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>

bool getAlias(char* path, char* alias) {
	bool is_alias = false;
	char* tmp;
	char map_path[MAX_PATH + 8]; // room to swap the filename for "map.txt"
	snprintf(map_path, sizeof(map_path), "%s", path);
	tmp = strrchr(map_path, '/');
	if (tmp) {
		tmp += 1;
		strcpy(tmp, "map.txt");
	}
	char* file_name = strrchr(path, '/');
	if (file_name)
		file_name += 1;

	if (exists(map_path)) {
		FILE* file = fopen(map_path, "r");
		if (file) {
			char line[256];
			while (fgets(line, 256, file) != NULL) {
				normalizeNewline(line);
				trimTrailingNewlines(line);
				if (strlen(line) == 0)
					continue; // skip empty lines

				tmp = strchr(line, '\t');
				if (tmp) {
					tmp[0] = '\0';
					char* key = line;
					char* value = tmp + 1;
					if (exactMatch(file_name, key)) {
						snprintf(alias, MAX_PATH, "%s", value);
						is_alias = true;
						break;
					}
				}
			}
			fclose(file);
		}
	}
	return is_alias;
}
