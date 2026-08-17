#!/usr/bin/env bash
# PC-SIDE RENDERING A/B: replay one captured Blue Dragon frame under different
# cvars and diff the render-target checksums. ~1.2 SECONDS PER ARM.
#
# WHY THIS EXISTS: the device loop for the same question was ~20 minutes per
# iteration and most of it was waiting.
#
#   build APK ......... 5 min        build desktop exe .... 1-2 min (once)
#   cool the Thor ..... 15 min       cool ................. none
#   boot + route ...... 50 s         boot ................. none
#   get a frame ....... thermally    get a result ......... 1.2 s
#                       gated, and
#                       often VOID
#
# It also removes the two things that made the device numbers untrustworthy:
#   - SCENE MATCHING. A replayed trace is the SAME guest draw stream every run,
#     byte for byte. On the device the arms ran at different speeds, so at a
#     fixed elapsed second a faster arm was FURTHER into the cutscene and the
#     screenshots were of different content.
#   - THERMALS. No 70C guard, no heat-soak spiral, no one-degree window between
#     "cold enough to reach the frame" and "voids".
#
# ⚠️ WHAT THIS CAN AND CANNOT ANSWER - the split is the whole point.
#
#   ✅ CORRECTNESS / VISUAL DIFFERENCE. "Does this cvar change what is
#      rendered?" is a property of the guest draw stream and our translation of
#      it, so it is host-independent. This answers it objectively.
#
#   ❌ PERFORMANCE. The desktop GPU is not a TBDR and is not an Adreno 740.
#      Frame times here mean NOTHING for the Thor. Never quote a speedup from
#      this harness. Measure perf on the device, and only once the rendering
#      question is already settled here.
#
# ⚠️ AND THE CHECKSUM IS SPARSE (samples=5120 per target), so it proves a
# difference when it reports one, but "identical" is weaker than "pixel
# identical". Treat IDENTICAL as "no difference detected at this sampling".
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EXE="${EXE:-$ROOT/build/bin/Windows/Release/xenia-gpu-vulkan-trace-dump.exe}"
TRACE="${TRACE:-$(ls -1 "$ROOT"/scratch/gpu/*.xtr 2>/dev/null | head -1)}"
OUT="$ROOT/scratch/dump"

[ -x "$EXE" ] || { echo "ABORT: no trace-dump exe at $EXE"; echo "  build: MSBuild build\\xenia-gpu-vulkan-trace-dump.vcxproj /p:Configuration=\"Release Windows\" /p:Platform=x64"; exit 1; }
[ -n "$TRACE" ] && [ -f "$TRACE" ] || { echo "ABORT: no .xtr trace in scratch/gpu/"; echo "  capture: run xenia.exe with the ISO, then press F4"; exit 1; }
mkdir -p "$OUT"
echo "trace: $(basename "$TRACE")  ($(( $(stat -c%s "$TRACE" 2>/dev/null || echo 0) / 1048576 )) MB)"

# arm <label> [cvars...]
arm() {
  local label="$1"; shift
  local t0 t1
  t0=$(date +%s%N)
  "$EXE" --target_trace_file="$TRACE" --vulkan_trace_dump_rt_image=true "$@" \
      > "$OUT/$label.log" 2>&1
  t1=$(date +%s%N)
  # Strip the log prefix so runs are comparable; keep only the checksum payload.
  grep -o 'rt_key=.*' "$OUT/$label.log" > "$OUT/$label.sums" 2>/dev/null
  printf "  %-12s %6.1fs  %3d targets\n" "$label" \
      "$(awk "BEGIN{printf \"%.1f\", ($t1-$t0)/1000000000}")" \
      "$(wc -l < "$OUT/$label.sums")"
}

echo "arms:"
arm baseline
for spec in "$@"; do
  label="${spec%%=*}"; cvars="${spec#*=}"
  # shellcheck disable=SC2086
  arm "$label" $cvars
done

echo
echo "=== rendering differences vs baseline ==="
for spec in "$@"; do
  label="${spec%%=*}"
  if diff -q "$OUT/baseline.sums" "$OUT/$label.sums" >/dev/null 2>&1; then
    echo "  $label: IDENTICAL (no difference detected at this sampling)"
  else
    n=$(diff "$OUT/baseline.sums" "$OUT/$label.sums" | grep -c '^[<>]')
    echo "  $label: DIFFERENT - $n changed checksum lines"
    diff "$OUT/baseline.sums" "$OUT/$label.sums" | grep '^[<>]' \
      | sed 's/\(.\{130\}\).*/\1.../' | head -6 | sed 's/^/      /'
  fi
done
