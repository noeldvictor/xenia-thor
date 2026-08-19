#!/usr/bin/env bash
# SPIN-BACKOFF A/B ON MAGNACARTA 2 - PERF **AND** WATTS.
#
# WHY THIS EXISTS: the whole spin/park subsystem ships DEFAULT-OFF -
# park_memory_poll_loops, collapse_ctr_spin_loops and a64_park_spin_backoff are
# all false - while its own cvar text says it is "aimed squarely at the power
# gap". RPCS3's arm64 work got 25% of a 60% win, and 25% better perf-per-watt,
# from exactly this class (unoptimized wait/spin handling). We have never run it.
#
# MC2 is the vehicle because it is UNCAPPED and its GPU frame is ~10 ms, i.e. it
# is the one title with CPU headroom to show a CPU-side win. BD cannot: it is
# ~93% GPU-bound, so every CPU lever there is capped at ~7%.
#
# WATTS ARE ONLY VALID UNPLUGGED. This project's protocol: cable OUT, adb over
# wifi, confirm `USB powered: false` and status Discharging, then compare the
# DELTA. A plugged-in current_now reading is fiction (it flips sign).
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
PKG=jp.xenia.emulator.github.debug
GAME="${GAME:-/storage/2664-21DE/Roms/xbox360/Magna Carta 2.m3u/MagnaCarta 2 (USA) (En,Fr) (Disc 1).iso}"
SEQ="${SEQ:-start@12000:1200;a@17000:1200;a@22000:1200;start@27000:1200;a@32000:1200;a@37000:1200;start@43000:1200;a@48000:1200;a@54000:1200;start@60000:1200;a@66000:1200;a@72000:1200;start@78000:1200;a@84000:1200;a@90000:1200}"
SECS="${SECS:-110}"; COOL="${COOL:-42000}"; GUARD="${GUARD:-70000}"
OUT=scratchpad/spin; mkdir -p "$OUT"
adb_(){ timeout 25 "$ADB" -s "$DEV" "$@"; }          # BOUNDED: a hung transport once left the guard inert at 78C
say(){ echo "[$(date +%H:%M:%S)] $*"; }
temp(){ t=$(adb_ shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d '\r')
        case "$t" in ''|*[!0-9]*) echo 999999;; *) echo "$t";; esac; }   # FAILS CLOSED
uw(){ i=$(adb_ shell "cat /sys/class/power_supply/battery/current_now" 2>/dev/null | tr -d '\r')
      v=$(adb_ shell "cat /sys/class/power_supply/battery/voltage_now" 2>/dev/null | tr -d '\r')
      case "$i$v" in ''|*[!0-9-]*) echo 0;; *) echo $(( (i<0?-i:i) / 1000 * v / 1000000 ));; esac; }

run_arm(){ # $1 label  $2 extra cvars
  local label="$1" extra="$2"
  busy=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '\r')
  [ "$busy" = "0" ] || { say "ABORT: rpcs3 running - shared device"; exit 1; }
  local lvl; lvl=$(adb_ shell "dumpsys battery | grep level" | grep -oE '[0-9]+' | tr -d '\r')
  [ "${lvl:-0}" -ge 30 ] || { say "ABORT: battery ${lvl}% below the 30% floor"; exit 1; }
  for i in $(seq 1 120); do T=$(temp); [ "$T" -le "$COOL" ] && break; sleep 10; done   # 20 min: back-to-back runs heat-soak the chassis and 400s was not enough
  [ "$T" -le "$COOL" ] || { say "ABORT: no cooldown to $((COOL/1000))C"; exit 1; }
  say "=== $label === cold=$((T/1000))C batt=${lvl}%"
  adb_ shell "am force-stop $PKG" >/dev/null; sleep 2
  adb_ logcat -G 64M >/dev/null 2>&1; adb_ logcat -c >/dev/null 2>&1
  adb_ shell "input keyevent KEYCODE_WAKEUP" >/dev/null
  local NATIVE DRV
  NATIVE=$(dirname "$(adb_ shell "pm path $PKG" | sed 's/^package://' | tr -d '\r')")/lib/arm64
  DRV=$(adb_ shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)   # NEVER hardcode: stale = silent Qualcomm blob
  [ -n "$DRV" ] || { say "ABORT: no Turnip driver"; exit 1; }
  adb_ shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm false --ez cpu_aot_maximize true --ez vulkan_trace_draw_outcomes_per_frame true --es hid nop --es hid_nop_button_sequence '$SEQ' $extra --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '/data/user/0/$PKG/files/gpu_drivers/$DRV/' --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1
  local n=$((SECS/10)) pw=0 pn=0
  for i in $(seq 1 $n); do
    sleep 10; tt=$(temp); alive=$(adb_ shell "pidof $PKG" | tr -d '\r')
    [ -z "$alive" ] && { say "  DIED at $((i*10))s"; break; }
    # MID-RUN contention check. A start-of-arm pre-flight is point-in-time: rpcs3
    # can appear later, take the foreground, and our activity then loses its
    # SurfaceView - the presenter drops every frame, the chassis stays cool, and
    # the arm reads like a huge power win. That exact thing voided a run here.
    mid=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '')
    [ "${mid:-0}" = "0" ] || { say "  VOID: rpcs3 appeared mid-run at $((i*10))s"; VOID=1; break; }
    [ "$tt" -ge "$GUARD" ] && { say "  70C guard at $((i*10))s"; break; }
    if [ $i -ge 5 ]; then w=$(uw); pw=$((pw+w)); pn=$((pn+1)); fi   # skip load window
    say "  $((i*10))s $((tt/1000))C ${w:-?}mW"
  done
  END=$(temp)
  adb_ logcat -d -s xenia:* > "$OUT/${label}.log" 2>/dev/null
  adb_ shell "am force-stop $PKG" >/dev/null
  local fr pv
  fr=$(grep -c "GPU draw outcomes" "$OUT/${label}.log" 2>/dev/null)
  pv=$(grep -oE "total_vertices=[0-9]+" "$OUT/${label}.log" 2>/dev/null | grep -oE "[0-9]+" | sort -n | tail -1)
  say "  end=$((END/1000))C  rise=$(((END-T)/1000))C  frames=$fr  peak_verts=${pv:-0}"
  if [ "${VOID:-0}" = "1" ] || [ "${fr:-0}" -lt 200 ] || [ "${pv:-0}" -lt 100000 ]; then
    say "  *** ARM VOID (contention or never reached gameplay) - do NOT report this as a result ***"
  fi
  echo "$label $((T/1000)) $((END/1000)) $([ $pn -gt 0 ] && echo $((pw/pn)) || echo 0)" >> "$OUT/power.txt"
}

: > "$OUT/power.txt"
[ "${ARM:-both}" = "spin" ] || run_arm base ""
# NOTE: log_memory_poll_park is deliberately NOT set here. It logs EVERY accept
# and reject - ~18,000 lines through logcat during AOT - so an arm carrying it
# differs from the baseline in TWO ways, and the logging alone can stall the
# emulator. Engagement was proven separately (350 instrumented loops); the
# measurement arm must differ ONLY in the levers.
if [ -n "${CUSTOM:-}" ]; then
  [ "${PAIR:-0}" = "1" ] && run_arm base ""
  run_arm "${LABEL:-custom}" "$CUSTOM"
else
  [ "${ARM:-both}" = "base" ] || run_arm spin "--ez park_memory_poll_loops true --ez collapse_ctr_spin_loops true --ez a64_park_spin_backoff true"
fi
echo; echo "=== ENGAGEMENT (a flat result from a lever that never fired is VOID) ==="
grep -ciE "MemoryPollPark|SPIN_BACKOFF" "$OUT/spin.log" 2>/dev/null || echo 0
echo "  instrumented loops: $(grep -c "instrumented poll loop" "$OUT/spin.log" 2>/dev/null || echo 0)"
echo "  top reject reasons:"; grep -oE "rejected: [a-z_]+" "$OUT/spin.log" 2>/dev/null | sort | uniq -c | sort -rn | head -5
