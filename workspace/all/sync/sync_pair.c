#include "sync_pair.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sha256.h"

static const char hex_digits[] = "0123456789abcdef";

bool sync_nonce_valid(const char* s, size_t len) {
	if (len != SYNC_NONCE_LEN)
		return false;
	for (size_t i = 0; i < len; i++)
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
			return false;
	return true;
}

bool sync_nonce_new(char* out) {
	unsigned char raw[SYNC_NONCE_LEN / 2];
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		return false;
	size_t got = 0;
	while (got < sizeof(raw)) {
		ssize_t n = read(fd, raw + got, sizeof(raw) - got);
		if (n <= 0)
			break;
		got += n;
	}
	close(fd);
	if (got != sizeof(raw))
		return false;
	for (size_t i = 0; i < sizeof(raw); i++) {
		out[i * 2] = hex_digits[raw[i] >> 4];
		out[i * 2 + 1] = hex_digits[raw[i] & 0xf];
	}
	out[SYNC_NONCE_LEN] = '\0';
	return true;
}

static void hash_label(const char* s, const char* label, unsigned char* digest) {
	SHA256_CTX ctx;
	sha256_init(&ctx);
	sha256_update(&ctx, (const BYTE*)s, strlen(s));
	sha256_update(&ctx, (const BYTE*)label, strlen(label));
	sha256_final(&ctx, digest);
}

bool sync_pair_derive(const char* own_ip, const char* own_nonce, const char* peer_ip, const char* peer_nonce,
					  char* code, char* password) {
	struct in_addr a;
	if (inet_pton(AF_INET, own_ip, &a) != 1 || inet_pton(AF_INET, peer_ip, &a) != 1)
		return false;
	if (!sync_nonce_valid(own_nonce, strlen(own_nonce)) || !sync_nonce_valid(peer_nonce, strlen(peer_nonce)))
		return false;

	char own[64], peer[64];
	snprintf(own, sizeof(own), "%s:%s", own_ip, own_nonce);
	snprintf(peer, sizeof(peer), "%s:%s", peer_ip, peer_nonce);
	// strcmp orders by unsigned bytes, the phone's plain string order for
	// these ASCII strings.
	bool own_first = strcmp(own, peer) < 0;
	char s[160];
	snprintf(s, sizeof(s), "nxsync3|%s|%s", own_first ? own : peer, own_first ? peer : own);

	unsigned char digest[SHA256_BLOCK_SIZE];
	hash_label(s, "|code", digest);
	uint32_t v = ((uint32_t)digest[0] << 24) | ((uint32_t)digest[1] << 16) | ((uint32_t)digest[2] << 8) | digest[3];
	snprintf(code, SYNC_PAIR_CODE_LEN + 1, "%04u", (unsigned)(v % 10000));

	hash_label(s, "|pass", digest);
	for (int i = 0; i < SYNC_PAIR_PASSWORD_LEN / 2; i++) {
		password[i * 2] = hex_digits[digest[i] >> 4];
		password[i * 2 + 1] = hex_digits[digest[i] & 0xf];
	}
	password[SYNC_PAIR_PASSWORD_LEN] = '\0';
	return true;
}

bool sync_daemon_auth_lines(FILE* fp, const char* peer_ip, const char* run_dir) {
	// max connections counts in the lock file; rsync's default one
	// (/var/run/rsyncd.lock) may not be writable on the device.
	int n = fprintf(fp,
					"hosts allow = %s\n"
					"auth users = %s\n"
					"secrets file = %s/%s\n"
					"strict modes = true\n"
					"max connections = %d\n"
					"lock file = %s/%s\n",
					peer_ip, SYNC_PAIR_USER, run_dir, SYNC_SECRETS_NAME, SYNC_MAX_CONNECTIONS, run_dir, SYNC_LOCK_NAME);
	return n > 0;
}

bool sync_write_secrets(const char* run_dir, const char* password) {
	char path[1024];
	int len = snprintf(path, sizeof(path), "%s/%s", run_dir, SYNC_SECRETS_NAME);
	if (len < 0 || (size_t)len >= sizeof(path))
		return false;
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	if (fd < 0)
		return false;
	char line[128];
	int n = snprintf(line, sizeof(line), "%s:%s\n", SYNC_PAIR_USER, password);
	// fchmod: the umask must not leave it looser or tighter than 0600.
	bool ok = n > 0 && (size_t)n < sizeof(line) && fchmod(fd, 0600) == 0 && write(fd, line, n) == n;
	if (close(fd) != 0)
		ok = false;
	if (!ok)
		unlink(path);
	return ok;
}

// setenv/unsetenv race another thread's popen: one at a time.
static pthread_mutex_t env_mutex = PTHREAD_MUTEX_INITIALIZER;

FILE* sync_popen_with_password(const char* cmd, const char* password) {
	pthread_mutex_lock(&env_mutex);
	FILE* fp = NULL;
	if (setenv(SYNC_PAIR_PASSWORD_ENV, password, 1) == 0)
		fp = popen(cmd, "r");
	unsetenv(SYNC_PAIR_PASSWORD_ENV);
	pthread_mutex_unlock(&env_mutex);
	return fp;
}

bool sync_daemon_log_auth_failed(const char* msg) {
	return !strncmp(msg, "auth failed", strlen("auth failed"));
}
