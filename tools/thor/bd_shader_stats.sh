#!/usr/bin/env bash
# Dump ir3's per-variant FRAGMENT SHADER stats for Blue Dragon's real shaders.
#
# WHY THIS IS THE DECIDING MEASUREMENT: BD is ~93% GPU-bound and 81.5% of its
# in-pass GPU time is TWO passes of ~890 alpha-blended, depth-writing draws.
# Everything else has been retired by measurement - EDRAM allocation (1.5%),
# GMEM/tiling (converges to parity, never wins), bandwidth (blend free, 2x
# bytes/pixel +8-10%, no cache cliff), LRZ (architectural, the guest's own
# blend+depth-write state). What is LEFT is the cost of the fragment shaders in
# those two passes, and nobody has ever measured them.
#
# ⚠ AND THE STATS WE ALREADY HAVE ARE OF THE WRONG SHADERS. A previous sample
# showed 8 variants at 17-96 instructions, 1-6 full registers, max_waves=16
# (FULL occupancy). XenDroid measured their worst at 2,195 instrs / 31 GPRs /
# 4 waves. Those cannot be the same population - the heavy pass runs ONE
# ps_hash across 671 draws, and nothing topping out at 96 instructions is it.
# So "our shaders are small and fully occupied" was concluded about resolve /
# transfer / UI shaders, not about the 81.5%.
#
# ⭐ WHY THIS RUN DIFFERS FROM THE ONE THAT VOIDED (2026-08-14): that attempt
# set MESA_SHADER_CACHE_DISABLE=true, which forces EVERY pipeline to recompile
# on EVERY use - during a load that already takes ~150s. It never reached the
# title in 240s. The instrument made the thing it was measuring too slow to
# reach.
#   THE FIX: do NOT disable the cache. Point it at a dir we control and DELETE
#   it once. Then each variant compiles EXACTLY ONCE (printing its stats) and
#   is cached normally, so the load is a normal cold-shader-cache load.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-192.168.1.33:5555}"
PKG=jp.xenia.emulator.github.debug
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
SEQ='start@25000:1200;a@35000:1200;a@45000:1200;start@55000:1200;a@65000:1200;a@75000:1200;a@85000:1200;a@95000:1200;a@105000:1200;a@115000:1200;a@125000:1200'
# Shaders compile on FIRST USE, so the field variants only appear once the route
# is actually in the field (~120-135s). Collect well past that.
RUN_S=${1:-150}
TITLE_WAIT_S=${TITLE_WAIT_S:-400}
OUT="${OUT:-$PWD/scratchpad}"
mkdir -p "$OUT" 2>/dev/null

say(){ echo "[$(date +%H:%M:%S)] $*"; }
# QUOTE the remote command. Unquoted, Git Bash rewrites /sys/... into
# C:/Program Files/Git/sys/... and the guard silently reads an empty string.
gt(){ "$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" | tr -d '\r'; }

cleanup() {
  # UNSET with '' - NEVER '""'. Setting the literal two-char value "" makes
  # Android treat it as a WRAPPER COMMAND, the app fails to exec, and EVERY
  # launch of this package is bricked until reboot. On a SHARED device that
  # silently poisons the next session's runs.
  "$ADB" -s "$DEV" shell "setprop wrap.$PKG ''" >/dev/null 2>&1
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1
  say "cleanup done (wrap unset, app stopped)"
}
trap cleanup EXIT

# ---- pre-flight. Every check ABORTS; a check that only prints is not a check.
busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
[ "${busy:-1}" = "0" ] || { say "ABORT: rpcs3 running - the device is SHARED"; exit 1; }
T=$(gt); case "$T" in ''|*[!0-9]*) say "ABORT: bad temp read"; exit 1;; esac
B=$("$ADB" -s "$DEV" shell dumpsys battery | grep -m1 level | grep -oE '[0-9]+')
say "pre-flight gpu=$((T/1000))C battery=${B}%"
[ "$T" -lt 45000 ] || { say "ABORT: too hot to start"; exit 1; }
[ "${B:-0}" -ge 25 ] || { say "ABORT: battery too low"; exit 1; }

DRV="$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ | head -1 | tr -d '\r')"
[ -n "$DRV" ] || { say "ABORT: no Turnip driver installed"; exit 1; }
say "driver = $DRV"
# Re-read the APK dir EVERY run: Android reinstalls under a fresh randomised
# path, and a stale hooks path reports the driver as "loaded" then finds ZERO
# physical devices - which reads exactly like a hang.
NATIVE="$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | head -1 | sed 's/^package://' | tr -d '\r')")/lib/arm64"

"$ADB" -s "$DEV" shell am force-stop $PKG
"$ADB" -s "$DEV" shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1

# ---- THE CACHE WIPE. This is what makes the stats appear at all.
SC="/data/data/$PKG/cache/m"
"$ADB" -s "$DEV" shell "run-as $PKG rm -rf $SC" >/dev/null 2>&1
say "wiped mesa shader cache ($SC) - every variant will compile once and print"

# BD compiles dozens of variants and this file records logcat EVICTING lines
# under heavy logging.
"$ADB" -s "$DEV" logcat -G 64M >/dev/null 2>&1
"$ADB" -s "$DEV" logcat -c

# NO double quotes inside the value, and the whole thing must be < 92 bytes or
# setprop refuses it and getprop returns EMPTY - a run that then looks normal in
# every other signal (fps, temp, faults, scene gate) and traces nothing.
WRAP="IR3_SHADER_DEBUG=fs MESA_SHADER_CACHE_DIR=$SC"
[ ${#WRAP} -lt 92 ] || { say "ABORT: wrap is ${#WRAP} bytes, limit 92"; exit 1; }
"$ADB" -s "$DEV" shell "setprop wrap.$PKG '$WRAP'"
GOT=$("$ADB" -s "$DEV" shell "getprop wrap.$PKG" | tr -d '\r')
say "wrap (${#WRAP}B) = $GOT"
case "$GOT" in *IR3_SHADER_DEBUG*) ;; *) say "ABORT: setprop did not apply"; exit 1;; esac

"$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' \
  --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true \
  --ez cpu_llvm_target_features_native true \
  --ez vulkan_trace_draw_outcomes_per_frame true \
  --es hid nop --es hid_nop_button_sequence '$SEQ' \
  --es gpu_vulkan_driver turnip \
  --es gpu_vulkan_driver_path '/data/data/$PKG/files/gpu_drivers/$DRV/' \
  --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
  --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null

# Gate on the TITLE, with a thermal guard while waiting - a cold-cache AOT can
# itself heat the device, and "alive" is not "progressing".
ok=""
for _ in $(seq 1 $((TITLE_WAIT_S / 5))); do
  "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep -q "Title name:" && { ok=1; break; }
  T=$(gt); case "$T" in ''|*[!0-9]*) say "ABORT: temp read failed while waiting"; exit 1;; esac
  [ "$T" -gt 70000 ] && { say "ABORT: 70C thermal guard during load"; exit 1; }
  sleep 5
done
[ -n "$ok" ] || { say "ABORT: no title in ${TITLE_WAIT_S}s - VOID, not a result"; exit 1; }

# ⭐⭐ THE GUARD THIS SCRIPT WAS MISSING, AND IT COST A RUN (2026-08-16).
# hid_nop_button_sequence timings are ABSOLUTE FROM LAUNCH. They are sized for a
# WARM object cache (title in ~15-20s). On a COLD cache the title took 280s, so
# every press fired into the loading screen, the route never left the title, and
# the capture was 20 title-screen variants - the exact WRONG POPULATION this
# script exists to avoid sampling.
#   LLVMobjload==0 means this run COMPILED the cache rather than using it, i.e.
#   it is a cache-warming run and CANNOT land the route. Abort and re-run.
OBJLOAD=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep -c "LLVMobjload")
if [ "${OBJLOAD:-0}" -eq 0 ]; then
  say "ABORT: LLVMobjload=0 - COLD object cache, so the route's absolute-timed"
  say "       presses fired during loading. This run WARMED the cache (good);"
  say "       re-run now and the title will come up in ~15s."
  exit 1
fi
say "object cache warm (LLVMobjload lines=$OBJLOAD)"
say "title reached; collecting ${RUN_S}s while the route drives into the field"

END=$((SECONDS + RUN_S))
while [ $SECONDS -lt $END ]; do
  sleep 15
  T=$(gt)
  case "$T" in ''|*[!0-9]*) say "ABORT: temp read failed mid-run"; break;; esac
  N=$("$ADB" -s "$DEV" shell "logcat -d -s MESA" 2>/dev/null | grep -c "FRAG prog")
  say "  t=${SECONDS}s gpu=$((T/1000))C frag_stat_lines=$N"
  # 70C, matching bd_gameplay_route.sh. An ad-hoc loop without this hit 71C.
  [ "$T" -gt 70000 ] && { say "THERMAL GUARD at 70C - stopping collection"; break; }
done

"$ADB" -s "$DEV" shell "logcat -d -s MESA" 2>/dev/null | tr -d '\r' > "$OUT/bd_mesa_raw.txt"
grep "FRAG prog" "$OUT/bd_mesa_raw.txt" > "$OUT/bd_frag_stats.txt"
say "captured $(wc -l < "$OUT/bd_frag_stats.txt") FRAG stat lines -> $OUT/bd_frag_stats.txt"
# A zero here is a VOID run (cache still warm / wrap not applied), NOT a finding.
[ -s "$OUT/bd_frag_stats.txt" ] || { say "VOID: no FRAG stats captured"; exit 1; }

# ⭐ SCENE GATE. The WHOLE POINT of this measurement is the FIELD's heavy
# fragment shaders. A title-screen capture is the wrong population and must
# never be reported as a result - that misread is what motivated this run in
# the first place. >50,000 verts/frame is the gameplay tier this repo uses.
PEAK=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null \
  | grep -oE "total_vertices=[0-9]+" | cut -d= -f2 | sort -n | tail -1)
say "peak verts/frame = ${PEAK:-0}"
if [ "${PEAK:-0}" -lt 50000 ]; then
  say "VOID: never reached gameplay tier (>50k verts). These are TITLE/MENU"
  say "      shaders, NOT the two heavy passes. Do not report them."
  exit 1
fi
python tools/thor/bd_shader_report.py "$OUT/bd_frag_stats.txt"
