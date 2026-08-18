#!/usr/bin/env bash
# VALIDATE THE DefineSymbol FIX - two parts, because the fix is in SHARED CPU code.
#
#  1. MagnaCarta 2 (a64): previously reached `Title name` and then died within
#     ~10s with SIGTRAP at fault addr 0x2a000025c, because ResolveFunction
#     handed back a kDeclared function with no extent and no machine code.
#     PASS = survives well past 10s with no RESOLVE FAILED and no SIGTRAP.
#
#  2. Blue Dragon: the REGRESSION CHECK. module.cc is shared by every title, so
#     a fix that unblocks MC2 and breaks the one title this project can already
#     measure is a net loss. PASS = still reaches the title and renders.
#
# Every adb call is bounded (a dropped endpoint once hung an unwrapped call and
# the thermal guard never evaluated - the device hit 78.1C).
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
PKG=jp.xenia.emulator.github.debug
OUT=scratchpad/titles
mkdir -p "$OUT"
adb_(){ timeout 25 "$ADB" -s "$DEV" "$@"; }
say(){ echo "[$(date +%H:%M:%S)] $*"; }
temp(){ t=$(adb_ shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d '\r')
        case "$t" in ''|*[!0-9]*) echo 999999;; *) echo "$t";; esac; }

run_title(){ # $1 label  $2 game path  $3 extra cvars  $4 seconds
  local label="$1" game="$2" extra="$3" secs="$4"
  say "===== $label ====="
  for i in $(seq 1 60); do T=$(temp); [ "$T" -le 45000 ] && break; sleep 10; done
  say "  start temp=$((T/1000))C"
  adb_ shell "am force-stop $PKG" >/dev/null
  adb_ logcat -G 64M >/dev/null 2>&1; adb_ logcat -c >/dev/null 2>&1
  adb_ shell "input keyevent KEYCODE_WAKEUP" >/dev/null
  local NATIVE DRV
  NATIVE=$(dirname "$(adb_ shell "pm path $PKG" | sed 's/^package://' | tr -d '\r')")/lib/arm64
  DRV=$(adb_ shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)
  adb_ shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$game' --es cpu arm64 --ez cpu_aot_maximize true --ez vulkan_trace_draw_outcomes_per_frame true $extra --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '/data/user/0/$PKG/files/gpu_drivers/$DRV/' --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1
  local n=$((secs/10))
  for i in $(seq 1 $n); do
    sleep 10
    local tt alive L ti fr vt rf
    tt=$(temp); alive=$(adb_ shell "pidof $PKG" | tr -d '\r')
    L=$(adb_ logcat -d -s xenia:* 2>/dev/null)
    ti=$(echo "$L" | grep -c "Title name:"); fr=$(echo "$L" | grep -c "GPU draw outcomes")
    vt=$(echo "$L" | grep -oE "total_vertices=[0-9]+" | tail -3 | grep -oE "[0-9]+" | sort -n | tail -1)
    rf=$(echo "$L" | grep -c "RESOLVE FAILED")
    say "  $((i*10))s $((tt/1000))C title=$ti frames=$fr verts=${vt:-0} resolvefail=$rf alive=$([ -n "$alive" ] && echo Y || echo N)"
    [ -z "$alive" ] && { say "  DIED at $((i*10))s"; break; }
    [ "$tt" -ge 70000 ] && { say "  70C guard"; break; }
  done
  adb_ logcat -d -s xenia:* > "$OUT/${label}.log" 2>/dev/null
  adb_ shell "am force-stop $PKG" >/dev/null
  echo "  --- $label verdict ---"
  echo "    title:        $(grep -oE 'Title name: .*' "$OUT/${label}.log" | head -1)"
  echo "    frames:       $(grep -c 'GPU draw outcomes' "$OUT/${label}.log")"
  echo "    peak verts:   $(grep -oE 'total_vertices=[0-9]+' "$OUT/${label}.log" | grep -oE '[0-9]+' | sort -n | tail -1)"
  echo "    RESOLVE FAILED: $(grep -c 'RESOLVE FAILED' "$OUT/${label}.log")"
  grep "RESOLVE FAILED" "$OUT/${label}.log" | sed -E 's/^.*xenia[ ]*: //' | cut -c1-160 | head -2
}

run_title mc2_fix "/storage/2664-21DE/Roms/xbox360/Magna Carta 2.m3u/MagnaCarta 2 (USA) (En,Fr) (Disc 1).iso" "--ez cpu_backend_llvm false" "${MC2_SECS:-120}"
run_title bd_regress "/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso" "--ez cpu_backend_llvm true" "${BD_SECS:-90}"
