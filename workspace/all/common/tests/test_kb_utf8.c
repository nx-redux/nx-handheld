// Host unit test for ui/kb_utf8.h (the keyboard's UTF-8 caret helpers). Built with AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ui/kb_utf8.h"

int main(void) {
	// "a" + e-acute (2 bytes) + euro (3) + grinning face (4)
	const char* s = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
	int len = (int)strlen(s);
	assert(len == 10);
	assert(KbUtf8_boundary(s, len) == len);
	// a sequence cut short anywhere is dropped whole
	assert(KbUtf8_boundary(s, 9) == 6 && KbUtf8_boundary(s, 8) == 6 && KbUtf8_boundary(s, 7) == 6);
	assert(KbUtf8_boundary(s, 5) == 3 && KbUtf8_boundary(s, 4) == 3);
	assert(KbUtf8_boundary(s, 2) == 1 && KbUtf8_boundary(s, 1) == 1);
	assert(KbUtf8_boundary(s, 0) == 0);
	assert(KbUtf8_boundary("\x80\x80", 2) == 0); // only continuation bytes

	// caret walk, one character at a time
	int at = len;
	at = KbUtf8_prev(s, at);
	assert(at == 6);
	at = KbUtf8_prev(s, at);
	assert(at == 3);
	at = KbUtf8_prev(s, at);
	assert(at == 1);
	at = KbUtf8_prev(s, at);
	assert(at == 0);
	at = KbUtf8_next(s, at, len);
	assert(at == 1);
	at = KbUtf8_next(s, at, len);
	assert(at == 3);
	at = KbUtf8_next(s, at, len);
	assert(at == 6);
	at = KbUtf8_next(s, at, len);
	assert(at == len);

	// backspace over the 3-byte euro, as the keyboard does it
	char buf[16];
	strcpy(buf, s);
	int caret = 6, blen = len;
	int start = KbUtf8_prev(buf, caret);
	memmove(buf + start, buf + caret, blen - caret + 1);
	blen -= caret - start;
	assert(start == 3 && blen == 7 && strcmp(buf, "a\xC3\xA9\xF0\x9F\x98\x80") == 0);
	printf("test_kb_utf8: OK\n");
	return 0;
}
