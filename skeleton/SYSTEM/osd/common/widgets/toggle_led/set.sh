#!/bin/sh
# Every model has several brightness nodes that must all be controlled
# together: Smart Pro/S and Brick have max_scale/_lr/_f1f2, Brick Pro adds
# _rear. Check all active zones + /tmp/leds_disabled for accurate status, and
# restore per-zone brightnesses from ledsettings when re-enabling.
LED_MAIN="/sys/class/led_anim/max_scale"
LED_LR="/sys/class/led_anim/max_scale_lr"
LED_F1F2="/sys/class/led_anim/max_scale_f1f2"
LED_REAR="/sys/class/led_anim/max_scale_rear"

if [ -e "$LED_REAR" ]; then
    LED_SETTINGS="/mnt/SDCARD/.userdata/shared/ledsettings_brickpro.txt"
elif [ "$(cut -d, -f1 /sys/class/graphics/fb0/virtual_size 2>/dev/null)" = "1024" ]; then
    LED_SETTINGS="/mnt/SDCARD/.userdata/shared/ledsettings_brick.txt"
else
    LED_SETTINGS="/mnt/SDCARD/.userdata/shared/ledsettings.txt"
fi

led_is_on() {
    [ -f /tmp/leds_disabled ] && return 1
    for node in "$LED_MAIN" "$LED_LR" "$LED_F1F2" "$LED_REAR"; do
        [ -e "$node" ] || continue
        value=$(cat "$node" 2>/dev/null)
        [ -n "$value" ] && [ "$value" != "0" ] && return 0
    done
    return 1
}

turn_all_leds_off() {
    for node in "$LED_MAIN" "$LED_LR" "$LED_F1F2" "$LED_REAR"; do
        [ -e "$node" ] && echo 0 > "$node" 2>/dev/null
    done
    for eff in f1 f2 m lr rear l r; do
        [ -e "/sys/class/led_anim/effect_$eff" ] && echo 0 > "/sys/class/led_anim/effect_$eff" 2>/dev/null
    done
    if [ -w "/sys/class/led_anim/frame_hex" ]; then
        printf "000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 " > /sys/class/led_anim/frame_hex 2>/dev/null
    fi
}

restore_leds_from_settings() {
    if [ ! -f "$LED_SETTINGS" ]; then
        for node in "$LED_MAIN" "$LED_LR" "$LED_F1F2" "$LED_REAR"; do
            [ -e "$node" ] && echo 50 > "$node" 2>/dev/null
        done
        return
    fi
    zone=""
    bright="0"
    eff="0"
    col="FFFFFF"
    any_lit=0
    apply_section() {
        [ -z "$zone" ] && return
        case "$zone" in
            m)    bnode="$LED_MAIN" ;;
            lr|l|r) bnode="$LED_LR" ;;
            f1|f2) bnode="$LED_F1F2" ;;
            rear) bnode="$LED_REAR" ;;
            *)    bnode="" ;;
        esac
        if [ -n "$bnode" ] && [ -e "$bnode" ]; then
            echo "$bright" > "$bnode" 2>/dev/null
            [ "$bright" != "0" ] && any_lit=1
        fi
        if [ -e "/sys/class/led_anim/effect_rgb_hex_$zone" ]; then
            echo "$col" > "/sys/class/led_anim/effect_rgb_hex_$zone" 2>/dev/null
        fi
        if [ -e "/sys/class/led_anim/effect_$zone" ]; then
            if [ "$bright" = "0" ]; then
                echo 0 > "/sys/class/led_anim/effect_$zone" 2>/dev/null
            else
                echo "$eff" > "/sys/class/led_anim/effect_$zone" 2>/dev/null
            fi
        fi
    }
    while IFS= read -r line; do
        case "$line" in
            \[*\])
                apply_section
                zone=""; bright="0"; eff="0"; col="FFFFFF"
                ;;
            filename=*) zone="${line#filename=}" ;;
            brightness=*) bright="${line#brightness=}" ;;
            effect=*) eff="${line#effect=}" ;;
            color1=0x*) col="${line#color1=0x}" ;;
            color1=*) col="${line#color1=}" ;;
        esac
    done < "$LED_SETTINGS"
    apply_section
    if [ "$any_lit" -eq 0 ]; then
        for node in "$LED_MAIN" "$LED_LR" "$LED_F1F2" "$LED_REAR"; do
            [ -e "$node" ] && echo 50 > "$node" 2>/dev/null
        done
    fi
}

mkdir -p /tmp/trimui_osd/toggle_led/

if [ $# -eq 0 ] ; then
    if led_is_on; then
        echo 1 > /tmp/trimui_osd/toggle_led/status
    else
        echo 0 > /tmp/trimui_osd/toggle_led/status
    fi
else
    if led_is_on; then
        touch /tmp/leds_disabled
        turn_all_leds_off
        echo 0 > /tmp/trimui_osd/toggle_led/status
    else
        rm -f /tmp/leds_disabled
        restore_leds_from_settings
        echo 1 > /tmp/trimui_osd/toggle_led/status
    fi
fi
