#ifndef _GNU_SOURCE
#define _GNU_SOURCE // O_DIRECTORY, unlinkat
#endif

#include "wiz_sync_util.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

//////////////////////////////////
// Pure helpers
//////////////////////////////////
//
// Split out of wizard_sync.c: no sockets, no SDL, no daemon. Text in, text out,
// plus the staging directory (filesystem only), so tests/run_tests.sh runs them
// on a development host.

// A value on its way into rsyncd.conf. That file is line-oriented (one directive
// per line, value to end of line), so a \n or \r inside a value would end the
// directive early and start one the caller never wrote — "hosts allow = x\nhosts
// allow = *" being the case that matters. Both values written here are trusted by
// construction; this is what makes that assumption checkable rather than assumed.
bool wiz_conf_value_is_safe(const char* value) {
	if (!value || !value[0])
		return false;

	for (const char* c = value; *c; c++)
		if (*c == '\n' || *c == '\r')
			return false;

	return true;
}

// A dotted quad, by shape. `hosts allow` also accepts names, ranges and
// patterns, so anything that is not digits and dots would WIDEN that ACL rather
// than narrow it ("*" being the extreme). wizard_net.c fills peer_ip from
// inet_ntop() and the client dials an address read the same way, so [0-9.] is
// the entire alphabet either side can produce. Shape only — this is not an
// address parser, and rsync validates the value itself.
bool wiz_ip_is_plain(const char* ip) {
	int digits = 0;

	if (!ip || !ip[0] || strlen(ip) >= 16)
		return false;

	for (const char* c = ip; *c; c++) {
		if (*c >= '0' && *c <= '9') {
			digits++;
			continue;
		}
		if (*c == '.')
			continue;
		return false;
	}

	return digits > 0;
}

// The whitelist wiz_proto.c's wiz_name_is_safe() applies on the wire, re-applied at this module's
// own boundary. Not redundancy for its own sake:
//   - these names are remote input and this file hands them to an exec and to
//     unlinkat(); a '/' or a ".." would reach outside fetch_to either way,
//   - wiz_name_is_safe() belongs to the wire protocol (wiz_proto.c), not to this module,
//     and
//   - a caller added later (a bench harness, say) must not be
//     able to skip the check by not being wizard_net.c.
// Deliberately identical to that one, down to the explicit ranges instead of
// isalnum() (locale-dependent); if either moves, both move.
bool wiz_sync_name_is_safe(const char* name) {
	if (!name || !name[0])
		return false;
	if (strlen(name) >= WIZ_SYNC_NAME_MAX)
		return false;
	if (name[0] == '-') // rsync would read it as an option
		return false;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return false;

	for (const char* c = name; *c; c++) {
		if (*c >= 'A' && *c <= 'Z')
			continue;
		if (*c >= 'a' && *c <= 'z')
			continue;
		if (*c >= '0' && *c <= '9')
			continue;
		if (*c == '.' || *c == '_' || *c == '-')
			continue;
		return false;
	}

	return true;
}

// Index of the first name that repeats an earlier one, or -1 when the set is
// what it claims to be: a set. O(n²) over at most WIZ_NET_SYNC_MAX_FILES = 32
// names, i.e. free.
int wiz_find_duplicate(char names[][WIZ_SYNC_NAME_MAX], int count) {
	for (int i = 1; i < count; i++)
		for (int j = 0; j < i; j++)
			if (strcmp(names[i], names[j]) == 0)
				return i;

	return -1;
}

// The percentage an --info=progress2 line carries, or -1 for a line that carries
// none (rsync's banners, blank lines). The line reads
//
//     131,072 100%   12.50MB/s    0:00:00 (xfr#1, to-chk=0/1)
//
// so the first '%' with digits in front of it is the transfer percentage; the
// rate that follows carries no '%' of its own. Bytes, not files, because this bar
// is per-file: sync.c's parse_progress2() counts files out of "to-chk=X/Y", which for a
// single-file pull only ever reads 0% then 100%.
int wiz_progress_percent(const char* line) {
	for (const char* c = line; *c; c++) {
		const char* start = c;
		int percent;

		if (*c != '%')
			continue;

		while (start > line && start[-1] >= '0' && start[-1] <= '9')
			start--;
		if (start == c)
			continue; // a '%' with no number in front of it

		percent = atoi(start);
		if (percent < 0)
			percent = 0;
		if (percent > 100)
			percent = 100;
		return percent;
	}

	return -1;
}

// Consume every complete line in buf, returning the newest percentage they
// carried (or `current` when none did) and moving the unterminated tail to the
// front. Lines end with \r OR \n: --info=progress2 redraws in place with \r and
// only ends with \n, which is why sync.c's read_line_cr() reads the same way. This one works
// off a buffer rather than fgetc() because the caller has to select() on the same
// descriptor for its deadline, and stdio's buffer is invisible to select().
//
// A buffer that fills up with no terminator in it at all is not progress output,
// so it is dropped rather than grown — which is also what guarantees the caller
// always has room to read into.
int wiz_take_percent(char* buf, size_t size, size_t* len, int current) {
	size_t start = 0;
	int percent = current;

	for (size_t i = 0; i < *len; i++) {
		if (buf[i] != '\r' && buf[i] != '\n')
			continue;

		buf[i] = '\0';
		if (i > start) {
			int found = wiz_progress_percent(buf + start);
			if (found >= 0)
				percent = found;
		}
		start = i + 1;
	}

	if (start > 0) {
		memmove(buf, buf + start, *len - start);
		*len -= start;
	} else if (*len == size) {
		*len = 0;
	}

	return percent;
}

//////////////////////////////////
// Staging directory
//////////////////////////////////
//
// Nothing the host sends is written into fetch_to. Every file lands in
// fetch_to/.netplay-staging first and is renamed in only once the WHOLE set has
// arrived, which is what makes the all-or-nothing rule true rather than
// approximately true.
//
// The rule this replaces — pull straight into fetch_to and unlink the names
// again on failure — cannot tell "rsync wrote this" from "this was already
// here". `-t` without `--checksum` SKIPS a file whose size and mtime already
// match, so two devices that synced yesterday and fail today would have had an
// intact, untouched local save deleted by the failure path. That is silent save
// loss on a device whose entire purpose is saves.
//
// No SDL and no network below: filesystem only, so the whole flow is exercisable
// on a development host.

// fetch_to/.netplay-staging.
void wiz_stage_path(char* out, size_t out_size, const char* fetch_to) {
	snprintf(out, out_size, "%s/%s", fetch_to, WIZ_SYNC_STAGE_DIR);
}

// The staging directory and everything in it, gone. Runs as the failure path,
// after a successful commit (by then it is empty), and again before a pull
// starts — a directory that already exists belongs to a run that was killed, so
// it is neither ours to commit nor the user's to keep.
//
// ONE LEVEL DEEP, deliberately. rsync is invoked without -r and without -d, so
// it never creates a directory in the destination; the only things in here are
// this file's own single-file pulls. The AT_REMOVEDIR fallback below therefore
// only ever has to clear an EMPTY directory left by something else. A non-empty
// one would survive, rmdir() would fail, and the next wiz_stage_prepare() would
// then fail on mkdir() and take wiz_sync_pull() out with fetch_to untouched —
// i.e. this fails closed, which is why it does not need to recurse.
void wiz_stage_purge(const char* fetch_to) {
	char path[WIZ_SYNC_PATH_MAX];
	DIR* dir;

	wiz_stage_path(path, sizeof(path), fetch_to);

	dir = opendir(path);
	if (dir) {
		int dir_fd = dirfd(dir);
		struct dirent* entry;

		while ((entry = readdir(dir)) != NULL) {
			if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
				continue;
			if (dir_fd < 0)
				continue;
			// Only ever our own pulls in here, but a directory left by anything
			// else must not stop the purge from finishing.
			if (unlinkat(dir_fd, entry->d_name, 0) != 0)
				unlinkat(dir_fd, entry->d_name, AT_REMOVEDIR);
		}

		closedir(dir);
	}

	rmdir(path);
}

// An empty staging directory, ready to receive. 0 = it exists and is empty.
int wiz_stage_prepare(const char* fetch_to) {
	char path[WIZ_SYNC_PATH_MAX];

	wiz_stage_purge(fetch_to);
	wiz_stage_path(path, sizeof(path), fetch_to);

	return mkdir(path, 0755) == 0 ? 0 : -1;
}

// Move the finished set into fetch_to, one rename() per name. Same filesystem by
// construction (the staging directory is inside fetch_to), so each rename is
// atomic and replaces a local save only at the instant the whole set is known
// good. 0 = every name moved.
//
// A failure part-way leaves the earlier renames in place: POSIX cannot move a
// set atomically. That window is the microseconds between two renames in a
// directory this process owns, against the multi-second transfer window the
// staging directory removes — and every rename that did land is a complete,
// correct file, so a retry simply redoes them. Best effort rather than
// stop-at-first-failure for the same reason: the closer to the full set, the
// better off both sides are.
int wiz_stage_commit(const char* fetch_to, char names[][WIZ_SYNC_NAME_MAX], int count) {
	char path[WIZ_SYNC_PATH_MAX];
	int stage_fd;
	int dest_fd;
	int result = 0;

	wiz_stage_path(path, sizeof(path), fetch_to);

	stage_fd = open(path, O_RDONLY | O_DIRECTORY);
	if (stage_fd < 0)
		return -1;

	dest_fd = open(fetch_to, O_RDONLY | O_DIRECTORY);
	if (dest_fd < 0) {
		close(stage_fd);
		return -1;
	}

	for (int i = 0; i < count; i++)
		if (renameat(stage_fd, names[i], dest_fd, names[i]) != 0)
			result = -1;

	close(dest_fd);
	close(stage_fd);
	return result;
}
