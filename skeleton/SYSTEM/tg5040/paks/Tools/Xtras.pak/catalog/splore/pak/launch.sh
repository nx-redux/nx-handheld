#!/bin/sh
# Splore: PICO-8's cart browser. pico8_64 -splore runs straight on the device's
# SDL (no minarch), with P8.pak's pico8shim.so preloaded in standalone mode
# (P8SHIM_STANDALONE=1): it lets SDL start without the sensor support the device
# SDL lacks, keeps cart saves in Saves/P8/cdata with the games in the roms folder,
# and makes Menu ask "Quit Splore?" (A quits, B goes back), as PICO-8 has no quit of
# its own in Splore. Installed into Tools/ from Xtras (catalog/splore), which checks
# for the user's own pico8_64 and pico8.dat; they can still go missing later, so
# this checks again. On the way out, the carts favourited in Splore are copied into
# the PICO-8 roms folder.

P8_PAK="$SYSTEM_PATH/paks/Emus/P8.pak"
LOG="$LOGS_PATH/splore.txt"

if ! RUNTIME=$(sh "$SHARED_SYSTEM_PATH/bin/pico8_runtime.sh" 2>/dev/null); then
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

# PICO-8's home, the one the pico8native core gives the games in the roms folder
P8_HOME="$SHARED_USERDATA_PATH/P8-native/home"
mkdir -p "$P8_HOME"

# The TrimUI pad as SDL sees it: A, B, Start b7 and Menu reported as guide b8
# (mapping from minui-pico-8-pak, MIT, LICENSE-minui-pico-8-pak.txt in P8.pak).
# PICO-8's O is SDL's A and its X SDL's B: the pad's B and A, as carts in the roms
# folder have them through minarch. The Xbox button layout swaps A/B and X/Y there,
# so it does here too. Rewritten each time (the layout can change); PICO-8 reads
# sdl_controllers.txt from its home.
. "$SYSTEM_PATH/bin/nx_button_layout.sh"
FACES='a:b1,b:b0,x:b3,y:b2'
CAPS=0
if [ "$NX_BUTTON_LAYOUT" = xbox ]; then
    FACES='a:b0,b:b1,x:b2,y:b3'
    # Hint labels = printed caps: the quit dialog names the cap you press
    nextval.elf hintlabels 2>/dev/null | grep -q '"hintlabels": 1' && CAPS=1
fi
GUID=030003f05e0400008e02000014010000
MAP="$GUID,X360 Controller,$FACES,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b8,leftshoulder:b4,leftstick:b9,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b10,righttrigger:a5,rightx:a3,righty:a4,start:b7,platform:Linux"
PADS="$P8_HOME/sdl_controllers.txt"
{
    grep -v "^$GUID," "$PADS" 2>/dev/null
    echo "$MAP"
} >"$PADS.new" && mv -f "$PADS.new" "$PADS"

echo "splore: pico8_64 -splore from $RUNTIME, roms in $ROMS" >"$LOG"
cd "$RUNTIME" || exit 1
P8SHIM_STANDALONE=1 P8SHIM_CAP_LABELS=$CAPS P8NATIVE_HOME="$P8_HOME" P8NATIVE_CDATA="$SAVES_PATH/P8/cdata" LD_PRELOAD="$P8_PAK/pico8shim.so" \
    "$RUNTIME/pico8_64" -home "$P8_HOME" -joystick 0 -desktop "$SDCARD_PATH/Screenshots" -root_path "$ROMS" -splore \
    >>"$LOG" 2>&1
echo "splore: pico8_64 exited with status $?" >>"$LOG"

# The script prints how many carts it copied or updated
COPIED=$(sh "$SHARED_SYSTEM_PATH/bin/pico8_copy_favourites.sh" "$P8_HOME" "$ROMS")

# new carts: the game list rebuilds instead of using its caches
if [ "${COPIED:-0}" -gt 0 ] 2>/dev/null; then
    rm -f "$USERDATA_PATH/emulist_cache.txt" "$USERDATA_PATH/romindex_cache.txt"
fi
