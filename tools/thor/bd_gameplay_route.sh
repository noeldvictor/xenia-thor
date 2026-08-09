#!/usr/bin/env bash
# Blue Dragon GAMEPLAY route + fps profile. Device-captured 2026-08-09.
#
# WHY THIS EXISTS: "capture a BD route" was the documented prerequisite for
# EVERY perf and power claim in this project, and five measurement attempts
# died without one. It was only reachable after the startup stall was
# root-caused to a stale LLVM object cache (see CLAUDE.md) - before that fix
# the guest main thread parked at its first wait and no route could work.
#
# WHAT IT REACHES, measured not assumed:
#   t=22-34s   ~33 fps            title screen
#   t=46-57s   ~28 -> ~22 fps     transition / cutscene
#   t=68s+     ~17.5 fps STABLE   3D gameplay scene, held 100s
#
# The end-state frames carry rendered=1219 draws and total_vertices=240,235
# per frame. A title screen does not draw 1,219 times or push a quarter of a
# million vertices - that is the discriminator that makes this gameplay-tier
# rather than "it looked busy".
#
# ⚠️ THERMALS - READ BEFORE RUNNING. This route crosses 70C at about t=57s and
# reaches 73C by t=155s. 70C is this project's force-stop limit, so
# THE LOOP BELOW ABORTS AT 70C. The capture run that produced these numbers did
# NOT abort and ran to 73C; that was a rule violation, and the guard exists so
# it is not repeated. Budget ONE run per cooldown from <=45C.
#
# ⚠️ AND THE MEASUREMENT CAVEAT: ~17.5 fps is this scene on this build. The
# ~9.9 fps "BD field" figure elsewhere in CLAUDE.md predates the Edge kernel
# merge and the LLVM backend, and may be a different part of the map. Do NOT
# treat 9.9 -> 17.5 as a measured improvement; they are not the same
# experiment.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
SEQ='start@20000:1500;a@28000:1500;a@34000:1500;a@40000:1500;start@46000:1500;a@52000:1500;a@58000:1500;start@64000:1500;a@70000:1500;a@76000:1500;a@82000:1500;start@88000:1500;a@94000:1500;a@100000:1500;a@106000:1500;start@112000:1500;a@118000:1500;a@124000:1500'
EXTRA="${EXTRA:-}"          # e.g. EXTRA='--ez cpu_llvm_vperm_tbx true'
SAMPLES="${SAMPLES:-15}"

temp() { "$ADB" -s "$DEV" shell 'cat /sys/class/kgsl/kgsl-3d0/temp' | tr -d '\r'; }

# --- pre-flight. Must ABORT, not print: the device is SHARED with an rpcs3
# --- session and a contended or hot run produces numbers that look valid.
busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs"' | tr -d '\r')
[ "$busy" = "0" ] || { echo "ABORT: rpcs3 is running - device is shared"; exit 1; }
t=$(temp); [ "$t" -lt 50000 ] || { echo "ABORT: GPU $((t/1000))C, need <50C"; exit 1; }
lvl=$("$ADB" -s "$DEV" shell dumpsys battery 2>/dev/null | grep -i 'level' | grep -oE '[0-9]+' | head -1)
[ "${lvl:-0}" -ge 30 ] || { echo "ABORT: battery ${lvl}%"; exit 1; }
echo "pre-flight OK: gpu $((t/1000))C, battery ${lvl}%"

NATIVE=$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | sed 's/^package://' | tr -d '\r')")/lib/arm64
DRV=$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ | tr -d '\r' | head -1)
FILES=/data/user/0/$PKG/files

"$ADB" -s "$DEV" shell am force-stop $PKG
"$ADB" -s "$DEV" logcat -c
"$ADB" -s "$DEV" shell input keyevent KEYCODE_WAKEUP >/dev/null
"$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity \
  --es target '$GAME' \
  --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true \
  --ez vulkan_trace_draw_outcomes_per_frame true \
  --es hid nop --es hid_nop_button_sequence '$SEQ' \
  $EXTRA \
  --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '$FILES/gpu_drivers/$DRV/' \
  --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
  --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1

T0=$(date +%s); prev=0
echo "t(s)  frames/10s   fps   gpuC"
for i in $(seq 1 "$SAMPLES"); do
  sleep 10
  n=$("$ADB" -s "$DEV" logcat -d 2>/dev/null | grep -c 'GPU draw outcomes')
  d=$((n - prev)); prev=$n
  tt=$(temp)
  printf "%4d  %8d  %5s  %3dC\n" "$(( $(date +%s) - T0 ))" "$d" \
         "$(awk "BEGIN{printf \"%.1f\", $d/10}")" "$((tt/1000))"
  if [ "$tt" -ge 70000 ]; then
    echo "THERMAL LIMIT 70C - force-stopping"; break
  fi
done

"$ADB" -s "$DEV" shell am force-stop $PKG; sleep 2
echo "stopped. pidof=[$("$ADB" -s "$DEV" shell pidof $PKG | tr -d '\r')] temp=$(( $(temp)/1000 ))C"
echo "faults: $("$ADB" -s "$DEV" logcat -d 2>/dev/null | grep -icE 'Fatal signal|SIGTRAP|Scudo')"
# A run that never rendered must ABORT, not report zeros - the stale-cache stall
# produced exactly that shape and it looked like a measurement.
[ "$prev" -gt 0 ] || { echo "VOID: no frames rendered at all"; exit 1; }
echo "scene at end:"
"$ADB" -s "$DEV" logcat -d 2>/dev/null | grep 'GPU draw outcomes' | tail -1 | \
  grep -oE 'rendered=[0-9]+|total_vertices=[0-9]+'
