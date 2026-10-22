#!/bin/sh
# Host-side unit tests for the netplay wizard's pure modules (wire protocol, save-sync helpers).
# No SDL. Run from anywhere. (wiz_caps.c and wiz_modes.c are tested from common/tests/run_tests.sh.)
set -eu
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}
CF="-std=gnu99 -Wall -Wextra -Werror -fsanitize=address -g -I$D -I$D/../netplay"
# Compile and run on separate lines: set -e ignores a failure on the left of &&.
cc $CF "$D/wiz_proto.c" "$D/wiz_caps.c" "$D/tests/test_wiz_proto.c" -o "$OUT/test_wiz_proto"
"$OUT/test_wiz_proto"
cc $CF "$D/wiz_sync_util.c" "$D/tests/test_wiz_sync_util.c" -o "$OUT/test_wiz_sync_util"
"$OUT/test_wiz_sync_util"
