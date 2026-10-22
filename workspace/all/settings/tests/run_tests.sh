#!/bin/sh
# Host-side unit tests for settings' pure helpers. No SDL, no cross toolchain. Run from anywhere.
set -eu
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}
CF="-std=gnu99 -Wall -Wextra -Werror -fsanitize=address -g -I$D"
# Compile and run on separate lines: set -e ignores a failure on the left of &&.
cc $CF "$D/ssh_desc.c" "$D/tests/test_ssh_desc.c" -o "$OUT/test_ssh_desc"
"$OUT/test_ssh_desc"
cc $CF "$D/updater_url.c" "$D/tests/test_updater_url.c" -o "$OUT/test_updater_url"
"$OUT/test_updater_url"
