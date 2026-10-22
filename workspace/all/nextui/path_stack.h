#ifndef PATH_STACK_H
#define PATH_STACK_H

#include <stdbool.h>

// The path walks behind launcher.c's directory stack (isDirectSubdirectory, pathToStack), SDL-free so they can be
// host-tested (common/tests/test_path_stack.c). `platform` is the PLATFORM folder name: Roms/<Console>/<PLATFORM>
// holds a console's platform-specific files and reads as part of the folder above it.

// True when child_path is exactly one level below parent_path ("/a" vs "/a/b"; "/ab" is no child of "/a"), not
// counting PLATFORM or "Roms" segments as levels. Stops at an empty segment ("//").
bool PathStack_isDirectChild(const char* parent_path, const char* child_path, const char* platform);

// Called per segment of PathStack_walk with the directory so far (base plus the segments up to this one). merge is
// true for a PLATFORM segment: it replaces the directory before it rather than being one of its own.
typedef void (*PathStack_segmentFn)(void* ctx, const char* dir_path, bool merge);

// Walks the segments of `path` below `base` (path starts with base). Stops at an empty segment or when the
// directory would not fit in PATH_MAX. Returns how many segments were walked (0 when path is base).
int PathStack_walk(const char* base, const char* path, const char* platform, PathStack_segmentFn cb, void* ctx);

#endif
