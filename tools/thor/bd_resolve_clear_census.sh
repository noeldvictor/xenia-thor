#!/usr/bin/env bash
# Count Blue Dragon's EDRAM resolve clears and price them.
#
# The harness measured a clear at ~44.4us per 1000 rows (1280 wide, Turnip) and
# showed scissoring recovers 4.8x. Our clear is ALREADY scissored - to the
# GUEST resolve rectangle - so the only open question is how big those
# rectangles are in a real title and how many are issued per frame.
#
# Pairs gpu_trace_resolve_clears with vulkan_trace_draw_outcomes_per_frame so
# the clear count can be divided by a real frame count.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
SEQ='start@25000:1200;a@35000:1200;a@45000:1200;start@55000:1200;a@65000:1200;a@75000:1200;a@85000:1200;a@95000:1200;a@105000:1200;a@115000:1200;a@125000:1200'
RUN_S=${1:-200}
OUT="${OUT:-/c/Users/LEANER~1/AppData/Local/Temp/claude/C--Users-leanerdesigner-Documents-xenia-thor-workspace/11556ba1-8110-420c-b7ae-933f76cffaf1/scratchpad}"

say(){ echo "[$(date +%H:%M:%S)] $*"; }
# QUOTE the remote command; an unquoted /sys path is rewritten by Git Bash and
# the read silently returns nothing.
gt(){ "$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" | tr -d '\r'; }

other=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
[ "${other:-0}" = "0" ] || { say "ABORT: rpcs3 running - device is SHARED"; exit 1; }
T=$(gt); B=$("$ADB" -s "$DEV" shell dumpsys battery | grep -m1 level | grep -oE '[0-9]+')
say "pre-flight gpu=$((T/1000))C battery=${B}%"
[ "$T" -gt 45000 ] && { say "ABORT: too hot"; exit 1; }
[ "$B" -lt 30 ] && { say "ABORT: battery too low"; exit 1; }

DRV="$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ | head -1 | tr -d '\r')"
[ -n "$DRV" ] || { say "ABORT: no Turnip driver"; exit 1; }
NATIVE="$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | head -1 | sed 's/^package://' | tr -d '\r')")/lib/arm64"

"$ADB" -s "$DEV" shell am force-stop $PKG; "$ADB" -s "$DEV" logcat -c
"$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' \
  --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true \
  --ez cpu_llvm_target_features_native true \
  --ez gpu_trace_resolve_clears true --ei gpu_trace_resolve_clears_budget -1 \
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
[ -n "$ok" ] || { say "ABORT: no title in 240s"; "$ADB" -s "$DEV" shell am force-stop $PKG; exit 1; }
say "title up; running ${RUN_S}s"
END=$((SECONDS + RUN_S))
while [ $SECONDS -lt $END ]; do
  sleep 20
  T=$(gt)
  say "  t=${SECONDS}s gpu=$((T/1000))C"
  [ -n "$T" ] && [ "$T" -gt 75000 ] && { say "THERMAL GUARD"; break; }
done

"$ADB" -s "$DEV" shell "logcat -d -s xenia:*" > "$OUT/bd_clears.log" 2>&1
"$ADB" -s "$DEV" shell am force-stop $PKG
say "captured $(wc -l < "$OUT/bd_clears.log") lines -> $OUT/bd_clears.log"
