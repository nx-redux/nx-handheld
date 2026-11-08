// sync_pair: the pairing check of a Device Sync run (protocol v3). Both
// devices derive a 4-digit code the user compares on the two screens and the
// password of the run's rsync daemon from the two (ip, nonce) pairs, so a
// device that answered the discovery in the middle can neither pass for the
// peer nor reach the daemon. The phone app derives the same; the test vectors
// in tests/ are shared with it.
#ifndef SYNC_PAIR_H
#define SYNC_PAIR_H

#include <stdbool.h>
#include <stdio.h>

// 16 lowercase hex characters (64 bits), new for every run.
#define SYNC_NONCE_LEN 16
#define SYNC_PAIR_CODE_LEN 4
#define SYNC_PAIR_PASSWORD_LEN 32
// The daemon's only user; the client puts it in every rsync URL.
#define SYNC_PAIR_USER "nxsync"
// The start of every client rsync URL: "<ip>:<port>/<module>/..." follows.
#define SYNC_RSYNC_URL "rsync://" SYNC_PAIR_USER "@"
#define SYNC_PAIR_PASSWORD_ENV "RSYNC_PASSWORD"
#define SYNC_SECRETS_NAME "rsyncd.secrets"
#define SYNC_LOCK_NAME "rsyncd.lock"
// A finished client's daemon process lingers a moment, so back-to-back runs (the
// ROM phases' listings) overlap: well above one at a time.
#define SYNC_MAX_CONNECTIONS 16

bool sync_nonce_valid(const char* s, size_t len);

// A fresh nonce from /dev/urandom into out (SYNC_NONCE_LEN + 1 bytes). False
// when the random bytes can't be read: the run must not start then.
bool sync_nonce_new(char* out);

// code (SYNC_PAIR_CODE_LEN + 1 bytes) and password (SYNC_PAIR_PASSWORD_LEN + 1
// bytes) of a pairing; either side's argument order gives the same result.
// False when an ip or a nonce is unusable.
bool sync_pair_derive(const char* own_ip, const char* own_nonce, const char* peer_ip, const char* peer_nonce,
					  char* code, char* password);

// The rsyncd.conf global lines that close the daemon to everyone but the
// paired peer with the run's password; run_dir holds the secrets and lock
// files. False when a write fails.
bool sync_daemon_auth_lines(FILE* fp, const char* peer_ip, const char* run_dir);

// Creates run_dir/SYNC_SECRETS_NAME, mode 0600 (rsync's strict modes refuse
// anything looser), holding "nxsync:<password>\n". False when the file
// already exists or can't be written; nothing is left behind then.
bool sync_write_secrets(const char* run_dir, const char* password);

// popen(cmd, "r") with the password in the child's environment only, never on
// a command line (any process can read another's). The variable is cleared in
// this process again before the call returns.
FILE* sync_popen_with_password(const char* cmd, const char* password);

// A line of the daemon's log, its "<date> <time> [<pid>] " prefix removed,
// that reports a client failing the password check. Transfer lines start
// with "send " / "recv ", so a file name can't pass for one.
bool sync_daemon_log_auth_failed(const char* msg);

#endif
