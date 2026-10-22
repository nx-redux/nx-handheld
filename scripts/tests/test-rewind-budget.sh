#!/usr/bin/env bash
# Host unit test for minarch's rewind RAM budget (ma_rewind_budget.c): the
# rewind buffer shrinks, or rewind turns off, when the state copies plus the
# buffer would not fit in half of MemAvailable.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/workspace/all/minarch"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=gnu99 -Wall -Werror -fsanitize=address -g -I"$SRC" \
   "$SRC/ma_rewind_budget.c" "$SRC/tests/test_rewind_budget.c" \
   -o "$TMP/test_rewind_budget"
"$TMP/test_rewind_budget"
