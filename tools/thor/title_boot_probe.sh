#!/usr/bin/env bash
# DOES THIS TITLE BOOT TO RENDERED GAMEPLAY, AND IS IT MEASURABLE?
#
# WHY: every performance conclusion in this project is Blue Dragon's. Gears
# stalls, Burnout sits on its 60fps cap with no headroom. A second MEASURABLE
# title is worth more than another lever on BD - and the cheapest way to get
# one is to ask each untried title the question directly instead of assuming.
#
# WHAT "MEASURABLE" MEANS HERE, and it is stricter than "it boots":
#   1. reaches `Title name:`                      - it loaded
#   2. renders frames with real geometry          - not a black screen / loader
#   3. is NOT pinned to a frame cap               - a capped title cannot show a
#      CPU or GPU win at all (this file's own rule), which is exactly why
#      Burnout is useless as a benchmark despite running perfectly
#
# TRAPS THIS CARRIES, all previously paid for in this repo:
#   - every adb call is bounded by `timeout`: a dropped USB endpoint once hung
#     an unwrapped call so the polling loop never came back round and THE 70C
#     GUARD NEVER EVALUATED (device reached 78.1C).
#   - the driver name is read off the DEVICE (a stale path silently falls back
#     to the Qualcomm blob), and nativeLibraryDir is derived fresh after any
#     install (Android reinstalls under a new random directory).
#   - a COLD object cache means a big title is still compiling for minutes;
#     this reports AOT progress so "no frames yet" is never confused with
#     "broken".
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
PKG=jp.xenia.emulator.github.debug
GAME="${GAME:?set GAME to the on-device ISO path}"
LABEL="${LABEL:-title}"
COOL="${COOL:-45000}"
GUARD="${GUARD:-70000}"
SECS="${SECS:-420}"
OUT=scratchpad/titles
mkdir -p "$OUT"
adb_(){ timeout 25 "$ADB" -s "$DEV" "$@"; }
say(){ echo "[$(date +%H:%M:%S)] $*"; }
temp(){ t=$(adb_ shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d '\r')
        case "$t" in ''|*[!0-9]*) echo 999999;; *) echo "$t";; esac; }

busy=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '\r')
[ "$busy" = "0" ] || { say "ABORT: rpcs3 running"; exit 1; }
T=$(temp); say "pre-flight temp=$((T/1000))C"
[ "$T" -le "$COOL" ] || { say "ABORT: too hot ($((T/1000))C > $((COOL/1000))C)"; exit 1; }

adb_ shell "am force-stop $PKG" >/dev/null
adb_ logcat -G 64M >/dev/null 2>&1; adb_ logcat -c >/dev/null 2>&1
adb_ shell "input keyevent KEYCODE_WAKEUP" >/dev/null
NATIVE=$(dirname "$(adb_ shell "pm path $PKG" | sed 's/^package://' | tr -d '\r')")/lib/arm64
DRV=$(adb_ shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)
[ -n "$DRV" ] || { say "ABORT: no Turnip driver"; exit 1; }
say "driver=$DRV"
# vulkan_trace_draw_outcomes_per_frame is what makes "is it RENDERING" answerable
# (total_vertices per frame) instead of a guess from a screenshot.
adb_ shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true --ez vulkan_trace_draw_outcomes_per_frame true --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '/data/user/0/$PKG/files/gpu_drivers/$DRV/' --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1

n=$((SECS/10))
for i in $(seq 1 $n); do
  sleep 10
  tt=$(temp); el=$((i*10))
  L=$(adb_ logcat -d -s xenia:* 2>/dev/null)
  title=$(echo "$L" | grep -c "Title name:")
  aot=$(echo "$L" | grep -oE "AOT precompile progress: [0-9]+" | tail -1 | grep -oE "[0-9]+$")
  frames=$(echo "$L" | grep -c "GPU draw outcomes")
  verts=$(echo "$L" | grep -oE "total_vertices=[0-9]+" | tail -5 | grep -oE "[0-9]+" | sort -n | tail -1)
  faults=$(echo "$L" | grep -ci "fault\|SIGSEGV\|Scudo")
  say "  ${el}s $((tt/1000))C title=$title aot=${aot:-0} frames=$frames verts=${verts:-0} faults=$faults"
  if [ "$tt" -ge "$GUARD" ]; then say "  70C guard"; break; fi
done
adb_ logcat -d -s xenia:* > "$OUT/${LABEL}.log" 2>/dev/null
adb_ shell "am force-stop $PKG" >/dev/null
say "stopped; log -> $OUT/${LABEL}.log"

echo
echo "================ VERDICT: $LABEL ================"
T_=$(grep -c "Title name:" "$OUT/${LABEL}.log")
F_=$(grep -c "GPU draw outcomes" "$OUT/${LABEL}.log")
V_=$(grep -oE "total_vertices=[0-9]+" "$OUT/${LABEL}.log" | grep -oE "[0-9]+" | sort -n | tail -1)
echo "  title reached : $([ "$T_" -gt 0 ] && echo YES || echo NO)   $(grep -oE 'Title name: .*' "$OUT/${LABEL}.log" | head -1)"
echo "  frames traced : $F_"
echo "  peak vertices : ${V_:-0}   (a loader/menu is low; real 3D is >50k)"
echo "  faults        : $(grep -ci 'fault\|SIGSEGV\|Scudo' "$OUT/${LABEL}.log")"
echo "  stalled waits : $(grep -c 'has waited' "$OUT/${LABEL}.log")"
