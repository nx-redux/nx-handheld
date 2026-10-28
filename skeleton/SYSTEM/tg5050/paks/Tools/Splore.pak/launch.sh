#!/bin/sh
# Splore: PICO-8's cart browser, in minarch through the pico8native core
# (P8NATIVE_SPLORE=1). Needs the user's own pico8_64 and pico8.dat. On the way
# out, the carts favourited in Splore are copied into the PICO-8 roms folder.

P8_PAK="$SYSTEM_PATH/paks/Emus/P8.pak"
LOG="$LOGS_PATH/splore.txt"

if ! sh "$SHARED_SYSTEM_PATH/bin/pico8_runtime.sh" >/dev/null 2>&1; then
    echo "no pico8_64 + pico8.dat in Bios/P8 or Bios/PICO" >>"$LOG"
    show2.elf --mode=progress --image=/dev/null --progress=-1 --texty=45 --progressy=55 --fontsize=28 \
        --text="Splore needs PICO-8. Put pico8_64 and pico8.dat in Bios/P8." --timeout=4
    exit 0
fi

# The roms folder is the one tagged (P8), whatever the user named it
ROMS=""
for d in "$ROMS_PATH"/*"(P8)"; do
    [ -d "$d" ] && { ROMS="$d"; break; }
done
[ -n "$ROMS" ] || ROMS="$ROMS_PATH/Pico-8 (P8)"

# minarch needs a game: an empty marker cart in a hidden folder, so the game
# list never shows it, under the (P8) folder, so the tag (config, saves) is P8
MARKER="$ROMS/.splore/Splore.p8"
mkdir -p "$ROMS/.splore"
[ -f "$MARKER" ] || : >"$MARKER"

HOME="$USERDATA_PATH"
cd "$HOME" || exit 1
# With a shader on, the GPU runs at performance (gpu-shader-boost.sh), read
# from the P8 native config that minarch uses here
EMU_TAG=P8
EMU_EXE=pico8native
ROM="$MARKER"
. "$SHARED_SYSTEM_PATH/bin/gpu-shader-boost.sh"
nx_gpu_shader_boost
P8NATIVE_SPLORE=1 minarch.elf "$P8_PAK/pico8native_libretro.so" "$MARKER" >"$LOG" 2>&1

# PICO-8's home, as the core sets it (core.c: $SHARED_USERDATA_PATH/P8-native/home).
# The script prints how many carts it copied or updated.
COPIED=$(sh "$SHARED_SYSTEM_PATH/bin/pico8_copy_favourites.sh" "$SHARED_USERDATA_PATH/P8-native/home" "$ROMS")

# new carts: the game list rebuilds instead of using its caches
if [ "${COPIED:-0}" -gt 0 ] 2>/dev/null; then
    rm -f "$USERDATA_PATH/emulist_cache.txt" "$USERDATA_PATH/romindex_cache.txt"
fi
