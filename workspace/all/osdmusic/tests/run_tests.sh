#!/bin/sh
# Host-side unit tests for osdmusic/. No SDL, no cross toolchain.
set -eu
cd "$(dirname "$0")"
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_osdmusic_config test_osdmusic_config.c
/tmp/nx_test_osdmusic_config
