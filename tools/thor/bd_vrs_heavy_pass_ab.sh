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
# ⭐ COOL TO 36C, NOT 42C (2026-08-17). The 70C guard ends a run ~60-70s into
# gameplay from a 42C start, which is BEFORE the route reaches deep scenes - so
# the 180k-300k vertex buckets collected only n=9-12 frames and could not
# support a conclusion, while the light buckets got n=77-150. The buckets that
# matter most for BD were the ones sampled WORST.
# Measured: a 35C start ran 109s vs ~60-70s from 42C. Cooling further nearly
# doubles the usable window at no safety cost - the guard is unchanged.
COOL="${COOL:-36}"

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

# ⚠ DATA LANDS IN THE REPO, NOT /tmp. Git Bash's /tmp is the MSYS temp dir while
# Windows python resolves '/tmp' to C:\tmp - so a bash `>` redirect and a python
# open() of the same literal path touch DIFFERENT FILES. That silently voided the
# reporting half of the 2026-08-17 run (the data was fine; the reader looked in
# the wrong place). Fourth instance of the Git Bash path-rewrite class here.
OUTDIR="$HERE/../../scratchpad"
mkdir -p "$OUTDIR" 2>/dev/null

report() {
  local label="$1"
  python - "$OUTDIR/vrs_$label.txt" <<'PY'
import re, sys
path = sys.argv[1]
label = path.replace('\\', '/').split('vrs_')[-1].replace('.txt', '')
rows = []
for line in open(path, encoding='utf-8', errors='replace'):
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
  # ⚠ RETRY THE PULL. The route force-stops the app, and a pull issued
  # immediately after can come back EMPTY while the very same query a moment
  # later returns thousands of lines - observed 2026-08-17 (0 bytes captured,
  # 1,342 lines present seconds afterwards). Treat an empty pull as a race, not
  # as "the lever produced nothing", or a good arm reads as VOID.
  local i n
  for i in 1 2 3 4 5; do
    "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | tr -d '\r' \
      | grep "GPU pass timing" > "$OUTDIR/vrs_$label.txt"
    n=$(wc -l < "$OUTDIR/vrs_$label.txt")
    [ "$n" -gt 0 ] && break
    echo "  pull returned 0 lines, retrying ($i/5)..."
    sleep 3
  done
  [ "$n" -gt 0 ] || { echo "ARM $label VOID: no pass-timing lines after 5 pulls"; return 1; }
  report "$label"
}

# base   : the currently recommended shipping setting (2x1 on blended/alpha-test)
# hp<N>  : same base rate, escalating to 2x2 once a pass is N draws deep
#
# ⭐ THRESHOLD SIZING, corrected by the 2026-08-17 run. 128 was chosen to cover
# ~81% of the 671-draw pass and it was FAR TOO CONSERVATIVE: BD's light passes
# carry AT MOST ONE draw each, so ANY threshold above ~2 already excludes all of
# them. At 128 we left 388 eligible draws/frame at the fine rate for no quality
# benefit, and captured only ~20% of the gap between 2x1 and global 2x2.
#   => sweep DOWNWARD. A low threshold approaches global 2x2's win while still
#      leaving every light pass (UI, text, menus) untouched, which is the whole
#      point of doing this per-pass instead of globally.
THRESHOLDS="${THRESHOLDS:-128}"
arm base --ei gpu_vrs_foliage_rate 1
for t in $THRESHOLDS; do
  arm "hp$t" --ei gpu_vrs_foliage_rate 1 --ei gpu_vrs_heavy_pass_rate 2 \
             --ei gpu_vrs_heavy_pass_draws "$t"
done

echo
echo "READ IT IN THIS ORDER:"
echo " 1. vrs_ESCALATED_draws on the heavy arm. If 0, the run is VOID - stop."
echo " 2. Compare gpu_frame_us WITHIN a vertex bucket, never the raw medians."
echo " 3. Then LOOK AT THE PANEL. VRS artifacts on blended particles are"
echo "    TEMPORAL (flicker) and neither a screenshot nor a frame time shows them."
