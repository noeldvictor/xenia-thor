#!/usr/bin/env bash
# Answer ONE question: does this host give us native BC, or is every guest DXT
# texture being DECOMPRESSED to RGBA8 (8x the bytes for DXT1, 4x for DXT3/5)?
#
# WHY IT MATTERS: the 2026-08-16 shader capture refuted the occupancy hypothesis
# but left one signal live - systall at or above the instruction count on the
# textured variants. The Adreno guide names uncompressed textures and cache
# misses as direct causes of texture-pipe stalls. Until the TEXcompress log line
# was added this fallback was COMPLETELY SILENT.
#
# Cheap by construction: the line is emitted at texture-cache init, during GPU
# setup, so this needs no route and no gameplay - just a launch.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
PKG=jp.xenia.emulator.github.debug
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'

busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs"' | tr -d '\r')
[ "${busy:-1}" = "0" ] || { echo "ABORT: rpcs3 running - device is SHARED"; exit 1; }

DRV="$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ | head -1 | tr -d '\r')"
[ -n "$DRV" ] || { echo "ABORT: no Turnip driver"; exit 1; }
# Re-read every run: Android reinstalls under a fresh randomised path.
NATIVE="$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | head -1 | sed 's/^package://' | tr -d '\r')")/lib/arm64"

"$ADB" -s "$DEV" shell am force-stop $PKG
"$ADB" -s "$DEV" logcat -c
"$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' \
  --es cpu arm64 --ez cpu_backend_llvm true \
  --es gpu_vulkan_driver turnip \
  --es gpu_vulkan_driver_path '/data/data/$PKG/files/gpu_drivers/$DRV/' \
  --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
  --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null

for i in $(seq 1 24); do
  sleep 5
  line=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep -m1 "TEXcompress")
  [ -n "$line" ] && { echo; echo "$line"; break; }
  echo "  t=$((i*5))s waiting for texture-cache init..."
done
echo
echo "--- driver actually in use (Turnip or the run means nothing) ---"
"$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep -m2 -iE "turnip|physical device|driverID" | tr -d '\r'
"$ADB" -s "$DEV" shell am force-stop $PKG
[ -n "${line:-}" ] || { echo "VOID: TEXcompress never printed"; exit 1; }
