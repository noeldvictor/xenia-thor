#!/bin/bash
# Launch a title on the Thor with Turnip explicitly applied and the display AWAKE.
#
# Two traps this exists to avoid:
#  1. A bare `am start` runs the QUALCOMM proprietary driver - Turnip is applied
#     by the Java launch path, so the four driver extras must be passed by hand.
#  2. If the panel is asleep the activity is stopped, the SurfaceView never gets
#     a surface, and the emulator silently drops every frame - screencap then
#     returns a fully black PNG while the emulator is perfectly healthy.
#
# Usage: XT_ISO="/path/on/device.iso" ./launch.sh [--ez cvar true ...]
A=/c/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe
PKG=jp.xenia.emulator.github.debug
ISO="${XT_ISO:-/storage/2664-21DE/Roms/xbox360/Burnout Revenge (USA).iso}"

TEMP=$($A -s c3ca0370 shell "cat /sys/class/kgsl/kgsl-3d0/temp" | tr -d '\r')
echo "GPU temp before launch: ${TEMP}"
if [ "${TEMP}" -gt 55000 ]; then
  echo "TOO HOT (>55C) - refusing to launch."; exit 1
fi

# Wake the panel, or everything below renders into the void.
$A -s c3ca0370 shell "input keyevent KEYCODE_WAKEUP" >/dev/null 2>&1
sleep 1
$A -s c3ca0370 shell "input keyevent 82" >/dev/null 2>&1
WAKE=$($A -s c3ca0370 shell "dumpsys power | grep -m1 mWakefulness=" | tr -d '\r')
echo "display: ${WAKE}"

NLD=$($A -s c3ca0370 shell "pm path $PKG" | tr -d '\r' | sed 's|package:||; s|/base.apk|/lib/arm64|')
DRVID=$($A -s c3ca0370 shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)
DRV="/data/user/0/$PKG/files/gpu_drivers/$DRVID/"

$A -s c3ca0370 shell "am force-stop $PKG"
$A -s c3ca0370 logcat -c
$A -s c3ca0370 shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity \
  --es cpu arm64 --es apu android --es gpu vulkan \
  --es gpu_vulkan_driver turnip \
  --es gpu_vulkan_driver_path '$DRV' \
  --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
  --es gpu_vulkan_driver_hooks_path '$NLD' \
  $* \
  --es target '$ISO'" >/dev/null 2>&1
echo "launched with driver: $DRVID"
echo "verify: logcat must show 'Turnip Adreno (TM) 740'; a ~10.9KB screencap means the panel is asleep."
