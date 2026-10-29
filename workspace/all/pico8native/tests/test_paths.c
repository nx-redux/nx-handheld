// Host test for shim/paths.c: PICO-8's home cdata/ → $P8NATIVE_CDATA.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../shim/paths.h"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

static char root[64], home[128], cdata[128];

static void cleanup(void) {
	char cmd[96];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
	if (system(cmd) != 0)
		fprintf(stderr, "could not remove %s\n", root);
}

// paths_redirect(in) must give expect; NULL expect means in comes back unchanged
static void expect(const char* in, const char* want) {
	char buf[256];
	const char* got = paths_redirect(in, buf, sizeof(buf));
	if (!want) {
		if (got != in) {
			fprintf(stderr, "'%s' should stay, got '%s'\n", in, got);
			exit(1);
		}
		return;
	}
	if (got != buf || strcmp(got, want) != 0) {
		fprintf(stderr, "'%s' should be '%s', got '%s'\n", in, want, got);
		exit(1);
	}
}

int main(void) {
	snprintf(root, sizeof(root), "/tmp/nx_test_paths.%d", (int)getpid());
	snprintf(home, sizeof(home), "%s/home", root);
	snprintf(cdata, sizeof(cdata), "%s/Saves/P8/cdata", root);
	atexit(cleanup);
	CHECK(mkdir(root, 0755) == 0);

	char in[256], want[256];

	// Not configured: nothing moves
	unsetenv("P8NATIVE_HOME");
	unsetenv("P8NATIVE_CDATA");
	snprintf(in, sizeof(in), "%s/cdata/foo.p8d.txt", home);
	expect(in, NULL);

	setenv("P8NATIVE_HOME", home, 1);
	setenv("P8NATIVE_CDATA", cdata, 1);

	// The save dir does not exist until the first redirect makes it, parents too
	struct stat st;
	CHECK(stat(cdata, &st) != 0);

	snprintf(in, sizeof(in), "%s/cdata/foo.p8d.txt", home);
	snprintf(want, sizeof(want), "%s/foo.p8d.txt", cdata);
	expect(in, want);
	CHECK(stat(cdata, &st) == 0 && S_ISDIR(st.st_mode));

	// The dir itself, for mkdir/opendir/stat, with or without a trailing slash
	snprintf(in, sizeof(in), "%s/cdata", home);
	expect(in, cdata);
	snprintf(in, sizeof(in), "%s/cdata/", home);
	expect(in, cdata);

	// Doubled slashes, and a home given with a trailing slash
	snprintf(in, sizeof(in), "%s//cdata//foo.p8d.txt", home);
	snprintf(want, sizeof(want), "%s/foo.p8d.txt", cdata);
	expect(in, want);
	char home_slash[160];
	snprintf(home_slash, sizeof(home_slash), "%s/", home);
	setenv("P8NATIVE_HOME", home_slash, 1);
	snprintf(in, sizeof(in), "%s/cdata/bar.p8d.txt", home);
	snprintf(want, sizeof(want), "%s/bar.p8d.txt", cdata);
	expect(in, want);
	setenv("P8NATIVE_HOME", home, 1);

	// Everything else stays where PICO-8 put it
	snprintf(in, sizeof(in), "%s/config.txt", home);
	expect(in, NULL);
	snprintf(in, sizeof(in), "%s/carts/cdata/foo.p8d.txt", home);
	expect(in, NULL);
	snprintf(in, sizeof(in), "%s/cdatax/foo.p8d.txt", home);
	expect(in, NULL);
	snprintf(in, sizeof(in), "%sx/cdata/foo.p8d.txt", home); // a sibling whose name starts with home's
	expect(in, NULL);
	expect("/mnt/SDCARD/cdata/foo.p8d.txt", NULL);
	expect("cdata/foo.p8d.txt", NULL);
	expect(NULL, NULL);

	// No room for the result: the original path, never a cut-off one
	char tiny[8];
	snprintf(in, sizeof(in), "%s/cdata/foo.p8d.txt", home);
	CHECK(paths_redirect(in, tiny, sizeof(tiny)) == in);

	puts("test_paths: ok");
	return 0;
}
