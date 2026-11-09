#!/bin/sh
# check_upstream.sh: fails unless rfu.c and serial_proto.c are gpSP's own files
# at the pinned commit with patches 001 and 002 applied, with only the
# "common.h" include swapped for gwl_shim.h. That keeps the wire protocol
# exactly gpSP's. The patched checkout is cached and reused by run_tests.sh:
# the last line printed is its path.
set -e
D=$(cd "$(dirname "$0")/.." && pwd)
CORES=$(cd "$D/.." && pwd)
WS=$(cd "$CORES/../.." && pwd)
# Same commit the gpSP core builds from, so a pin bump re-checks this port.
HASH=$(sed -n 's/^gpsp_HASH *= *//p' "$WS/tg5040/cores/Makefile")
[ -n "$HASH" ] || { echo "error: gpsp_HASH not found in tg5040/cores/Makefile" >&2; exit 1; }
PATCHES="$CORES/patches/gpsp/001-rfu_disconnect_fix.patch $CORES/patches/gpsp/002-rfu_queue_size.patch"
SUM=$(cat $PATCHES | cksum | cut -d' ' -f1-2)
T=${TMPDIR:-/tmp}
SRC=${T%/}/gwl_gpsp_$HASH
if [ "$(cat "$SRC/.gwl-patched" 2>/dev/null)" != "$SUM" ]; then
	rm -rf "$SRC"
	mkdir -p "$SRC"
	(
		cd "$SRC"
		git init -q
		git fetch -q --depth 1 https://github.com/libretro/gpsp "$HASH"
		git checkout -q FETCH_HEAD
		for P in $PATCHES; do git apply -p1 < "$P"; done
	) >&2
	echo "$SUM" > "$SRC/.gwl-patched"
fi
FAIL=0
for F in rfu.c serial_proto.c; do
	if [ ! -f "$D/$F" ]; then
		echo "check_upstream: $F missing" >&2
		FAIL=1
		continue
	fi
	sed 's|^#include "common.h"$|#include "gwl_shim.h"|' "$SRC/$F" > "$SRC/$F.gwl"
	if ! diff -u "$SRC/$F.gwl" "$D/$F" >&2; then
		echo "check_upstream: $F differs from gpSP $HASH + 001/002" >&2
		FAIL=1
	fi
done
[ "$FAIL" = 0 ] || exit 1
echo "check_upstream: rfu.c, serial_proto.c match gpSP $HASH + 001/002" >&2
echo "$SRC"
