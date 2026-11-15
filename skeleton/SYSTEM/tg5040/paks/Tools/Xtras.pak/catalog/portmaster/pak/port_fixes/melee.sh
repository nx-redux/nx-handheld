#!/bin/sh
# NX Redux port fix for PortMaster's Super Smash Bros. Melee (melee.zip,
# zalo/melee), sourced by ports_launch.sh (see run_port_fixes there) with the
# launcher's ROM_PATH and PORTS_DIR set.

# PowerVR Rogue GE8300 (tg5040 / Allwinner A133P):
# 1. present_worker.cpp's shared-context EGLSync waits stall pvrsrvkm 1.19 for
#    148-465 ms per frame (~2.15 fps); synchronous presentation drops present
#    cost to ~1.0 ms.
# 2. Dawn's GL_PIXEL_UNPACK_BUFFER path triggers deferred transfer stalls on
#    PowerVR (same class as libmali); DAWN_GL_TEXTURE_UPLOAD=map uploads
#    immediately from mapped memory.
# 3. PowerVR's libglslcompiler.so corrupts heap state when Aurora's background
#    pipeline worker compiles shaders concurrently with rendering; keep shader
#    compilation synchronous on the render thread (cached in dawn_cache.db).
# 4. Default render_scale=0 renders at full 1024x768 / 1280x720; scale=2
#    (512x384 on Brick/Brick Pro, ~35 drawn fps @ 60 Hz logic) or scale=4 via
#    .ports/melee/render_scale.txt (~52-55 drawn fps @ 60 Hz logic) keeps
#    full-speed 60 Hz gameplay.
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
    # PowerVR GE8300 has a 16 KiB UBO limit and trims explicit-location uniform
    # arrays, which causes resident display-list uniform-table slots 1..3 to
    # mix textures and spills USC shared registers on dynamic UBO indexing.
    # Running Dawn's reference GLES submission without uniform_table / resident_dl
    # eliminates HUD/fighter texture mixing and roughly doubles match FPS.
    export MELEE_FLIP_DIRECT_GLES="${MELEE_FLIP_DIRECT_GLES:-0}"
    export MELEE_FLIP_BATCH_DRAWS="${MELEE_FLIP_BATCH_DRAWS:-0}"
    export MELEE_FLIP_UNIFORM_TABLE="${MELEE_FLIP_UNIFORM_TABLE:-0}"
    export MELEE_FLIP_RESIDENT_DL="${MELEE_FLIP_RESIDENT_DL:-0}"
    export AURORA_GLES_VERTEX_API="${AURORA_GLES_VERTEX_API:-pointer}"
    export AURORA_GLES_INDEX_DRAW="${AURORA_GLES_INDEX_DRAW:-plain}"

    # One-time shader cache reset so pre-fix trimmed program binaries are purged.
    local cache_stamp="$gamedir/runtime/cache/.nx_meleepvr_v2"
    if [ -d "$gamedir/runtime/cache" ] && [ ! -f "$cache_stamp" ]; then
        rm -rf "$gamedir"/runtime/cache/melee-native/pipeline-* 2>/dev/null || true
        touch "$cache_stamp" 2>/dev/null || true
    fi

    # When MELEE_FLIP_PRESENT_THREAD=0, Dawn's SDL2-shim path defaults to an
    # offscreen pbuffer; libmeleepvr.so binds Dawn's swapchain directly to
    # SDL2's window EGLSurface so synchronous presentation reaches the panel.
    local shim="$SYSTEM_PATH/lib/libmeleepvr.so"
    if [ "${MELEE_FLIP_PRESENT_THREAD}" = "0" ] && [ -f "$shim" ]; then
        case ":${LD_PRELOAD:-}:" in
            *":$shim:"*) ;;
            *) export LD_PRELOAD="${shim}${LD_PRELOAD:+:$LD_PRELOAD}" ;;
        esac
    fi

    # Seed fast handheld defaults when no user settings.cfg exists yet; once
    # created, in-game changes under Options > Port Settings are preserved.
    local cfg="$gamedir/runtime/config/melee-native/settings.cfg"
    if [ -d "$gamedir" ] && [ ! -f "$cfg" ]; then
        mkdir -p "$(dirname "$cfg")" 2>/dev/null || return 0
        printf 'low_detail_fighters = 1\nfighter_shadows = 0\n' > "$cfg"
    fi
}
