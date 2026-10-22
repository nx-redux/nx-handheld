#!/bin/sh
# Host-side unit tests for ratools' pure modules. No SDL, rcheevos or libzip. Run from anywhere.
set -eu
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}
CF="-std=gnu99 -Wall -Wextra -Werror -fsanitize=address -g -I$D -I$D/../common"
# Compile and run on separate lines: set -e ignores a failure on the left of &&.
cc $CF "$D/tests/test_display_order.c" -o "$OUT/test_display_order"
"$OUT/test_display_order" >/dev/null
cc $CF "$D/ratools_scan.c" "$D/ratools_logic.c" "$D/tests/test_ratools_scan.c" -o "$OUT/test_ratools_scan"
"$OUT/test_ratools_scan"
