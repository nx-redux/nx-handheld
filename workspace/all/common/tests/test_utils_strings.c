// Host test for the string helpers in utils.c: replaceString, the shell
// single-quote helpers, trimString, removeExtension and putInt. Built with
// AddressSanitizer by run_tests.sh; heap buffers are sized exactly so an
// over-read or over-write fails loudly.
#include "../utils.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// Returns a heap copy of s in a buffer of exactly `size` bytes.
static char* heap_buf(const char* s, size_t size) {
	char* b = malloc(size);
	assert(b && strlen(s) < size);
	strcpy(b, s);
	return b;
}

// Regression: the recursion passed the full buf_size after advancing the
// pointer, so a second match could write past the buffer.
static void test_replace_string(void) {
	char* b = heap_buf("ab'cd'e", 12);
	assert(replaceString(b, 12, "'", "'\\''") == -1);
	free(b);

	b = heap_buf("ab'cd'e", 14);
	assert(replaceString(b, 14, "'", "'\\''") == 2);
	assert(strcmp(b, "ab'\\''cd'\\''e") == 0);
	free(b);

	b = heap_buf("aXbXc", 6);
	assert(replaceString(b, 6, "X", "") == 2);
	assert(strcmp(b, "abc") == 0);
	assert(replaceString(b, 6, "Q", "zz") == 0);
	assert(replaceString(b, 6, "", "zz") == 0);
	free(b);
}

static void test_escape_single_quotes(void) {
	char* b = heap_buf("Link's Awakening", 32);
	assert(strcmp(escapeSingleQuotes(b, 32), "Link'\\''s Awakening") == 0);
	free(b);

	// Too small: emptied, never half-escaped (an unbalanced quote).
	b = heap_buf("a'b'c'd", 12);
	assert(strcmp(escapeSingleQuotes(b, 12), "") == 0);
	free(b);

	// Exact fit.
	b = heap_buf("''", 9);
	assert(strcmp(escapeSingleQuotes(b, 9), "'\\'''\\''") == 0);
	free(b);
}

// Runs `printf %s <quoted>` through sh and checks the round trip.
static void assert_roundtrip(const char* raw) {
	char* q = shellQuoteAlloc(raw);
	assert(q);
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "printf %%s %s", q);
	FILE* p = popen(cmd, "r");
	assert(p);
	char out[512] = {0};
	size_t n = fread(out, 1, sizeof(out) - 1, p);
	out[n] = '\0';
	pclose(p);
	assert(strcmp(out, raw) == 0);
	free(q);
}

static void test_shell_quote(void) {
	// Regression: http.c/scraper_api.c allocated 3 bytes per quote for a
	// 4-byte (net) expansion.
	char* q = shellQuoteAlloc("'''");
	assert(strcmp(q, "''\\'''\\'''\\'''") == 0);
	free(q);
	q = shellQuoteAlloc(NULL);
	assert(strcmp(q, "''") == 0);
	free(q);

	assert_roundtrip("Legend of Zelda, The - Link's Awakening");
	assert_roundtrip("'; rm -rf /tmp/nope; '");
	assert_roundtrip("$(echo no) `echo no` \"x\" \\ end'");
	assert_roundtrip("");

	char dst[8];
	assert(shellEscapeQuotes(dst, sizeof(dst), "a'b") == 6);
	assert(strcmp(dst, "a'\\''b") == 0);
	assert(shellEscapeQuotes(dst, sizeof(dst), "a''b") == -1);
	assert(dst[0] == '\0');
	assert(shellEscapeQuotes(dst, sizeof(dst), "1234567") == 7);
	assert(shellEscapeQuotes(dst, sizeof(dst), "12345678") == -1);
}

// Regression: an empty or all-separator string walked past its terminator.
static void test_trim_string(void) {
	char out[16];
	char* b = heap_buf("", 1);
	assert(trimString(out, sizeof(out), b, true) == 1 && out[0] == '\0');
	free(b);
	b = heap_buf("  ,{} ", 7);
	assert(trimString(out, sizeof(out), b, false) == 1 && out[0] == '\0');
	free(b);
	b = heap_buf("  \"abc\" ,", 10);
	trimString(out, sizeof(out), b, true);
	assert(strcmp(out, "abc") == 0);
	free(b);
	b = heap_buf("  hello world  ", 16);
	trimString(out, 6, b, false);
	assert(strcmp(out, "hello") == 0);
	free(b);
}

// Regression: a name ending in '.' read one byte past the copy.
static void test_remove_extension(void) {
	char* r = removeExtension("Game.");
	assert(strcmp(r, "Game.") == 0);
	free(r);
	r = removeExtension("Game.gba");
	assert(strcmp(r, "Game") == 0);
	free(r);
	r = removeExtension("a.b");
	assert(strcmp(r, "a.b") == 0); // one-char extension kept
	free(r);
	r = removeExtension("Mr. Driller");
	assert(strcmp(r, "Mr. Driller") == 0);
	free(r);
}

static void test_put_int(void) {
	char path[64];
	snprintf(path, sizeof(path), "/tmp/nx_test_putint_%d", (int)getpid());
	putInt(path, INT_MIN);
	assert(getInt(path) == INT_MIN);
	putInt(path, 2000000000);
	assert(getInt(path) == 2000000000);
	unlink(path);
}

// writeFileAtomic + allocFile, including a path past the old MAX_PATH temp
// buffer (which used to cut the ".tmp" name).
static void test_write_file_atomic(void) {
	char dir[700];
	int n = snprintf(dir, sizeof(dir), "/tmp/nx_test_wfa_%d", (int)getpid());
	for (int part = 0; part < 6; part++) { // 6 x 100-character components
		dir[n++] = '/';
		for (int i = 0; i < 99; i++)
			dir[n++] = 'd';
	}
	dir[n] = '\0';
	char mk[720];
	snprintf(mk, sizeof(mk), "mkdir -p '%s'", dir);
	assert(system(mk) == 0);
	char path[800];
	snprintf(path, sizeof(path), "%s/file.cfg", dir);
	assert(strlen(path) > 512);
	assert(writeFileAtomic(path, "abc", 3));
	char* s = allocFile(path);
	assert(s && strcmp(s, "abc") == 0);
	free(s);
	char tmp[820];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	assert(access(tmp, F_OK) != 0); // renamed away
	assert(writeFileAtomic(path, "", 0));
	s = allocFile(path);
	assert(s && s[0] == '\0');
	free(s);
	unlink(path);
	char rm[760];
	snprintf(rm, sizeof(rm), "rm -rf /tmp/nx_test_wfa_%d", (int)getpid());
	assert(system(rm) == 0);
}

// Regression: getFile() used ftell(SEEK_END) for its NUL index instead of
// fread()'s byte count, leaving trailing stack garbage on short reads (such as
// 4096-byte Linux sysfs attributes) and returning empty on unseekable streams.
static void test_get_file(void) {
	char path[96];
	snprintf(path, sizeof(path), "/tmp/nx_test_getfile_%d", (int)getpid());
	assert(writeFileAtomic(path, "schedutil\n", 10));

	char buf[32];
	memset(buf, 'X', sizeof(buf));
	getFile(path, buf, sizeof(buf));
	assert(strcmp(buf, "schedutil\n") == 0);

	// Capped read truncates cleanly at buffer_size - 1
	memset(buf, 'X', sizeof(buf));
	getFile(path, buf, 5);
	assert(strcmp(buf, "sche") == 0);

	// Zero-size buffer is a safe no-op
	buf[0] = 'Z';
	getFile(path, buf, 0);
	assert(buf[0] == 'Z');
	unlink(path);

	// Unseekable FIFO (models procfs/sysfs where SEEK_END fails or misreports)
	char fifo[96];
	snprintf(fifo, sizeof(fifo), "/tmp/nx_test_fifo_%d", (int)getpid());
	unlink(fifo);
	assert(mkfifo(fifo, 0600) == 0);
	pid_t child = fork();
	assert(child >= 0);
	if (child == 0) {
		FILE* wf = fopen(fifo, "w");
		if (wf) {
			fputs("ondemand\n", wf);
			fclose(wf);
		}
		_exit(0);
	}
	memset(buf, 'Y', sizeof(buf));
	getFile(fifo, buf, sizeof(buf));
	int st = 0;
	waitpid(child, &st, 0);
	unlink(fifo);
	assert(strcmp(buf, "ondemand\n") == 0);
}

int main(void) {
	test_replace_string();
	test_escape_single_quotes();
	test_shell_quote();
	test_trim_string();
	test_remove_extension();
	test_put_int();
	test_write_file_atomic();
	test_get_file();
	printf("test_utils_strings: OK\n");
	return 0;
}
