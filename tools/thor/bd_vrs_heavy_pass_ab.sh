#!/usr/bin/env bash
# A/B: gpu_vrs_heavy_pass_rate (PER-PASS VRS escalation) on Blue Dragon.
#
# THE LEVER: 81.5% of BD's in-pass GPU time is TWO passes carrying ~890
# alpha-blended draws; 61 of its 74 passes issue AT MOST ONE draw. Global 2x2
# measured +63% fps but a user watching the panel called it "a little blurry" -
# because it also coarsened UI, text and menus, which cost almost nothing.
# Escalating on PASS DRAW DEPTH puts the coarse rate only where the overdraw
# actually concentrates, and leaves every light pass at the base rate.
#
# ⚠ COMPARE BUCKETED BY VERTEX COUNT, NOT RAW MEDIANS. Each arm aborts at the
# 70C guard at a DIFFERENT route depth, so the raw medians differ by SCENE, not
# by lever. This file already records a case where that read as "+75.9%" from a
# cvar that did nothing of the sort. Bucketing on verts is what fixes it.
#
# ⚠ AND CHECK vrs_esc BEFORE BELIEVING ANY NUMBER. A flat result from a lever
# that never fired is indistinguishable from a lever that fired and did nothing.
# vrs_esc==0 on the heavy arm = VOID run, not a null result.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
export ADB DEV
COOL="${COOL:-42}"

temp() {
  local t
  t=$("$ADB" -s "$DEV" shell 'cat /sys/class/kgsl/kgsl-3d0/temp' 2>/dev/null | tr -d '\r')
  # Fail CLOSED: a bad read must look TOO HOT, never like 0C.
  case "$t" in ''|*[!0-9]*) echo 99999 ;; *) echo "$t" ;; esac
}

cool_to() {
  local cap="$1" t i
  echo "cooling to ${cap}C..."
  for i in $(seq 1 90); do
    t=$(temp)
    [ "$t" -le $((cap * 1000)) ] && { echo "  at $((t/1000))C"; return 0; }
    sleep 10
  done
  echo "ABORT: never cooled to ${cap}C"; exit 1
}

battery() { "$ADB" -s "$DEV" shell dumpsys battery | grep -m1 level | grep -oE '[0-9]+'; }

report() {
  local label="$1"
  python - "$label" <<'PY'
import re, sys
label = sys.argv[1]
rows = []
for line in open('/tmp/vrs_%s.txt' % label, encoding='utf-8', errors='replace'):
    m = re.search(r'gpu_frame_us=(\d+).*?verts=(\d+).*?vrs_base=(\d+) vrs_esc=(\d+)', line)
    if m:
        rows.append(tuple(int(g) for g in m.groups()))
if not rows:
    print('%-10s NO PASS-TIMING LINES - is vulkan_trace_pass_timestamps on?' % label)
    raise SystemExit
esc_total = sum(r[3] for r in rows)
base_total = sum(r[2] for r in rows)
print('%-10s frames=%d  vrs_base_draws=%d  vrs_ESCALATED_draws=%d'
      % (label, len(rows), base_total, esc_total))
# Bucket by scene complexity so the arms are compared on like frames.
buckets = [(50000,120000), (120000,180000), (180000,230000), (230000,300000)]
def med(v):
    v = sorted(v); return v[len(v)//2]
for lo, hi in buckets:
    sel = [r[0] for r in rows if lo <= r[1] < hi]
    if len(sel) >= 8:
        print('   verts %6d-%6d  n=%3d  gpu_frame_us=%7d  (%.2f fps)'
              % (lo, hi, len(sel), med(sel), 1e6/med(sel)))
PY
}

arm() {  # arm <label> <extra cvars...>
  local label="$1"; shift
  local b; b=$(battery)
  echo; echo "########## ARM: $label   (battery ${b}%) ##########"
  # The emulator outdraws USB charging; below this there is no run left.
  [ "${b:-0}" -ge 25 ] || { echo "ABORT: battery ${b}% too low"; exit 1; }
  cool_to "$COOL"
  echo "extra: $*"
  EXTRA="--ez vulkan_trace_pass_timestamps true $*" \
    bash "$HERE/bd_gameplay_route.sh" || { echo "ARM $label VOID/ABORTED"; return 1; }
  "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | tr -d '\r' \
    | grep "GPU pass timing" > "/tmp/vrs_$label.txt"
  report "$label"
}

# base  : the currently recommended shipping setting (2x1 on blended/alpha-test)
# heavy : same base rate, escalating to 2x2 once a pass is 128 draws deep
arm base  --ei gpu_vrs_foliage_rate 1
arm heavy --ei gpu_vrs_foliage_rate 1 --ei gpu_vrs_heavy_pass_rate 2 --ei gpu_vrs_heavy_pass_draws 128

echo
echo "READ IT IN THIS ORDER:"
echo " 1. vrs_ESCALATED_draws on the heavy arm. If 0, the run is VOID - stop."
echo " 2. Compare gpu_frame_us WITHIN a vertex bucket, never the raw medians."
echo " 3. Then LOOK AT THE PANEL. VRS artifacts on blended particles are"
echo "    TEMPORAL (flicker) and neither a screenshot nor a frame time shows them."
