#ifndef MUSIC_SERVICE_CLIENT_H
#define MUSIC_SERVICE_CLIENT_H

#include <stddef.h>
#include "music_service_protocol.h"

int MusicService_connect(const char* socket_path, int timeout_ms);
void MusicService_disconnect(int fd);
int MusicService_request(int fd, uint16_t command, const void* payload,
						 size_t payload_length, MusicResponseWire* response,
						 int timeout_ms);
const char* MusicService_socketPath(void);
/* One-shot query for a process without a client connection (the launcher):
 * 1 while the owner is playing (not paused or stopped). Never starts the
 * owner; no owner means nothing plays. */
int MusicService_isPlaying(void);

#endif
