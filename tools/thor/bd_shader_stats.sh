#!/usr/bin/env bash
# Dump ir3's per-variant FRAGMENT SHADER stats for Blue Dragon's real shaders.
#
# WHY THIS IS THE RIGHT NEXT MEASUREMENT: the render-mode trace retired every
# tile/GMEM lever, leaving fragment shading as the measured cost. The one form
# of "smarter Xenos emulation" the evidence has never excluded is whether OUR
# SPIR-V translation emits materially more work than the Xenos original -
# instruction count, register pressure and the resulting wave occupancy.
# XenDroid measured 2,195 instrs @ 4 waves / 31 GPRs and 26% NOPs on THEIR
# title. Nobody has ever measured OURS.
#
# The instrument only became usable today: IR3_SHADER_DEBUG output goes to
# LOGCAT under tag MESA (not stdout), and the wrap property has to carry NO
# double quotes or the app is killed before it attaches.
#
# MESA_SHADER_CACHE_DISABLE is REQUIRED - a variant served from Turnip's on-disk
# cache prints no stats at all, which looks exactly like "the option did not
# work".
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
SEQ='start@25000:1200;a@35000:1200;a@45000:1200;start@55000:1200;a@65000:1200;a@75000:1200;a@85000:1200;a@95000:1200;a@105000:1200;a@115000:1200;a@125000:1200'
RUN_S=${1:-90}
OUT="${OUT:-$PWD/scratchpad}"
mkdir -p "$OUT" 2>/dev/null

say(){ echo "[$(date +%H:%M:%S)] $*"; }
gt(){ "$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" | tr -d '\r'; }

cleanup() {
  "$ADB" -s "$DEV" shell "setprop wrap.$PKG ''" >/dev/null 2>&1
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1
}
trap cleanup EXIT

busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
[ "${busy:-1}" = "0" ] || { say "ABORT: rpcs3 running - device is SHARED"; exit 1; }
T=$(gt); case "$T" in ''|*[!0-9]*) say "ABORT: bad temp read"; exit 1;; esac
B=$("$ADB" -s "$DEV" shell dumpsys battery | grep -m1 level | grep -oE '[0-9]+')
say "pre-flight gpu=$((T/1000))C battery=${B}%"
[ "$T" -lt 45000 ] || { say "ABORT: too hot"; exit 1; }
[ "${B:-0}" -ge 30 ] || { say "ABORT: battery too low"; exit 1; }

DRV="$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ | head -1 | tr -d '\r')"
[ -n "$DRV" ] || { say "ABORT: no Turnip driver"; exit 1; }
NATIVE="$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | head -1 | sed 's/^package://' | tr -d '\r')")/lib/arm64"

"$ADB" -s "$DEV" shell am force-stop $PKG
# This dumps EVERY fragment variant - BD compiles dozens and the file already
# records logcat EVICTING lines under heavy logging.
"$ADB" -s "$DEV" logcat -G 64M >/dev/null 2>&1
"$ADB" -s "$DEV" logcat -c

WRAP="IR3_SHADER_DEBUG=fs MESA_SHADER_CACHE_DISABLE=true"
[ ${#WRAP} -lt 92 ] || { say "ABORT: wrap ${#WRAP} bytes >= 92"; exit 1; }
# NO double quotes in the value - '"VAR=x"' makes the shell try to execute a
# program named VAR=x and the app never attaches.
"$ADB" -s "$DEV" shell "setprop wrap.$PKG '$WRAP'"
GOT=$("$ADB" -s "$DEV" shell "getprop wrap.$PKG" | tr -d '\r')
say "wrap = $GOT"
case "$GOT" in *IR3_SHADER_DEBUG*) ;; *) say "ABORT: setprop did not apply"; exit 1;; esac

"$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' \
  --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true \
  --ez cpu_llvm_target_features_native true \
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
[ -n "$ok" ] || { say "ABORT: no title in 240s - VOID"; exit 1; }
say "title up; collecting ${RUN_S}s (shaders compile on first use, so drive into the field)"

END=$((SECONDS + RUN_S))
while [ $SECONDS -lt $END ]; do
  sleep 20
  T=$(gt); say "  t=${SECONDS}s gpu=$((T/1000))C"
  case "$T" in ''|*[!0-9]*) say "THERMAL READ FAILED"; break;; esac
  [ "$T" -gt 75000 ] && { say "THERMAL GUARD"; break; }
done

"$ADB" -s "$DEV" shell "logcat -d -s MESA" 2>/dev/null | grep "FRAG prog" > "$OUT/bd_frag_stats.txt"
say "captured $(wc -l < "$OUT/bd_frag_stats.txt") FRAG stat lines -> $OUT/bd_frag_stats.txt"
python tools/thor/bd_shader_report.py "$OUT/bd_frag_stats.txt"
