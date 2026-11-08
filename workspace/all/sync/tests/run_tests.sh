#!/bin/sh
# Host-side unit tests for Device Sync's pure protocol modules. Run from anywhere.
set -e
D=$(cd "$(dirname "$0")/.." && pwd)
OUT=${TMPDIR:-/tmp}
CFLAGS="-std=gnu99 -Wall -Wextra -Werror -pthread -I$D"
cc $CFLAGS "$D/sync_proto.c" "$D/sync_pair.c" "$D/sha256.c" "$D/tests/test_sync_proto.c" -o "$OUT/test_sync_proto"
cc $CFLAGS "$D/sync_pair.c" "$D/sha256.c" "$D/tests/test_sync_pair.c" -o "$OUT/test_sync_pair"
"$OUT/test_sync_proto"
"$OUT/test_sync_pair"
