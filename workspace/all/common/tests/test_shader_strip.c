// Host unit test for shader_strip.h (load_shader_from_file's "#pragma
// parameter" filter, extractPragmaParameters' line reader and parser). Built
// with AddressSanitizer by run_tests.sh.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../shader_strip.h"

// Regression: the output was sized strlen(source)+1 but every kept line got a
// '\n' appended, so a file without a final newline overflowed by one byte.
static void test_no_trailing_newline(void) {
	char* out = ShaderStrip_parameters("void main(){}");
	assert(strcmp(out, "void main(){}\n") == 0);
	free(out);
}

static void test_strips_parameters(void) {
	char* out = ShaderStrip_parameters("#version 100\n#pragma parameter X \"x\" 1.0 0.0 2.0 0.1\n\nuniform float X;\r\n");
	assert(strcmp(out, "#version 100\nuniform float X;\r\n") == 0);
	free(out);
	out = ShaderStrip_parameters("");
	assert(strcmp(out, "") == 0);
	free(out);
	out = ShaderStrip_parameters("#pragma parameter A \"a\" 0 0 1 1");
	assert(strcmp(out, "") == 0);
	free(out);
}

static void test_parse_pragma(void) {
	char name[128], label[128];
	float v[4];
	assert(ShaderStrip_parsePragma("#pragma parameter  GAMMA \"Gamma (out)\" 2.2 1.0 3.0 0.05", name, label, v) == 1);
	assert(strcmp(name, "GAMMA") == 0 && strcmp(label, "Gamma (out)") == 0);
	assert(v[0] == 2.2f && v[1] == 1.0f && v[2] == 3.0f && v[3] == 0.05f);
	assert(ShaderStrip_parsePragma("#pragma parameter X \"x\" 1.0 0.0 2.0", name, label, v) == -1); // 5 fields
	assert(ShaderStrip_parsePragma("#pragma parameter X x 1 0 2 1", name, label, v) == -1);			// unquoted label
	assert(ShaderStrip_parsePragma("uniform float X;", name, label, v) == 0);
	assert(ShaderStrip_parsePragma("", name, label, v) == 0);
	// an over-long name or label is cut at its 128-byte buffer (ASan)
	char longline[600];
	snprintf(longline, sizeof(longline), "#pragma parameter %0300d \"l\" 1 0 2 1", 7);
	assert(ShaderStrip_parsePragma(longline, name, label, v) == -1 && strlen(name) == 127);
}

static void test_read_line(void) {
	char line[8];
	const char* src = "ab\r\n\nabcdefghijkl\n#x";
	const char* p = src;
	ShaderStrip_readLine(&p, line, sizeof(line));
	assert(strcmp(line, "ab\r") == 0);
	ShaderStrip_readLine(&p, line, sizeof(line));
	assert(strcmp(line, "") == 0);
	// Regression: the rest of a line longer than the buffer was read as the next line
	ShaderStrip_readLine(&p, line, sizeof(line));
	assert(strcmp(line, "abcdefg") == 0);
	ShaderStrip_readLine(&p, line, sizeof(line));
	assert(strcmp(line, "#x") == 0 && *p == '\0');
}

int main(void) {
	test_no_trailing_newline();
	test_strips_parameters();
	test_parse_pragma();
	test_read_line();
	printf("test_shader_strip: OK\n");
	return 0;
}
