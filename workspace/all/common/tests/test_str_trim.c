// Host test for str_trim.h.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../str_trim.h"

int main(void) {
	char a[] = "name \t\r\n";
	assert(strTrimRight(a, " \t\r") == 8 && strcmp(a, "name \t\r\n") == 0); // '\n' not in the set: stops
	assert(strTrimRight(a, " \t\r\n") == 4 && strcmp(a, "name") == 0);
	char b[] = "  ";
	assert(strTrimRight(b, " ") == 0 && b[0] == '\0');
	char c[] = "";
	assert(strTrimRight(c, " ") == 0);

	char d[] = " \t key = v \r\n";
	char* t = strTrimSpace(d);
	assert(strcmp(t, "key = v") == 0 && t == d + 3);
	char e[] = " \n\t ";
	assert(strcmp(strTrimSpace(e), "") == 0);
	char f[] = "x";
	assert(strTrimSpace(f) == f && strcmp(f, "x") == 0);
	assert(strTrimSpace(NULL) == NULL);

	char g[] = "  Mario (USA) ";
	strTrimSpaceInPlace(g);
	assert(strcmp(g, "Mario (USA)") == 0);
	char h[] = "   ";
	strTrimSpaceInPlace(h);
	assert(h[0] == '\0');

	printf("test_str_trim: OK\n");
	return 0;
}
