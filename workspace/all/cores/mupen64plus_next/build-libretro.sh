#!/bin/sh
# Builds mupen64plus-next (libretro) for the cores Makefile (mupen64plus_next_MAKE).
# Run from the checkout; $1 is the platform, the cores template appends -jN.
# GLES3 + GLideN64 + new aarch64 dynarec. Leaves the .so in the checkout root.
set -e
PLAT="$1"
JOBS="${2:--j4}"
HERE=$(cd "$(dirname "$0")" && pwd)
INC="$HERE/../../include"
if [ "$JOBS" = clean ]; then
	make platform=arm64_cortex_a53_gles3 clean
	exit 0
fi
# tg5040 (PowerVR) ships libEGL; tg5050 (Mali) has the EGL symbols in libmali.so.0
# only (the device also has a libEGL.so, but the toolchain sysroot does not).
EGL_LIB=-lEGL
[ "$PLAT" = tg5050 ] && EGL_LIB=-lmali

# Rice video plugin (patch 0003): one relocatable object linked into the
# core. NX_RICE=0 builds GLideN64 only.
NX_RICE="${NX_RICE:-1}"
RICE_ARGS=""
RICE_LDFLAGS=""
if [ "$NX_RICE" = 1 ]; then
	sh "$HERE/build-rice.sh" "$(pwd)" "$(pwd)/rice.o"
	RICE_ARGS="HAVE_RICE=1 RICE_OBJ=$(pwd)/rice.o"
	# its critical sections are SDL mutexes (the device's own libSDL2)
	RICE_LDFLAGS="-lSDL2"
fi
# The licence that ships beside the core (mupen64plus_next_LICENSE): the
# core's own, plus Rice's when it is built in.
cp LICENSE NX-LICENSES.txt
if [ "$NX_RICE" = 1 ]; then
	printf '\n==== mupen64plus-video-rice (built in) ====\nSource: https://github.com/mupen64plus/mupen64plus-video-rice\nCommit: %s\n\n' \
		"$(git -C rice-src rev-parse HEAD)" >> NX-LICENSES.txt
	cat rice-src/LICENSES >> NX-LICENSES.txt
fi

# GLES3/EGL/KHR headers from workspace/all/include (tg5050's sysroot only has
# GLES2). Passed through the environment: a command-line INCFLAGS would replace
# the Makefile's own include list.
CPPFLAGS="-I$INC" LDFLAGS="$RICE_LDFLAGS" \
	make platform=arm64_cortex_a53_gles3 ARCH=aarch64 \
	EGL_LIB="$EGL_LIB" $RICE_ARGS NX_SRC_DIR="$HERE/nx" "$JOBS"
[ -n "$NX_NOSTRIP" ] || /opt/aarch64-nextui-linux-gnu/bin/aarch64-nextui-linux-gnu-strip mupen64plus_next_libretro.so
