#!/bin/sh
# Xtras catalog: Splore (CODE side). Copies the launchable pak payload into
# Tools/ and busts the launcher list caches so it appears without reboot.
# Refuses without the user's own PICO-8 (pico8_64 + pico8.dat), looked for
# where pico8_runtime.sh looks: Bios/P8, then Bios/PICO. It does NOT write the
# version marker: this is an internal entry, so extras.elf writes
# $XTRAS_STATE_DIR/splore.version after a successful install.
# Runs under extras.elf's scrubbed env (SDCARD_PATH/PLATFORM/LOGS_PATH/
# CATALOG_DIR/XTRAS_STATE_DIR). No network.
set -u

TOOLS_PAK="$SDCARD_PATH/Tools/Splore.pak"
USERDATA_DIR="$SDCARD_PATH/.userdata/$PLATFORM"

fail() { echo "ERROR: $1"; exit 1; }

[ -f "$CATALOG_DIR/pak/launch.sh" ] || fail "catalog pak payload missing (launch.sh)"

echo "@20 Looking for PICO-8..."
found=""
for d in "$SDCARD_PATH/Bios/P8" "$SDCARD_PATH/Bios/PICO"; do
    [ -e "$d/pico8_64" ] && [ -e "$d/pico8.dat" ] && { found=1; break; }
done
[ -n "$found" ] || fail "Splore needs PICO-8: put pico8_64 and pico8.dat in Bios/P8 first"

echo "@40 Installing Splore..."
mkdir -p "$TOOLS_PAK" || fail "cannot create the Splore pak"
cp -f "$CATALOG_DIR/pak/launch.sh" "$TOOLS_PAK/launch.sh" || fail "could not copy the pak launcher"
chmod +x "$TOOLS_PAK/launch.sh" 2>/dev/null || true

echo "@90 Refreshing tools list..."
rm -f "$USERDATA_DIR/emulist_cache.txt" "$USERDATA_DIR/romindex_cache.txt"

echo "@100 Done"
echo "Installed. Open Splore in Tools."
exit 0
