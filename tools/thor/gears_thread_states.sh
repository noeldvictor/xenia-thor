#!/usr/bin/env bash
# WHICH THREADS ARE ACTUALLY RUNNING DURING THE GEARS FREEZE?
#
# WHY THIS EXISTS: the stall log names threads parked in an INFINITE wait, and
# it found three-to-five of them all blocked at the same guest call site
# (guest_lr=82613DE0). But the MAIN guest thread never appears - not in the
# single-object log and not in the multi-object log either (that tripwire was
# added and came back empty). So the main thread is NOT parked on a kernel
# object at all. It is either spinning in guest code, or looping on a FINITE
# wait, and neither of those logs anything.
#
# That is exactly the Lost Odyssey signature this repo already recorded: "main
# thread polls a guest flag in a KeDelayExecutionThread wrapper; workers idle".
#
# THE DISCRIMINATOR IS CPU TICKS, NOT LOGS. Sample every thread twice and diff:
#   ticks advancing + vctx flat   -> SPINNING (burning CPU, making no progress)
#   ticks flat + vctx advancing   -> parking and waking repeatedly (finite wait)
#   ticks flat + vctx flat        -> genuinely blocked, never woken
# This is the method that inverted a wrong "the CP thread never ran" verdict
# before: CPU ticks alone cannot tell "never started" from "correctly parked",
# and voluntary_ctxt_switches separates them.
#
# TWO PARSING TRAPS, both already paid for in this repo:
#  - /proc/<tid>/stat CANNOT be indexed as $14/$15: comm contains spaces AND
#    parentheses. Count from the END (NF-37, NF-36).
#  - A shell `case` pattern containing an escaped space collapses through
#    nested adb quoting and silently matches nothing, which once made every
#    thread read as 0 ticks. Filter LOCALLY instead of with a remote pattern.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
GAME="/storage/2664-21DE/Roms/xbox360/Gears of War (World) (En,Fr,De,Es,It,Zh,Ko).iso"
GUARD="${GUARD:-70000}"
T1="${T1:-45}"    # first sample, after the freeze has set in (~t=27s)
T2="${T2:-60}"    # second sample
OUT=scratchpad/gears
mkdir -p "$OUT"
say(){ echo "[$(date +%H:%M:%S)] $*"; }
temp(){ t=$("$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d '\r')
        case "$t" in ''|*[!0-9]*) echo 999999;; *) echo "$t";; esac; }

busy=$("$ADB" -s "$DEV" shell "ps -A -o NAME | grep -icE rpcs" | tr -d '\r')
[ "$busy" = "0" ] || { say "ABORT: rpcs3 running"; exit 1; }
T=$(temp); say "pre-flight temp=$((T/1000))C"
[ "$T" -le 45000 ] || { say "ABORT: too hot to start ($((T/1000))C)"; exit 1; }

"$ADB" -s "$DEV" shell "am force-stop $PKG" >/dev/null
"$ADB" -s "$DEV" logcat -G 64M >/dev/null 2>&1; "$ADB" -s "$DEV" logcat -c >/dev/null 2>&1
"$ADB" -s "$DEV" shell "input keyevent KEYCODE_WAKEUP" >/dev/null
NATIVE=$(dirname "$("$ADB" -s "$DEV" shell "pm path $PKG" | sed 's/^package://' | tr -d '\r')")/lib/arm64
DRV=$("$ADB" -s "$DEV" shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)
FILES=/data/user/0/$PKG/files
say "driver=$DRV"
"$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm true --ez cpu_aot_maximize true --ei gpu_frame_limit_fps 30 --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '$FILES/gpu_drivers/$DRV/' --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1

sample(){ # $1 = label
  P=$("$ADB" -s "$DEV" shell "pidof $PKG" | tr -d '\r')
  [ -n "$P" ] || { say "process gone"; return 1; }
  "$ADB" -s "$DEV" shell "for t in /proc/$P/task/*; do n=\$(cat \$t/comm 2>/dev/null); s=\$(awk '{print \$(NF-37)+\$(NF-36)}' \$t/stat 2>/dev/null); v=\$(grep voluntary_ctxt_switches \$t/status 2>/dev/null | head -1 | tr -dc '0-9'); echo \"\$n|\$s|\$v\"; done" \
    | tr -d '\r' > "$OUT/threads_$1.txt"
  say "sample $1: $(wc -l < "$OUT/threads_$1.txt") threads"
}

for i in $(seq 1 12); do
  sleep 5
  tt=$(temp); el=$((i*5))
  [ "$tt" -ge "$GUARD" ] && { say "70C guard at ${el}s"; break; }
  [ "$el" -eq "$T1" ] && sample A
  [ "$el" -eq "$T2" ] && { sample B; break; }
done
"$ADB" -s "$DEV" logcat -d -s xenia:* > "$OUT/threads_run.log" 2>/dev/null
"$ADB" -s "$DEV" shell "am force-stop $PKG" >/dev/null
say "force-stopped"

echo
echo "=========== THREADS: ticks and voluntary switches, ${T1}s -> ${T2}s ==========="
python - "$OUT/threads_A.txt" "$OUT/threads_B.txt" <<'PY'
import sys,io
def rd(p):
    d={}
    try: lines=io.open(p,encoding='utf-8',errors='replace').read().split('\n')
    except Exception: return d
    for i,l in enumerate(lines):
        parts=l.strip().split('|')
        if len(parts)==3 and parts[1].strip().isdigit():
            d[(parts[0].strip(),i)]=(int(parts[1]),int(parts[2] or 0))
    return d
a,b=rd(sys.argv[1]),rd(sys.argv[2])
if not a or not b:
    print("  (missing a sample - the run did not reach both points)"); raise SystemExit
print("  %-22s %10s %10s   %s" % ("thread","d_ticks","d_vctx","reading"))
for k in sorted(a.keys(), key=lambda x:x[1]):
    if k not in b: continue
    dt=b[k][0]-a[k][0]; dv=b[k][1]-a[k][1]
    if dt>0 and dv<=1:   r="SPINNING (cpu, no parks)"
    elif dt<=1 and dv>1: r="parking/waking (finite wait)"
    elif dt<=1 and dv<=1:r="BLOCKED, never woken"
    else:                r="working"
    print("  %-22s %10d %10d   %s" % (k[0],dt,dv,r))
PY
