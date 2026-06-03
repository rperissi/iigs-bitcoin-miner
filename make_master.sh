#!/bin/bash
# make_master.sh - build a pristine, shippable GSMINER disk (apps_master.2mg).
#
# Clean first-open layout (only these are visible in the Finder):
#   /GSMINER/GSMINE<rev>      the application (S16)
#   /GSMINER/SYSFILES/        PANEL, CONFIG  (+ MINER.CONF/LOG/OLD created at run)
#   /GSMINER/Icons/           GSMINER.ICONS  (must live at the volume root)
#
# We clone a known-good ProDOS-order image (apps_dev_backup.2mg) so the disk geometry
# stays MAME-friendly, wipe the visible dev clutter, then lay down the clean structure.
# The app finds its data RELATIVE to where it was launched (GS/OS prefix 1 -> miner_sysfile()
# in miner/paths.c), with a fallback to /GSMINER/SYSFILES/..., so the three parts work
# from any folder/volume - not just a volume literally named GSMINER.
#
# The clone is a *headerless* ProDOS-order image (good for AppleCommander + emulators),
# so as a final step we wrap a copy in a real 2IMG header (wrap_2mg.py) -> the shippable
# gsminer_v0<rev>.2mg, which also mounts on real CFFA3000 hardware (a bare .2mg without
# the 2IMG header is rejected as "invalid image").
#
# Usage:  ./make_master.sh            # rev from #define APPVER in miner/viz.c
#         ./make_master.sh 89         # explicit rev
# Then (with Ample's apps.2mg ejected):  cp apps_master.2mg apps.2mg
# Ship:   gsminer_v0<rev>.2mg  (CFFA-ready; hand this to users / attach to the release)
set -euo pipefail

JAVA=/opt/homebrew/opt/openjdk/bin/java
HERE="$(cd "$(dirname "$0")" && pwd)"
AC="$HERE/AppleCommander.jar"
SRC="$HERE/miner/viz"
ICON="$HERE/viz/gsminer.icons"
PANEL_SRC="$HERE/viz/frame.shr"
CONFIG_SRC="$HERE/viz/config.shr"
TEMPLATE="$HERE/apps_dev_backup.2mg"
[ -f "$TEMPLATE" ] || TEMPLATE="$HERE/apps.2mg"
OUT="$HERE/apps_master.2mg"
VOLNAME="GSMINER"        # shipped ProDOS volume name (the app finds its data app-relative
                         # via prefix 1, and also falls back to /GSMINER/SYSFILES - see paths.c)

[ -f "$SRC" ]  || { echo "build product missing: $SRC (run the occ build first)"; exit 1; }
[ -f "$ICON" ] || { echo "icon missing: $ICON (run viz/build_icon.py)"; exit 1; }

REV="${1:-}"
if [ -z "$REV" ]; then
  REV=$(grep '#define APPVER' "$SRC.c" | sed -n 's/.*"V0\.\([0-9]*\)".*/\1/p')
fi
REV=$(printf "%02d" "$((10#${REV:-0}))")
APP="GSMINE${REV}"

echo "=== building clean master ${OUT} (app ${APP}) from ${TEMPLATE##*/} ==="
cp "$TEMPLATE" "$OUT"; chmod u+w "$OUT"

# wipe visible dev clutter + every old rev + stale icon, AND the inherited Finder
# layout files so the shipped disk opens with a fresh auto-arranged grid (the old
# FINDER.DATA held positions for the dev disk's files, which scattered our items).
for name in $("$JAVA" -jar "$AC" -l "$OUT" | grep -oE 'GSMINE[0-9]{2}' | sort -u); do
  "$JAVA" -jar "$AC" -d "$OUT" "$name" 2>/dev/null || true
done
for f in MINER.LOG MINER.OLD MINER.CONF PANEL CONFIG VIZ GSMINER2 GSMINER \
         SYSFILES/PANEL SYSFILES/CONFIG SYSFILES/MINER.CONF SYSFILES/MINER.LOG \
         SYSFILES/MINER.OLD Icons/GSMINER.ICONS Icons/FINDER.DATA \
         FINDER.DATA FINDER.ROOT; do
  "$JAVA" -jar "$AC" -d "$OUT" "$f" 2>/dev/null || true
done

echo "--- laying down clean structure ---"
cat "$SRC"        | "$JAVA" -jar "$AC" -p "$OUT" "$APP" S16 '$0000'
cat "$PANEL_SRC"  | "$JAVA" -jar "$AC" -p "$OUT" SYSFILES/PANEL  BIN '$0000'
cat "$CONFIG_SRC" | "$JAVA" -jar "$AC" -p "$OUT" SYSFILES/CONFIG BIN '$0000'
cat "$ICON"       | "$JAVA" -jar "$AC" -p "$OUT" Icons/GSMINER.ICONS ICN

# Rename the ProDOS volume to /GSMINER/ for a finished look. Done
# AFTER all add/delete ops, which address files relative to the volume root. The app reads
# its data app-relative (prefix 1) with a /GSMINER/SYSFILES fallback, so the name is cosmetic.
echo "--- renaming volume -> ${VOLNAME} ---"
"$JAVA" -jar "$AC" -n "$OUT" "$VOLNAME"

echo "--- ${OUT##*/} contents ---"
"$JAVA" -jar "$AC" -l "$OUT"

# Wrap a copy in a real 2IMG header for distribution / real CFFA hardware. The raw
# apps_master.2mg stays headerless for the AppleCommander + Ample dev loop.
SHIP="$HERE/gsminer_v0.$((10#$REV)).2mg"      # e.g. rev 94 -> gsminer_v0.94.2mg
if [ -f "$HERE/wrap_2mg.py" ]; then
  echo "--- wrapping shippable 2IMG (CFFA-ready) ---"
  python3 "$HERE/wrap_2mg.py" "$OUT" "$SHIP"
fi
echo
echo "OK. Dev:  with Ample's floppy ejected,  cp '$OUT' '$HERE/apps.2mg'  then reboot."
echo "Ship: ${SHIP##*/}  (has the 2IMG header; mounts on CFFA3000 + emulators)."
