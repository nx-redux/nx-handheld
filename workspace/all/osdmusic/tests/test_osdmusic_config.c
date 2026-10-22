// Host-compiled unit test for osdmusic_config_int (osdmusic_config.h).
// Build & run (from workspace/all/osdmusic):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. tests/test_osdmusic_config.c -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>

#include "../osdmusic_config.h"

int main(void) {
	const char* json = "{\n"
					   "  \"canvaswidth\": 512,\n"
					   "  \"canvasheight\":\t256,\n"
					   "  \"gridwidth\" : 2,\n"
					   "  \"name\": \"gridheight\",\n"
					   "  \"negative\": -7,\n"
					   "  \"text\": \"abc\"\n"
					   "}";
	assert(osdmusic_config_int(json, "canvaswidth", 0) == 512);
	assert(osdmusic_config_int(json, "canvasheight", 0) == 256); // tab after the colon
	assert(osdmusic_config_int(json, "gridwidth", 0) == 2);		 // space before the colon
	assert(osdmusic_config_int(json, "negative", 0) == -7);
	assert(osdmusic_config_int(json, "missing", 42) == 42);
	assert(osdmusic_config_int(json, "text", 9) == 9);	 // not a number
	assert(osdmusic_config_int(json, "canvas", 5) == 5); // a key is matched whole, quotes included
	// "gridheight" only appears as a string value: not read as the key (it
	// used to take the number after the next ':', here "negative"'s -7)
	assert(osdmusic_config_int(json, "gridheight", 3) == 3);
	assert(osdmusic_config_int("{\"a\": \"gridheight\", \"gridheight\": 4}", "gridheight", 3) == 4);
	assert(osdmusic_config_int("{\"x\"", "x", 4) == 4); // no colon
	assert(osdmusic_config_int("", "x", 4) == 4);
	printf("test_osdmusic_config: all tests passed\n");
	return 0;
}
