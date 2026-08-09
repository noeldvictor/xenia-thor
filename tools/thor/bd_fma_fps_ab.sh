#!/usr/bin/env bash
# Blue Dragon A/B for the scalar-FMA LLVM lowering, measured in FPS.
#
# WHY FPS AND NOT entry_delta: entry_delta is emitted by A64Emitter and counts
# ONLY a64-compiled functions, so under the shipping LLVM config it reports the
# fallback slice - proven 2026-08-08, 14.1M vs 130.6M on one flag. Blue Dragon's
# field runs ~9.9 fps, FAR below any frame cap, so fps is a direct and valid CPU
# metric here. (It is NOT valid on Burnout, which sits at its 60fps cap and
# cannot show a CPU win at all.)
#
# THE ARMS are the same build; only the lowering differs:
#   ON  : default - scalar MUL_ADD/MUL_SUB lowered in LLVM (828 more functions
#         stay on LLVM and keep register residency)
#   OFF : --es cpu_backend_llvm_skip_opcodes 77,78 forces those opcodes back to
#         the a64 fallback = the behaviour before the change
# 77/78 are OPCODE_MUL_ADD/OPCODE_MUL_SUB (resolved from hir/opcodes.h).
#
# WHAT THIS IS TESTING, honestly: the new lowering is ~16 insns vs a64's ~8-insn
# branchy fast path, so per-FMA it is HEAVIER. The bet is that keeping the
# function on LLVM pays for it. If OFF wins, the fix is the branchy shape, not a
# revert.
#
# EVERY GUARD HERE EXISTS BECAUSE IT FAILED ON 2026-08-08:
#  - require_device_free ABORTS: the Thor is shared with an rpcs3 session, and a
#    check that only prints is not a check (I ran on top of theirs once).
#  - no-title ABORTS: a run that never prints "Title name:" once slept anyway and
#    reported llvm=0 a64=0 fps=[] - numbers that look like data and mean "the
#    emulator never started". Two arms, eleven minutes, nothing.
#  - NO census cvars: cpu_llvm_guest_entry_census + a huge fallback budget are the
#    prime suspects for that stall. Do not instrument a perf run with them.
#  - force-stop BEFORE and AFTER: leftovers skew the cold gate and burn the other
#    session's thermal budget.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
DRV=mesa-turnip-v26.3.0-20260803-r7-vulkan-1.4.354-7
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
# BD reaches the heavy field at ~120-135s. UNVERIFIED as a route - if the fps
# samples come back near the menu rate, the run did not reach the field and the
# numbers do not count (in-game only).
SEQ='start@25000:1200;a@35000:1200;a@45000:1200;start@55000:1200;a@65000:1200;a@75000:1200;a@85000:1200;a@95000:1200;a@105000:1200;a@115000:1200;a@125000:1200'
COOL_TO=42000
SAMPLE_AFTER=140     # s after title before sampling - past the route, in the field
SAMPLE_FOR=60        # s of fps samples

say(){ echo "[$(date +%H:%M:%S)] $*"; }
gt(){ "$ADB" -s "$DEV" shell 'cat /sys/class/kgsl/kgsl-3d0/temp' 2>/dev/null | tr -d '\r'; }

require_device_free() {
  local other mine
  other=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
  [ "${other:-0}" = "0" ] || { say "ABORT: rpcs3 is running - device is SHARED"; return 1; }
  mine=$("$ADB" -s "$DEV" shell pidof $PKG 2>/dev/null | tr -d '\r')
  [ -z "$mine" ] || { say "force-stopping stale xenia ($mine)"; "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1; }
  return 0
}

arm() {                     # $1 = label, $2.. = extra extras
  local label="$1"; shift
  require_device_free || return 1
  local t
  for _ in $(seq 1 50); do t=$(gt); [ -n "$t" ] && [ "$t" -le "$COOL_TO" ] && break; sleep 15; done
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
  # HARD GATE: no title in 120s => the run is void, say so and stop.
  local ok=""
  for _ in $(seq 1 40); do
    "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep -q "Title name:" && { ok=1; break; }
    sleep 3
  done
  if [ -z "$ok" ]; then
    say "ABORT arm $label: no 'Title name:' in 120s - the emulator never started."
    "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null; return 1
  fi
  say "arm $label: title up, driving the route (${SAMPLE_AFTER}s) then sampling ${SAMPLE_FOR}s"
  sleep "$SAMPLE_AFTER"
  # Clear the log HERE so the frame count covers exactly the sample window and
  # not the whole boot - otherwise fps = frames/window is meaningless.
  "$ADB" -s "$DEV" logcat -c
  local peak=0
  for _ in $(seq 1 $((SAMPLE_FOR/5))); do
    t=$(gt); [ -n "$t" ] && [ "$t" -gt "$peak" ] && peak=$t
    [ -n "$t" ] && [ "$t" -gt 72000 ] && { say "  72C guard"; break; }
    sleep 5
  done
  # FPS IS NOT IN LOGCAT AS A STRING. The on-screen number is drawn by the JAVA
  # overlay from nativeGetGuestSwapCount (EmulatorActivity.java:1962); xenia never
  # logs "N.N FPS", so grepping for it returns 0 and looks like a dead run - that
  # cost an arm on 2026-08-08.
  #
  # But `vulkan_trace_draw_outcomes_per_frame` emits ONE "GPU draw outcomes/frame"
  # line PER FRAME, carrying guest_ms and gpu_frame_us. So:
  #   frames in the sample window / window seconds = FPS   (automatable, exact)
  #   mean guest_ms                                = guest CPU time per frame,
  #                                                  which is what an FMA change
  #                                                  should actually move
  # Counting LINES is robust; this file warns that gpu_frame_us in ABSOLUTE terms
  # is unreliable because it includes idle, so guest_ms is reported alongside
  # rather than instead of the frame count.
  local log n avg gms
  log=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null)
  n=$(echo "$log" | grep -c "GPU draw outcomes/frame")
  avg=$(awk -v f="$n" -v w="$SAMPLE_FOR" 'BEGIN{ if (w>0) printf "%.2f", f/w; else printf "n/a" }')
  gms=$(echo "$log" | grep -oE "guest_ms=[0-9.]+" | cut -d= -f2 | tail -40 \
        | awk '{s+=$1;c++} END{ if(c) printf "%.2f", s/c; else printf "n/a" }')
  local flt; flt=$(echo "$log" | grep -cE "UNHANDLED host fault|SIGTRAP|Scudo ERROR")
  # MID-RUN INTRUSION CHECK. require_device_free is a point-in-time gate; on
  # 2026-08-08 rpcs3 started DURING an arm, so the arm was contended AND the
  # screencap captured THEIR foreground game (Folklore, with the rpcs3 overlay).
  # An arm that was not alone is void - say so rather than reporting a number.
  local intruder fg
  intruder=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '')
  fg=$("$ADB" -s "$DEV" shell dumpsys activity activities 2>/dev/null | grep -m1 -E "ResumedActivity|topResumedActivity" | tr -d '')
  if [ "${intruder:-0}" != "0" ] || ! echo "$fg" | grep -q "$PKG"; then
    say "VOID arm $label: rpcs3 appeared mid-run or xenia lost the foreground"
    say "  foreground was: $fg"
    "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null; return 1
  fi
  "$ADB" -s "$DEV" shell screencap -p //sdcard/bdfma_$label.png >/dev/null 2>&1
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null
  printf '%-6s cold=%sC peak=%sC fps=%s (frames=%s/%ss) guest_ms=%s faults=%s\n' \
    "$label" "$((t0/1000))" "$((peak/1000))" "$avg" "$n" "$SAMPLE_FOR" "$gms" "$flt" \
    | tee -a /tmp/bdfma.txt
}

: > /tmp/bdfma.txt
arm "ON"
arm "OFF" "--es cpu_backend_llvm_skip_opcodes 77,78"
"$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1
echo
say "RESULTS (BD field, fps - higher is better):"
cat /tmp/bdfma.txt
cat <<'NOTE'

READ IT LIKE THIS:
  ON  > OFF  -> the coverage win pays for the heavier per-FMA sequence. Keep it.
  OFF > ON   -> the sequence costs more than coverage buys. Do NOT revert the
                lowering; emit the BRANCHY shape instead (early-out on
                "no source is NaN" around a bare llvm.fma, mirroring a64), which
                keeps the coverage and drops the cost.
  fps ~equal -> unresolved at this sample size; drift on this device is ~2.8%.
  screenshots at /sdcard/bdfma_ON.png / _OFF.png - CHECK PIXELS, a float bug is
  wrong output, not a crash.
NOTE
