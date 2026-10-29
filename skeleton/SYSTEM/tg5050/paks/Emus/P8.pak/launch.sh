#!/bin/sh

# The user's own PICO-8 (pico8_64 + pico8.dat in Bios/P8 or Bios/PICO) runs carts
# through the pico8native bridge core; without it, fake08 as before.
EMU_EXE=fake08
sh "$SHARED_SYSTEM_PATH/bin/pico8_runtime.sh" >/dev/null 2>&1 && EMU_EXE=pico8native
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
HOME="$USERDATA_PATH"
cd "$HOME"
# With a shader on, the GPU runs at performance (gpu-shader-boost.sh).
. "$SHARED_SYSTEM_PATH/bin/gpu-shader-boost.sh"
nx_gpu_shader_boost
minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt"
