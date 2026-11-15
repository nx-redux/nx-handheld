#!/bin/sh
# Power off. Follows the system contract (see MinUI.pak/launch.sh): remove
# /tmp/nextui_exec so the launch loop stops respawning nextui, flag
# /tmp/poweroff, then terminate the foreground app — the loop then runs
# poweroff_next. Note: pressing this in-game exits minarch without a
# quicksave (same as a hard power cut, minus filesystem risk).
STATUS_DIR=/tmp/trimui_osd/toggle_power
mkdir -p $STATUS_DIR
echo 0 > $STATUS_DIR/status

# no-arg call = status query only
[ $# -eq 0 ] && exit 0

musicplayerctl.elf shutdown >/dev/null 2>&1 || true

# close the OSD for a clean shutdown screen
touch /tmp/hide_osdd
sync
rm -f /tmp/nextui_exec
touch /tmp/poweroff
killall -CONT keymon.elf audiomon.elf 2>/dev/null || true
killall nextui.elf minarch.elf settings.elf musicplayer.elf mediaplayer.elf imageviewer.elf gametime.elf ratools.elf extras.elf cheatdb.elf sync.elf scraper.elf options.elf drastic 2>/dev/null || true
# Safety net: if the foreground app didn't unwind into MinUI.pak/launch.sh, run
# poweroff_next directly so the SD card unmounts cleanly and tg5040's AXP2202
# PMIC cuts power without entering warm limbo.
( sleep 2; /mnt/SDCARD/.system/bin/poweroff_next || poweroff ) &
