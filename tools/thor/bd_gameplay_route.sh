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
# 🚨 THE TRAP THAT VOIDS A RUN SILENTLY - READ THIS BEFORE TRUSTING ANY NUMBER.
# The button timings below are ABSOLUTE MILLISECONDS FROM LAUNCH, so they assume
# the guest starts rendering within ~10s. That only holds with a FULLY WARM
# object cache. With a cold or PARTIALLY warm cache the guest does not render
# until t=120s+, every press fires into the loading screen, and the game sits on
# the title.
#
# THAT FAILURE DOES NOT LOOK LIKE A FAILURE. Observed 2026-08-09: a partly
# warmed cache produced "~32 fps, rock stable, 0 faults" that was entirely TITLE
# SCREEN. 32 fps is a believable baseline; the fps column cannot tell you which
# scene you were in. The scene gate at the bottom of this script exists for
# exactly that reason.
#
# ⇒ WARM THE CACHE FIRST AND LET AOT ACTUALLY FINISH: launch with no route, wait
# for "resumed main guest thread" PLUS ~60s, then force-stop. Stopping right
# after the resume line leaves the cache half-populated, which is what produced
# the void run above.
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

# ⚠️ RETURNS A SENTINEL, NEVER AN EMPTY STRING. If the device drops off adb the
# read yields "" and every numeric comparison downstream becomes a shell error
# that evaluates as FALSE - so `until [ $(temp) -lt 42000 ]` spins forever on a
# disconnected device instead of failing. That burned a full 10-minute timeout
# on 2026-08-10, and worse, the emulator it had launched was left RUNNING on a
# shared device because the script never reached its force-stop.
#
# 99999 is deliberately above every threshold: a missing read now reads as
# "too hot", so callers abort rather than proceed blind.
temp() {
  local t
  t=$("$ADB" -s "$DEV" shell 'cat /sys/class/kgsl/kgsl-3d0/temp' 2>/dev/null | tr -d '\r')
  case "$t" in
    ''|*[!0-9]*) echo 99999 ;;
    *) echo "$t" ;;
  esac
}

# Abort if the device is not reachable. Call before anything that launches or
# writes - "offline" must stop the run, not be treated as a slow read.
require_device() {
  local st
  st=$("$ADB" devices 2>/dev/null | awk -v d="$DEV" '$1==d {print $2}')
  [ "$st" = "device" ] || { echo "ABORT: device state '${st:-absent}'"; exit 1; }
}

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
last=$("$ADB" -s "$DEV" logcat -d 2>/dev/null | grep 'GPU draw outcomes' | tail -1)
echo "$last" | grep -oE 'rendered=[0-9]+|total_vertices=[0-9]+'
# SCENE GATE. A title screen draws a couple of hundred times and pushes under a
# thousand vertices; gameplay is ~1,200 draws and ~240,000 vertices. Without
# this gate a title-screen run reports "~32 fps, stable, 0 faults" - which is a
# perfectly plausible baseline and is what a partially-warm cache produced on
# 2026-08-09. fps alone CANNOT tell you which scene you measured.
verts=$(echo "$last" | grep -oE 'total_vertices=[0-9]+' | grep -oE '[0-9]+')
if [ "${verts:-0}" -lt 50000 ]; then
  echo "VOID: end scene has only ${verts:-0} vertices/frame - never reached gameplay."
  echo "      Cause is almost always a cold/partly-warm object cache: the button"
  echo "      timings are ABSOLUTE from launch, so a slow guest start makes every"
  echo "      press fire into the loading screen. Warm the cache fully first."
  exit 1
fi
echo "OK: gameplay-tier scene confirmed (${verts} vertices/frame)"
