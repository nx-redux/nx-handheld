#!/usr/bin/env bash
# Host test for workspace/all/cores/apply-patches.sh: a patch set whose later
# patch fails leaves the checkout unpatched (the earlier patches reversed, no
# marker), so fixing the set and rebuilding applies it cleanly; a good set is
# applied once and marked.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APPLY="$ROOT/workspace/all/cores/apply-patches.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() {
	echo "FAIL: $1" >&2
	exit 1
}

SRC="$TMP/core"
PD="$TMP/patches"
mkdir -p "$SRC" "$PD"
printf 'one\n' >"$SRC/a.txt"
printf 'two\n' >"$SRC/b.txt"

cat >"$PD/0001-a.patch" <<'P'
--- a/a.txt
+++ b/a.txt
@@ -1 +1 @@
-one
+ONE
P
# does not apply: b.txt holds "two", not "zwei"
cat >"$PD/0002-b.patch" <<'P'
--- a/b.txt
+++ b/b.txt
@@ -1 +1 @@
-zwei
+TWO
P

if sh "$APPLY" "$SRC" "$PD" 2>"$TMP/err"; then
	fail "a failing patch set reported success"
fi
grep -q "0002-b.patch does not apply" "$TMP/err" || fail "no error naming the failing patch"
[ "$(cat "$SRC/a.txt")" = "one" ] || fail "patch 1 was left applied after patch 2 failed"
[ ! -e "$SRC/.patched-all" ] || fail "marker written for a failed set"

# the set fixed: the next build applies all of it
cat >"$PD/0002-b.patch" <<'P'
--- a/b.txt
+++ b/b.txt
@@ -1 +1 @@
-two
+TWO
P
sh "$APPLY" "$SRC" "$PD" || fail "the fixed set did not apply"
[ "$(cat "$SRC/a.txt")" = "ONE" ] && [ "$(cat "$SRC/b.txt")" = "TWO" ] || fail "fixed set not applied"
[ -s "$SRC/.patched-all" ] || fail "no marker after a clean apply"

# applied once: a second run is a no-op
sh "$APPLY" "$SRC" "$PD" || fail "re-run of an applied set failed"
[ "$(cat "$SRC/a.txt")" = "ONE" ] || fail "re-run changed the checkout"

echo "PASS: test-apply-patches"
