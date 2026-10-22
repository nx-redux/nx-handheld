#!/bin/sh
# Host-side unit tests for show2/. No cross toolchain; needs the host SDL2,
# SDL2_image and SDL2_ttf (skipped when sdl2-config is missing). Built without
# -fsanitize=address: Homebrew's sdl2-compat fails to load SDL3 under ASan
# and blocks on an error dialog. Uses /tmp/show2.fifo.
set -eu
cd "$(dirname "$0")"
if ! command -v sdl2-config >/dev/null 2>&1; then
	echo "SKIP test_show2: sdl2-config not found (SDL2 needed)"
	exit 0
fi
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
c++ -std=c++17 -Wall -Werror -g $(sdl2-config --cflags) -I"$(sdl2-config --prefix)/include" -o /tmp/nx_test_show2 test_show2.cpp $(sdl2-config --libs) -lSDL2_image -lSDL2_ttf -lpthread
/tmp/nx_test_show2
