#!/bin/bash
# dump_minerlog.sh - pull the GS Miner's on-disk diagnostic logs off a disk image and print them.
#
# The app writes /GSMINER/SYSFILES/MINER.LOG (current session) and .../MINER.OLD (the previous
# 8 KB block) as ProDOS TXT files. This extracts both via AppleCommander, converts the ProDOS
# CR line endings to newlines so they read cleanly, and prints them (current first). Quit Ample
# / eject the disk first so the image isn't locked.
#
# Usage (run from the repo root):
#   scripts/dump_minerlog.sh                      # default image (apps.2mg in the repo root)
#   scripts/dump_minerlog.sh -s                    # also save copies under ./logs/ with a timestamp
#   scripts/dump_minerlog.sh path/to/disk.2mg      # pull from a specific image (e.g. gsminer_v0.95.2mg)
#   scripts/dump_minerlog.sh -s path/to/disk.2mg
#
# Manual equivalent (single file):
#   java -jar AppleCommander.jar -g <image>.2mg SYSFILES/MINER.LOG | tr '\r' '\n'
set -euo pipefail

JAVA=/opt/homebrew/opt/openjdk/bin/java
HERE="$(cd "$(dirname "$0")/.." && pwd)"   # repo root (this script lives in scripts/)
AC="$HERE/AppleCommander.jar"

SAVE=0
IMG=""
for a in "$@"; do
  case "$a" in
    -s) SAVE=1 ;;
    *)  IMG="$a" ;;
  esac
done
[ -z "$IMG" ] && IMG="$HERE/apps.2mg"   # default to the dev image; pass a path for a shipped disk

[ -f "$IMG" ] || { echo "image not found: $IMG"; exit 1; }
[ -f "$AC" ]  || { echo "AppleCommander.jar not found: $AC"; exit 1; }
echo "image: ${IMG##*/}"
echo

dump_one() {
  local path="$1" base="${1##*/}" txt
  echo "===================== $base ====================="
  if txt=$("$JAVA" -jar "$AC" -g "$IMG" "$path" 2>/dev/null); then
    printf '%s\n' "$txt" | tr '\r' '\n'
    if [ "$SAVE" -eq 1 ]; then
      mkdir -p "$HERE/logs"
      local out="$HERE/logs/${base}.$(date +%Y%m%d_%H%M%S).txt"
      printf '%s\n' "$txt" | tr '\r' '\n' > "$out" && echo "  (saved -> $out)"
    fi
  else
    echo "  (not present - run the miner at least once to create it)"
  fi
  echo
}

dump_one SYSFILES/MINER.LOG
dump_one SYSFILES/MINER.OLD
