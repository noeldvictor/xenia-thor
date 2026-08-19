#!/usr/bin/env bash
# Retry one A/B arm until it produces a NON-VOID result. The Thor is shared and
# rpcs3 reappears every few minutes, and a mid-run appearance voids the arm - so
# a single attempt is mostly luck. Stops immediately on a valid arm; never
# reports a void one.
set -u
N="${N:-5}"
for i in $(seq 1 "$N"); do
  echo "=== attempt $i/$N ==="
  ARM="${ARM:-spin}" bash tools/thor/wait_then_run.sh bash tools/thor/mc2_spin_ab.sh 2>&1 | grep -vE "waited [0-9]+s of"
  if ! grep -q "ARM VOID" scratchpad/spin/../spin_retry_last 2>/dev/null; then :; fi
  # the harness prints the verdict; re-derive it from the arm log itself
  fr=$(grep -c "GPU draw outcomes" "scratchpad/spin/${ARM:-spin}.log" 2>/dev/null)
  pv=$(grep -oE "total_vertices=[0-9]+" "scratchpad/spin/${ARM:-spin}.log" 2>/dev/null | grep -oE "[0-9]+" | sort -n | tail -1)
  if [ "${fr:-0}" -ge 200 ] && [ "${pv:-0}" -ge 100000 ]; then
    echo "=== VALID ARM on attempt $i: frames=$fr peak_verts=$pv ==="; exit 0
  fi
  echo "  attempt $i void (frames=${fr:-0} peak=${pv:-0}) - retrying"
done
echo "=== all $N attempts void ==="; exit 1
