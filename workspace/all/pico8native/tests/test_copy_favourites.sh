#!/bin/sh
# pico8_copy_favourites.sh against favourites.txt files in PICO-8 0.2.7's layout
# (fixtures/favourites/): |file |slug |id |author |... |title, fields padded with
# spaces, a CRLF line, the older |0|bbs/1/<id>.p8.png|<id> layout, and an empty file.
#
#   sh workspace/all/pico8native/tests/test_copy_favourites.sh
#
# The script runs under the shell named by TEST_SH (default sh). With --busybox it
# runs itself again under busybox sh in the Linux toolchain container, as on device.
set -u
cd "$(dirname "$0")" || exit 1
TESTS=$(pwd)
if [ "${1:-}" = --busybox ]; then
	WS=$(cd ../../.. && pwd)
	ROOT=$(cd "$WS/.." && pwd)
	exec docker run --rm -v "$ROOT/skeleton:/root/skeleton" -v "$WS:/root/workspace" \
		ghcr.io/loveretro/tg5040-toolchain:latest \
		/bin/sh -c 'B=$(command -v busybox) || { echo "no busybox in the container"; exit 1; }
			TEST_SH="$B sh" exec $B sh /root/workspace/all/pico8native/tests/test_copy_favourites.sh'
fi
SH=${TEST_SH:-sh}
SCRIPT=$(cd ../../../.. && pwd)/skeleton/SYSTEM/shared/bin/pico8_copy_favourites.sh
FIX=$TESTS/fixtures/favourites
T=${TMPDIR:-/tmp}/nx_test_copy_favourites.$$
trap 'rm -rf "$T"' EXIT INT TERM

FAILS=0
PASSES=0
ok() { PASSES=$((PASSES + 1)); }
fail() { FAILS=$((FAILS + 1)); echo "FAIL: $*"; }
# file_is <path> <expected content>
file_is() {
	if [ ! -f "$1" ]; then
		fail "missing $1"
	elif [ "$(cat "$1")" != "$2" ]; then
		fail "$1 holds '$(cat "$1")', want '$2'"
	else
		ok
	fi
}
absent() { if [ -e "$1" ]; then fail "unexpected $1"; else ok; fi; }
# the visible carts in a roms folder, one per line, sorted
carts() { (cd "$1" && ls | sort); }
run() { # home roms
	LOGS_PATH="$T/logs" $SH "$SCRIPT" "$1" "$2"
	rc=$?
	if [ $rc -ne 0 ]; then fail "exit $rc for $1"; else ok; fi
}
cart() { mkdir -p "$(dirname "$1")" && printf '%s' "$2" >"$1"; }

rm -rf "$T"
mkdir -p "$T/logs"

# --- main case: padded fields, CRLF, a title with / and :, a name clash, revisions
H=$T/home
R=$T/roms
mkdir -p "$H" "$R"
cp "$FIX/favourites.txt" "$H/favourites.txt"
cart "$H/bbs/carts/suika_demake-8.p8.png" "suika 8"
cart "$H/bbs/carts/suika_demake-9.p8.png" "suika 9"
cart "$H/bbs/5/56576.p8.png" "buns"
cart "$H/bbs/carts/celeste_classic-2.p8.png" "celeste 2"
cart "$H/bbs/carts/picodriller-0.p8.png" "driller"
cart "$H/bbs/carts/otherthing-1.p8.png" "not a favourite"
cart "$H/bbs/carts/temp-celeste_classic-3.p8.png" "partial download"
printf 'my own driller' >"$R/picodriller.p8.png"
printf 'my own game' >"$R/mine.p8.png"

run "$H" "$R"
file_is "$R/suika game demake.p8.png" "suika 9"
file_is "$R/mai-chan's sweet buns.p8.png" "buns"
file_is "$R/celeste- classic - remix.p8.png" "celeste 2"
file_is "$R/picodriller.p8.png" "my own driller"
file_is "$R/picodriller (picodriller).p8.png" "driller"
file_is "$R/mine.p8.png" "my own game"
absent "$R/otherthing.p8.png"
want=$(printf '%s\n' "celeste- classic - remix.p8.png" "mai-chan's sweet buns.p8.png" "mine.p8.png" \
	"picodriller (picodriller).p8.png" "picodriller.p8.png" "suika game demake.p8.png" | sort)
if [ "$(carts "$R")" = "$want" ]; then ok; else fail "first run carts:
$(carts "$R")"; fi
if grep -q "$(printf 'suika_demake\t9\tsuika game demake.p8.png')" "$R/.splore/copied.txt" 2>/dev/null; then ok; else
	fail "copied.txt lacks suika_demake rev 9: $(cat "$R/.splore/copied.txt" 2>/dev/null)"; fi
if [ -s "$T/logs/splore.txt" ]; then ok; else fail "nothing logged to splore.txt"; fi

# --- a second run with nothing new changes nothing
before=$(carts "$R")
lines=$(wc -l <"$R/.splore/copied.txt")
run "$H" "$R"
if [ "$(carts "$R")" = "$before" ]; then ok; else fail "second run changed the carts"; fi
if [ "$(wc -l <"$R/.splore/copied.txt")" = "$lines" ]; then ok; else fail "second run grew copied.txt"; fi
file_is "$R/picodriller (picodriller).p8.png" "driller"

# --- a newer revision replaces the earlier copy in place; nothing is deleted
cart "$H/bbs/carts/suika_demake-10.p8.png" "suika 10"
run "$H" "$R"
file_is "$R/suika game demake.p8.png" "suika 10"
if [ "$(carts "$R")" = "$before" ]; then ok; else fail "the update added or removed carts:
$(carts "$R")"; fi
if grep -q "$(printf 'suika_demake\t10\t')" "$R/.splore/copied.txt"; then ok; else fail "copied.txt not at rev 10"; fi
file_is "$H/bbs/carts/suika_demake-8.p8.png" "suika 8"

# --- an older cached revision never replaces a newer copy
rm -f "$H/bbs/carts/suika_demake-10.p8.png"
run "$H" "$R"
file_is "$R/suika game demake.p8.png" "suika 10"

# --- a file the user made identical to a favourite is adopted, not duplicated
H2=$T/home2
R2=$T/roms2
mkdir -p "$H2" "$R2"
printf '|picodriller-0   |picodriller   |1794   |johanp   |   |picodriller\n' >"$H2/favourites.txt"
cart "$H2/bbs/carts/picodriller-0.p8.png" "driller"
printf 'driller' >"$R2/picodriller.p8.png"
run "$H2" "$R2"
if [ "$(carts "$R2")" = "picodriller.p8.png" ]; then ok; else fail "identical file duplicated:
$(carts "$R2")"; fi

# --- empty favourites: nothing copied, exit 0
H3=$T/home3
R3=$T/roms3
mkdir -p "$H3" "$R3"
cp "$FIX/favourites-empty.txt" "$H3/favourites.txt"
cart "$H3/bbs/carts/suika_demake-9.p8.png" "suika 9"
run "$H3" "$R3"
if [ -z "$(carts "$R3")" ]; then ok; else fail "empty favourites copied: $(carts "$R3")"; fi

# --- no favourites.txt and no bbs at all: exit 0, nothing copied
H4=$T/home4
R4=$T/roms4
mkdir -p "$H4" "$R4"
run "$H4" "$R4"
if [ -z "$(carts "$R4")" ]; then ok; else fail "missing favourites copied: $(carts "$R4")"; fi

# --- the .lexaloffle/pico-8 layout and the older |0|bbs/1/<id>.p8.png|<id> rows
H5=$T/home5
R5=$T/roms5
mkdir -p "$H5/.lexaloffle/pico-8" "$R5"
cp "$FIX/favourites-lexaloffle.txt" "$H5/.lexaloffle/pico-8/favourites.txt"
cart "$H5/.lexaloffle/pico-8/bbs/1/15133.p8.png" "id cart"
cart "$H5/.lexaloffle/pico-8/bbs/carts/freecell.p8.png" "freecell"
run "$H5" "$R5"
file_is "$R5/15133.p8.png" "id cart"
file_is "$R5/freecell.p8.png" "freecell"

# --- a long title is cut to 120 bytes without splitting a UTF-8 character
H6=$T/home6
R6=$T/roms6
mkdir -p "$H6" "$R6"
long=a$(printf 'é%.0s' $(seq 1 70))
printf '|long-1   |long   |1   |a   |   |  %s  \n' "$long" >"$H6/favourites.txt"
cart "$H6/bbs/carts/long-1.p8.png" "long"
run "$H6" "$R6"
name=$(carts "$R6")
stem=${name%.p8.png}
bytes=$(printf '%s' "$stem" | LC_ALL=C wc -c | tr -d ' ')
if [ "$bytes" = 119 ] && [ "$stem" = "a$(printf 'é%.0s' $(seq 1 59))" ]; then ok; else
	fail "long title gave '$name' ($bytes bytes)"; fi

echo "test_copy_favourites ($SH): $PASSES passed, $FAILS failed"
[ "$FAILS" -eq 0 ]
