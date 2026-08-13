#!/usr/bin/env bash
# Blue Dragon A/B for the spin-backoff subsystem, measured in FPS.
#
# WHY BD AND NOT BURNOUT: BD's field runs ~9.9 fps, far below any frame cap, so
# fps is a direct CPU metric. Burnout sits at its 60fps cap and cannot show a
# CPU win at all. (Same reasoning as bd_fma_fps_ab.sh, which this is modelled
# on - every guard below exists because it failed on 2026-08-08.)
#
# THE ARMS are the same build; only the cvars differ:
#   OFF : shipping defaults - all three levers off
#   ON  : collapse_ctr_spin_loops + park_memory_poll_loops + a64_park_spin_backoff
#
# The ON arm also runs with log_spin_loop_rejects so the collapse COUNT is
# captured at load time. A pass that instruments nothing is indistinguishable
# from one that is switched off, and on the PPC corpus this pass fired zero
# times - so the count is the point, not a nicety.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
SEQ='start@25000:1200;a@35000:1200;a@45000:1200;start@55000:1200;a@65000:1200;a@75000:1200;a@85000:1200;a@95000:1200;a@105000:1200;a@115000:1200;a@125000:1200'
COOL_TO=42000
SAMPLE_AFTER=140
SAMPLE_FOR=60
OUT="${OUT:-/c/Users/LEANER~1/AppData/Local/Temp/claude/C--Users-leanerdesigner-Documents-xenia-thor-workspace/11556ba1-8110-420c-b7ae-933f76cffaf1/scratchpad}"

say(){ echo "[$(date +%H:%M:%S)] $*"; }
# QUOTE the remote command. Unquoted, Git Bash rewrites /sys/... into a Windows
# path, the read returns nothing, and the thermal guard silently never fires.
gt(){ "$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d '\r'; }

# Read the driver off the device. The older route scripts hardcode an r7 build
# that is no longer installed, and a stale path falls back to the stock blob.
DRV="$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ 2>/dev/null | head -1 | tr -d '\r')"
[ -n "$DRV" ] || { say "ABORT: no Turnip driver installed"; exit 1; }
say "driver: $DRV"

require_device_free() {
  local other mine
  other=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
  [ "${other:-0}" = "0" ] || { say "ABORT: rpcs3 is running - device is SHARED"; return 1; }
  mine=$("$ADB" -s "$DEV" shell pidof $PKG 2>/dev/null | tr -d '\r')
  [ -z "$mine" ] || "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1
  return 0
}

arm() {
  local label="$1"; shift
  require_device_free || return 1
  local t
  for _ in $(seq 1 50); do t=$(gt); [ -n "$t" ] && [ "$t" -le "$COOL_TO" ] && break; say "  cooling ($((${t:-0}/1000))C)"; sleep 15; done
  local t0; t0=$(gt)
  "$ADB" -s "$DEV" shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null; "$ADB" -s "$DEV" logcat -c
  local nat; nat="$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | head -1 | sed 's/^package://' | tr -d '\r')")/lib/arm64"
  "$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity \
    --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true \
    --ez cpu_llvm_target_features_native true \
    --ez vulkan_trace_draw_outcomes_per_frame true \
    --es hid nop --es hid_nop_button_sequence '$SEQ' \
    --es gpu_vulkan_driver turnip \
    --es gpu_vulkan_driver_path '/data/data/$PKG/files/gpu_drivers/$DRV/' \
    --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
    --es gpu_vulkan_driver_hooks_path '$nat' $*" >/dev/null 2>&1
  # 240s, not the 120s the older harness hardcodes. Burnout was measured taking
  # 143s to load on this device on 2026-08-13, and a cold AOT compile under any
  # contention pushes BD past 120s too - so the old gate can abort a perfectly
  # good arm and report it as "the emulator never started".
  local ok=""
  for _ in $(seq 1 80); do
    "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep -q "Title name:" && { ok=1; break; }
    sleep 3
  done
  if [ -z "$ok" ]; then
    say "ABORT arm $label: no 'Title name:' in 120s - the emulator never started."
    "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null; return 1
  fi
  # Capture the collapse/park counts BEFORE the sample-window logcat clear -
  # the passes run at load time, so clearing first would erase the evidence.
  local boot col park
  boot=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null)
  col=$(echo "$boot" | grep -c "collapsed self-loop")
  park=$(echo "$boot" | grep -c "MemoryPollPark: instrumented")
  echo "$boot" | grep -E "SpinLoopBackoff|MemoryPollPark" > "$OUT/bdspin_${label}_passes.txt" 2>/dev/null
  say "arm $label: title up (collapsed=$col parked=$park); route ${SAMPLE_AFTER}s then sample ${SAMPLE_FOR}s"
  sleep "$SAMPLE_AFTER"
  "$ADB" -s "$DEV" logcat -c
  local peak=0
  for _ in $(seq 1 $((SAMPLE_FOR/5))); do
    t=$(gt); [ -n "$t" ] && [ "$t" -gt "$peak" ] && peak=$t
    [ -n "$t" ] && [ "$t" -gt 72000 ] && { say "  72C guard"; break; }
    sleep 5
  done
  local log n avg gms flt
  log=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null)
  n=$(echo "$log" | grep -c "GPU draw outcomes/frame")
  avg=$(awk -v f="$n" -v w="$SAMPLE_FOR" 'BEGIN{ if (w>0) printf "%.2f", f/w; else printf "n/a" }')
  gms=$(echo "$log" | grep -oE "guest_ms=[0-9.]+" | cut -d= -f2 | tail -40 \
        | awk '{s+=$1;c++} END{ if(c) printf "%.2f", s/c; else printf "n/a" }')
  flt=$(echo "$log" | grep -cE "UNHANDLED host fault|SIGTRAP|Scudo ERROR")
  local intruder fg
  intruder=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
  fg=$("$ADB" -s "$DEV" shell dumpsys activity activities 2>/dev/null | grep -m1 -E "ResumedActivity|topResumedActivity" | tr -d '\r')
  if [ "${intruder:-0}" != "0" ] || ! echo "$fg" | grep -q "$PKG"; then
    say "VOID arm $label: rpcs3 appeared mid-run or xenia lost the foreground"
    "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null; return 1
  fi
  "$ADB" -s "$DEV" exec-out screencap -p > "$OUT/bdspin_$label.png" 2>/dev/null
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null
  printf '%-4s cold=%sC peak=%sC fps=%s (frames=%s/%ss) guest_ms=%s faults=%s collapsed=%s parked=%s\n' \
    "$label" "$((t0/1000))" "$((peak/1000))" "$avg" "$n" "$SAMPLE_FOR" "$gms" "$flt" "$col" "$park" \
    | tee -a "$OUT/bdspin.txt"
}

: > "$OUT/bdspin.txt"
arm "OFF"
arm "ON" --ez collapse_ctr_spin_loops true --ez park_memory_poll_loops true \
         --ez a64_park_spin_backoff true --ez log_spin_loop_rejects true \
         --ez log_memory_poll_park true
echo; cat "$OUT/bdspin.txt"
