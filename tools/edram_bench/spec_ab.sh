#!/usr/bin/env bash
# Uber-shader vs specialization-constant probe.
#
# THE HYPOTHESIS (Adreno guide + our own 31-GPR measurement): our guest shader
# translator is an uber-shader with 61 runtime kSysFlag_ tests and ZERO
# specialization constants, which inflates GPR count and collapses wave
# occupancy. Promoting the flags to spec constants lets ir3 dead-strip the
# untaken branches.
#
# THE ARMS differ ONLY in where the flag word comes from - same source file,
# same 16 gated blocks, same flag VALUE. So any delta is the compiler folding.
set -u
TURNIP=/data/local/tmp/libvulkan_freedreno.so
PKG=jp.xenia.emulator.github.debug

if ! adb shell "cd /data/local/tmp && ls libvulkan_freedreno.so" >/dev/null 2>&1; then
  D=$(adb shell "run-as $PKG ls files/gpu_drivers/" | head -1 | tr -d '\r')
  [ -n "$D" ] || { echo "ABORT: no Turnip driver installed"; exit 1; }
  adb shell "run-as $PKG cat files/gpu_drivers/$D/libvulkan_freedreno.so > $TURNIP"
fi

run() { adb shell "cd /data/local/tmp && ./edram_bench --driver $TURNIP $*"; }

echo "### driver check"
run --label probe --width 1280 --height 720 --view-height 720 --draws 1 | head -1

# Fragment-bound by construction: full-screen coverage, several draws per pass,
# so the shader body dominates and pass overhead does not.
COMMON="--width 1280 --height 720 --view-width 1280 --view-height 720 --passes 4 --draws 8 --loadop dontcare"

echo
echo "### flag-density sweep: dyn vs spec"
for fv in 0x0000 0x5555 0xffff; do
  for fs in dyn spec; do
    run --label "$fs-$fv" $COMMON --flag-shader "$fs" --flag-value "$fv" | grep median
  done
done

echo
echo "### baseline (trivial fill shader, no flag blocks)"
run --label fill $COMMON --flag-shader none | grep median
