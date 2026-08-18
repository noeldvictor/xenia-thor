#!/usr/bin/env bash
# TRY CANDIDATE FIXES FOR THE GEARS LOADING FREEZE, ONE ARM PER COOLDOWN.
#
# PASS = peak vertices climbs past the frozen loading screen (13,725) AND the
# >=30s stall lines stop appearing. Both are read from the same log, so an arm
# cannot pass on one signal and be quietly failing on the other.
#
# The freeze sets in ~20s after launch and needs NO route, so each arm is ~60s.
# a64 backend: it skips the LLVM AOT entirely (~10s to title instead of 320s).
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
PKG=jp.xenia.emulator.github.debug
GAME="${GAME:-/storage/2664-21DE/Roms/xbox360/Gears of War (World) (En,Fr,De,Es,It,Zh,Ko).iso}"
SECS="${SECS:-60}"
OUT=scratchpad/gears
mkdir -p "$OUT"
adb_(){ timeout 25 "$ADB" -s "$DEV" "$@"; }
say(){ echo "[$(date +%H:%M:%S)] $*"; }
temp(){ t=$(adb_ shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d '\r')
        case "$t" in ''|*[!0-9]*) echo 999999;; *) echo "$t";; esac; }

busy=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '\r')
[ "$busy" = "0" ] || { say "ABORT: rpcs3 running - shared device"; exit 1; }
BAT=$(adb_ shell "dumpsys battery" | tr -d '\r')
LVL=$(echo "$BAT" | sed -nE 's/^ *level: ([0-9]+)/\1/p')
CHG=$(echo "$BAT" | sed -nE 's/^ *status: ([0-9]+)/\1/p')
say "battery=${LVL}% status=${CHG}"
[ "${LVL:-0}" -ge 30 ] || [ "$CHG" = "2" ] || { say "ABORT: low battery"; exit 1; }

run_arm(){ # $1 label  $2 extra cvars
  local label="$1" extra="$2" T tt alive vt st
  say "===== $label ====="
  for i in $(seq 1 40); do T=$(temp); [ "$T" -le 50000 ] && break; sleep 10; done
  [ "$T" -le 50000 ] || { say "  ABORT: no cooldown"; return 1; }
  say "  start temp=$((T/1000))C"
  adb_ shell "am force-stop $PKG" >/dev/null
  adb_ logcat -G 64M >/dev/null 2>&1; adb_ logcat -c >/dev/null 2>&1
  adb_ shell "input keyevent KEYCODE_WAKEUP" >/dev/null
  local NATIVE DRV
  NATIVE=$(dirname "$(adb_ shell "pm path $PKG" | sed 's/^package://' | tr -d '\r')")/lib/arm64
  DRV=$(adb_ shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)
  [ -n "$DRV" ] || { say "  ABORT: no Turnip driver"; return 1; }
  adb_ shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm false --ez cpu_aot_maximize true --ez vulkan_trace_draw_outcomes_per_frame true --ei gpu_frame_limit_fps 30 $extra --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '/data/user/0/$PKG/files/gpu_drivers/$DRV/' --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1
  for i in $(seq 1 $((SECS/10))); do
    sleep 10; tt=$(temp); alive=$(adb_ shell "pidof $PKG" | tr -d '\r')
    vt=$(adb_ logcat -d -s xenia:* 2>/dev/null | grep -oE "total_vertices=[0-9]+" | grep -oE "[0-9]+" | sort -n | tail -1)
    st=$(adb_ logcat -d -s xenia:* 2>/dev/null | grep -c "has waited")
    say "  $((i*10))s $((tt/1000))C peakverts=${vt:-0} stalls=$st alive=$([ -n "$alive" ] && echo Y || echo N)"
    [ -z "$alive" ] && { say "  DIED"; break; }
    [ "$tt" -ge 70000 ] && { say "  70C guard"; break; }
  done
  adb_ logcat -d -s xenia:* > "$OUT/fix_${label}.log" 2>/dev/null
  adb_ shell "am force-stop $PKG" >/dev/null
  vt=$(grep -oE "total_vertices=[0-9]+" "$OUT/fix_${label}.log" | grep -oE "[0-9]+" | sort -n | tail -1)
  st=$(grep -c "has waited" "$OUT/fix_${label}.log")
  echo "  RESULT $label peakverts=${vt:-0} stalls=$st  (frozen baseline = 13725 / stalls>0)"
}

# Arms come from argv as "label=cvars" so a new candidate needs no edit.
if [ "$#" -gt 0 ]; then
  for a in "$@"; do run_arm "${a%%=*}" "${a#*=}"; done
else
  run_arm sched   "--ez guest_scheduler true"
  run_arm condvar "--ez threading_per_object_condvar true"
fi
