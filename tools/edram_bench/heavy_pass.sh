#!/usr/bin/env bash
# THE REPRESENTATIVE-SHAPE HARNESS. Answers the three EDRAM questions in one run.
#
# WHY THIS EXISTS: every earlier harness result was measured on 1-64 fullscreen
# triangles into ONE colour attachment with no depth. Turnip never binned that
# ("too simple to bin"), so it measured SYSMEM and reported "LOAD is free at any
# attachment height" - which became the oversized-RT exoneration, which was
# retracted on 2026-08-14. The data was right; what the data was OF was wrong.
#
# BD's two dominant passes are 1280x2048 WITH DEPTH and THOUSANDS OF DRAWS, and
# they are 65% of the GPU frame. This script measures that shape.
#
# THE MODE IS VERIFIED FROM THE DRIVER ON EVERY ARM. Never infer it.
#
# Sections:
#   1. DISCRIMINATOR  - is the heavy shape ALU-bound or BANDWIDTH-bound?
#                       Decides whether GMEM can EVER help: GMEM buys bandwidth,
#                       not ALU. Nothing else in this project can separate them,
#                       because both scale linearly with pixel count.
#   2. ALLOCATION     - EDRAM-span vs screen-sized, at the representative shape,
#                       in both modes. Re-runs the exoneration honestly.
#   3. GMEM ENGAGEMENT- does autotune pick gmem for a HEAVY pass, and is gmem
#                       faster there? At 1 draw gmem LOSES (74.3 vs 58.0 us).
#                       Nobody has ever tested it on the passes that matter.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
TURNIP=/data/local/tmp/libvulkan_freedreno.so
PKG=jp.xenia.emulator.github.debug

# ---- device hygiene. The guard FAILS CLOSED: an unreadable temp aborts. ----
# NOTE THE QUOTES around the remote command. Unquoted, Git Bash rewrites
# /sys/... into a Windows path, the read returns empty, and the guard silently
# evaluates against 0 - which happened for a whole 300s run on 2026-08-13.
busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
[ "${busy:-1}" = "0" ] || { echo "ABORT: rpcs3 running - device is SHARED"; exit 1; }

cool() {  # cool <max_c> - abort if we cannot read, or cannot get under the cap
  local cap="$1" t i
  for i in $(seq 1 60); do
    t=$("$ADB" -s "$DEV" shell "cat /sys/class/kgsl/kgsl-3d0/temp" | tr -d '\r')
    case "$t" in ''|*[!0-9]*) echo "ABORT: bad temp read '$t'"; exit 1;; esac
    [ "$t" -lt "$((cap * 1000))" ] && { echo "  gpu=$((t/1000))C ok"; return 0; }
    sleep 10
  done
  echo "ABORT: gpu never fell below ${cap}C"; exit 1
}
cool 55

if ! "$ADB" -s "$DEV" shell "cd /data/local/tmp && ls libvulkan_freedreno.so" >/dev/null 2>&1; then
  D=$("$ADB" -s "$DEV" shell "run-as $PKG ls files/gpu_drivers/" | head -1 | tr -d '\r')
  [ -n "$D" ] || { echo "ABORT: no Turnip driver installed"; exit 1; }
  "$ADB" -s "$DEV" shell "run-as $PKG cat files/gpu_drivers/$D/libvulkan_freedreno.so > $TURNIP"
fi

run() {  # run <TU_DEBUG|""> <args...>
  local dbg="$1"; shift
  local env=""; [ -n "$dbg" ] && env="TU_DEBUG=$dbg"
  "$ADB" -s "$DEV" shell "cd /data/local/tmp && $env ./edram_bench --driver $TURNIP $*"
}

mode() {  # mode <TU_DEBUG|""> <args...> - what the DRIVER actually did
  local dbg="$1"; shift
  local env=""; [ -n "$dbg" ] && env="TU_DEBUG=$dbg"
  "$ADB" -s "$DEV" shell "cd /data/local/tmp && rm -f m.txt && \
     MESA_GPU_TRACEFILE=/data/local/tmp/m.txt MESA_GPU_TRACES=print $env \
     ./edram_bench --driver $TURNIP --iters 1 $*" >/dev/null 2>&1
  "$ADB" -s "$DEV" shell "cd /data/local/tmp && \
     grep -o 'tiledRender=[a-z]*' m.txt | sort | uniq -c | tr -d '\r'; \
     grep -o 'tilingDisableReason=[^,]*' m.txt | sort -u | tr -d '\r'; \
     grep -o 'numberOfBins=[0-9]*' m.txt | sort -u | tr -d '\r'"
}

# The representative shape: EDRAM-span attachment, depth, many overlapping
# draws. --draws are fullscreen triangles, so draws == overdraw factor.
HEAVY="--width 1280 --height 2048 --view-width 1280 --view-height 720 \
       --depth --passes 4 --draws 64 --loadop load"

echo
echo "################ 1. DISCRIMINATOR: ALU or BANDWIDTH? ################"
echo "# Pixels are FIXED in every arm below. Only the named resource moves."
echo "# time tracks alu-iters  -> ALU-bound      -> GMEM can never help"
echo "# time tracks blend/fmt  -> BANDWIDTH-bound -> GMEM is the right tool"
echo "--- mode check for the heavy shape ---"
mode "" --label m $HEAVY
echo "--- (a) ALU sweep, blend off, rgba8 ---"
for n in 1 8 32 128; do
  run "" --label "alu$n" $HEAVY --alu-iters "$n" | grep median
done
cool 50
echo "--- (b) BLEND off/on, alu fixed at 8 ---"
for b in "" "--blend"; do
  run "" --label "blend${b:-off}" $HEAVY --alu-iters 8 $b | grep median
done
echo "--- (c) FORMAT rgba8 (4B) vs rgba16f (8B), alu fixed at 8 ---"
for f in rgba8 rgba16f; do
  run "" --label "fmt-$f" $HEAVY --alu-iters 8 --format "$f" | grep median
done

cool 50
echo
echo "################ 2. ALLOCATION at the representative shape ################"
echo "# The exoneration re-run honestly: depth + 64 draws, BOTH modes, verified."
for dbg in "" "gmem,forcebin"; do
  echo "===== TU_DEBUG=${dbg:-<autotune>} ====="
  for h in 720 2048; do
    echo "--- attachment 1280x$h ---"
    mode "$dbg" --label m --width 1280 --height "$h" --view-width 1280 \
         --view-height 720 --depth --passes 1 --draws 64 --loadop load
    for op in load dontcare; do
      run "$dbg" --label "h$h-$op" --width 1280 --height "$h" \
          --view-width 1280 --view-height 720 --depth --passes 4 --draws 64 \
          --loadop "$op" | grep median
    done
  done
  cool 50
done

echo
echo "################ 3. GMEM ENGAGEMENT on a heavy pass ################"
echo "# At 1 draw, gmem LOSES (74.3 vs 58.0 us). Does that invert with draws?"
echo "# If gmem wins at high draw counts, the two heavy BD passes are the"
echo "# target and the whole tile-lever family comes back for THEM ONLY."
for d in 1 16 64 256; do
  echo "--- draws=$d ---"
  mode "" --label m --width 1280 --height 2048 --view-width 1280 \
       --view-height 720 --depth --passes 1 --draws "$d" --loadop load
  for dbg in "" "gmem,forcebin"; do
    run "$dbg" --label "d$d-${dbg:-auto}" --width 1280 --height 2048 \
        --view-width 1280 --view-height 720 --depth --passes 4 --draws "$d" \
        --loadop load | grep median
  done
  cool 50
done

echo
echo "DONE. Record in CLAUDE.md WITH the tiledRender/tilingDisableReason lines."
echo "A number without its mode is how the last exoneration went wrong."
