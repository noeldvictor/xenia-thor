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
# Usage: XT_SERIAL=<serial|ip:5555> XT_ISO="/path/on/device.iso" ./launch.sh [--ez cvar true ...]
# Override with XT_ADB if adb is not on PATH.
A="${XT_ADB:-adb}"
# USB on this Thor drops out mid-session; ADB over Wi-Fi is the reliable
# transport. `adb tcpip 5555` once, then `adb connect <ip>:5555`, then set
# XT_SERIAL to that ip:port (or to the USB serial from `adb devices`).
SERIAL="${XT_SERIAL:?set XT_SERIAL to your device serial or ip:5555}"
PKG=jp.xenia.emulator.github.debug
ISO="${XT_ISO:-/storage/2664-21DE/Roms/xbox360/Burnout Revenge (USA).iso}"

TEMP=$($A -s "$SERIAL" shell "cat /sys/class/kgsl/kgsl-3d0/temp" | tr -d '\r')
echo "GPU temp before launch: ${TEMP}"
if [ "${TEMP}" -gt 55000 ]; then
  echo "TOO HOT (>55C) - refusing to launch."; exit 1
fi

# Wake the panel, or everything below renders into the void.
$A -s "$SERIAL" shell "input keyevent KEYCODE_WAKEUP" >/dev/null 2>&1
sleep 1
$A -s "$SERIAL" shell "input keyevent 82" >/dev/null 2>&1
WAKE=$($A -s "$SERIAL" shell "dumpsys power | grep -m1 mWakefulness=" | tr -d '\r')
echo "display: ${WAKE}"

NLD=$($A -s "$SERIAL" shell "pm path $PKG" | tr -d '\r' | sed 's|package:||; s|/base.apk|/lib/arm64|')
DRVID=$($A -s "$SERIAL" shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)
DRV="/data/user/0/$PKG/files/gpu_drivers/$DRVID/"

$A -s "$SERIAL" shell "am force-stop $PKG"
$A -s "$SERIAL" logcat -c
$A -s "$SERIAL" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity \
  --es cpu arm64 --es apu android --es gpu vulkan \
  --es gpu_vulkan_driver turnip \
  --es gpu_vulkan_driver_path '$DRV' \
  --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
  --es gpu_vulkan_driver_hooks_path '$NLD' \
  $* \
  --es target '$ISO'" >/dev/null 2>&1
echo "launched with driver: $DRVID"
echo "verify: logcat must show 'Turnip Adreno (TM) 740'; a ~10.9KB screencap means the panel is asleep."
