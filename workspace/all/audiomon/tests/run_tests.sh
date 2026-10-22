#!/bin/sh
# Host-side unit tests for audiomon/. No D-Bus/udev/ALSA, no cross toolchain.
# scripts/tests/test-audiomon-spk-guard.sh also runs test_spk_guard, plus the
# integration string checks of the speaker guard.
set -eu
cd "$(dirname "$0")"
# Compile and run on separate lines: set -e ignores a failure on the left of &&, so a test that
# fails to compile would otherwise be skipped silently.
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_spk_guard ../spk_guard.c test_spk_guard.c
/tmp/nx_test_spk_guard
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I.. -o /tmp/nx_test_audiomon_parse test_audiomon_parse.c
/tmp/nx_test_audiomon_parse
