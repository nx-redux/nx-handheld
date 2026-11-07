#!/bin/sh
# NX Redux port fix for PortMaster's The Binding of Isaac: Rebirth
# (tboirebirth.zip), sourced by ports_launch.sh (see run_port_fixes there)
# with the launcher's ROM_PATH set.

# The game lowercases the whole path when it reads its save files
# (persistentgamedata<n>.dat, gamestate<n>.dat) but writes them with the path
# as given. The port keeps them in $GAMEDIR/conf, under /mnt/SDCARD, so every
# read looked in /mnt/sdcard, found nothing, and each launch started with
# empty progress and no Continue (options.ini, read another way, kept
# working). Point the port's CONFDIR at an all-lowercase link in /tmp to the
# same folder. The edit lives in the port's script, so it keeps working
# between launches; a port update restores the script and this re-applies it.
tboirebirth_pre() {
    grep -q 'GAMEDIR=.*/ports/tboirebirth' "$ROM_PATH" 2>/dev/null || return 0
    grep -q '^CONFDIR="\$GAMEDIR/conf/"' "$ROM_PATH" || return 0
    sed -i 's|^CONFDIR="\$GAMEDIR/conf/"|mkdir -p "$GAMEDIR/conf"; ln -sfn "$GAMEDIR/conf" /tmp/nx_tboirebirth_conf; CONFDIR="/tmp/nx_tboirebirth_conf/"  # NX Redux: the game lowercases its save path|' "$ROM_PATH"
}
