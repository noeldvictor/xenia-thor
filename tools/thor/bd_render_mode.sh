#!/usr/bin/env bash
# WHICH RENDER MODE DO BLUE DRAGON'S PASSES ACTUALLY USE?
#
# The harness retraction (2026-08-14) showed every EDRAM harness result was
# measured in SYSMEM mode, where attachment height is trivially free. In binning
# mode the EDRAM-span attachment costs ~+31us/pass. Which of those two tables
# describes our real frame depends entirely on the mode BD's passes run in, and
# the driver reports it per pass.
#
# 61 of BD's 74 passes issue AT MOST ONE DRAW, which is the Adreno guide's own
# direct-mode trigger - so a large part of the frame may legitimately be sysmem.
#
# Turnip emits at every end_render_pass:
#   tiledRender=<bool>, tilingDisableReason=<why>, drawCount=<n>
# and at every start_render_pass:
#   width=, height=, numberOfBins=, binWidth=, binHeight=
# Pairing them gives mode x geometry x draw count for the whole frame.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
SEQ='start@25000:1200;a@35000:1200;a@45000:1200;start@55000:1200;a@65000:1200;a@75000:1200;a@85000:1200;a@95000:1200;a@105000:1200;a@115000:1200;a@125000:1200'
RUN_S=${1:-120}
TRACE=/sdcard/u.txt
OUT="${OUT:-$PWD/scratchpad}"
mkdir -p "$OUT" 2>/dev/null

say(){ echo "[$(date +%H:%M:%S)] $*"; }
# QUOTE the remote command - an unquoted /sys path is rewritten by Git Bash and
# the read silently returns nothing, which is how a thermal guard once ran inert
# for 300 seconds.
gt(){ "$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" | tr -d '\r'; }

cleanup() {
  # The wrap property PERSISTS UNTIL REBOOT and applies to every launch of this
  # package. On a SHARED device, leaving it set silently contaminates the next
  # session's measurements. Unset it on EVERY exit path.
  "$ADB" -s "$DEV" shell "setprop wrap.$PKG '\"\"'" >/dev/null 2>&1
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1
}
trap cleanup EXIT

busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
[ "${busy:-1}" = "0" ] || { say "ABORT: rpcs3 running - device is SHARED"; exit 1; }
T=$(gt); case "$T" in ''|*[!0-9]*) say "ABORT: bad temp read '$T'"; exit 1;; esac
B=$("$ADB" -s "$DEV" shell dumpsys battery | grep -m1 level | grep -oE '[0-9]+')
say "pre-flight gpu=$((T/1000))C battery=${B}%"
[ "$T" -lt 45000 ] || { say "ABORT: too hot"; exit 1; }
[ "${B:-0}" -ge 30 ] || { say "ABORT: battery too low"; exit 1; }

DRV="$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ | head -1 | tr -d '\r')"
[ -n "$DRV" ] || { say "ABORT: no Turnip driver"; exit 1; }
NATIVE="$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | head -1 | sed 's/^package://' | tr -d '\r')")/lib/arm64"

"$ADB" -s "$DEV" shell am force-stop $PKG
"$ADB" -s "$DEV" shell "rm -f $TRACE" >/dev/null 2>&1
"$ADB" -s "$DEV" logcat -c

# An Android property VALUE is limited to 92 bytes. The app's files-dir path
# does not fit; /sdcard does. Confirm it applied - a failed setprop does NOT
# stop the run, it just silently produces no trace.
# NOWRAP=1 runs the identical route with NO wrap property - the control that
# says whether a voided run is the tracing or the route.
if [ "${NOWRAP:-0}" = "1" ]; then
  say "NOWRAP=1 - control run, no tracing"
else
WRAP="MESA_GPU_TRACEFILE=$TRACE MESA_GPU_TRACES=print"
[ ${#WRAP} -lt 92 ] || { say "ABORT: wrap value ${#WRAP} bytes >= 92"; exit 1; }
"$ADB" -s "$DEV" shell "setprop wrap.$PKG '\"$WRAP\"'"
GOT=$("$ADB" -s "$DEV" shell "getprop wrap.$PKG" | tr -d '\r')
say "wrap = $GOT"
case "$GOT" in *MESA_GPU_TRACES*) ;; *) say "ABORT: setprop did not apply"; exit 1;; esac
fi

"$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' \
  --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true \
  --ez cpu_llvm_target_features_native true \
  --ez vulkan_trace_draw_outcomes_per_frame true \
  --es hid nop --es hid_nop_button_sequence '$SEQ' \
  --es gpu_vulkan_driver turnip \
  --es gpu_vulkan_driver_path '/data/data/$PKG/files/gpu_drivers/$DRV/' \
  --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
  --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null

ok=""
for _ in $(seq 1 80); do
  "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep -q "Title name:" && { ok=1; break; }
  sleep 3
done
[ -n "$ok" ] || { say "ABORT: no title in 240s - VOID, not a measurement"; exit 1; }
say "title up; tracing ${RUN_S}s"

END=$((SECONDS + RUN_S))
while [ $SECONDS -lt $END ]; do
  sleep 20
  T=$(gt); say "  t=${SECONDS}s gpu=$((T/1000))C"
  case "$T" in ''|*[!0-9]*) say "THERMAL READ FAILED - stopping"; break;; esac
  [ "$T" -gt 75000 ] && { say "THERMAL GUARD"; break; }
done

# Aggregate ON DEVICE. The raw trace is ~1,250 records/sec; pulling it whole is
# a huge transfer for data we only want summarised.
say "trace size: $("$ADB" -s "$DEV" shell "ls -l $TRACE 2>/dev/null | awk '{print \$5}'" | tr -d '\r') bytes"
"$ADB" -s "$DEV" shell "grep -oE 'width=[0-9]+, height=[0-9]+|numberOfBins=[0-9]+|tiledRender=[a-z]+|tilingDisableReason=[^,]*|drawCount=[0-9]+' $TRACE" \
  > "$OUT/bd_mode_raw.txt" 2>/dev/null
say "extracted $(wc -l < "$OUT/bd_mode_raw.txt") fields -> $OUT/bd_mode_raw.txt"
