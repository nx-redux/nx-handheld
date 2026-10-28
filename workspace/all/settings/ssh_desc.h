#ifndef SSH_DESC_H
#define SSH_DESC_H

#include <stddef.h>

// The stock firmware's root SSH password, or NULL when it has none.
//
// tg5040 (Brick / Brick Pro): "tina".
// tg5050 (Smart Pro S): none up to firmware 1.0.1, "root" from 1.0.2 on.
// fw_version is the /etc/version text (e.g. "1.0.2"); NULL or unparsable
// counts as the oldest firmware.
const char* dev_ssh_password(int is_tg5050, const char* fw_version);

// Format the "Enable SSH / SFTP" hint text into buf.
//
// running  - non-zero when sshd is up
// password - the root password, or NULL when there is none
// ip       - the device IP, or NULL/"" when not on a network
//
// Returns like snprintf: the number of characters that WOULD have been written
// (a value >= len means the text was truncated), or -1 on a NULL/zero buffer.
// Pure and host-compilable: no SDL, no api.h.
int dev_format_ssh_desc(char* buf, size_t len, int running, const char* password, const char* ip);

#endif // SSH_DESC_H
