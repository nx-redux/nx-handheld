#!/bin/sh

# Splore's marker cart (Tools/Splore.pak, installed from Xtras) comes back here when a Splore session is
# auto-resumed after sleep and power-off: reopen Splore, which runs minarch itself. Splore removed since: nothing to run.
case "$1" in
*/.splore/Splore.p8)
	SPLORE="$SDCARD_PATH/Tools/Splore.pak/launch.sh"
	[ -f "$SPLORE" ] && exec sh "$SPLORE"
	exit 0
	;;
esac

# The user's own PICO-8 (pico8_64 + pico8.dat in Bios/P8 or Bios/PICO) runs carts
# through the pico8native bridge core; without it, fake08 as before.
EMU_EXE=fake08
sh "$SHARED_SYSTEM_PATH/bin/pico8_runtime.sh" >/dev/null 2>&1 && EMU_EXE=pico8native
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
