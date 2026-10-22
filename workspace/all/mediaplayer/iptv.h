#ifndef __IPTV_H__
#define __IPTV_H__

#include <stdbool.h>

#define IPTV_MAX_NAME 128
#define IPTV_MAX_URL 1024
#define IPTV_MAX_GROUP 64
#define IPTV_MAX_LOGO 256
#define IPTV_MAX_KEY 64
#define IPTV_MAX_USER_CHANNELS 64

// A single IPTV channel
typedef struct {
	char name[IPTV_MAX_NAME];
	char url[IPTV_MAX_URL];
	char group[IPTV_MAX_GROUP];
	char logo[IPTV_MAX_LOGO];
	char decryption_key[IPTV_MAX_KEY]; // ClearKey hex string (empty = none)
} IPTVChannel;

// Initialize IPTV module (loads user channels from disk)
void IPTV_init(void);

// User channel management
const IPTVChannel* IPTV_getUserChannels(void);
int IPTV_getUserChannelCount(void);
int IPTV_addUserChannel(const char* name, const char* url, const char* group, const char* logo, const char* decryption_key);
void IPTV_removeUserChannel(int index);
bool IPTV_removeUserChannelByUrl(const char* url);
bool IPTV_userChannelExists(const char* url);
void IPTV_saveUserChannels(void);
void IPTV_loadUserChannels(void);

#endif
