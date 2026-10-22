// Host test for file_util.h (mkdir -p helpers, capped whole-file reader).
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../file_util.h"

#define DIR_ "/tmp/nx_fx_file_util"

static bool is_dir(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void write_file(const char* path, const char* body, size_t len) {
	FILE* f = fopen(path, "wb");
	assert(f);
	fwrite(body, 1, len, f);
	fclose(f);
}

int main(void) {
	assert(system("rm -rf " DIR_) == 0);

	mkdir_p_mode(DIR_ "/a/b/c", 0755);
	assert(is_dir(DIR_ "/a/b/c"));
	mkdir_p_mode(DIR_ "/a/b/c", 0755); // existing: no-op
	mkdir_p_mode(DIR_ "/t/", 0755);	   // trailing slash
	assert(is_dir(DIR_ "/t"));
	mkdir_p_mode("", 0755); // empty: no-op

	mkdir_parent_mode(DIR_ "/p/q/file.jpg", 0755);
	assert(is_dir(DIR_ "/p/q") && !is_dir(DIR_ "/p/q/file.jpg"));
	mkdir_parent_mode("file.jpg", 0755);  // no directory part: no-op
	mkdir_parent_mode("/file.jpg", 0755); // root parent: no-op

	long len = -1;
	write_file(DIR_ "/f.txt", "hello", 5);
	char* s = allocFileCapped(DIR_ "/f.txt", LONG_MAX, &len);
	assert(s && strcmp(s, "hello") == 0 && len == 5);
	free(s);
	s = allocFileCapped(DIR_ "/f.txt", 5, NULL); // exactly at the cap
	assert(s && strcmp(s, "hello") == 0);
	free(s);
	assert(allocFileCapped(DIR_ "/f.txt", 4, &len) == NULL); // over the cap
	assert(allocFileCapped(DIR_ "/missing", LONG_MAX, NULL) == NULL);

	write_file(DIR_ "/empty", "", 0);
	s = allocFileCapped(DIR_ "/empty", 0, &len);
	assert(s && s[0] == '\0' && len == 0);
	free(s);

	write_file(DIR_ "/bin", "a\0b", 3); // embedded NUL: length still reported
	s = allocFileCapped(DIR_ "/bin", 16, &len);
	assert(s && len == 3 && s[0] == 'a' && s[1] == '\0' && s[2] == 'b' && s[3] == '\0');
	free(s);

	assert(system("rm -rf " DIR_) == 0);
	printf("test_file_util: OK\n");
	return 0;
}
