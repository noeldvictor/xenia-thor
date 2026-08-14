#!/usr/bin/env bash
# A/B: force binning mode (TU_DEBUG=gmem) against the driver's own autotune.
#
# WHY: the per-pass trace showed Blue Dragon renders 248,033 of 248,055 passes
# in DIRECT/SYSMEM mode - autotune only bins passes carrying ~686 draws. The
# Adreno guide says direct mode "is often less performant than Binning Mode",
# so autotune may be leaving performance on the table for the 30-54 draw passes
# that dominate the frame.
#
# ⚠ EXPECT A MIXED RESULT. The harness measured gmem as SLOWER than sysmem for a
# 1-draw pass (74.3 vs 58.0 us), and ~61 of BD's 74 passes per frame carry at
# most one draw. Forcing gmem globally helps the heavy passes and hurts those.
# This run BRACKETS the question; it is not a shipping change either way.
#
# PROTOCOL (from this file's own rules): one arm per cooldown from equal thermal
# starts, average gpu_frame_us over TENS of gameplay frames filtered by SCENE
# CONTENT (total_vertices), never by a function of the metric under test.
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
  # '' NOT '""' - the literal 2-char value "" is treated as a wrapper command
  # and bricks every launch of the package until reboot.
  "$ADB" -s "$DEV" shell "setprop wrap.$PKG ''" >/dev/null 2>&1
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1
}
trap cleanup EXIT

busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
[ "${busy:-1}" = "0" ] || { say "ABORT: rpcs3 running - device is SHARED"; exit 1; }

cooldown() {
  local target=$1
  for _ in $(seq 1 60); do
    T=$(gt); case "$T" in ''|*[!0-9]*) say "ABORT: bad temp read"; exit 1;; esac
    [ "$T" -lt "$target" ] && { say "cooled to $((T/1000))C"; return 0; }
    sleep 15
  done
  say "ABORT: never cooled below $((target/1000))C"; exit 1
}

DRV="$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ | head -1 | tr -d '\r')"
[ -n "$DRV" ] || { say "ABORT: no Turnip driver"; exit 1; }
NATIVE="$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | head -1 | sed 's/^package://' | tr -d '\r')")/lib/arm64"

arm() { # $1 = label, $2 = wrap value ('' for none)
  local label="$1" wrap="$2"
  cooldown 45000
  B=$("$ADB" -s "$DEV" shell dumpsys battery | grep -m1 level | grep -oE '[0-9]+')
  [ "${B:-0}" -ge 30 ] || { say "ABORT: battery ${B}%"; exit 1; }
  "$ADB" -s "$DEV" shell am force-stop $PKG
  "$ADB" -s "$DEV" logcat -c
  # No double quotes inside the value - see wrap_probe.sh.
  "$ADB" -s "$DEV" shell "setprop wrap.$PKG '$wrap'"
  got=$("$ADB" -s "$DEV" shell "getprop wrap.$PKG" | tr -d '\r')
  say "=== $label : cold=$(( $(gt)/1000 ))C wrap=[$got] ==="

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
  [ -n "$ok" ] || { say "ABORT: $label never reached the title - VOID"; exit 1; }

  local end=$((SECONDS + RUN_S))
  while [ $SECONDS -lt $end ]; do
    sleep 20
    T=$(gt); say "  t=${SECONDS}s gpu=$((T/1000))C"
    case "$T" in ''|*[!0-9]*) say "THERMAL READ FAILED"; break;; esac
    [ "$T" -gt 75000 ] && { say "THERMAL GUARD"; break; }
  done

  # Grep the FIELD, never the line prefix - logcat WRAPS the frame-trace line
  # and everything past the wrap point lands on prefix-less continuation lines.
  "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null \
    | grep -oE "total_vertices=[0-9]+|gpu_frame_us=[0-9]+" > "$OUT/gmem_$label.txt"
  say "$label: $(wc -l < "$OUT/gmem_$label.txt") fields -> $OUT/gmem_$label.txt"
  "$ADB" -s "$DEV" shell am force-stop $PKG
}

arm autotune ""
arm gmem     "TU_DEBUG=gmem"

python tools/thor/bd_gmem_report.py "$OUT/gmem_autotune.txt" "$OUT/gmem_gmem.txt"
