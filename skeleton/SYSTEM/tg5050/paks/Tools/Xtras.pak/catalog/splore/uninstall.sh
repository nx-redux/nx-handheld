#!/bin/sh
# Xtras catalog: Splore uninstall. Removes the tool and its
# $XTRAS_STATE_DIR/splore.version marker. Keeps PICO-8 itself (Bios/P8), its
# home (Splore's cart cache, favourites, settings) and the favourites already
# copied into the roms folder. Self-contained; no network.
set -u

: "${XTRAS_STATE_DIR:=$SDCARD_PATH/.userdata/shared/xtras}"
TOOLS_PAK="$SDCARD_PATH/Tools/Splore.pak"
USERDATA_DIR="$SDCARD_PATH/.userdata/$PLATFORM"

echo "@50 Removing the tool..."
rm -rf "$TOOLS_PAK"
rm -f "$XTRAS_STATE_DIR/splore.version"
rm -f "$USERDATA_DIR/emulist_cache.txt" "$USERDATA_DIR/romindex_cache.txt"

echo "@100 Done"
echo "Removed. Your PICO-8 carts are kept."
exit 0
