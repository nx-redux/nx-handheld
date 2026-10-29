#ifndef CORE_ARGS_H
#define CORE_ARGS_H

// Pure helpers behind pico8native_libretro.so (core.c), kept apart so the host
// tests can run them without a frontend.

#include <stddef.h>

// pico8_64 -home <home> -run <cart>, NULL-terminated in argv. Returns argc, or -1
// if argv (max slots including the NULL) is too small or a string is missing.
int core_build_argv(const char* bin, const char* home, const char* cart, char** argv, int max);

// out = base with each "NAME=value" in set (NULL-terminated) replacing the
// variable of that name or added at the end; NULL-terminated. The strings are
// borrowed, not copied. Returns the count, or -1 if out (max slots including
// the NULL) is too small.
int core_build_env(char* const* base, const char* const* set, char** out, int max);

// Finds the dir holding both pico8_64 and pico8.dat: system_dir (Bios/P8), then
// its sibling Bios/PICO. Returns 0 with dir_out filled, -1 if neither has both.
int core_find_runtime(const char* system_dir, char* dir_out, size_t size);

// PICO-8 must keep host_framerate_control 1 to present under the shim. Rewrites
// the line in <config> to 1 if it says otherwise, or adds it if the file has none;
// a missing file is left alone (PICO-8 writes its default, 1). Returns 1 if
// changed, 0 if not, -1 on error.
int core_force_framerate_control(const char* config);

#endif
