// Host-compiled unit test for the user channel list in iptv.c: duplicate
// URLs refused, the IPTV_MAX_USER_CHANNELS cap, overlong fields cut and
// terminated, the "General" default group, removal and the JSON round trip.
// Build & run (from workspace/all/mediaplayer):
//   cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I. -I../common -I../include -I../../tg5040/platform \
//      -DPLATFORM='"tg5040"' -DIPTV_CHANNELS_DIR='"/tmp/nx_test_iptv_user"' \
//      iptv.c ../include/parson/parson.c tests/test_iptv_user.c -o /tmp/t && /tmp/t
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../iptv.h"

#define CHANNELS_FILE "/tmp/nx_test_iptv_user/channels.json"

static void write_file(const char* path, const char* body) {
	FILE* f = fopen(path, "w");
	assert(f);
	fputs(body, f);
	fclose(f);
}

static void test_add_remove(void) {
	system("rm -rf /tmp/nx_test_iptv_user");
	IPTV_init();
	assert(IPTV_getUserChannelCount() == 0);

	assert(IPTV_addUserChannel("One", "http://a/1.m3u8", NULL, NULL, NULL) == 0);
	assert(IPTV_addUserChannel("Dup", "http://a/1.m3u8", "News", NULL, NULL) == -1);
	assert(IPTV_addUserChannel("Two", "http://a/2.m3u8", "News", "http://logo", "00ff") == 1);
	assert(IPTV_addUserChannel(NULL, "http://a/3", NULL, NULL, NULL) == -1);
	assert(IPTV_addUserChannel("x", NULL, NULL, NULL, NULL) == -1);

	const IPTVChannel* ch = IPTV_getUserChannels();
	assert(strcmp(ch[0].group, "General") == 0 && ch[0].logo[0] == '\0' && ch[0].decryption_key[0] == '\0');
	assert(strcmp(ch[1].group, "News") == 0 && strcmp(ch[1].decryption_key, "00ff") == 0);
	assert(IPTV_userChannelExists("http://a/2.m3u8") && !IPTV_userChannelExists("http://a/9"));
	assert(!IPTV_userChannelExists(NULL));

	// Saved on every change: a reload sees the same list
	IPTV_init();
	assert(IPTV_getUserChannelCount() == 2);
	ch = IPTV_getUserChannels();
	assert(strcmp(ch[1].name, "Two") == 0 && strcmp(ch[1].logo, "http://logo") == 0);
	assert(strcmp(ch[1].decryption_key, "00ff") == 0);

	assert(IPTV_removeUserChannelByUrl("http://a/1.m3u8"));
	assert(!IPTV_removeUserChannelByUrl("http://a/1.m3u8"));
	assert(!IPTV_removeUserChannelByUrl(NULL));
	assert(IPTV_getUserChannelCount() == 1 && strcmp(IPTV_getUserChannels()[0].name, "Two") == 0);
	IPTV_removeUserChannel(5); // out of range: ignored
	IPTV_removeUserChannel(-1);
	assert(IPTV_getUserChannelCount() == 1);
	IPTV_removeUserChannel(0);
	IPTV_init();
	assert(IPTV_getUserChannelCount() == 0);
}

static void test_cap_and_truncation(void) {
	system("rm -rf /tmp/nx_test_iptv_user");
	IPTV_init();
	char url[64];
	for (int i = 0; i < IPTV_MAX_USER_CHANNELS; i++) {
		snprintf(url, sizeof(url), "http://a/%d", i);
		assert(IPTV_addUserChannel("c", url, NULL, NULL, NULL) == i);
	}
	assert(IPTV_addUserChannel("c", "http://a/over", NULL, NULL, NULL) == -1);
	assert(IPTV_getUserChannelCount() == IPTV_MAX_USER_CHANNELS);

	system("rm -rf /tmp/nx_test_iptv_user");
	IPTV_init();
	static char long_name[IPTV_MAX_NAME * 2], long_url[IPTV_MAX_URL * 2], long_group[IPTV_MAX_GROUP * 2];
	memset(long_name, 'n', sizeof(long_name) - 1);
	memset(long_url, 'u', sizeof(long_url) - 1);
	memset(long_group, 'g', sizeof(long_group) - 1);
	assert(IPTV_addUserChannel(long_name, long_url, long_group, NULL, NULL) == 0);
	const IPTVChannel* ch = IPTV_getUserChannels();
	assert(strlen(ch[0].name) == IPTV_MAX_NAME - 1);
	assert(strlen(ch[0].url) == IPTV_MAX_URL - 1);
	assert(strlen(ch[0].group) == IPTV_MAX_GROUP - 1);
}

static void test_load_file(void) {
	system("rm -rf /tmp/nx_test_iptv_user && mkdir -p /tmp/nx_test_iptv_user");
	write_file(CHANNELS_FILE, "[{\"name\":\"A\",\"url\":\"http://a\",\"group\":\"\"},"
							  "{\"name\":\"no url\"},"
							  "42,"
							  "{\"name\":\"B\",\"url\":\"http://b\",\"group\":\"Kids\",\"logo\":\"l\"}]");
	IPTV_init();
	assert(IPTV_getUserChannelCount() == 2);
	const IPTVChannel* ch = IPTV_getUserChannels();
	assert(strcmp(ch[0].name, "A") == 0 && strcmp(ch[0].group, "General") == 0);
	assert(strcmp(ch[1].name, "B") == 0 && strcmp(ch[1].group, "Kids") == 0 && strcmp(ch[1].logo, "l") == 0);

	// More entries than the cap: only the first IPTV_MAX_USER_CHANNELS load
	FILE* f = fopen(CHANNELS_FILE, "w");
	assert(f);
	fputs("[", f);
	for (int i = 0; i < IPTV_MAX_USER_CHANNELS + 10; i++)
		fprintf(f, "%s{\"name\":\"c\",\"url\":\"http://a/%d\"}", i ? "," : "", i);
	fputs("]", f);
	fclose(f);
	IPTV_init();
	assert(IPTV_getUserChannelCount() == IPTV_MAX_USER_CHANNELS);

	write_file(CHANNELS_FILE, "{\"not\":\"an array\"}");
	IPTV_init();
	assert(IPTV_getUserChannelCount() == 0);
	write_file(CHANNELS_FILE, "garbage");
	IPTV_init();
	assert(IPTV_getUserChannelCount() == 0);
}

int main(void) {
	test_add_remove();
	test_cap_and_truncation();
	test_load_file();
	system("rm -rf /tmp/nx_test_iptv_user");
	printf("test_iptv_user: OK\n");
	return 0;
}
