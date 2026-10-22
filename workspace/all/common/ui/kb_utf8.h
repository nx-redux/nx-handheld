#ifndef __KB_UTF8_H__
#define __KB_UTF8_H__

// UTF-8 caret helpers behind the on-screen keyboard (ui_keyboard.c), header-only so they can be unit-tested on the
// host without SDL: see tests/test_kb_utf8.c. Offsets are byte offsets into a NUL-terminated string.

// Length of s with any trailing partial UTF-8 sequence dropped, so a cut or a backspace never leaves half a
// character behind.
static inline int KbUtf8_boundary(const char* s, int len) {
	int i = len;
	while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80)
		i--; // continuation bytes
	if (i == 0)
		return 0;
	unsigned char lead = (unsigned char)s[i - 1];
	int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3
							  : lead >= 0xC0   ? 2
											   : 1;
	return (len - (i - 1)) >= need ? len : i - 1;
}

// Byte offset of the character before `at` (a char boundary, > 0).
static inline int KbUtf8_prev(const char* s, int at) {
	do
		at--;
	while (at > 0 && ((unsigned char)s[at] & 0xC0) == 0x80);
	return at;
}

// Byte offset of the character after `at` (a char boundary, < len).
static inline int KbUtf8_next(const char* s, int at, int len) {
	do
		at++;
	while (at < len && ((unsigned char)s[at] & 0xC0) == 0x80);
	return at;
}

#endif
