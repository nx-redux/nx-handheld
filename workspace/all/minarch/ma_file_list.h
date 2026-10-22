#pragma once

// The regular files in folderPath, skipping macOS "._*" and ".DS_Store" and,
// with extensionFilter (eg. ".cfg"), files without that extension. With
// defaultElement, it comes first and the files follow, sorted by name.
// NULL-terminated; *fileCount is the number of entries (0 on NULL).
// Plain C, host-tested by tests/test_file_list.c.
char** list_files_in_folder(const char* folderPath, int* fileCount, const char* defaultElement, const char* extensionFilter);
void free_file_list(char** list);
