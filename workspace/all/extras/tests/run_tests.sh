#!/bin/sh
# Host-side unit tests for the Xtras catalog's pure helpers. No SDL. Run from anywhere.
# (xtras_compat.c is tested from common/tests/run_tests.sh.)
set -eu
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}
# Compile and run on separate lines: set -e ignores a failure on the left of &&.
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address -g -I"$D" "$D/xtras_meta.c" "$D/tests/test_xtras_meta.c" \
	-o "$OUT/test_xtras_meta"
"$OUT/test_xtras_meta"
