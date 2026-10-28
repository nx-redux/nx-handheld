#!/bin/sh
# NX Redux port fix for PortMaster's Mina the Hollower (mina.zip), sourced by
# ports_launch.sh (see run_port_fixes there) with the launcher's ROM_PATH,
# NX_ASOUND and NX_ASOUND_DEST set.

# The game opens SDL audio asking for 512-sample callbacks and allows SDL to
# change the size, but never reads back the size it got: its mixer keeps about
# 512 samples ready. The firmware's speaker dmix (PlaybackDmix) fixes every
# client's period at its own (tg5040: 2048, tg5050: 1024) and ignores
# var_periodsize (the Brick's alsa-lib 1.1.8), so SDL asked for that much per
# callback and the shortfall played as constant crackle. For the port session, route the
# default PCM through a dmix of its own with 512-frame periods, keeping the
# Game Volume control. Only on the speaker, and only while nothing else holds
# it open (background music): a second dmix can't open a busy device, so then
# the shared one stays.
mina_pre() {
    grep -q 'MinaTheHollower' "$ROM_PATH" 2>/dev/null || return 0
    [ -e /proc/asound/audiocodec ] || return 0
    grep -q -E 'nx_bt_base|nx_usb_dmix' "$HOME/.asoundrc" 2>/dev/null && return 0
    grep -q '^closed' /proc/asound/audiocodec/pcm0p/sub0/status 2>/dev/null || return 0
    # The session copy may already be bound over /etc/asound.conf: build the
    # new one beside it, then rebind.
    { cat "$NX_ASOUND_DEST" && cat <<'EOF'
# ---- NX Redux: Mina the Hollower 512-frame speaker dmix ----
pcm.nx_mina_dmix { type plug; slave.pcm { type dmix; ipc_key 0x4e584d; ipc_perm 0666; slave { pcm "hw:audiocodec,0"; format S16_LE; rate 48000; period_size 512; buffer_size 4096; } } }
pcm.nx_mina_game { type softvol; slave.pcm "nx_mina_dmix"; control { name "Game Volume"; card audiocodec; } }
pcm.!default { type asym; playback.pcm "nx_mina_game"; capture.pcm "Capture"; }
EOF
    } > "$NX_ASOUND.mina" || { rm -f "$NX_ASOUND.mina"; return 0; }
    umount "$NX_ASOUND_DEST" 2>/dev/null
    mv -f "$NX_ASOUND.mina" "$NX_ASOUND" && mount --bind "$NX_ASOUND" "$NX_ASOUND_DEST"
}
