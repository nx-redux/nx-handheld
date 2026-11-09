#!/usr/bin/env bash
# Device test for a rebuilt mupen64plus_next core on a tg5040 (Brick / Smart Pro), over adb; run by hand.
#
#   scripts/tests/test-n64-core-device.sh [--serial S] [--rom "Super Mario 64.z64"] <new core .so>
#   scripts/tests/test-n64-core-device.sh [--serial S] --restore
#
# The game's existing auto-resume state (made by the shipped core) is resumed on the shipped core, then on the new
# one, then the new core boots the game cold. Pass: every run keeps minarch up with no state or core error in the
# log, the new core's resume looks like the shipped core's, and both look unlike the cold boot (the state loaded).
# The shipped core is kept as <core>.keep and the new one stays installed for a netplay session by hand;
# --restore puts the shipped core back. The auto state is restored from its backup before every run.
set -euo pipefail
SERIAL=5c000c8997414781d1d
ROM="Super Mario 64.z64"
RESTORE=0
NEW=""
while [ $# -gt 0 ]; do
	case "$1" in
		--serial) SERIAL="$2"; shift 2 ;;
		--rom) ROM="$2"; shift 2 ;;
		--restore) RESTORE=1; shift ;;
		*) NEW="$1"; shift ;;
	esac
done
A() { adb -s "$SERIAL" "$@"; }
SD=/mnt/SDCARD
PAK="$SD/.system/paks/Emus/N64.pak"
CORE="$PAK/mupen64plus_next_libretro.so"
STATE="$SD/.userdata/shared/N64-mupen64plus_next/${ROM%.*}.state.auto"
LOG="$SD/.userdata/tg5040/logs/N64.txt"
OUT="${TMPDIR:-/tmp}/n64-core-device"
mkdir -p "$OUT"

if [ "$RESTORE" = 1 ]; then
	A shell "[ -f '$CORE.keep' ] && mv -f '$CORE.keep' '$CORE' && sync && echo restored || echo 'no backup: nothing to restore'"
	exit 0
fi
[ -f "$NEW" ] || { echo "usage: $0 [--serial S] [--rom R] <new core .so> | --restore" >&2; exit 2; }
A shell "[ -f '$STATE' ]" || { echo "no auto state for $ROM on the device ($STATE): play it once on the shipped core" >&2; exit 2; }
A shell "[ -f '$CORE.keep' ] || cp '$CORE' '$CORE.keep'; [ -f '$STATE.keep' ] || cp '$STATE' '$STATE.keep'; sync"

FAIL=0
fail() { echo "FAIL: $*"; FAIL=1; }

# run <name> <core on the device> <resume: 1/0>: reboot into the game, capture the screen after it settles
run() {
	local name="$1" core="$2" resume="$3"
	A shell "cp '$core' '$CORE' && cp '$STATE.keep' '$STATE' && sync"
	[ "$resume" = 1 ] || A shell "rm -f '$STATE'; sync"
	A shell "printf '%s' '/Roms/Nintendo 64 (N64)/$ROM' > $SD/.userdata/shared/.minui/auto_resume.txt; sync; reboot" >/dev/null 2>&1 || true
	sleep 10
	for _ in $(seq 1 60); do A shell true >/dev/null 2>&1 && break; sleep 2; done
	sleep 35
	[ "$(A shell 'ps | grep -v grep | grep -c minarch.elf' | tr -d '\r')" -ge 1 ] || fail "$name: minarch not running"
	A shell "$SD/.system/bin/screenshot.elf -c /tmp/n64shot.bmp" >/dev/null 2>&1 || true
	A pull /tmp/n64shot.bmp "$OUT/$name.bmp" >/dev/null 2>&1 || fail "$name: no screen capture"
	A pull "$LOG" "$OUT/$name.log" >/dev/null 2>&1 || true
	# mupen64plus-next refuses a state until it has run: the first try fails and minarch retries (ma_saves.c)
	local errors="Error reading state|Segmentation|undefined symbol|Failed to load core"
	grep -q "Resume state not accepted yet" "$OUT/$name.log" 2>/dev/null || errors="Error restoring save state|$errors"
	if grep -qE "$errors" "$OUT/$name.log" 2>/dev/null; then
		fail "$name: errors in the log:"
		grep -E "$errors" "$OUT/$name.log" | head -3
	fi
}

A push "$NEW" "$PAK/mupen64plus_next_libretro.so.new" >/dev/null
run shipped "$CORE.keep" 1
run new "$PAK/mupen64plus_next_libretro.so.new" 1
run cold "$PAK/mupen64plus_next_libretro.so.new" 0
A shell "cp '$STATE.keep' '$STATE' && rm -f '$STATE.keep' && mv -f '$PAK/mupen64plus_next_libretro.so.new' '$CORE' && sync"

# mean per-pixel difference (0-255) between two captures
diffmean() {
	python3 -I - "$1" "$2" <<'PY'
import sys
def px(p):
    b = open(p, 'rb').read()
    off = int.from_bytes(b[10:14], 'little')
    return b[off:]
a, b = px(sys.argv[1]), px(sys.argv[2])
n = min(len(a), len(b))
print(int(sum(abs(a[i] - b[i]) for i in range(0, n, 97)) / len(range(0, n, 97))))
PY
}
if [ -f "$OUT/shipped.bmp" ] && [ -f "$OUT/new.bmp" ] && [ -f "$OUT/cold.bmp" ]; then
	SAME=$(diffmean "$OUT/shipped.bmp" "$OUT/new.bmp")
	COLD=$(diffmean "$OUT/new.bmp" "$OUT/cold.bmp")
	echo "screen difference: new vs shipped resume $SAME, new resume vs cold boot $COLD"
	[ "$SAME" -lt "$COLD" ] || fail "the new core's resume looks no more like the shipped core's than a cold boot (state not loaded?)"
fi
echo "captures and logs: $OUT"
[ "$FAIL" = 0 ] && echo "ALL PASS (the new core stays installed; --restore puts the shipped one back)" || exit 1
