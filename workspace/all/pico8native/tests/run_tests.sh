#!/bin/sh
# Host-side unit tests for pico8native/. p8ipc needs process-shared sem_init and
# sem_timedwait, which macOS lacks, so these build with the host cc inside the
# Linux toolchain container. From the repo root:
#
#   docker run --rm -v "$PWD/workspace:/root/workspace" ghcr.io/loveretro/tg5040-toolchain:latest \
#       /bin/sh /root/workspace/all/pico8native/tests/run_tests.sh
#
# Run on a non-Linux host, this script does that docker run itself.
set -eu
cd "$(dirname "$0")"
if [ "$(uname -s)" != Linux ]; then
	WS=$(cd ../../.. && pwd)
	exec docker run --rm -v "$WS:/root/workspace" ghcr.io/loveretro/tg5040-toolchain:latest \
		/bin/sh /root/workspace/all/pico8native/tests/run_tests.sh
fi
# Compile and run on separate lines: set -e ignores a failure on the left of &&.
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_p8ipc ../p8ipc.c test_p8ipc.c -pthread
/tmp/nx_test_p8ipc
# p8ipc_wait against wall-clock steps: includes p8ipc.c to put it on fake clocks
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -o /tmp/nx_test_p8ipc_clock test_p8ipc_clock.c -pthread
/tmp/nx_test_p8ipc_clock
