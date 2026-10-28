#!/bin/sh
# Host-side unit tests for pico8native/. p8ipc needs process-shared sem_init and
# sem_timedwait, which macOS lacks, so these build with the host cc inside the
# Linux toolchain container. The device scripts' tests need skeleton/ next to workspace/.
# From the repo root:
#
#   docker run --rm -v "$PWD/workspace:/root/workspace" -v "$PWD/skeleton:/root/skeleton" \
#       ghcr.io/loveretro/tg5040-toolchain:latest /bin/sh /root/workspace/all/pico8native/tests/run_tests.sh
#
# Run on a non-Linux host, this script does that docker run itself.
set -eu
cd "$(dirname "$0")"
if [ "$(uname -s)" != Linux ]; then
	WS=$(cd ../../.. && pwd)
	exec docker run --rm -v "$WS:/root/workspace" -v "$WS/../skeleton:/root/skeleton" \
		ghcr.io/loveretro/tg5040-toolchain:latest /bin/sh /root/workspace/all/pico8native/tests/run_tests.sh
fi
# Compile and run on separate lines: set -e ignores a failure on the left of &&.
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_p8ipc ../p8ipc.c test_p8ipc.c -pthread
/tmp/nx_test_p8ipc
# p8ipc_wait against wall-clock steps: includes p8ipc.c to put it on fake clocks
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_p8ipc_clock test_p8ipc_clock.c -pthread
/tmp/nx_test_p8ipc_clock
# Shim pieces that need no SDL: frame conversion, cdata redirect, the frame clock, audio pulls
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o /tmp/nx_test_frame ../shim/frame.c test_frame.c
/tmp/nx_test_frame
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o /tmp/nx_test_paths ../shim/paths.c test_paths.c
/tmp/nx_test_paths
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o /tmp/nx_test_clock ../shim/clock.c test_clock.c
/tmp/nx_test_clock
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o /tmp/nx_test_audio ../shim/audio.c test_audio.c
/tmp/nx_test_audio
# The core's pure helpers: pico8_64 argv/env, the Bios lookup, the config.txt fix-up
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o /tmp/nx_test_core_args ../core_args.c test_core_args.c
/tmp/nx_test_core_args
# The save state header the core checks before a state reaches PICO-8, and its SHA-256
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o /tmp/nx_test_state_header ../p8state.c test_state_header.c
/tmp/nx_test_state_header
# The shim as a whole: fake_pico8 runs under it against a fake libSDL2, test_shim is the core.
# fake_pico8 is not PIE: its image stays put between runs, as pico8_64's does with ASLR off.
# No ASan here: it must come first in LD_PRELOAD, ahead of the shim.
T=/tmp/nx_test_shim_bin
rm -rf $T && mkdir -p $T
cc -std=gnu99 -shared -fPIC -fvisibility=hidden -Wall -Wextra -Werror -U_FORTIFY_SOURCE -g -o $T/pico8shim.so \
	../shim/shim.c ../shim/frame.c ../shim/paths.c ../shim/clock.c ../shim/audio.c ../shim/state.c ../p8ipc.c ../p8state.c \
	-ldl -lpthread
cc -std=gnu99 -shared -fPIC -Wall -Wextra -Werror -g -Wl,-soname,libSDL2-2.0.so.0 -o $T/libSDL2-2.0.so.0 fake_sdl.c
cc -std=gnu99 -Wall -Wextra -Werror -g -no-pie -o $T/fake_pico8 fake_pico8.c $T/libSDL2-2.0.so.0 -Wl,-rpath,$T -lpthread
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o $T/test_shim ../p8ipc.c test_shim.c -pthread
$T/test_shim $T/pico8shim.so $T/fake_pico8 60
$T/test_shim $T/pico8shim.so $T/fake_pico8 30
# Save states end to end: save, load, damaged states, and a load into a fresh run
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -g -o $T/test_state ../p8ipc.c ../p8state.c test_state.c -pthread
$T/test_state $T/pico8shim.so $T/fake_pico8
# The core as minarch drives it, starting fake_pico8 as pico8_64: slow frames, PICO-8's own
# quit, states going off in the shim. Built as the .so minarch loads, next to the shim.
cc -std=gnu99 -shared -fPIC -fvisibility=hidden -Wall -Wextra -Werror -g -I.. -I../../minarch/libretro-common/include \
	-o $T/pico8native_libretro.so ../core.c ../core_args.c ../p8ipc.c ../p8state.c -ldl -lpthread
cc -std=gnu99 -Wall -Wextra -Werror -g -I../../minarch/libretro-common/include -o $T/test_core test_core.c -ldl
$T/test_core $T
# The device scripts Splore runs: favourites copy and the P8/Splore launchers. Under this
# sh, then under busybox's sh and applets (awk, cksum, find...) as on the devices.
sh test_copy_favourites.sh
sh test_splore_launch.sh
if B=$(command -v busybox); then
	BB=/tmp/nx_test_busybox
	rm -rf $BB && mkdir -p $BB && $B --install -s $BB
	PATH=$BB:$PATH sh test_copy_favourites.sh
	PATH=$BB:$PATH sh test_splore_launch.sh
else
	echo "run_tests: no busybox here, the device scripts ran under $(readlink -f /bin/sh) only"
fi
