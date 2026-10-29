#!/bin/sh
# The P8.pak and Splore.pak launchers (both platforms) and Splore's Xtras
# install/uninstall against a fake SD card, with minarch.elf, show2.elf and the
# user's pico8_64 stubbed to record how they were called:
# - Xtras refuses to install Splore without the PICO-8 files, installs it into
#   Tools/ with them, and uninstall removes it
# - Splore runs pico8_64 -splore itself, no minarch: pico8shim.so preloaded in
#   standalone mode, cart saves in Saves/P8/cdata, the device's SDL (not P8.pak's
#   lib/), the pad's mapping added to sdl_controllers.txt once; favourites are
#   copied after it exits, and its exit status is logged
# - P8.pak given a cart runs pico8native with the PICO-8 files, fake08 without;
#   a .splore/Splore.p8 left from the old minarch Splore is just a cart now
# - Splore.pak whose PICO-8 files went missing shows its message and runs nothing
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
fails() { ! "$@"; }

for PLAT in tg5040 tg5050; do
	rm -rf "$T"
	SD=$T/sd
	mkdir -p "$T/bin" "$T/sys/paks/Emus" "$T/sys/paks/Tools" "$SD/Roms/Pico-8 (P8)" \
		"$SD/.userdata/$PLAT/logs" "$SD/.userdata/shared/P8-native/home/bbs/carts"
	ln -s "$REPO/skeleton/SYSTEM/$PLAT/paks/Emus/P8.pak" "$T/sys/paks/Emus/P8.pak"
	CATALOG="$REPO/skeleton/SYSTEM/$PLAT/paks/Tools/Xtras.pak/catalog/splore"
	printf '#!/bin/sh\necho "minarch splore=${P8NATIVE_SPLORE:-} $*" >>"%s/calls"\n' "$T" >"$T/bin/minarch.elf"
	printf '#!/bin/sh\necho "show2 $*" >>"%s/calls"\n' "$T" >"$T/bin/show2.elf"
	chmod +x "$T/bin/minarch.elf" "$T/bin/show2.elf"
	printf '|a-1 |a |1 |x | |a game\n' >"$SD/.userdata/shared/P8-native/home/favourites.txt"
	printf 'cart' >"$SD/.userdata/shared/P8-native/home/bbs/carts/a-1.p8.png"
	MARKER="$SD/Roms/Pico-8 (P8)/.splore/Splore.p8"
	PADS="$SD/.userdata/shared/P8-native/home/sdl_controllers.txt"
	CART="$SD/Roms/Pico-8 (P8)/game.p8"
	run() { # <pak launch.sh> <args...>; synchronous: P8.pak's &> backgrounds minarch in some shells
		_l=$1
		shift
		env PATH="$T/bin:$PATH" SDCARD_PATH="$SD" ROMS_PATH="$SD/Roms" SAVES_PATH="$SD/Saves" SYSTEM_PATH="$T/sys" \
			LD_LIBRARY_PATH="/usr/trimui/lib" \
			SHARED_SYSTEM_PATH="$REPO/skeleton/SYSTEM/shared" USERDATA_PATH="$SD/.userdata/$PLAT" \
			SHARED_USERDATA_PATH="$SD/.userdata/shared" LOGS_PATH="$SD/.userdata/$PLAT/logs" \
			NX_GPU_GOVERNOR_PATH="$T/no-governor" sh "$_l" "$@"
		i=0
		while [ ! -s "$T/calls" ] && [ $i -lt 50 ]; do sleep 0.1 2>/dev/null || sleep 1; i=$((i + 1)); done
	}
	xtras() { # <install.sh|uninstall.sh>: as extras.elf runs it, output in $T/xtras
		env -i PATH="$PATH" SDCARD_PATH="$SD" PLATFORM="$PLAT" LOGS_PATH="$SD/.userdata/$PLAT/logs" \
			CATALOG_DIR="$CATALOG" XTRAS_STATE_DIR="$SD/.userdata/shared/xtras" sh "$CATALOG/$1" >"$T/xtras" 2>&1
	}
	P8="$T/sys/paks/Emus/P8.pak/launch.sh"
	SPLORE="$SD/Tools/Splore.pak/launch.sh"
	CACHES="$SD/.userdata/$PLAT/emulist_cache.txt $SD/.userdata/$PLAT/romindex_cache.txt"

	# no PICO-8 files: Xtras won't install Splore, P8.pak runs fake08
	check "$PLAT install without runtime fails" fails xtras install.sh
	check "$PLAT install without runtime says why" grep -q "^ERROR: Splore needs PICO-8" "$T/xtras"
	check "$PLAT install without runtime adds no tool" test ! -e "$SD/Tools/Splore.pak"
	run "$CATALOG/pak/launch.sh"
	check "$PLAT splore whose runtime went missing shows the message" has "Splore needs PICO-8"
	check "$PLAT splore whose runtime went missing runs no minarch" lacks "minarch"
	check "$PLAT splore whose runtime went missing runs no pico8_64" lacks "pico8_64 standalone"
	rm -f "$T/calls"
	run "$P8" "$CART"
	check "$PLAT cart without runtime runs fake08" has "P8.pak/fake08_libretro.so $CART"
	rm -f "$T/calls"

	# the user's pico8_64: records how it was started, and whether favourites were copied yet
	mkdir -p "$SD/Bios/P8"
	cat >"$SD/Bios/P8/pico8_64" <<-EOF
		#!/bin/sh
		[ -e "$SD/Roms/Pico-8 (P8)/a game.p8.png" ] && copied=yes || copied=no
		echo "pico8_64 standalone=\${P8SHIM_STANDALONE:-} preload=\${LD_PRELOAD:-} home=\${P8NATIVE_HOME:-} cdata=\${P8NATIVE_CDATA:-} libs=\${LD_LIBRARY_PATH:-} cwd=\$(pwd) copied=\$copied \$*" >>"$T/calls"
		exit 7
	EOF
	chmod +x "$SD/Bios/P8/pico8_64"
	: >"$SD/Bios/P8/pico8.dat"
	run "$P8" "$CART"
	check "$PLAT cart with runtime runs pico8native" has "minarch splore= $T/sys/paks/Emus/P8.pak/pico8native_libretro.so $CART"
	rm -f "$T/calls"

	# the old minarch Splore's marker cart is no longer special
	mkdir -p "$(dirname "$MARKER")"
	: >"$MARKER"
	run "$P8" "$MARKER"
	check "$PLAT old marker is just a cart" has "minarch splore= $T/sys/paks/Emus/P8.pak/pico8native_libretro.so $MARKER"
	check "$PLAT old marker opens no Splore" lacks "pico8_64 standalone"
	rm -f "$T/calls" "$MARKER"

	# with the files, Xtras installs Splore into Tools/ and refreshes the lists
	for c in $CACHES; do : >"$c"; done
	check "$PLAT install with runtime succeeds" xtras install.sh
	check "$PLAT install puts Splore in Tools" test -x "$SPLORE"
	for c in $CACHES; do check "$PLAT install clears $(basename "$c")" test ! -e "$c"; done

	# Splore runs pico8_64 itself, then copies favourites
	for c in $CACHES; do : >"$c"; done
	H="$SD/.userdata/shared/P8-native/home"
	run "$SPLORE"
	check "$PLAT splore runs pico8_64 once" count 1
	check "$PLAT splore runs no minarch" lacks "minarch"
	check "$PLAT splore runs pico8_64 -splore" has "copied=no -home $H -joystick 0 -desktop $SD/Screenshots -root_path $SD/Roms/Pico-8 (P8) -splore"
	check "$PLAT splore preloads the shim standalone" has "pico8_64 standalone=1 preload=$T/sys/paks/Emus/P8.pak/pico8shim.so "
	check "$PLAT splore keeps PICO-8's home and cart saves" has " home=$H cdata=$SD/Saves/P8/cdata "
	check "$PLAT splore runs on the device SDL" has " libs=/usr/trimui/lib "
	check "$PLAT splore runs from the runtime folder" has " cwd=$SD/Bios/P8 "
	check "$PLAT splore makes no marker cart" test ! -e "$MARKER"
	check "$PLAT splore logs pico8_64's exit status" grep -q "pico8_64 exited with status 7" "$SD/.userdata/$PLAT/logs/splore.txt"
	check "$PLAT splore adds the pad mapping" test "$(grep -c '^030003f05e0400008e02000014010000,X360 Controller,a:b1,b:b0,.*guide:b8,.*start:b7,' "$PADS")" -eq 1
	check "$PLAT splore copies favourites after pico8_64" test -f "$SD/Roms/Pico-8 (P8)/a game.p8.png"
	for c in $CACHES; do check "$PLAT a copy clears $(basename "$c")" test ! -e "$c"; done

	# a Splore session that copies nothing keeps the game list caches, and the mapping once
	rm -f "$T/calls"
	for c in $CACHES; do : >"$c"; done
	run "$SPLORE"
	check "$PLAT splore again runs pico8_64" count 1
	for c in $CACHES; do check "$PLAT nothing copied keeps $(basename "$c")" test -e "$c"; done
	check "$PLAT the pad mapping is added once" test "$(grep -c '^030003f05e0400008e02000014010000,' "$PADS")" -eq 1

	# a mapping file of the user's own, its last line without a newline: kept, the pad's on a line of its own
	rm -f "$T/calls"
	printf 'ffff0000000000000000000000000000,Other Pad,a:b0,platform:Linux' >"$PADS"
	run "$SPLORE"
	check "$PLAT the user's mapping is kept" grep -q '^ffff0000000000000000000000000000,Other Pad,a:b0,platform:Linux$' "$PADS"
	check "$PLAT the pad's mapping on its own line" test "$(grep -c '^030003f05e0400008e02000014010000,' "$PADS")" -eq 1

	# uninstall removes the tool and its version marker, keeps the copied carts
	mkdir -p "$SD/.userdata/shared/xtras"
	: >"$SD/.userdata/shared/xtras/splore.version"
	check "$PLAT uninstall succeeds" xtras uninstall.sh
	check "$PLAT uninstall removes the tool" test ! -e "$SD/Tools/Splore.pak"
	check "$PLAT uninstall removes the version marker" test ! -e "$SD/.userdata/shared/xtras/splore.version"
	check "$PLAT uninstall keeps the copied carts" test -f "$SD/Roms/Pico-8 (P8)/a game.p8.png"
done

echo "test_splore_launch: $PASSES passed, $FAILS failed"
[ "$FAILS" -eq 0 ]
