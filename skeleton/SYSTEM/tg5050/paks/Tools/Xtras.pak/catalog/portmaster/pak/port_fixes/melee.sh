#!/bin/sh
# NX Redux port fix for PortMaster's Super Smash Bros. Melee (melee.zip,
# zalo/melee), sourced by ports_launch.sh (see run_port_fixes there) with the
# launcher's ROM_PATH and PORTS_DIR set.

melee_pre() {
    grep -q 'PORTMASTER: melee\.zip\|GAMEDIR=.*/ports/melee"' "$ROM_PATH" 2>/dev/null || return 0
    [ -d /sys/module/pvrsrvkm ] || return 0

    local gamedir="$PORTS_DIR/melee"
    local scale="2"
    if [ -f "$gamedir/render_scale.txt" ]; then
        read -r scale < "$gamedir/render_scale.txt" 2>/dev/null || scale="2"
    fi

    export MELEE_FLIP_PRESENT_THREAD="${MELEE_FLIP_PRESENT_THREAD:-0}"
    export DAWN_GL_TEXTURE_UPLOAD="${DAWN_GL_TEXTURE_UPLOAD:-map}"
    export MELEE_FLIP_RENDER_SCALE="${MELEE_FLIP_RENDER_SCALE:-$scale}"
    export MELEE_FLIP_DIRECT_GLES="${MELEE_FLIP_DIRECT_GLES:-0}"
    export MELEE_FLIP_BATCH_DRAWS="${MELEE_FLIP_BATCH_DRAWS:-0}"
    export MELEE_FLIP_UNIFORM_TABLE="${MELEE_FLIP_UNIFORM_TABLE:-0}"
    export MELEE_FLIP_RESIDENT_DL="${MELEE_FLIP_RESIDENT_DL:-0}"
    export AURORA_GLES_VERTEX_API="${AURORA_GLES_VERTEX_API:-pointer}"
    export AURORA_GLES_INDEX_DRAW="${AURORA_GLES_INDEX_DRAW:-plain}"

    local cache_stamp="$gamedir/runtime/cache/.nx_meleepvr_v2"
    if [ -d "$gamedir/runtime/cache" ] && [ ! -f "$cache_stamp" ]; then
        rm -rf "$gamedir"/runtime/cache/melee-native/pipeline-* 2>/dev/null || true
        touch "$cache_stamp" 2>/dev/null || true
    fi

    local shim="$SYSTEM_PATH/lib/libmeleepvr.so"
    if [ "${MELEE_FLIP_PRESENT_THREAD}" = "0" ] && [ -f "$shim" ]; then
        case ":${LD_PRELOAD:-}:" in
            *":$shim:"*) ;;
            *) export LD_PRELOAD="${shim}${LD_PRELOAD:+:$LD_PRELOAD}" ;;
        esac
    fi

    local cfg="$gamedir/runtime/config/melee-native/settings.cfg"
    if [ -d "$gamedir" ] && [ ! -f "$cfg" ]; then
        mkdir -p "$(dirname "$cfg")" 2>/dev/null || return 0
        printf 'low_detail_fighters = 1\nfighter_shadows = 0\n' > "$cfg"
    fi
}
