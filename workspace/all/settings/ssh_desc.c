/*
 * ssh_desc.c - Formatter for the Developer page's "Enable SSH / SFTP" hint.
 *
 * Kept free of SDL/api.h so it can be unit-tested on the host
 * (see tests/test_ssh_desc.c).
 */

#include "ssh_desc.h"

#include <stdio.h>

const char* dev_ssh_password(int is_tg5050, const char* fw_version) {
	if (!is_tg5050)
		return "tina";

	int major = 0, minor = 0, patch = 0;
	if (fw_version)
		sscanf(fw_version, "%d.%d.%d", &major, &minor, &patch);
	if (major > 1 || (major == 1 && (minor > 0 || patch >= 2)))
		return "root";
	return NULL;
}

int dev_format_ssh_desc(char* buf, size_t len, int running, const char* password, const char* ip) {
	if (!buf || len == 0)
		return -1;

	int has_ip = (ip && ip[0] != '\0');

	if (!running)
		return snprintf(buf, len, "Start SSH/SFTP server for remote access and file transfer.");

	if (has_ip) {
		if (!password)
			return snprintf(buf, len, "SSH/SFTP active. root@%s  No password", ip);
		return snprintf(buf, len, "SSH/SFTP active. root@%s  Password: %s", ip, password);
	}

	if (!password)
		return snprintf(buf, len, "SSH/SFTP active. Login: root, no password (no network)");
	return snprintf(buf, len, "SSH/SFTP active. Login: root / %s (no network)", password);
}
