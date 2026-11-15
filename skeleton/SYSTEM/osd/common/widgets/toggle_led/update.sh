#!/bin/sh
mkdir -p /tmp/trimui_osd/toggle_led/
if [ -f /tmp/leds_disabled ]; then
    echo 0 > /tmp/trimui_osd/toggle_led/status
    exit 0
fi
for node in /sys/class/led_anim/max_scale /sys/class/led_anim/max_scale_lr /sys/class/led_anim/max_scale_f1f2 /sys/class/led_anim/max_scale_rear; do
    [ -e "$node" ] || continue
    value=$(cat "$node" 2>/dev/null)
    if [ -n "$value" ] && [ "$value" != "0" ]; then
        echo 1 > /tmp/trimui_osd/toggle_led/status
        exit 0
    fi
done
echo 0 > /tmp/trimui_osd/toggle_led/status
