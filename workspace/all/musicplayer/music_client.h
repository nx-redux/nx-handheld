#ifndef MUSIC_CLIENT_H
#define MUSIC_CLIENT_H

#include <stdbool.h>
#include "music_service_protocol.h"

int MusicClient_init(const char* daemon_path);
void MusicClient_quit(void);
void MusicClient_update(void);
/* Refresh the snapshot when the copy is older than max_age_ms. MusicClient_update
 * uses the 250 ms status cadence; the spectrum asks for fresher samples. */
void MusicClient_updateWithin(int max_age_ms);
void MusicClient_disconnect(void);
const MusicSnapshotWire* MusicClient_snapshot(void);
const char* MusicClient_error(void);

int MusicClient_load(const char* path);
int MusicClient_loadFolder(const char* path, const char* selected_path);
int MusicClient_loadPlaylist(const char* path, int index);
int MusicClient_loadRadio(const char* url);
int MusicClient_loadPodcast(const char* feed_url, const char* episode_guid);
int MusicClient_play(void);
int MusicClient_pause(void);
int MusicClient_stop(void);
int MusicClient_toggle(void);
int MusicClient_next(void);
int MusicClient_previous(void);
int MusicClient_seek(int position_ms);
int MusicClient_setPodcastProgress(const char* feed_url, const char* episode_guid, int position_sec);
int MusicClient_markPodcastPlayed(const char* feed_url, const char* episode_guid, bool played);
int MusicClient_setSpeed(float speed);
int MusicClient_setVolume(int volume);
int MusicClient_setAudioSettings(int bass_filter_hz, float soft_limiter_threshold,
								 int rate_mode_follow, int resampler_quality, int buffer_frames);
int MusicClient_setRepeat(bool repeat);
int MusicClient_setShuffle(bool shuffle);

bool MusicClient_isConnected(void);
bool MusicClient_isStopped(void);
bool MusicClient_isRadioActive(void);
bool MusicClient_isPodcastActive(void);
int MusicClient_position(void);
int MusicClient_duration(void);
float MusicClient_speed(void);

#endif
