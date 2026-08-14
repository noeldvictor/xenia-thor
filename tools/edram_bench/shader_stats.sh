#!/usr/bin/env bash
# Dump ir3's own per-variant shader stats for the uber-shader probe.
#
# WHY A SCRIPT AND NOT AN INLINE COMMAND: the stats do NOT arrive on stdout.
# IR3_SHADER_DEBUG routes through mesa's logger, which on Android is
# __android_log, so the numbers land in LOGCAT under tag MESA even though the
# harness is a plain adb-run binary. An inline `adb shell ... | grep` therefore
# shows nothing and reads like the option is unsupported.
#
# The stats line is the MECHANISM check behind the timing result: instruction
# count and max_waves are exactly what the Adreno guide says an uber-shader
# inflates.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
TURNIP=/data/local/tmp/libvulkan_freedreno.so
FLAGS="${1:-0x5555}"

for fs in dyn spec; do
  "$ADB" -s "$DEV" logcat -c
  # An adb shell argument that STARTS with '/' is rewritten by Git Bash even
  # inside quotes - lead with cd, same as build.sh.
  # MESA_SHADER_CACHE_DISABLE IS REQUIRED. Turnip keeps an on-disk shader cache,
  # so a variant compiled by an earlier run is served from it and prints NO
  # stats at all - which looks exactly like "the option did not work". Two runs
  # of this script silently reported different arms until this was added.
  "$ADB" -s "$DEV" shell "cd /data/local/tmp && MESA_SHADER_CACHE_DISABLE=true IR3_SHADER_DEBUG=fs ./edram_bench \
      --driver $TURNIP --label $fs --width 640 --height 360 \
      --view-width 640 --view-height 360 --passes 1 --draws 1 --iters 1 \
      --loadop dontcare --flag-shader $fs --flag-value $FLAGS" >/dev/null 2>&1
  sleep 1
  echo "===== $fs  flags=$FLAGS ====="
  # Print the WHOLE stat block. An earlier `tail -4` sliced different lines out
  # of each arm and made the two arms look incomparable.
  "$ADB" -s "$DEV" logcat -d -s MESA 2>/dev/null \
    | grep -E "FRAG prog" | sed 's/^.*MESA *: *//' | sort -u
done
