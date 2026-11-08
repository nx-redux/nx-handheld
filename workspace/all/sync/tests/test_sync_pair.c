// Host-compiled unit tests for sync_pair and sha256 (no device toolchain).
// Build & run: workspace/all/sync/tests/run_tests.sh
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../sha256.h"
#include "../sync_pair.h"

static void test_sha256(void) {
	SHA256_CTX ctx;
	unsigned char d[SHA256_BLOCK_SIZE];
	char hex[SHA256_BLOCK_SIZE * 2 + 1];
	sha256_init(&ctx);
	sha256_update(&ctx, (const BYTE*)"abc", 3);
	sha256_final(&ctx, d);
	for (int i = 0; i < SHA256_BLOCK_SIZE; i++)
		snprintf(hex + i * 2, 3, "%02x", d[i]);
	assert(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

	// Over one block, fed in two pieces: the padding's second-block branch.
	const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	sha256_init(&ctx);
	sha256_update(&ctx, (const BYTE*)m, 10);
	sha256_update(&ctx, (const BYTE*)m + 10, strlen(m) - 10);
	sha256_final(&ctx, d);
	for (int i = 0; i < SHA256_BLOCK_SIZE; i++)
		snprintf(hex + i * 2, 3, "%02x", d[i]);
	assert(!strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

static void test_vectors(void) {
	// The table in the pairing spec, shared with the phone's tests.
	struct {
		const char *ip_a, *nonce_a, *ip_b, *nonce_b, *code, *password;
	} v[] = {
		{"192.168.1.20", "0123456789abcdef", "192.168.1.31", "fedcba9876543210", "2995", "f55d7cdf6e74ca135fdcb442ca1a924e"},
		{"10.0.0.2", "00000000000000ff", "10.0.0.10", "a1b2c3d4e5f60718", "3032", "ac7f22bee829d36307216f2e87f80c2c"},
		{"192.168.42.129", "1111111111111111", "192.168.42.1", "2222222222222222", "7669", "80b95a44278f8880c299a57f1a96cd4b"},
	};
	for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
		char code[SYNC_PAIR_CODE_LEN + 1], pass[SYNC_PAIR_PASSWORD_LEN + 1];
		assert(sync_pair_derive(v[i].ip_a, v[i].nonce_a, v[i].ip_b, v[i].nonce_b, code, pass));
		assert(!strcmp(code, v[i].code) && !strcmp(pass, v[i].password));
		memset(code, 0, sizeof(code));
		memset(pass, 0, sizeof(pass));
		assert(sync_pair_derive(v[i].ip_b, v[i].nonce_b, v[i].ip_a, v[i].nonce_a, code, pass));
		assert(!strcmp(code, v[i].code) && !strcmp(pass, v[i].password));
	}

	char code[SYNC_PAIR_CODE_LEN + 1], pass[SYNC_PAIR_PASSWORD_LEN + 1];
	assert(!sync_pair_derive("192.168.1.20", "0123456789ABCDEF", "192.168.1.31", "fedcba9876543210", code, pass));
	assert(!sync_pair_derive("192.168.1.20", "0123456789abcdef", "192.168.1.31", "fedcba987654321", code, pass));
	assert(!sync_pair_derive("192.168.1.20; x", "0123456789abcdef", "192.168.1.31", "fedcba9876543210", code, pass));
}

static void test_nonce(void) {
	char a[SYNC_NONCE_LEN + 1], b[SYNC_NONCE_LEN + 1];
	assert(sync_nonce_new(a) && sync_nonce_new(b));
	assert(strlen(a) == SYNC_NONCE_LEN && sync_nonce_valid(a, strlen(a)));
	assert(sync_nonce_valid(b, strlen(b)) && strcmp(a, b) != 0);
	assert(sync_nonce_valid("0123456789abcdef", 16));
	assert(!sync_nonce_valid("0123456789abcdef", 15));
	assert(!sync_nonce_valid("0123456789abcdeF", 16));
	assert(!sync_nonce_valid("0123456789abcde ", 16));
	assert(!sync_nonce_valid("0123456789abcdefa", 17));
}

static void test_daemon_auth(void) {
	char dir[] = "/tmp/test_sync_pair-XXXXXX";
	assert(mkdtemp(dir));
	FILE* fp = tmpfile();
	assert(fp && sync_daemon_auth_lines(fp, "192.168.1.31", dir));
	rewind(fp);
	char conf[1024];
	size_t n = fread(conf, 1, sizeof(conf) - 1, fp);
	conf[n] = '\0';
	fclose(fp);
	char line[512];
	assert(strstr(conf, "hosts allow = 192.168.1.31\n"));
	assert(strstr(conf, "auth users = nxsync\n"));
	snprintf(line, sizeof(line), "secrets file = %s/rsyncd.secrets\n", dir);
	assert(strstr(conf, line));
	assert(strstr(conf, "strict modes = true\n"));
	assert(strstr(conf, "max connections = 16\n"));
	snprintf(line, sizeof(line), "lock file = %s/rsyncd.lock\n", dir);
	assert(strstr(conf, line));

	const char* pw = "f55d7cdf6e74ca135fdcb442ca1a924e";
	umask(0);
	assert(sync_write_secrets(dir, pw));
	umask(022);
	char path[512];
	snprintf(path, sizeof(path), "%s/rsyncd.secrets", dir);
	struct stat st;
	assert(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600 && st.st_uid == getuid());
	fp = fopen(path, "r");
	assert(fp);
	n = fread(line, 1, sizeof(line) - 1, fp);
	line[n] = '\0';
	fclose(fp);
	assert(!strcmp(line, "nxsync:f55d7cdf6e74ca135fdcb442ca1a924e\n"));
	// Never over an existing file (one planted there before the run).
	assert(!sync_write_secrets(dir, pw));
	unlink(path);
	assert(!sync_write_secrets("/nonexistent-sync-dir", pw));
	rmdir(dir);
}

static void test_client_password(void) {
	const char* pw = "ac7f22bee829d36307216f2e87f80c2c";
	// The URL every client rsync uses carries the user, never the password.
	char url[128];
	snprintf(url, sizeof(url), SYNC_RSYNC_URL "%s:%d/shared/", "10.0.0.2", 18730);
	assert(!strcmp(url, "rsync://nxsync@10.0.0.2:18730/shared/"));
	assert(!strstr(url, pw));

	// The child sees it in its environment; its command line doesn't hold it
	// and this process's environment is clean again afterwards.
	assert(!getenv("RSYNC_PASSWORD"));
	const char* cmd = "printf '%s|' \"$RSYNC_PASSWORD\"; cat /proc/$$/cmdline 2>/dev/null || ps -o args= -p $$";
	assert(!strstr(cmd, pw));
	FILE* fp = sync_popen_with_password(cmd, pw);
	assert(fp);
	assert(!getenv("RSYNC_PASSWORD"));
	char out[1024];
	size_t n = fread(out, 1, sizeof(out) - 1, fp);
	out[n] = '\0';
	assert(pclose(fp) == 0);
	char want[64];
	snprintf(want, sizeof(want), "%s|", pw);
	assert(!strncmp(out, want, strlen(want)));
	assert(!strstr(out + strlen(want), pw));
}

static void test_auth_failed_line(void) {
	assert(sync_daemon_log_auth_failed("auth failed on module shared from x (192.168.1.9) for nxsync: password mismatch"));
	assert(sync_daemon_log_auth_failed("auth failed on module saves from x (192.168.1.9): unauthorized user"));
	assert(!sync_daemon_log_auth_failed("recv auth failed on module.sav (12 bytes)"));
	assert(!sync_daemon_log_auth_failed("connect from x (192.168.1.31)"));
	assert(!sync_daemon_log_auth_failed(""));
}

int main(void) {
	test_sha256();
	test_vectors();
	test_nonce();
	test_daemon_auth();
	test_client_password();
	test_auth_failed_line();
	printf("sync_pair: all tests passed\n");
	return 0;
}
