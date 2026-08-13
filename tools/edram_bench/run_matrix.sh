#!/usr/bin/env bash
# EDRAM harness matrix: attachment height x loadOp, on a chosen driver.
#
# The 2026-08-13 blob result was: LOAD is free at every height, storeOp is
# irrelevant, only CLEAR scales (~35.8us per 1000 rows at 1280 wide). THE OPEN
# QUESTION IS WHETHER TURNIP AGREES - we ship Turnip, and Mesa's clear lowering
# and untouched-tile elision are a different implementation. If Turnip does not
# elide untouched-tile LOADs, the oversized-RT theory comes back.
#
#   bash run_matrix.sh                 # system loader (Qualcomm blob)
#   bash run_matrix.sh turnip          # extracted Turnip ICD
set -u
DEV_BIN=/data/local/tmp/edram_bench
TURNIP=/data/local/tmp/libvulkan_freedreno.so
PKG=jp.xenia.emulator.github.debug

DRIVER_ARG=""
if [ "${1:-}" = "turnip" ]; then
  # The ICD lives in the app's private dir; copy it somewhere `shell` can read.
  if ! adb shell "cd /data/local/tmp && ls libvulkan_freedreno.so" >/dev/null 2>&1; then
    D=$(adb shell "run-as $PKG ls files/gpu_drivers/" | head -1 | tr -d '\r')
    [ -n "$D" ] || { echo "ABORT: no Turnip driver installed"; exit 1; }
    echo "extracting $D"
    adb shell "run-as $PKG cat files/gpu_drivers/$D/libvulkan_freedreno.so > $TURNIP"
  fi
  DRIVER_ARG="--driver $TURNIP"
fi

# An adb shell argument that STARTS with '/' is rewritten by Git Bash even
# inside quotes - always lead with cd.
run() { adb shell "cd /data/local/tmp && ./edram_bench $DRIVER_ARG $*"; }

echo "### A: colour only - height x loadOp, view 1280x720, 1 draw, 16 passes"
run --label probe --width 1280 --height 720 --view-height 720 --draws 1 | head -1
for h in 720 2048 8192; do
  for op in load clear dontcare; do
    run --label "h$h-$op" --width 1280 --height "$h" --view-width 1280 \
        --view-height 720 --draws 1 --loadop "$op" | grep median
  done
done

# Hypothesis (b): the colour-only harness may show off-screen rows as free
# while the real passes do not, because the real passes carry DEPTH. Our own
# LRZ note says Turnip disables Adreno LRZ when depth enters via LOAD_OP_LOAD,
# which is a strong hint that Turnip treats a depth LOAD very differently from
# a colour one.
echo
echo "### B: with depth - colour loadOp=load, depth loadOp swept"
for h in 720 2048 8192; do
  for dop in load clear dontcare; do
    run --label "d$h-$dop" --width 1280 --height "$h" --view-width 1280 \
        --view-height 720 --draws 1 --loadop load --depth --depth-loadop "$dop" \
        | grep median
  done
done
