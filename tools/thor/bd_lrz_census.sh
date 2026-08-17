#!/usr/bin/env bash
# Blue Dragon per-pass COMPOSITION census - does the LRZ hack have anything to
# recover, and does the route reach the scenes where BD is actually slow?
#
# WHY THIS EXISTS AND IS NOT bd_gameplay_route.sh:
#
# 1. THE ROUTE'S 50C COLD GATE IS TOO WARM TO REACH THE HEAVY SCENES. This is
#    measured, not guessed: the 2026-08-17 clamp A/B recorded that at a 42C
#    cooldown the 70C guard ended runs before the deep scenes and the
#    180k-300k vertex buckets carried n=9-12 - too thin to support any
#    conclusion, and the first attempt read +7.4%/-1.2% with the signs
#    DISAGREEING. Cooling to 37-38C first took those same buckets to
#    n=248-356. So a run that stops at ~158k verts has not found a lighter
#    game, it has run out of thermal budget. This script gates at 38C.
#
# 2. IT REPORTS THE VERTEX DISTRIBUTION, NOT JUST THE LAST FRAME. The route's
#    scene gate reads the LAST frame only, which says where the run ENDED and
#    nothing about whether it ever reached the heavy buckets. A run can peak at
#    260k and end at 158k on the way back out of a scene, and the route would
#    report only the 158k.
#
# 3. IT AGGREGATES THE BIGPASS COMPOSITION COUNTERS. Those counters exist to
#    answer ONE question and it is worth stating the decision rule in advance,
#    so the result cannot be rationalised after the fact:
#
#        recoverable = zwrite_after_blend - zwrite_masked_after
#
#    ZERO   -> the LRZ hack is DEAD for BD. Every depth-write that follows a
#              blend in the pass is already masked, so suppressing depth-write
#              on blended draws cannot restore LRZ write. Drop the hack.
#    NON-0  -> that is the size of the population the hack can act on.
#
# ⚠️ THE COUNTERS ARE ONLY MEANINGFUL WITH THE 2026-08-17 ORDERING FIX. Before
# it, the pass-teardown path reset them BEFORE MaybeLogSmallGuestPass ran, so
# every BIGPASS line reported blend_draws=0 on a 194-draw blended pass.
# pass_draws survived only because it is computed from a mark rather than a
# member. If blend_draws is 0 on a pass whose blendctl0 is non-trivial, you are
# running an APK from before that fix - rebuild, do not interpret.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
# Overridable, because the thermal budget and the route length are in direct
# tension and which one binds depends on the start temperature. The default is
# the PROVEN sequence from bd_gameplay_route.sh - do not "improve" it casually,
# it is device-captured and its timings are ABSOLUTE MILLISECONDS FROM LAUNCH.
SEQ="${SEQ:-start@20000:1500;a@28000:1500;a@34000:1500;a@40000:1500;start@46000:1500;a@52000:1500;a@58000:1500;start@64000:1500;a@70000:1500;a@76000:1500;a@82000:1500;start@88000:1500;a@94000:1500;a@100000:1500;a@106000:1500;start@112000:1500;a@118000:1500;a@124000:1500}"
EXTRA="${EXTRA:-}"
SAMPLES="${SAMPLES:-18}"
# 37C, not the route's 50C, and the difference decides whether this run means
# anything. The measured budget: a 45C start reached the 70C guard at t=46s -
# and the route does not reach gameplay until t=68s, so that run stopped
# BEFORE the scene it was launched to sample and ended at 158k vertices. The
# heat is front-loaded (45C -> 63C in the first 11s, which is the load), so
# every degree at the start buys most of a second at the end.
COLD="${COLD:-37000}"
OUT="${OUT:-scratchpad/bd_lrz_census.log}"

# Sentinel, never empty - an unreachable device must read as TOO HOT so callers
# abort rather than spin. See bd_gameplay_route.sh for the incident this comes
# from (a disconnected device spun a cooldown loop for a full timeout while the
# emulator it had launched stayed running on shared hardware).
temp() {
  local t
  t=$("$ADB" -s "$DEV" shell 'cat /sys/class/kgsl/kgsl-3d0/temp' 2>/dev/null | tr -d '\r')
  case "$t" in ''|*[!0-9]*) echo 99999 ;; *) echo "$t" ;; esac
}

st=$("$ADB" devices 2>/dev/null | awk -v d="$DEV" '$1==d {print $2}')
[ "$st" = "device" ] || { echo "ABORT: device state '${st:-absent}'"; exit 1; }

busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs"' | tr -d '\r')
if [ "$busy" != "0" ]; then
  if [ "${ALLOW_SHARED:-0}" = "1" ]; then
    echo "WARNING: rpcs3 running, ALLOW_SHARED=1 - this run is CONTENDED."
  else
    echo "ABORT: rpcs3 is running - device is shared"; exit 1
  fi
fi

lvl=$("$ADB" -s "$DEV" shell dumpsys battery 2>/dev/null | grep -i 'level' | grep -oE '[0-9]+' | head -1)
[ "${lvl:-0}" -ge 30 ] || { echo "ABORT: battery ${lvl}%"; exit 1; }

# Cooldown to the COLD gate. Bounded, and it ABORTS rather than proceeding warm -
# a warm start does not fail, it silently produces the thin heavy buckets this
# script exists to avoid.
t=$(temp)
if [ "$t" -ge "$COLD" ]; then
  echo "cooling from $((t/1000))C to $((COLD/1000))C (device idle, up to 20 min)"
  for i in $(seq 1 80); do
    sleep 15
    t=$(temp)
    [ "$t" -lt 99999 ] || { echo "ABORT: lost the device while cooling"; exit 1; }
    printf "  %3dm%02ds  %3dC\n" $(( i*15/60 )) $(( i*15%60 )) $((t/1000))
    [ "$t" -lt "$COLD" ] && break
  done
fi
[ "$t" -lt "$COLD" ] || { echo "ABORT: GPU $((t/1000))C, need <$((COLD/1000))C"; exit 1; }
echo "pre-flight OK: gpu $((t/1000))C, battery ${lvl}%"

NATIVE=$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | sed 's/^package://' | tr -d '\r')")/lib/arm64
DRV=$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ | tr -d '\r' | head -1)
FILES=/data/user/0/$PKG/files

"$ADB" -s "$DEV" shell am force-stop $PKG
# Raise the buffer OURSELVES rather than inheriting whatever the last run left.
# BIGPASS is per-pass and BD runs ~74 passes/frame, so a 256K buffer evicts the
# early frames; and the route's own frame counting reads NEGATIVE fps once
# eviction starts, because it counts cumulatively over the whole buffer.
"$ADB" -s "$DEV" logcat -G 64M >/dev/null 2>&1
"$ADB" -s "$DEV" logcat -c
"$ADB" -s "$DEV" shell input keyevent KEYCODE_WAKEUP >/dev/null
"$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity \
  --es target '$GAME' \
  --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true \
  --ez vulkan_trace_draw_outcomes_per_frame true \
  --ez gpu_trace_resolve_timing true \
  --es hid nop --es hid_nop_button_sequence '$SEQ' \
  $EXTRA \
  --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '$FILES/gpu_drivers/$DRV/' \
  --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
  --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1

T0=$(date +%s); prev=0; peak=0
echo "t(s)  frames/10s   fps   peakVerts  gpuC"
for i in $(seq 1 "$SAMPLES"); do
  sleep 10
  n=$("$ADB" -s "$DEV" logcat -d 2>/dev/null | grep -c 'GPU draw outcomes')
  d=$((n - prev)); prev=$n
  # Peak, not last. A run can crest at 260k and end at 158k on the way out of a
  # scene; reporting only the last frame hides that it ever got there.
  v=$("$ADB" -s "$DEV" logcat -d 2>/dev/null | grep -oE 'total_vertices=[0-9]+' \
      | grep -oE '[0-9]+' | sort -n | tail -1)
  [ "${v:-0}" -gt "$peak" ] && peak=${v:-0}
  tt=$(temp)
  printf "%4d  %8d  %5s  %9d  %3dC\n" "$(( $(date +%s) - T0 ))" "$d" \
         "$(awk "BEGIN{printf \"%.1f\", $d/10}")" "$peak" "$((tt/1000))"
  [ "$tt" -ge 99999 ] && { echo "ABORT: lost the device mid-run"; break; }
  [ "$tt" -ge 70000 ] && { echo "THERMAL LIMIT 70C - force-stopping"; break; }
done

# Pull BEFORE force-stop where possible, and RETRY. A pull issued right after
# force-stop can come back EMPTY - observed 0 bytes, then 1,342 lines seconds
# later. An empty pull is a RACE, not "the run produced nothing", and it
# presents exactly like a void arm.
mkdir -p "$(dirname "$OUT")"
for a in 1 2 3 4 5; do
  "$ADB" -s "$DEV" logcat -d > "$OUT" 2>/dev/null
  [ -s "$OUT" ] && break
  echo "  logcat pull empty (attempt $a) - retrying"; sleep 3
done

"$ADB" -s "$DEV" shell am force-stop $PKG; sleep 2
echo "stopped. pidof=[$("$ADB" -s "$DEV" shell pidof $PKG | tr -d '\r')] temp=$(( $(temp)/1000 ))C"
echo "faults: $(grep -icE 'Fatal signal|SIGTRAP|Scudo' "$OUT")"
[ -s "$OUT" ] || { echo "VOID: could not pull logcat"; exit 1; }
[ "$prev" -gt 0 ] || { echo "VOID: no frames rendered at all"; exit 1; }

python tools/thor/bd_lrz_report.py "$OUT"
