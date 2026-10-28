#!/bin/sh
# The P8.pak and Splore.pak launchers (both platforms) against a fake SD card,
# with minarch.elf and show2.elf stubbed to record how they were called:
# - P8.pak given Splore's marker cart (an auto-resumed Splore session) opens
#   Splore in Splore mode and copies favourites, and Splore does not loop back
# - P8.pak given a cart runs pico8native with the PICO-8 files, fake08 without
# - Splore.pak without the PICO-8 files shows its message and runs nothing
#
#   sh workspace/all/pico8native/tests/test_splore_launch.sh
set -u
cd "$(dirname "$0")" || exit 1
REPO=$(cd ../../../.. && pwd)
T=${TMPDIR:-/tmp}/nx_test_splore_launch.$$
trap 'rm -rf "$T"' EXIT INT TERM

FAILS=0
PASSES=0
check() { # <description> <command...>
	d=$1
	shift
	if "$@"; then PASSES=$((PASSES + 1)); else FAILS=$((FAILS + 1)); echo "FAIL: $d"; echo "  calls: $(cat "$T/calls" 2>/dev/null)"; fi
}
has() { grep -qF -- "$1" "$T/calls" 2>/dev/null; }
lacks() { ! grep -qF -- "$1" "$T/calls" 2>/dev/null; }
count() { [ "$(grep -c '' "$T/calls" 2>/dev/null)" = "$1" ]; }

for PLAT in tg5040 tg5050; do
	rm -rf "$T"
	SD=$T/sd
	mkdir -p "$T/bin" "$T/sys/paks/Emus" "$T/sys/paks/Tools" "$SD/Roms/Pico-8 (P8)" \
		"$SD/.userdata/$PLAT/logs" "$SD/.userdata/shared/P8-native/home/bbs/carts"
	ln -s "$REPO/skeleton/SYSTEM/$PLAT/paks/Emus/P8.pak" "$T/sys/paks/Emus/P8.pak"
	ln -s "$REPO/skeleton/SYSTEM/$PLAT/paks/Tools/Splore.pak" "$T/sys/paks/Tools/Splore.pak"
	printf '#!/bin/sh\necho "minarch splore=${P8NATIVE_SPLORE:-} $*" >>"%s/calls"\n' "$T" >"$T/bin/minarch.elf"
	printf '#!/bin/sh\necho "show2 $*" >>"%s/calls"\n' "$T" >"$T/bin/show2.elf"
	chmod +x "$T/bin/minarch.elf" "$T/bin/show2.elf"
	printf '|a-1 |a |1 |x | |a game\n' >"$SD/.userdata/shared/P8-native/home/favourites.txt"
	printf 'cart' >"$SD/.userdata/shared/P8-native/home/bbs/carts/a-1.p8.png"
	MARKER="$SD/Roms/Pico-8 (P8)/.splore/Splore.p8"
	CART="$SD/Roms/Pico-8 (P8)/game.p8"
	run() { # <pak launch.sh> <args...>; synchronous: P8.pak's &> backgrounds minarch in some shells
		_l=$1
		shift
		env PATH="$T/bin:$PATH" SDCARD_PATH="$SD" ROMS_PATH="$SD/Roms" SYSTEM_PATH="$T/sys" \
			SHARED_SYSTEM_PATH="$REPO/skeleton/SYSTEM/shared" USERDATA_PATH="$SD/.userdata/$PLAT" \
			SHARED_USERDATA_PATH="$SD/.userdata/shared" LOGS_PATH="$SD/.userdata/$PLAT/logs" \
			NX_GPU_GOVERNOR_PATH="$T/no-governor" sh "$_l" "$@"
		i=0
		while [ ! -s "$T/calls" ] && [ $i -lt 50 ]; do sleep 0.1 2>/dev/null || sleep 1; i=$((i + 1)); done
	}
	P8="$T/sys/paks/Emus/P8.pak/launch.sh"
	SPLORE="$T/sys/paks/Tools/Splore.pak/launch.sh"

	# no PICO-8 files: Splore says so, P8.pak runs fake08
	run "$SPLORE"
	check "$PLAT splore without runtime shows the message" has "Splore needs PICO-8"
	check "$PLAT splore without runtime runs no minarch" lacks "minarch"
	rm -f "$T/calls"
	run "$P8" "$CART"
	check "$PLAT cart without runtime runs fake08" has "P8.pak/fake08_libretro.so $CART"
	rm -f "$T/calls"

	mkdir -p "$SD/Bios/P8"
	: >"$SD/Bios/P8/pico8_64"
	: >"$SD/Bios/P8/pico8.dat"
	run "$P8" "$CART"
	check "$PLAT cart with runtime runs pico8native" has "minarch splore= $T/sys/paks/Emus/P8.pak/pico8native_libretro.so $CART"
	rm -f "$T/calls"

	# the auto-resumed Splore marker reopens Splore, once, and copies favourites
	mkdir -p "$(dirname "$MARKER")"
	: >"$MARKER"
	run "$P8" "$MARKER"
	check "$PLAT resumed marker opens Splore mode" has "minarch splore=1 $T/sys/paks/Emus/P8.pak/pico8native_libretro.so $MARKER"
	check "$PLAT resumed marker runs minarch once" count 1
	check "$PLAT resumed marker copies favourites" test -f "$SD/Roms/Pico-8 (P8)/a game.p8.png"
done

echo "test_splore_launch: $PASSES passed, $FAILS failed"
[ "$FAILS" -eq 0 ]
