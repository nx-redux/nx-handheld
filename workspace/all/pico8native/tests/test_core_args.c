// Host test for core_args.c: pico8_64's argv and env, the Bios lookup and the
// host_framerate_control fix-up that pico8native_libretro.so does before a launch.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../core_args.h"

#define CHECK(x)                                                                  \
	do {                                                                          \
		if (!(x)) {                                                               \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); \
			exit(1);                                                              \
		}                                                                         \
	} while (0)

// argv must equal want (NULL-terminated) exactly, element for element
static void expect_vec(char** got, int n, const char* const* want) {
	int i = 0;
	for (; want[i]; i++) {
		if (i >= n || !got[i] || strcmp(got[i], want[i]) != 0) {
			fprintf(stderr, "argv[%d] should be '%s', got '%s'\n", i, want[i], i < n && got[i] ? got[i] : "(null)");
			exit(1);
		}
	}
	CHECK(n == i);
	CHECK(got[i] == NULL);
}

static void test_argv(void) {
	const char* bin = "/mnt/SDCARD/Bios/P8/pico8_64";
	const char* home = "/mnt/SDCARD/.userdata/shared/P8-native/home";
	const char* cart = "/mnt/SDCARD/Roms/PICO-8 (P8)/Celeste Classic.p8.png";
	char* argv[16];

	int n = core_build_argv(bin, home, cart, argv, 16);
	const char* run[] = {bin, "-home", home, "-run", cart, NULL};
	expect_vec(argv, n, run);

	CHECK(core_build_argv(bin, home, NULL, argv, 16) == -1); // a cart is needed (Splore no longer runs here)
	CHECK(core_build_argv(bin, NULL, cart, argv, 16) == -1);
	CHECK(core_build_argv(bin, home, cart, argv, 6) == 5); // exactly fits with the NULL
	CHECK(core_build_argv(bin, home, cart, argv, 5) == -1);
}

static void test_env(void) {
	char* base[] = {"PATH=/usr/bin", "LD_PRELOAD=/lib/libosdwait.so", "LD_PRELOADX=keep", "HOME=/root", NULL};
	const char* set[] = {"LD_PRELOAD=/pak/pico8shim.so", "P8NATIVE_IPC=/tmp/p8native.1.ipc", NULL};
	char* out[8];
	int n = core_build_env(base, set, out, 8);
	const char* want[] = {"PATH=/usr/bin", "LD_PRELOAD=/pak/pico8shim.so", "LD_PRELOADX=keep", "HOME=/root",
						  "P8NATIVE_IPC=/tmp/p8native.1.ipc", NULL};
	expect_vec(out, n, want);
	CHECK(core_build_env(base, set, out, 6) == 5);
	CHECK(core_build_env(base, set, out, 5) == -1);

	char* empty[] = {NULL};
	n = core_build_env(empty, set, out, 8);
	const char* only_set[] = {"LD_PRELOAD=/pak/pico8shim.so", "P8NATIVE_IPC=/tmp/p8native.1.ipc", NULL};
	expect_vec(out, n, only_set);
}

static char root[64];

static void touch(const char* dir, const char* name) {
	char p[256];
	snprintf(p, sizeof(p), "%s/%s", dir, name);
	FILE* f = fopen(p, "w");
	CHECK(f);
	fclose(f);
}

static void test_runtime(void) {
	char p8[128], pico[128], dir[256];
	snprintf(p8, sizeof(p8), "%s/Bios/P8", root);
	snprintf(pico, sizeof(pico), "%s/Bios/PICO", root);
	char cmd[320];
	snprintf(cmd, sizeof(cmd), "mkdir -p '%s' '%s'", p8, pico);
	CHECK(system(cmd) == 0);

	CHECK(core_find_runtime(p8, dir, sizeof(dir)) == -1); // neither
	touch(p8, "pico8_64");
	touch(pico, "pico8.dat");
	CHECK(core_find_runtime(p8, dir, sizeof(dir)) == -1); // split across both doesn't count
	touch(pico, "pico8_64");
	CHECK(core_find_runtime(p8, dir, sizeof(dir)) == 0); // Bios/PICO, the sibling
	CHECK(strcmp(dir, pico) == 0);
	char slash[160];
	snprintf(slash, sizeof(slash), "%s/", p8); // a trailing slash still finds the sibling
	CHECK(core_find_runtime(slash, dir, sizeof(dir)) == 0);
	CHECK(strcmp(dir, pico) == 0);
	touch(p8, "pico8.dat");
	CHECK(core_find_runtime(p8, dir, sizeof(dir)) == 0); // Bios/P8 wins
	CHECK(strcmp(dir, p8) == 0);
	CHECK(core_find_runtime(p8, dir, 8) == -1); // does not fit
	CHECK(core_find_runtime(NULL, dir, sizeof(dir)) == -1);
}

static void write_file(const char* path, const char* text) {
	FILE* f = fopen(path, "w");
	CHECK(f);
	fputs(text, f);
	fclose(f);
}

static void expect_file(const char* path, const char* text) {
	char buf[1024] = {0};
	FILE* f = fopen(path, "r");
	CHECK(f);
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	if (strcmp(buf, text) != 0) {
		fprintf(stderr, "%s should be:\n%s\ngot:\n%s\n", path, text, buf);
		exit(1);
	}
}

static void test_config(void) {
	char cfg[128];
	snprintf(cfg, sizeof(cfg), "%s/config.txt", root);

	CHECK(core_force_framerate_control(cfg) == 0); // no file: PICO-8 writes its default

	const char* on = "// :: Video Settings\nwindow_size 0 0\nhost_framerate_control 1\nshow_fps 0\n";
	write_file(cfg, on);
	CHECK(core_force_framerate_control(cfg) == 0);
	expect_file(cfg, on);

	write_file(cfg, "// :: Video Settings\nwindow_size 0 0\nhost_framerate_control 0\nshow_fps 0\n");
	CHECK(core_force_framerate_control(cfg) == 1);
	expect_file(cfg, on);

	// last line, no newline, other value
	write_file(cfg, "show_fps 0\nhost_framerate_control 2");
	CHECK(core_force_framerate_control(cfg) == 1);
	expect_file(cfg, "show_fps 0\nhost_framerate_control 1");

	// a longer name sharing the prefix is not it: the real line is added at the end
	write_file(cfg, "host_framerate_control_x 0\nshow_fps 0\n");
	CHECK(core_force_framerate_control(cfg) == 1);
	expect_file(cfg, "host_framerate_control_x 0\nshow_fps 0\nhost_framerate_control 1\n");

	// a config without the line gets it, after a last line with no newline too
	write_file(cfg, "show_fps 0");
	CHECK(core_force_framerate_control(cfg) == 1);
	expect_file(cfg, "show_fps 0\nhost_framerate_control 1\n");
	CHECK(core_force_framerate_control(cfg) == 0); // and is then left alone
	expect_file(cfg, "show_fps 0\nhost_framerate_control 1\n");

	// an empty config (PICO-8 cut short writing it) too
	write_file(cfg, "");
	CHECK(core_force_framerate_control(cfg) == 1);
	expect_file(cfg, "host_framerate_control 1\n");
}

int main(void) {
	snprintf(root, sizeof(root), "/tmp/nx_test_core_args.%d", (int)getpid());
	test_argv();
	test_env();
	test_runtime();
	test_config();
	char cmd[96];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", root);
	if (system(cmd) != 0)
		fprintf(stderr, "could not remove %s\n", root);
	printf("test_core_args: ok\n");
	return 0;
}
