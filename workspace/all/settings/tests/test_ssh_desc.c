// Host-compiled unit test for ssh_desc.c (no device toolchain).
// Build & run:
//   cc -I. ssh_desc.c tests/test_ssh_desc.c -o /tmp/test_ssh_desc && /tmp/test_ssh_desc
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ssh_desc.h"

int main(void) {
	char b[128];

	// --- not running (ip/no_password are irrelevant) ---
	assert(dev_format_ssh_desc(b, sizeof(b), 0, "tina", "192.168.1.8") >= 0);
	assert(strcmp(b, "Start SSH/SFTP server for remote access and file transfer.") == 0);
	assert(dev_format_ssh_desc(b, sizeof(b), 0, NULL, "") >= 0);
	assert(strcmp(b, "Start SSH/SFTP server for remote access and file transfer.") == 0);

	// --- running, ip non-empty, password (tg5040 Brick / Brick Pro) ---
	dev_format_ssh_desc(b, sizeof(b), 1, "tina", "192.168.1.8");
	assert(strcmp(b, "SSH/SFTP active. root@192.168.1.8  Password: tina") == 0);

	// --- running, ip non-empty, no password (tg5050 Smart Pro S) ---
	dev_format_ssh_desc(b, sizeof(b), 1, NULL, "192.168.1.8");
	assert(strcmp(b, "SSH/SFTP active. root@192.168.1.8  No password") == 0);

	// --- running, ip non-empty, root password (tg5050 firmware 1.0.2+) ---
	dev_format_ssh_desc(b, sizeof(b), 1, "root", "192.168.1.8");
	assert(strcmp(b, "SSH/SFTP active. root@192.168.1.8  Password: root") == 0);

	// --- running, ip empty, password ---
	dev_format_ssh_desc(b, sizeof(b), 1, "tina", "");
	assert(strcmp(b, "SSH/SFTP active. Login: root / tina (no network)") == 0);

	// --- running, ip empty, no password ---
	dev_format_ssh_desc(b, sizeof(b), 1, NULL, "");
	assert(strcmp(b, "SSH/SFTP active. Login: root, no password (no network)") == 0);

	// NULL ip is treated the same as an empty one (no network)
	dev_format_ssh_desc(b, sizeof(b), 1, "tina", NULL);
	assert(strcmp(b, "SSH/SFTP active. Login: root / tina (no network)") == 0);

	// --- password per platform / firmware ---
	assert(strcmp(dev_ssh_password(0, NULL), "tina") == 0);
	assert(strcmp(dev_ssh_password(0, "1.1.0"), "tina") == 0);
	assert(dev_ssh_password(1, "1.0.1") == NULL);
	assert(dev_ssh_password(1, "1.0.0") == NULL);
	assert(dev_ssh_password(1, "") == NULL);
	assert(dev_ssh_password(1, NULL) == NULL);
	assert(strcmp(dev_ssh_password(1, "1.0.2"), "root") == 0);
	assert(strcmp(dev_ssh_password(1, "1.0.2\n"), "root") == 0);
	assert(strcmp(dev_ssh_password(1, "1.0.10"), "root") == 0);
	assert(strcmp(dev_ssh_password(1, "1.1.0"), "root") == 0);
	assert(strcmp(dev_ssh_password(1, "2.0.0"), "root") == 0);

	// --- truncation: a tiny buffer must not overflow and must report it ---
	char tiny[8];
	int n = dev_format_ssh_desc(tiny, sizeof(tiny), 1, "tina", "192.168.1.8");
	assert(n >= (int)sizeof(tiny));			  // snprintf-style would-be length
	assert(strlen(tiny) == sizeof(tiny) - 1); // safely NUL-terminated

	// NULL / zero buffer
	assert(dev_format_ssh_desc(NULL, sizeof(b), 1, "tina", "192.168.1.8") == -1);
	assert(dev_format_ssh_desc(b, 0, 1, "tina", "192.168.1.8") == -1);

	// --- longest string still fits the 128-byte on-device buffer ---
	int longest = dev_format_ssh_desc(b, sizeof(b), 1, "tina", "255.255.255.255");
	assert(longest > 0 && longest < (int)sizeof(b));

	printf("test_ssh_desc: all tests passed\n");
	return 0;
}
