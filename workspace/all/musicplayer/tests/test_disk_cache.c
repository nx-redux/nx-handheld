// Host test for disk_cache.c: the album-art / lyrics cache cap.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "disk_cache.h"

static char dir[] = "/tmp/nx_test_disk_cacheXXXXXX";

static void make(const char* name, int bytes, long mtime) {
	char p[256];
	snprintf(p, sizeof(p), "%s/%s", dir, name);
	FILE* f = fopen(p, "wb");
	assert(f);
	for (int i = 0; i < bytes; i++)
		fputc('x', f);
	fclose(f);
	struct timeval tv[2] = {{mtime, 0}, {mtime, 0}};
	utimes(p, tv);
}

static int present(const char* name) {
	char p[256];
	snprintf(p, sizeof(p), "%s/%s", dir, name);
	return access(p, F_OK) == 0;
}

int main(void) {
	if (!mkdtemp(dir))
		return 1;

	make("a.jpg", 100, 1000);
	make("b.jpg", 100, 2000);
	make("c.jpg", 100, 3000);
	make("d.jpg", 100, 4000);
	make(".hidden", 5000, 1);

	// under both caps: nothing removed
	assert(DiskCache_trim(dir, 10, 100000) == 0);
	assert(present("a.jpg") && present("d.jpg"));

	// count cap: the two oldest go
	assert(DiskCache_trim(dir, 2, 100000) == 2);
	assert(!present("a.jpg") && !present("b.jpg") && present("c.jpg") && present("d.jpg"));

	// byte cap: 200 bytes left, cap 150 -> the older one goes
	assert(DiskCache_trim(dir, 10, 150) == 1);
	assert(!present("c.jpg") && present("d.jpg"));
	assert(present(".hidden")); // dot files are not cache entries

	// missing dir
	assert(DiskCache_trim("/nonexistent/nx", 1, 1) == 0);

	char p[256];
	const char* left[] = {"d.jpg", ".hidden"};
	for (int i = 0; i < 2; i++) {
		snprintf(p, sizeof(p), "%s/%s", dir, left[i]);
		unlink(p);
	}
	rmdir(dir);
	printf("test_disk_cache: all assertions passed\n");
	return 0;
}
