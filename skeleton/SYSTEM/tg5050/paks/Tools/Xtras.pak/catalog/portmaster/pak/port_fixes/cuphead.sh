#!/bin/sh
# NX Redux port fix for PortMaster's Cuphead (cuphead.zip), sourced by
# ports_launch.sh (see run_port_fixes there) with the launcher's ROM_PATH,
# NX_ASOUND and NX_ASOUND_DEST set.

# The same audio fault as Mina the Hollower (mina.sh): the port's audio layer
# (Unity's FMOD through its OpenSL shim to SDL) keeps only a short queue
# ready, while the firmware's speaker dmix (PlaybackDmix) fixes every client's
# period at its own (tg5040: 2048, tg5050: 1024), so each big callback drained
# the queue and the sound glitched throughout. For the port session, route the
# default PCM through a dmix of its own with 512-frame periods, keeping the
# Game Volume control. Only on the speaker, and only while nothing else holds
# it open (background music): a second dmix can't open a busy device, so then
# the shared one stays.
cuphead_pre() {
    grep -q 'GAMEDIR=.*/ports/cuphead"' "$ROM_PATH" 2>/dev/null || return 0
    [ -e /proc/asound/audiocodec ] || return 0
    grep -q -E 'nx_bt_base|nx_usb_dmix' "$HOME/.asoundrc" 2>/dev/null && return 0
    grep -q '^closed' /proc/asound/audiocodec/pcm0p/sub0/status 2>/dev/null || return 0
    # The session copy may already be bound over /etc/asound.conf: build the
    # new one beside it, then rebind.
    { cat "$NX_ASOUND_DEST" && cat <<'CONF'
# ---- NX Redux: Cuphead 512-frame speaker dmix ----
pcm.nx_cup_dmix { type plug; slave.pcm { type dmix; ipc_key 0x4e5843; ipc_perm 0666; slave { pcm "hw:audiocodec,0"; format S16_LE; rate 48000; period_size 512; buffer_size 4096; } } }
pcm.nx_cup_game { type softvol; slave.pcm "nx_cup_dmix"; control { name "Game Volume"; card audiocodec; } }
pcm.!default { type asym; playback.pcm "nx_cup_game"; capture.pcm "Capture"; }
CONF
    } > "$NX_ASOUND.cup" || { rm -f "$NX_ASOUND.cup"; return 0; }
    umount "$NX_ASOUND_DEST" 2>/dev/null
    mv -f "$NX_ASOUND.cup" "$NX_ASOUND" && mount --bind "$NX_ASOUND" "$NX_ASOUND_DEST" || return 0
    # ALSA reads ~/.asoundrc after /etc/asound.conf, and the launcher's
    # session copy there (Cuphead keeps HOME) sets pcm.!default back to the
    # shared dmix: repeat the default in it. It's re-copied at every launch
    # and removed at exit.
    [ -f "$HOME/.asoundrc" ] &&
        echo 'pcm.!default { type asym; playback.pcm "nx_cup_game"; capture.pcm "Capture"; }' >> "$HOME/.asoundrc"
}
