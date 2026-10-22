#!/bin/sh
# Host-side unit tests for imageviewer/. No cross toolchain.
# test_iv_cache needs only the SDL2 headers (SDL is stubbed). test_iv_rotate
# links the host SDL2 + SDL2_image and is built without -fsanitize=address:
# Homebrew's sdl2-compat fails to load SDL3 under ASan and blocks on an error
# dialog. Both are skipped when sdl2-config (or SDL_image.h) is missing.
set -eu
cd "$(dirname "$0")"
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -I../../common -o /tmp/nx_test_iv_browser ../iv_browser.c ../../common/dir_list.c test_iv_browser.c
/tmp/nx_test_iv_browser
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_iv_fileops ../iv_fileops.c test_iv_fileops.c
/tmp/nx_test_iv_fileops
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -I../../common -o /tmp/nx_test_iv_probe ../iv_probe.c ../../common/image_probe.c test_iv_probe.c
/tmp/nx_test_iv_probe
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_iv_view_math test_iv_view_math.c
/tmp/nx_test_iv_view_math
if command -v sdl2-config >/dev/null 2>&1; then
	SDL_PREFIX="$(sdl2-config --prefix)"
	cc -std=gnu99 -Wall -Werror -fsanitize=address -g -DUSE_SDL2 -I.. -I../../common -I../../../tg5040/platform -DPLATFORM=\"tg5040\" $(sdl2-config --cflags) -I"$SDL_PREFIX/include" -o /tmp/nx_test_iv_cache ../iv_cache.c test_iv_cache.c
	/tmp/nx_test_iv_cache
	if [ -f "$SDL_PREFIX/include/SDL2/SDL_image.h" ]; then
		cc -std=gnu99 -Wall -Werror -g -DUSE_SDL2 -I.. -I../../common $(sdl2-config --cflags) -I"$SDL_PREFIX/include" -o /tmp/nx_test_iv_rotate ../iv_rotate.c ../../common/proc_spawn.c test_iv_rotate.c $(sdl2-config --libs) -lSDL2_image
		/tmp/nx_test_iv_rotate
	else
		echo "SKIP test_iv_rotate: SDL2_image headers not found"
	fi
else
	echo "SKIP test_iv_cache, test_iv_rotate: sdl2-config not found (SDL2 needed)"
fi
