// launcher.c's path walks, SDL-free so they can be host-tested (common/tests/test_path_stack.c).

#include "path_stack.h"

#include <limits.h>
#include <string.h>

// segment is exactly `name`
static bool segmentIs(const char* segment, size_t len, const char* name) {
	return strncmp(segment, name, len) == 0 && name[len] == '\0';
}

bool PathStack_isDirectChild(const char* parent_path, const char* child_path, const char* platform) {
	size_t parent_len = strlen(parent_path);
	size_t child_len = strlen(child_path);

	// Child must be longer than parent to be a subdirectory
	if (child_len <= parent_len || strncmp(child_path, parent_path, parent_len) != 0)
		return false;

	// Next char after parent path must be '/'
	if (child_path[parent_len] != '/')
		return false;

	// Walk through the child path after parent, skipping PLATFORM segments
	const char* cursor = child_path + parent_len + 1; // skip the slash

	int levels = 0;
	while (*cursor) {
		const char* next = strchr(cursor, '/');
		size_t segment_len = next ? (size_t)(next - cursor) : strlen(cursor);

		if (segment_len == 0)
			break;
		if (segment_len >= PATH_MAX)
			return false;

		// Count level only if it's not PLATFORM
		if (!segmentIs(cursor, segment_len, platform) && !segmentIs(cursor, segment_len, "Roms"))
			levels++;

		if (!next)
			break;
		cursor = next + 1;
	}

	return levels == 1; // exactly one meaningful level deeper
}

int PathStack_walk(const char* base, const char* path, const char* platform, PathStack_segmentFn cb, void* ctx) {
	char temp_path[PATH_MAX];
	size_t current_len = strlen(base);
	if (current_len == 0 || current_len >= sizeof(temp_path))
		return 0;
	memcpy(temp_path, base, current_len + 1);

	const char* cursor = path + current_len;
	if (*cursor == '/')
		cursor++;

	int walked = 0;
	while (*cursor) {
		const char* next = strchr(cursor, '/');
		size_t segment_len = next ? (size_t)(next - cursor) : strlen(cursor);
		if (segment_len == 0 || segment_len >= PATH_MAX)
			break;

		// Append '/' if needed
		if (temp_path[current_len - 1] != '/') {
			if (current_len + 1 >= PATH_MAX)
				break;
			temp_path[current_len++] = '/';
			temp_path[current_len] = '\0';
		}

		// Append segment
		if (current_len + segment_len >= PATH_MAX)
			break;
		memcpy(temp_path + current_len, cursor, segment_len);
		current_len += segment_len;
		temp_path[current_len] = '\0';

		cb(ctx, temp_path, segmentIs(cursor, segment_len, platform));
		walked++;

		if (!next)
			break;
		cursor = next + 1;
	}
	return walked;
}
