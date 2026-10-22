#include "ma_file_list.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char** list_files_in_folder(const char* folderPath, int* fileCount, const char* defaultElement, const char* extensionFilter) {
	*fileCount = 0; // callers read this even when we return NULL

	DIR* dir = opendir(folderPath);
	if (!dir) {
		perror("opendir");
		return NULL;
	}

	struct dirent* entry;
	struct stat fileStat;
	char** fileList = NULL;

	if (defaultElement) {
		fileList = malloc(sizeof(char*) * 2);
		char* first = strdup(defaultElement);
		if (!fileList || !first) {
			free(fileList);
			free(first);
			closedir(dir);
			return NULL;
		}
		fileList[0] = first;
		fileList[1] = NULL;
		(*fileCount)++;
	}

	while ((entry = readdir(dir)) != NULL) {
		// skip all entries starting with ._ (hidden files on macOS)
		if (entry->d_name[0] == '.' && entry->d_name[1] == '_')
			continue;
		// skip macOS .DS_Store files
		if (strcmp(entry->d_name, ".DS_Store") == 0)
			continue;

		char fullPath[1024];
		snprintf(fullPath, sizeof(fullPath), "%s/%s", folderPath, entry->d_name);

		if (stat(fullPath, &fileStat) == 0 && S_ISREG(fileStat.st_mode)) {
			if (extensionFilter) {
				const char* ext = strrchr(entry->d_name, '.');
				if (!ext || strcmp(ext, extensionFilter) != 0) {
					continue;
				}
			}

			char** temp = realloc(fileList, sizeof(char*) * (*fileCount + 1));
			if (!temp) {
				perror("realloc");
				for (int i = 0; i < *fileCount; ++i) {
					free(fileList[i]);
				}
				free(fileList);
				closedir(dir);
				*fileCount = 0;
				return NULL;
			}
			fileList = temp;
			fileList[*fileCount] = strdup(entry->d_name);
			if (fileList[*fileCount]) // out of memory: leave the file out
				(*fileCount)++;
		}
	}

	closedir(dir);

	// Alphabetical sort, after the default element: an overlay named "GB.png"
	// sorted ahead of "None" and was on by default (index 0)
	for (int i = defaultElement ? 1 : 0; i < *fileCount - 1; ++i) {
		for (int j = i + 1; j < *fileCount; ++j) {
			if (strcmp(fileList[i], fileList[j]) > 0) {
				char* temp = fileList[i];
				fileList[i] = fileList[j];
				fileList[j] = temp;
			}
		}
	}

	// NUll terminate the list
	char** temp = realloc(fileList, sizeof(char*) * (*fileCount + 1));
	if (!temp) {
		perror("realloc");
		for (int i = 0; i < *fileCount; ++i) {
			free(fileList[i]);
		}
		free(fileList);
		*fileCount = 0;
		return NULL;
	}
	fileList = temp;
	fileList[*fileCount] = NULL;

	return fileList;
}

void free_file_list(char** list) {
	if (!list)
		return;
	for (int i = 0; list[i]; i++)
		free(list[i]);
	free(list);
}
