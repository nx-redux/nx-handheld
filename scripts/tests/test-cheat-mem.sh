#!/usr/bin/env bash
# Host unit test for minarch's pure RAM-patch cheat engine (ma_cheat_mem.c).
# No device toolchain: compiles with the host cc and runs.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/workspace/all/minarch"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=gnu99 -Wall -Wextra -I"$SRC" \
   "$SRC/ma_cheat_mem.c" "$SRC/tests/test_cheat_mem.c" \
   -o "$TMP/test_cheat_mem"
"$TMP/test_cheat_mem"
