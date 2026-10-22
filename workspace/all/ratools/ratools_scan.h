#ifndef __RATOOLS_SCAN_H__
#define __RATOOLS_SCAN_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// Pure helpers behind the achievement prefetch's rom scan (ratools_prefetch.c). No libzip, no
// SDL: host-testable.

// "Game Boy (GB)" -> "GB" (the last parenthesised group). false without one, or when it is empty.
bool rat_dir_tag(const char* dirname, char* tag, size_t n);

// true for a file that is never a game: no extension, or saves, art, configs, disc subchannels...
bool rat_skip_extension(const char* name);

// true if `name` is a raw disc track (bin/img/raw/sub) that belongs to a cue/gdi/ccd/toc index
// rather than being a game on its own.
bool rat_is_track_file(const char* name);

// .zip or .7z (any case)
bool rat_is_archive(const char* name);

// The rom pick rule inside an archive, the way minarch picks it: a file (not a directory) whose
// base name is neither a skipped extension nor another archive, and whose full path fits in n.
bool rat_entry_is_rom(const char* name, size_t n);

// The first rom (rat_entry_is_rom) in a 7zzs "l -slt" technical listing: "Path = ...",
// "Size = ...", "Attributes = D..." per entry, a blank line between entries. Its path goes to
// entry, its unpacked size to *size.
bool rat_7z_pick_entry(FILE* listing, char* entry, size_t n, long long* size);

// The libretro core a pak's launch.sh runs, as minarch's core.name: the EMU_EXE= value (stock and
// most community paks), else the name in front of the first "_libretro.so". Empty when the script
// is missing or names neither (e.g. a standalone emulator), or the name is built from a $variable.
void rat_launch_core(const char* launch_path, char* out, size_t n);

#endif
