#!/bin/sh
# Master motor switch. The state lives in libmsettings shared memory (persisted
# to msettings.bin) via osdctl, so every app's vibration thread and the
# shutdown tap read the same flag.
export LD_LIBRARY_PATH="/mnt/SDCARD/.system/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
OSDCTL="/mnt/SDCARD/.system/bin/osdctl"
RUMBLE_GPIO="/sys/class/gpio/gpio227/value"
MOTOR_VOLT="/sys/class/motor/voltage"

mkdir -p /tmp/trimui_osd/toggle_rumble/

if [ $# -eq 0 ] ; then
    value=$($OSDCTL get rumble)
    echo $value > /tmp/trimui_osd/toggle_rumble/status
else
    value=$($OSDCTL get rumble)
    if [ "$value" -eq 1 ] ; then
        $OSDCTL set rumble 0
        echo 0 > /tmp/trimui_osd/toggle_rumble/status
    else
        $OSDCTL set rumble 1
        # Set model-appropriate motor voltage before pulsing (Brick Pro 1.7 V, Brick 3.3 V)
        if [ -e "/sys/class/led_anim/max_scale_rear" ]; then
            echo 1700000 > "$MOTOR_VOLT" 2>/dev/null
        elif [ "$(cut -d, -f1 /sys/class/graphics/fb0/virtual_size 2>/dev/null)" = "1024" ]; then
            echo 3300000 > "$MOTOR_VOLT" 2>/dev/null
        else
            echo 1500000 > "$MOTOR_VOLT" 2>/dev/null
        fi
        echo 1 > $RUMBLE_GPIO 2>/dev/null
        sleep 0.1
        echo 0 > $RUMBLE_GPIO 2>/dev/null
        echo 1 > /tmp/trimui_osd/toggle_rumble/status
    fi
fi
