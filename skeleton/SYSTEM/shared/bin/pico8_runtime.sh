#!/bin/sh
# Prints the folder holding the user's PICO-8 runtime (pico8_64 and pico8.dat
# together) and exits 0, or exits 1 when there is none. Same order as
# PICO8_findRuntime (workspace/all/common/utils.c): Bios/P8, then Bios/PICO.
for d in "${SDCARD_PATH:-/mnt/SDCARD}/Bios/P8" "${SDCARD_PATH:-/mnt/SDCARD}/Bios/PICO"; do
	if [ -e "$d/pico8_64" ] && [ -e "$d/pico8.dat" ]; then
		echo "$d"
		exit 0
	fi
done
exit 1
