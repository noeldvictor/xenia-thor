#!/usr/bin/env bash
# A/B: vulkan_direct_host_resolve on Blue Dragon gameplay.
#
# WHY THIS ONE: the 2026-08-16 device verdict closed the IN-PASS EDRAM story
# (attachment shape 1.5%, GMEM never wins, framebuffer bandwidth not the
# constraint across a 36x working set). What it did NOT measure is the other
# 17% of the frame:
#     INSIDE passes : 46,856 us (82%)
#     BETWEEN passes: 10,070 us (17%)  <- transfers, resolves, barriers
# That 17% IS the EDRAM emulation machinery, and direct_host_resolve is the UMA
# fast path for its resolve half: compute-resolve the host render target
# straight into guest memory, with no EDRAM dump and no round trip.
#
# ⚠ MEASURE THE BUCKET, NOT ONLY fps. BD field is documented CPU-bound, so a
# real GPU win can be invisible in fps. vulkan_trace_pass_timestamps logs
# gpu_frame_us and gpu_pass_us; between = frame - pass. That is the number this
# lever should move, and it is far more sensitive than the frame rate.
#
# ⚠ THE ROUTE HAS A HARD PREREQUISITE: a FULLY WARM AOT object cache, or every
# button press fires into the loading screen and the run measures the title
# screen at a plausible-looking ~32 fps. bd_gameplay_route.sh has a scene gate
# (>=50,000 vertices/frame) that VOIDS such a run - trust it, do not bypass it.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
export ADB DEV
COOL="${COOL:-45}"

temp() {
  local t
  t=$("$ADB" -s "$DEV" shell 'cat /sys/class/kgsl/kgsl-3d0/temp' 2>/dev/null | tr -d '\r')
  case "$t" in ''|*[!0-9]*) echo 99999 ;; *) echo "$t" ;; esac
}

cool_to() {  # cool_to <C> - fails closed; a bad read is 99999 = "too hot"
  local cap="$1" t i
  echo "cooling to ${cap}C..."
  for i in $(seq 1 90); do
    t=$(temp)
    [ "$t" -lt "$((cap * 1000))" ] && { echo "  gpu=$((t/1000))C ok"; return 0; }
    sleep 10
  done
  echo "ABORT: never reached ${cap}C"; exit 1
}

# Pull the pass-timing split out of the run's logcat. The last 30 gameplay
# frames only - the early ones are load/title and would drag the mean.
report() {  # report <label>
  local label="$1"
  "$ADB" -s "$DEV" logcat -d 2>/dev/null | grep 'GPU draw outcomes' | tail -30 \
    > /tmp/dhr_$label.txt
  python - "$label" <<'PY'
import re, sys
label = sys.argv[1]
frame, passes = [], []
for line in open('/tmp/dhr_%s.txt' % label, encoding='utf-8', errors='replace'):
    f = re.search(r'gpu_frame_us=(\d+)', line)
    p = re.search(r'gpu_pass_us=(\d+)', line)
    if f and p:
        frame.append(int(f.group(1)))
        passes.append(int(p.group(1)))
if not frame:
    print('%-10s NO PASS TIMESTAMPS - is vulkan_trace_pass_timestamps on?' % label)
    raise SystemExit
def med(v):
    v = sorted(v)
    return v[len(v)//2]
mf, mp = med(frame), med(passes)
print('%-10s frames=%d  gpu_frame_us=%d  in_pass=%d (%.0f%%)  BETWEEN=%d (%.0f%%)'
      % (label, len(frame), mf, mp, 100.0*mp/mf, mf-mp, 100.0*(mf-mp)/mf))
PY
}

arm() {  # arm <label> <extra args...>
  local label="$1"; shift
  cool_to "$COOL"
  echo
  echo "########## ARM: $label ##########"
  echo "extra: $*"
  EXTRA="--ez vulkan_trace_pass_timestamps true $*" \
    bash "$HERE/bd_gameplay_route.sh" || { echo "ARM $label VOID/ABORTED"; return 1; }
  report "$label"
}

arm baseline
arm dhr --ez vulkan_direct_host_resolve true

echo
echo "Compare BETWEEN-pass us. That is the bucket this lever attacks."
echo "fps may not move at all - BD field is CPU-bound."
