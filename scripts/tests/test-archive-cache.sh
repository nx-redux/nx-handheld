#!/usr/bin/env bash
# Host unit test for minarch's /tmp/nextarch unpack cache (ma_archive_cache.c):
# per-archive folders (#222), eviction of other games, free-space check.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/workspace/all/minarch"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I"$SRC" \
   "$SRC/ma_archive_cache.c" "$SRC/tests/test_archive_cache.c" \
   -o "$TMP/test_archive_cache"
"$TMP/test_archive_cache"
