#!/usr/bin/env bash
# Does clamping renderArea BELOW the attachment change anything - and does it
# flip Turnip out of binning mode?
#
# THE ONLY UNEXPLAINED GPU DATAPOINT IN THIS PROJECT: clamping renderArea to the
# guest scissor halved BD's in-pass time (46.9 -> 23.5 ms) while making the
# frame 18% slower. Four explanations are dead - attachment traffic, clear
# rects, bin count, fragment count.
#
# THE SURVIVING CANDIDATE is a FlexRender binned-vs-direct mode flip. The Adreno
# guide lists "small number of vertices and/or draws" as a direct-mode trigger,
# and 61 of our 74 passes issue at most one draw.
#
# THE ARMS: attachment 1280x2048, viewport 1280x720 (so the drawn region and the
# fragment work are IDENTICAL), renderArea either the full 2048 or clamped to
# 720. loadOp/storeOp DONT_CARE so load/store and clears cannot confound it.
#
# Turnip REPORTS the mode per pass, so this does not need inference:
# MESA_GPU_TRACES=print emits `gmem` (true = binning) and `gmem_disable_reason`
# at every end_render_pass.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
TURNIP=/data/local/tmp/libvulkan_freedreno.so
PKG=jp.xenia.emulator.github.debug

busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
[ "${busy:-1}" = "0" ] || { echo "ABORT: rpcs3 running - device is SHARED"; exit 1; }
T=$("$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" | tr -d '\r')
case "$T" in ''|*[!0-9]*) echo "ABORT: bad temp read '$T'"; exit 1;; esac
[ "$T" -lt 55000 ] || { echo "ABORT: gpu $((T/1000))C too hot"; exit 1; }
echo "pre-flight gpu=$((T/1000))C"

if ! "$ADB" -s "$DEV" shell "cd /data/local/tmp && ls libvulkan_freedreno.so" >/dev/null 2>&1; then
  D=$("$ADB" -s "$DEV" shell "run-as $PKG ls files/gpu_drivers/" | head -1 | tr -d '\r')
  [ -n "$D" ] || { echo "ABORT: no Turnip driver installed"; exit 1; }
  "$ADB" -s "$DEV" shell "run-as $PKG cat files/gpu_drivers/$D/libvulkan_freedreno.so > $TURNIP"
fi

run() { "$ADB" -s "$DEV" shell "cd /data/local/tmp && ./edram_bench --driver $TURNIP $*"; }

echo "### timing: attachment 1280x2048, viewport 1280x720, renderArea full vs clamped"
for d in 1 8 64; do
  for ah in 2048 720; do
    run --label "d${d}-area${ah}" --width 1280 --height 2048 \
        --view-width 1280 --view-height 720 --passes 4 --draws "$d" \
        --area-height "$ah" --loadop dontcare --storeop dontcare | grep median
  done
done

echo
echo "### render mode per pass, straight from the driver"
# MESA_GPU_TRACES writes to the file named by MESA_GPU_TRACEFILE. Do NOT use
# log.redirect-stdio - this file records that it does not capture a wrapped
# app's stdout. Here we run the binary directly, so the env var is enough.
for ah in 2048 720; do
  "$ADB" -s "$DEV" shell "cd /data/local/tmp && rm -f ut.txt && \
     MESA_GPU_TRACEFILE=/data/local/tmp/ut.txt MESA_GPU_TRACES=print \
     ./edram_bench --driver $TURNIP --label mode$ah --width 1280 --height 2048 \
     --view-width 1280 --view-height 720 --passes 2 --draws 1 --iters 1 \
     --area-height $ah --loadop dontcare --storeop dontcare" >/dev/null 2>&1
  echo "--- renderArea height $ah ---"
  "$ADB" -s "$DEV" shell "cd /data/local/tmp && grep -o 'gmem=[a-z0-9]*\|gmem_disable_reason=[^,]*\|tiles=[0-9x]*' ut.txt | sort | uniq -c" 2>/dev/null | head -8
done
