#!/usr/bin/env bash
# Validate the single-sourced host-RT size (commit 731c54d07) and the resolution
# win it unblocks.
#
# WHY: BD gameplay is ~93% GPU-bound and 81.5% of GPU in-pass time is two passes
# of ~890 alpha-blended depth-writing draws. Fewer fragments is the ONLY lever
# measured to move that class:
#     pct=100 -> 16.17 fps      pct=71 -> 22.37 (1.38x)      pct=50 -> 29.00 (1.79x)
# That win was previously unshippable because the host RT size was open-coded at
# ~14 sites while the downscale was applied at only two.
#
# THREE THINGS THIS RUN MUST ESTABLISH, in order:
#   1. NO REGRESSION WITH THE CVAR OFF. ApplyResolutionDownscale is the identity
#      when pct is 0/100, so arm "off" must match the historical baseline
#      (~16 fps, gpu_frame_us ~60,000). If it does not, the refactor broke
#      something and NOTHING else in this run matters.
#   2. The win still lands at 71 and 50.
#   3. ⚠ A HUMAN LOOKS AT THE PANEL. The expected quality cost is UNIFORM
#      SOFTNESS. Misaligned resolves look like tearing/offset/garbage, not
#      softness. This file's standing rule: only a human eye clears pixels.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
export ADB DEV
PKG=jp.xenia.emulator.github.debug

temp() {
  local t
  t=$("$ADB" -s "$DEV" shell 'cat /sys/class/kgsl/kgsl-3d0/temp' 2>/dev/null | tr -d '\r')
  case "$t" in ''|*[!0-9]*) echo 99999 ;; *) echo "$t" ;; esac
}
cool_to() {
  local cap="$1" t i
  for i in $(seq 1 90); do
    t=$(temp)
    [ "$t" -lt "$((cap * 1000))" ] && { echo "  gpu=$((t/1000))C"; return 0; }
    sleep 10
  done
  echo "ABORT: never reached ${cap}C"; exit 1
}

# Median gpu_frame_us over GAMEPLAY-TIER frames only. The all-frames median is a
# light-frame statistic and means nothing here.
report() {
  local label="$1" out="${OUTDIR:-$HERE}/res_$label.txt"
  "$ADB" -s "$DEV" logcat -d 2>/dev/null | grep 'GPU pass timing' > "$out"
  python - "$label" "$out" <<'PYEOF'
import re, sys
label, path = sys.argv[1], sys.argv[2]
rows = []
for line in open(path, encoding='utf-8', errors='replace'):
    g = lambda k: (lambda m: int(m.group(1)) if m else None)(re.search(k + r'=(\d+)', line))
    fr, pa, v = g('gpu_frame_us'), g('gpu_pass_us'), g('verts')
    if fr and pa is not None and fr > 0 and (v or 0) > 50000:
        rows.append((fr, pa, v))
if not rows:
    print('%-8s NO GAMEPLAY-TIER FRAMES' % label)
    raise SystemExit
rows.sort()
m = rows[len(rows)//2]
print('%-8s n=%-4d frame=%6d us  %5.2f fps   in_pass=%6d  verts=%d'
      % (label, len(rows), m[0], 1e6/m[0], m[1], m[2]))
PYEOF
}

arm() {  # arm <label> <pct|off>
  local label="$1" pct="$2" extra=""
  cool_to 45
  [ "$pct" != "off" ] && extra="--ei gpu_resolution_downscale_pct $pct"
  echo
  echo "########## pct=$pct ##########"
  EXTRA="--ez vulkan_trace_pass_timestamps true $extra" \
    bash "$HERE/bd_gameplay_route.sh" >/dev/null 2>&1 || echo "  (route ended: thermal or gate)"
  report "$label"
}

echo "=== 1. NO-REGRESSION CONTROL (cvar off) - must match ~16 fps baseline ==="
arm off off
echo
echo "=== 2. THE WIN ==="
arm 71 71
arm 50 50
echo
echo "⚠ NOW LOOK AT THE PANEL. Expected: uniform softness."
echo "  Tearing / offset / garbage = the resolve chain is still misaligned."
