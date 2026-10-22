#ifndef PROC_UTIL_H
#define PROC_UTIL_H

// pidof without spawning a shell. Header-only, no SDL; host-tested in
// common/tests/test_proc_util.c.

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// True when a process whose <proc_root>/<pid>/comm is exactly `comm_name`
// exists (comm is the kernel's 15-character process name).
static inline bool proc_running_in(const char* proc_root, const char* comm_name) {
	DIR* proc = opendir(proc_root);
	if (!proc)
		return false;
	bool found = false;
	struct dirent* entry;
	while (!found && (entry = readdir(proc))) {
		if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
			continue;
		char path[512];
		snprintf(path, sizeof(path), "%s/%s/comm", proc_root, entry->d_name);
		FILE* f = fopen(path, "r");
		if (!f)
			continue;
		char comm[64] = {0};
		if (fgets(comm, sizeof(comm), f)) {
			comm[strcspn(comm, "\n")] = '\0';
			found = strcmp(comm, comm_name) == 0;
		}
		fclose(f);
	}
	closedir(proc);
	return found;
}

static inline bool proc_running(const char* comm_name) {
	return proc_running_in("/proc", comm_name);
}

#endif // PROC_UTIL_H
