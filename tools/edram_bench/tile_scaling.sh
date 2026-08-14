#!/usr/bin/env bash
# Does in-pass cost scale with TILE COUNT x DRAW COUNT?
#
# THE HYPOTHESIS, from the Adreno guide (mobile_best_practices.txt):
#   L379  "If a triangle spans multiple tiles in binning mode, the full triangle
#          will be rasterized per tile."
#   L1201 per-tile visibility-stream overhead "can add up if the draw call count
#          is high and draws are present in many tiles."
#
# renderArea sets the tile grid extent. At the tile size derived for a7xx
# (320x544), a 1280x2048 attachment is 16 tiles and 1280x720 is 8 - exactly 2x,
# and the in-app renderArea clamp measured exactly 2x (46.9 -> 23.5 ms in-pass).
#
# THE ARMS HOLD FRAGMENT WORK CONSTANT: the viewport is 1280x720 in every arm,
# so every draw shades the same pixels. ONLY the attachment height changes, and
# with it the tile count. If cost per draw grows with attachment height, the
# extra cost is PER-TILE, not per-fragment - which is the thing neither the
# attachment-size nor the clear-rect measurement could explain.
#
# loadOp=dontcare throughout, so this cannot be confounded by load/store or by
# clears - both already measured free/scissored.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
TURNIP=/data/local/tmp/libvulkan_freedreno.so
PKG=jp.xenia.emulator.github.debug

# Guards live in the script and must EXIT, never echo - this file records a
# thermal guard that printed for 300s without ever evaluating.
busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
[ "${busy:-1}" = "0" ] || { echo "ABORT: rpcs3 running - device is SHARED"; exit 1; }
# QUOTE the remote command or Git Bash rewrites the sysfs path and the read
# silently returns nothing.
T=$("$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" | tr -d '\r')
case "$T" in ''|*[!0-9]*) echo "ABORT: bad temp read '$T'"; exit 1;; esac
[ "$T" -lt 55000 ] || { echo "ABORT: gpu $((T/1000))C too hot"; exit 1; }
echo "pre-flight gpu=$((T/1000))C"

if ! "$ADB" -s "$DEV" shell "cd /data/local/tmp && ls libvulkan_freedreno.so" >/dev/null 2>&1; then
  D=$("$ADB" -s "$DEV" shell "run-as $PKG ls files/gpu_drivers/" | head -1 | tr -d '\r')
  [ -n "$D" ] || { echo "ABORT: no Turnip driver installed"; exit 1; }
  "$ADB" -s "$DEV" shell "run-as $PKG cat files/gpu_drivers/$D/libvulkan_freedreno.so > $TURNIP"
fi

# Lead with cd: an adb shell argument starting with '/' is rewritten by Git Bash
# even inside quotes.
run() { "$ADB" -s "$DEV" shell "cd /data/local/tmp && ./edram_bench --driver $TURNIP $*"; }

echo "### tile-count x draw-count, viewport fixed at 1280x720, loadOp=dontcare"
echo "### fragment work is IDENTICAL in every row - only the tile grid changes"
for h in 720 2048 8192; do
  for d in 1 8 64 256; do
    run --label "h${h}-d${d}" --width 1280 --height "$h" \
        --view-width 1280 --view-height 720 --passes 4 --draws "$d" \
        --loadop dontcare --storeop dontcare | grep median
  done
done
