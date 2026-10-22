#include "sync_rsyncd.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "defines.h"
#include "utils.h"

const char* sync_rsyncd_bin(void) {
	static char path[MAX_PATH];
	if (!path[0])
		snprintf(path, sizeof(path), "%s/rsync", SHARED_BIN_PATH);
	return path;
}

int sync_rsyncd_start(const char* conf_path, const char* pid_path) {
	// No user data in this command line; quote the binary against spaces all
	// the same (an escape that doesn't fit leaves '' and the start fails)
	char bin_q[MAX_PATH * 4];
	shellEscapeQuotes(bin_q, sizeof(bin_q), sync_rsyncd_bin());
	char cmd[MAX_PATH * 4 + 128];
	snprintf(cmd, sizeof(cmd), "'%s' --daemon --config=%s", bin_q, conf_path);
	if (system(cmd) != 0)
		return -1;

	usleep(500000);
	return access(pid_path, F_OK) == 0 ? 0 : -1;
}

void sync_rsyncd_stop(const char* pid_path, const char* conf_path) {
	FILE* fp = fopen(pid_path, "r");
	if (fp) {
		int pid = 0;
		if (fscanf(fp, "%d", &pid) == 1 && pid > 0)
			kill(pid, SIGTERM);
		fclose(fp);
		unlink(pid_path);
	}
	system("killall rsync 2>/dev/null");
	unlink(conf_path);
}
