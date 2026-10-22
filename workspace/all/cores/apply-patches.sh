#!/bin/sh
# apply-patches.sh <checkout> <patch dir>: applies <patch dir>/*.patch (sorted)
# to a core checkout once. The marker <checkout>/.patched-all records a checksum
# of the patch set, so a patch added or changed after the checkout was patched
# fails loudly instead of being silently skipped. An empty marker comes from
# before the checksum and is adopted as-is.
set -e
SRC="$1"
DIR="$2"
MARK="$SRC/.patched-all"
PATCHES=""
[ -d "$DIR" ] && PATCHES=$(ls "$DIR"/*.patch 2>/dev/null | sort) || true
# The checksum skips each patch's leading "# license:" line, so adding or
# rewording it doesn't invalidate an already-patched checkout.
WANT=$(cat /dev/null $PATCHES | grep -v '^# license: ' | cksum | cut -d' ' -f1-2)
if [ -f "$MARK" ]; then
	HAVE=$(cat "$MARK")
	if [ -z "$HAVE" ] || [ "$HAVE" = "$WANT" ]; then
		echo "$WANT" > "$MARK"
		exit 0
	fi
	echo "error: the patches in $DIR changed after $SRC was patched; rm -rf $SRC and rebuild" >&2
	exit 1
fi
ABS=$(cd "${DIR:-.}" 2>/dev/null && pwd || true)
# All or nothing: git apply is atomic per patch, and when one fails the ones
# before it are reversed (newest first), so the checkout is left unpatched and
# the next build retries the whole set instead of re-applying patch 1 onto a
# half-patched tree.
APPLIED=""
for P in $PATCHES; do
	F="$ABS/$(basename "$P")"
	if ! (cd "$SRC" && git apply -p1 < "$F"); then
		for R in $APPLIED; do
			(cd "$SRC" && git apply -R -p1 < "$R") ||
				echo "error: could not reverse $R; rm -rf $SRC and rebuild" >&2
		done
		echo "error: $(basename "$P") does not apply to $SRC (earlier patches reversed)" >&2
		exit 1
	fi
	APPLIED="$F $APPLIED"
done
echo "$WANT" > "$MARK"
