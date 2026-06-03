#!/bin/bash
# inject_gsminer.sh - inject GSMINER with rev in the ProDOS name (8 chars: GSMINE04).
#
# Keeps TWO versions on the floppy: current rev + previous rev.
# Purges rev-2 and legacy names (GSMINER2, VIZ, …).
#
# Usage (run from the repo root):
#   scripts/inject_gsminer.sh           # rev from #define APPVER "V0.N" in miner/viz.c
#   scripts/inject_gsminer.sh 04        # explicit rev
#   scripts/inject_gsminer.sh -f 04     # allow while Ample runs (eject disk first!)
#
# Tip: EJECT the floppy in Ample before injecting, then RE-INSERT after.
set -euo pipefail

FORCE=0
if [ "${1:-}" = "-f" ]; then FORCE=1; shift; fi

JAVA=/opt/homebrew/opt/openjdk/bin/java
HERE="$(cd "$(dirname "$0")/.." && pwd)"   # repo root (this script lives in scripts/)
AC="$HERE/AppleCommander.jar"
IMG="$HERE/apps.2mg"
SRC="$HERE/miner/viz"

if pgrep -f mame64 >/dev/null 2>&1 && [ "$FORCE" -ne 1 ]; then
  echo "REFUSING: mame64 running. Quit Ample or eject disk and use -f."
  exit 1
fi

REV="${1:-}"
if [ -z "$REV" ]; then
  MINOR=$(grep '#define APPVER' "$SRC.c" | sed -n 's/.*"V0\.\([0-9]*\)".*/\1/p')
  REV=$(printf "%02d" "${MINOR:-0}")
fi
REV=$(printf "%02d" "$((10#$REV))")

APP="GSMINE${REV}"
PREV=$(printf "%02d" $((10#$REV - 1)))
KEEP="GSMINE${PREV}"
# No pinned checkpoints: GSMINE69 (last OLD-toolchain mock-ACCEPT A/B reference) has been
# retired now that real-pool mining is proven end-to-end (M6 + Stratum + midstate, with the
# large-model SHA fix in 65816-crypto/sha256.asm). All historical baselines live in baseline/.
PINS=""

[ -f "$SRC" ] || { echo "build product not found: $SRC"; exit 1; }
[ -f "$IMG" ] || { echo "floppy not found: $IMG"; exit 1; }

chmod u+w "$IMG" 2>/dev/null || true

echo "=== GSMINER inject rev ${REV} -> ${APP} ==="
echo "keep previous: ${KEEP} (if present)"
echo "purge:         all other GSMINE## + legacy names"

for legacy in VIZ GSMINER2; do
  "$JAVA" -jar "$AC" -d "$IMG" "$legacy" 2>/dev/null || true
done

# dynamically purge every GSMINE## on disk except the previous rev (KEEP);
# the current rev is always dropped so it can be re-injected cleanly.
for name in $("$JAVA" -jar "$AC" -l "$IMG" | grep -oE 'GSMINE[0-9]{2}' | sort -u); do
  [ "$name" = "$KEEP" ] && continue
  case " $PINS " in *" $name "*) continue;; esac   # keep pinned checkpoints
  "$JAVA" -jar "$AC" -d "$IMG" "$name" 2>/dev/null || true
done

echo "injecting ${APP} from ${SRC} ..."
cat "$SRC" | "$JAVA" -jar "$AC" -p "$IMG" "$APP" S16 '$0000'

# Data files live in <appdir>/SYSFILES/ so the disk root shows just the app +
# the SYSFILES folder + the (root-mandated) Icons folder. The app finds them at
# runtime relative to its own launch dir (GS/OS prefix 1 -> miner_sysfile() in
# miner/paths.c), falling back to /GSMINER/SYSFILES/..., so the parts also work
# copied into any folder. AppleCommander auto-creates SYSFILES. Purge any legacy
# root PANEL/CONFIG from older builds.
for legacy in PANEL CONFIG; do
  "$JAVA" -jar "$AC" -d "$IMG" "$legacy" 2>/dev/null || true
done

# refresh PANEL plate when frame.shr exists
if [ -f "$HERE/viz/frame.shr" ]; then
  "$JAVA" -jar "$AC" -d "$IMG" SYSFILES/PANEL 2>/dev/null || true
  cat "$HERE/viz/frame.shr" | "$JAVA" -jar "$AC" -p "$IMG" SYSFILES/PANEL BIN '$0000'
  echo "refreshed SYSFILES/PANEL (frame.shr)"
fi

# refresh CONFIG plate when config.shr exists
if [ -f "$HERE/viz/config.shr" ]; then
  "$JAVA" -jar "$AC" -d "$IMG" SYSFILES/CONFIG 2>/dev/null || true
  cat "$HERE/viz/config.shr" | "$JAVA" -jar "$AC" -p "$IMG" SYSFILES/CONFIG BIN '$0000'
  echo "refreshed SYSFILES/CONFIG (config.shr)"
fi

# refresh the Finder icon ($CA) when viz/gsminer.icons exists. The Finder scans an
# "Icons" folder on every mounted volume; AppleCommander auto-creates it. Bind is by
# filename GSMINE* + filetype S16 (see viz/build_icon.py). Reboot the GS / rebuild the
# desktop once for the coin to appear.
if [ -f "$HERE/viz/gsminer.icons" ]; then
  "$JAVA" -jar "$AC" -d "$IMG" Icons/GSMINER.ICONS 2>/dev/null || true
  cat "$HERE/viz/gsminer.icons" | "$JAVA" -jar "$AC" -p "$IMG" Icons/GSMINER.ICONS ICN
  echo "refreshed Icons/GSMINER.ICONS (gsminer.icons)"
fi

echo "--- apps.2mg now contains (GSMIN* / PANEL): ---"
"$JAVA" -jar "$AC" -l "$IMG" | grep -iE 'GSMIN|PANEL' || true
echo
echo "OK: run ${APP} on the GS. Re-insert the floppy if Ample was open."
