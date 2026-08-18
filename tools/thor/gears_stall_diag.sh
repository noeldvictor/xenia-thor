#!/usr/bin/env bash
# GEARS ACT-1 STALL DIAGNOSTIC - name the objects five guest threads park on.
#
# WHAT IT ANSWERS: this tree records five threads blocked 30/60/90s on events
# (XObject type 2) that NOTHING in a 6,003-line event trace ever touches. The
# enhanced stall log (XObject::Wait) now prints handle / guest_object / origin,
# which splits the only fork that matters:
#
#   guest_object != 0, origin=guest-native  -> the dispatcher header lives in
#       GUEST memory (the XDK inlined KeInitialize; we wrapped it on first
#       use). Nobody in OUR code owns it - we are failing to DELIVER a
#       completion or APC that the guest is waiting for.
#   guest_object == 0, origin=Nt.../xam...  -> our own HLE made it and never
#       signalled it. The origin string names the export.
#
# WHY NOT THE "Added handle for typeid" PLAN THIS REPO HAD SCOPED: all four
# XEvent creation sites share one typeid, so it prints XEvent five times. It
# also needs log_level=Debug, and Debug floods here (~135 handle adds/sec of
# async-I/O churn on UE3 asset loads) until logcat EVICTS the very lines it was
# raised to capture. The stall log is ~10 lines, only on the bug, at the
# default level.
#
# THREE TRAPS THE OLD gears_gameplay_route.sh CARRIES - ALL VERIFIED LIVE ON
# THIS DEVICE 2026-08-17, ALL FIXED HERE:
#  1. DRV hardcoded to ...-r7-...; the device has -r11. A stale driver path
#     does not error - it SILENTLY FALLS BACK TO THE QUALCOMM BLOB, which is
#     the "Turnip is mandatory" trap and reads like a rendering bug.
#     -> read the driver name off the device.
#  2. "adb shell cat /sys/..." UNQUOTED. Git Bash rewrites the path and the
#     device replies "cat: C:/Program: No such file". The script then parsed an
#     empty string as the temperature and THE 70C GUARD NEVER EVALUATED.
#     -> quote every remote command, and make the guard FAIL CLOSED.
#  3. nativeLibraryDir captured once. Android reinstalls under a fresh random
#     directory, so a path from before an install is dead - and it fails
#     MISLEADINGLY (driver "loaded", then "No Vulkan physical devices
#     available", i.e. a black screen that looks like a GPU bug).
#     -> derive it fresh, after install.
#
# AND THE COLD-CACHE TRAP: hid_nop timings are ABSOLUTE FROM LAUNCH. Gears is
# ~28.5k functions; on a cold cache it is still compiling at t=180s, so every
# button press lands in the loading screen and the route never leaves the
# title. An APK rebuild moves the object-cache stamp and prunes the cache, so
# A WARM-UP PHASE IS MANDATORY AFTER EVERY BUILD. Phase 1 does it, and phase 2
# ABORTS rather than reporting a route that never ran.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
GAME="/storage/2664-21DE/Roms/xbox360/Gears of War (World) (En,Fr,De,Es,It,Zh,Ko).iso"
# Device-verified route into the Act 1 prison corridor at ~125-150s.
SEQ="${SEQ:-start@40000:1200;start@47000:1200;start@53000:1200;start@59000:1200;start@65000:1200;start@71000:1200;a@79000:1200;a@86000:1200;a@93000:1200;a@100000:1200;a@107000:1200;a@114000:1200}"
EXTRA="${EXTRA:-}"        # extra launch cvars, e.g. a frame cap for thermal relief
COOL="${COOL:-40000}"     # milli-C. Gears runs hot; start cold or it guards out early.
GUARD="${GUARD:-70000}"   # hard force-stop limit, per the standing thermal rule
OUT=scratchpad/gears
mkdir -p "$OUT"

say(){ echo "[$(date +%H:%M:%S)] $*"; }

# QUOTED, and returns a sentinel that fails the guard CLOSED on a bad read.
temp(){ t=$("$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d "\r")
        case "$t" in ""|*[!0-9]*) echo 999999;; *) echo "$t";; esac; }

preflight(){
  busy=$("$ADB" -s "$DEV" shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d "\r")
  [ "$busy" = "0" ] || { say "ABORT: rpcs3 is running - shared device"; exit 1; }
  B=$("$ADB" -s "$DEV" shell "dumpsys battery | grep level" | tr -d "\r" | grep -oE "[0-9]+" | head -1)
  # grep "powered" alone matches "AC powered: false" FIRST and reports a
  # CHARGING device as discharging, which would abort a perfectly safe run.
  CH=$("$ADB" -s "$DEV" shell "dumpsys battery" | grep -E "USB powered|AC powered" | grep -q "true" && echo true || echo false)
  T=$(temp)
  say "pre-flight: temp=$((T/1000))C battery=${B}% charging=${CH}"
  if [ "${B:-0}" -lt 30 ] && [ "$CH" != "true" ]; then
    say "ABORT: battery ${B}% and not charging"; exit 1; fi
}

cooldown(){
  t=999999
  for i in $(seq 1 160); do t=$(temp); [ "$t" -le "$COOL" ] && break; sleep 10; done
  [ "$t" -le "$COOL" ] || { say "ABORT: no cooldown to $((COOL/1000))C (stuck at $((t/1000))C)"; return 1; }
  say "cold at $((t/1000))C"; return 0
}

launch(){ # $1 = extra args
  "$ADB" -s "$DEV" shell "am force-stop $PKG" >/dev/null
  "$ADB" -s "$DEV" logcat -G 64M >/dev/null 2>&1
  "$ADB" -s "$DEV" logcat -c >/dev/null 2>&1
  "$ADB" -s "$DEV" shell "input keyevent KEYCODE_WAKEUP" >/dev/null
  # Trap 3: derive these AFTER any install, never cache them across runs.
  NATIVE=$(dirname "$("$ADB" -s "$DEV" shell "pm path $PKG" | sed "s/^package://" | tr -d "\r")")/lib/arm64
  DRV=$("$ADB" -s "$DEV" shell "run-as $PKG ls files/gpu_drivers/" | tr -d "\r" | head -1)
  FILES=/data/user/0/$PKG/files
  [ -n "$DRV" ] || { say "ABORT: no Turnip driver installed"; exit 1; }
  say "driver=$DRV"
  "$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '$FILES/gpu_drivers/$DRV/' --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE' $1" >/dev/null 2>&1
}

# ---------------------------------------------------------------- phase 1: warm
if [ "${SKIP_WARM:-0}" != "1" ]; then
  preflight; cooldown || exit 1
  say "PHASE 1: warming the Gears object cache (~28.5k functions, expect minutes)"
  launch ""
  for i in $(seq 1 60); do
    sleep 10; tt=$(temp)
    if [ "$tt" -ge "$GUARD" ]; then say "  70C guard at t=$((i*10))s"; break; fi
    L=$("$ADB" -s "$DEV" logcat -d -s xenia:* 2>/dev/null)
    title=$(echo "$L" | grep -c "Title name:")
    prog=$(echo "$L" | grep -oE "AOT precompile progress: [0-9]+" | tail -1 | grep -oE "[0-9]+$")
    say "  $((i*10))s $((tt/1000))C title=$title aot=${prog:-0}"
    if [ "$title" -gt 0 ]; then say "  title reached - cache warm"; break; fi
  done
  "$ADB" -s "$DEV" shell "am force-stop $PKG" >/dev/null
fi

# ------------------------------------------------------------ phase 2: diagnose
preflight; cooldown || exit 1
say "PHASE 2: route into Act 1 with the stall diagnostic"
launch "--es hid nop --es hid_nop_button_sequence '$SEQ' $EXTRA"
for i in $(seq 1 42); do
  sleep 10; tt=$(temp)
  L=$("$ADB" -s "$DEV" logcat -d -s xenia:* 2>/dev/null)
  ol=$(echo "$L" | grep -c "LLVMobjload")
  title=$(echo "$L" | grep -c "Title name:")
  stalls=$(echo "$L" | grep -c "has waited")
  say "  $((i*10))s $((tt/1000))C title=$title objload=$ol stalls=$stalls"
  # Cold-cache trap: absolute-timed presses cannot land while we are still
  # compiling. Fail loudly instead of reporting a route that never ran.
  if [ "$i" -ge 4 ] && [ "$title" -eq 0 ] && [ "$ol" -eq 0 ]; then
    say "ABORT: cold cache (objload=0, no title by $((i*10))s) - the route cannot land"
    "$ADB" -s "$DEV" shell "am force-stop $PKG" >/dev/null; exit 1
  fi
  if [ "$stalls" -gt 0 ] && [ "$i" -ge 6 ]; then say "  stall lines captured"; break; fi
  if [ "$tt" -ge "$GUARD" ]; then say "  70C guard at t=$((i*10))s"; break; fi
done
"$ADB" -s "$DEV" logcat -d -s xenia:* > "$OUT/gears_stall.log" 2>/dev/null
"$ADB" -s "$DEV" shell "am force-stop $PKG" >/dev/null
say "force-stopped; log -> $OUT/gears_stall.log ($(wc -l < "$OUT/gears_stall.log") lines)"

echo
echo "================ STALLED WAITS ================"
grep "has waited" "$OUT/gears_stall.log" | sed "s/^.*XObject::Wait/XObject::Wait/" | sort -u
echo
echo "================ DISTINCT OBJECTS ============="
grep -oE "handle=[0-9A-F]+ guest_object=[0-9A-F]+ origin=[^ ]+" "$OUT/gears_stall.log" | sort | uniq -c | sort -rn
echo
echo "(no lines above = the run never reached the stall; check title/objload)"
