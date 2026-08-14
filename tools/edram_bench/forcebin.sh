#!/usr/bin/env bash
# CONTROL FOR EVERY EARLIER HARNESS RESULT: were they measured in the mode the
# game actually uses?
#
# Turnip's own u_trace says our harness passes run as:
#   tiledRender=false, tilingDisableReason=Autotune selected sysmem
# i.e. DIRECT/SYSMEM mode, NOT binning. In sysmem mode there is no tile
# load/store at all - which would trivially explain why loadOp=LOAD measured
# free at every attachment height, and would mean the oversized-RT exoneration
# was measured in the wrong mode.
#
# TU_DEBUG=forcebin forces binning mode. If the height sweep still shows LOAD as
# flat under forcebin, the exoneration holds and is now mode-proof. If LOAD
# starts scaling with height, the oversized-RT theory comes back and the earlier
# verdict must be withdrawn.
#
# The mode is VERIFIED from the driver on every arm, not assumed - that is the
# whole point of this script.
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

# $1 = TU_DEBUG value ("" for default), rest = harness args
run() {
  local dbg="$1"; shift
  local env=""
  [ -n "$dbg" ] && env="TU_DEBUG=$dbg"
  "$ADB" -s "$DEV" shell "cd /data/local/tmp && $env ./edram_bench --driver $TURNIP $*"
}

# Read the mode the driver ACTUALLY used, per arm.
mode() {
  local dbg="$1"; shift
  local env=""
  [ -n "$dbg" ] && env="TU_DEBUG=$dbg"
  "$ADB" -s "$DEV" shell "cd /data/local/tmp && rm -f m.txt && \
     MESA_GPU_TRACEFILE=/data/local/tmp/m.txt MESA_GPU_TRACES=print $env \
     ./edram_bench --driver $TURNIP --iters 1 $*" >/dev/null 2>&1
  "$ADB" -s "$DEV" shell "cd /data/local/tmp && grep -o 'tiledRender=[a-z]*' m.txt | sort | uniq -c | tr -d '\r'; \
     grep -o 'numberOfBins=[0-9]*' m.txt | sort -u | tr -d '\r'"
}

for dbg in "" "gmem" "gmem,forcebin"; do
  label="${dbg:-autotune}"
  echo
  echo "############ TU_DEBUG=${dbg:-<default>} ############"
  for h in 720 2048 8192; do
    echo "--- attachment 1280x$h, mode reported by the driver ---"
    mode "$dbg" --label m --width 1280 --height "$h" --view-width 1280 \
         --view-height 720 --passes 1 --draws 1 --loadop load
    for op in load clear dontcare; do
      run "$dbg" --label "$label-h$h-$op" --width 1280 --height "$h" \
          --view-width 1280 --view-height 720 --passes 4 --draws 1 \
          --loadop "$op" | grep median
    done
  done
done
