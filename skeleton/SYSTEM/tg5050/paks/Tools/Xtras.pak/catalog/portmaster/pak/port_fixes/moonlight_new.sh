#!/bin/sh
# NX Redux port fix for PortMaster's Moonlight New (moonlightnew.zip), sourced
# by ports_launch.sh (see run_port_fixes there) with the launcher's ROM_PATH
# and PORTS_DIR set.

# Its Love2D launcher runs moonlight (pair, list) with LD_LIBRARY_PATH replaced
# by "moonlight/libs:/usr/lib/compat", where muOS and Knulli keep the ffmpeg 58
# it links against. Here that ffmpeg is PortMaster's lib/, on the inherited
# path the launcher dropped, so pairing failed with "libavcodec.so.58: cannot
# open shared object file". Keep the inherited path after theirs (the line goes
# through sh, which expands it). Re-applied each launch: an update restores it.
moonlight_new_pre() {
    local lua="$PORTS_DIR/moonlightnew/gui/main.lua"
    [ -f "$lua" ] || return 0
    grep -q 'moonlight/libs:/usr/lib/compat %s' "$lua" || return 0
    sed -i 's|moonlight/libs:/usr/lib/compat %s|moonlight/libs:/usr/lib/compat:$LD_LIBRARY_PATH %s|' "$lua"
}
