// Host unit test for shader_strip.h (load_shader_from_file's "#pragma
// parameter" filter). Built with AddressSanitizer by run_tests.sh.
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

int main(void) {
	test_no_trailing_newline();
	test_strips_parameters();
	printf("test_shader_strip: OK\n");
	return 0;
}
