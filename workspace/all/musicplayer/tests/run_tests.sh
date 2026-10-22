#!/bin/sh
# Host-side unit tests for musicplayer/. No SDL library linked, no cross toolchain.
# The two device tests here (test_music_service.c, test_music_client.c) are
# cross-compiled and run on a Brick by scripts/tests/test-music-service-e2e.sh,
# not from this runner.
# Tests whose code includes common/api.h need the SDL2 headers (found with
# sdl2-config) but link no SDL; they are skipped when sdl2-config is missing.
set -eu
cd "$(dirname "$0")"
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -o /tmp/nx_test_music_balance ../music_balance.c test_music_balance.c
/tmp/nx_test_music_balance
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -o /tmp/nx_test_music_request_validation ../music_request_validation.c test_music_request_validation.c
/tmp/nx_test_music_request_validation
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -I../../common -o /tmp/nx_test_resume ../resume.c test_resume.c
/tmp/nx_test_resume
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -I../../include -o /tmp/nx_test_radio_catalog ../radio_catalog_parse.c ../../include/parson/parson.c test_radio_catalog.c
/tmp/nx_test_radio_catalog
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -I../../common -I../../../tg5040/platform -DMUSIC_SETTINGS_TEST_DIR='"/tmp/nx_music_settings_roundtrip"' -o /tmp/nx_test_music_settings ../settings.c test_music_settings.c -lm
/tmp/nx_test_music_settings
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -I.. -o /tmp/nx_test_embedded_lyrics ../embedded_lyrics.c ../tag_meta.c test_embedded_lyrics.c
/tmp/nx_test_embedded_lyrics
cc -std=gnu99 -Wall -Werror -I.. -o /tmp/nx_test_radio_hls_id3 ../radio_hls.c ../tag_meta.c test_radio_hls_id3.c
/tmp/nx_test_radio_hls_id3
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_radio_hls_playlist ../radio_hls.c ../tag_meta.c test_radio_hls_playlist.c
/tmp/nx_test_radio_hls_playlist
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_http_chunk test_http_chunk.c
/tmp/nx_test_http_chunk
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_audio_downmix test_audio_downmix.c
/tmp/nx_test_audio_downmix
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -I../include/yxml -o /tmp/nx_test_podcast_rss ../podcast_rss.c ../include/yxml/yxml.c test_podcast_rss.c
/tmp/nx_test_podcast_rss
cc -std=gnu99 -Wall -Werror -D_GNU_SOURCE -fsanitize=address -g -I.. -o /tmp/nx_test_disk_cache ../disk_cache.c test_disk_cache.c
/tmp/nx_test_disk_cache
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_tag_meta_id3 ../tag_meta.c test_tag_meta_id3.c
/tmp/nx_test_tag_meta_id3
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_http_location test_http_location.c
/tmp/nx_test_http_location
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_lrc_parse ../lrc_parse.c test_lrc_parse.c
/tmp/nx_test_lrc_parse
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_radio_url test_radio_url.c
/tmp/nx_test_radio_url
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_radio_hls_ts ../radio_hls.c ../tag_meta.c test_radio_hls_ts.c
/tmp/nx_test_radio_hls_ts
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -I../../common -I../../../tg5040/platform -DPLATFORM=\"tg5040\" -o /tmp/nx_test_playlist_m3u ../playlist_m3u.c ../music_format.c test_playlist_m3u.c
/tmp/nx_test_playlist_m3u
if command -v sdl2-config >/dev/null 2>&1; then
	SDL_HEADERS="-DUSE_SDL2 $(sdl2-config --cflags) -I$(sdl2-config --prefix)/include"
	# -Wno-typedef-redefinition: player.h forward-declares SDL_Surface (C11 allows the repeat, gnu99 warns)
	cc -std=gnu99 -Wall -Werror -Wno-typedef-redefinition -fsanitize=address -g -I.. -I../../common -I../../../tg5040/platform -DPLATFORM=\"tg5040\" $SDL_HEADERS -o /tmp/nx_test_playlist_dir ../playlist.c ../music_format.c test_playlist_dir.c
	/tmp/nx_test_playlist_dir
	cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -I../../common -I../../include -I../../../tg5040/platform -DPLATFORM=\"tg5040\" $SDL_HEADERS -o /tmp/nx_test_podcast_search ../podcast_search.c ../../include/parson/parson.c test_podcast_search.c
	/tmp/nx_test_podcast_search
else
	echo "SKIP test_playlist_dir, test_podcast_search: sdl2-config not found (SDL2 headers needed)"
fi
