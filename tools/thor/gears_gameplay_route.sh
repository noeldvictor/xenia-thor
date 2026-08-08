#!/usr/bin/env bash
# Gears of War -> ACT 1 GAMEPLAY (the prison corridor), device-verified 2026-08-07.
#
# Reaching Gears gameplay by adb keyevents does NOT work: alternating START/B
# skips the cinematics but then B BACKS OUT of the main menu to the title, and A
# does nothing on the title. Two attempts failed and each cost ~35C of thermal
# headroom. The nop HID driver injects buttons into the GUEST instead, with
# ABSOLUTE timings, so the whole route runs unattended.
#
# Verified landing: Act 1 prison corridor at ~125-150s, entry_delta ~3.6-3.9M/5s
# (the title screen is ~1.1M/5s, so this is ~3.5x and genuinely gameplay tier).
#
# THERMALS: 38C cold start -> 73C at 150s -> 78C shortly after. Budget one run.
set -u
ADB="C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe"
DEV=c3ca0370; PKG=jp.xenia.emulator.github.debug
DRV=mesa-turnip-v26.3.0-20260803-r7-vulkan-1.4.354-7
# MUST be re-read after every install - the APK dir is randomised each time.
NATIVE="$(dirname "$("$ADB" -s $DEV shell pm path $PKG | head -1 | sed 's/^package://' | tr -d '\r')")/lib/arm64"
GAME='/storage/2664-21DE/Roms/xbox360/Gears of War (World) (En,Fr,De,Es,It,Zh,Ko).iso'
SEQ='start@40000:1200;start@47000:1200;start@53000:1200;start@59000:1200;start@65000:1200;start@71000:1200;a@79000:1200;a@86000:1200;a@93000:1200;a@100000:1200;a@107000:1200;a@114000:1200'

T=$("$ADB" -s $DEV shell cat /sys/class/kgsl/kgsl-3d0/temp | tr -d '\r')
B=$("$ADB" -s $DEV shell dumpsys battery | grep -m1 level | grep -oE '[0-9]+')
echo "pre-flight: temp=$((T/1000))C battery=${B}%"
[ "$T" -gt 45000 ] && { echo "too hot for a full route - cool below 45C first"; exit 1; }
[ "$B" -lt 30 ] && { echo "battery too low"; exit 1; }

"$ADB" -s $DEV shell am force-stop $PKG; "$ADB" -s $DEV logcat -c
"$ADB" -s $DEV shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' \
  --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true --ez cpu_llvm_target_features_native true \
  --ei arm64_speed_profile_interval_ms 5000 \
  --es hid nop --es hid_nop_button_sequence '$SEQ' \
  --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '/data/data/$PKG/files/gpu_drivers/$DRV/' \
  --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE'"
echo "route running; gameplay at ~125s. Guard at 70C."
