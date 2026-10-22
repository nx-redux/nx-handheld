// Host test for settings_kv.h (audiomon's and osdmusic's settings readers).
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../settings_kv.h"

#define CFG "/tmp/nx_test_settings_kv.txt"

static void write_cfg(const char* body) {
	FILE* f = fopen(CFG, "w");
	assert(f);
	fputs(body, f);
	fclose(f);
}

int main(void) {
	assert(settings_kv_value("key=1", "key") && strcmp(settings_kv_value("key=1", "key"), "1") == 0);
	assert(!settings_kv_value("keyX=1", "key"));
	assert(!settings_kv_value("xkey=1", "key"));
	assert(!settings_kv_value("key", "key"));

	remove(CFG);
	assert(settings_kv_int(CFG, "btMaxRate", 48000) == 48000); // no file
	unsigned hex = 7;
	assert(!settings_kv_hex(CFG, "color2", &hex) && hex == 7);

	write_cfg("audioRateNegotiationX=5\n"
			  "audioRateNegotiation=0\n"
			  "xbtMaxRate=1\n"
			  "btMaxRateLimit=22050\n"
			  "btMaxRate=44100\n"
			  "btMaxRate=32000\n"
			  "empty=\n"
			  "bad=abc\n"
			  "bad=12\n"
			  "hexint=0x10\n"
			  "spaced= 5\n"
			  "neg=-3\n"
			  "color1=0xFFFFFFFF\n"
			  "color2=plain\n"
			  "color2=0X002222ff\n"
			  "color2=0x00ff00ff\n"
			  "color3=123\n");
	// a key is not matched by a longer key it is a prefix of (or one that ends with it)
	assert(settings_kv_int(CFG, "btMaxRate", 48000) == 44100);	  // first match wins
	assert(settings_kv_int(CFG, "audioRateNegotiation", 1) == 0); // not the "X" key
	assert(settings_kv_int(CFG, "btMax", 7) == 7);
	assert(settings_kv_int(CFG, "missing", -3) == -3);
	assert(settings_kv_int(CFG, "empty", 9) == 9);
	assert(settings_kv_int(CFG, "bad", 0) == 12); // an unparsable line is passed over
	assert(settings_kv_int(CFG, "hexint", 0) == 16);
	assert(settings_kv_int(CFG, "spaced", 0) == 5);
	assert(settings_kv_int(CFG, "neg", 0) == -3);

	assert(settings_kv_hex(CFG, "color2", &hex) && hex == 0x002222ffU); // "plain" passed over, 0X accepted
	assert(settings_kv_hex(CFG, "color1", &hex) && hex == 0xFFFFFFFFU);
	assert(!settings_kv_hex(CFG, "color3", &hex)); // not 0x-prefixed
	assert(!settings_kv_hex(CFG, "color", &hex));
	remove(CFG);

	printf("test_settings_kv: OK\n");
	return 0;
}
