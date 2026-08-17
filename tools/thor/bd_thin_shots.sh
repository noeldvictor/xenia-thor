#!/usr/bin/env bash
# Capture ONE gameplay frame per speed-hack arm, so the quality trade can be
# judged instead of assumed.
#
# WHY A SEPARATE SCRIPT: a frame time cannot decide a thin factor. Thinning
# removes overdraw LAYERS - foliage gets sparser, transparency gets thinner -
# and whether that is acceptable is the user's call, not a number. The A/B
# harness measures; this shows.
#
# 🚨 FOUR SCREENSHOT ATTEMPTS HAVE FAILED IN THIS PROJECT, THREE OF THEM BY
# THEIR OWN GATING RATHER THAN BY ANYTHING ON THE DEVICE. Every guard below is
# one of those failures:
#
#  1. A VERTEX-COUNT GATE FIRED ON THE CINEMATIC. The opening cinematic pushes
#     MORE vertices than the field (237,861 was a cinematic), so "verts > N"
#     alone does not mean "in the field". Gate on SUSTAINED high vertex counts
#     instead: the cinematic peaks and passes, the field holds.
#  2. A WALL-CLOCK GATE (t >= 100s) NEVER FIRED, because the thermal guard ends
#     the run at 50-70s. Never gate a capture on a time later than the run can
#     survive to.
#  3. AN OVERWRITE LOOP REPLACED A GOOD FRAME WITH THE ANDROID HOME SCREEN
#     after the force-stop. KEEP THE LARGEST capture, never the last.
#  4. INPUT WENT TO ANOTHER SESSION'S GAME. `screencap` has no target either -
#     it grabs whatever is in the foreground. VERIFY THE FOREGROUND PACKAGE
#     BEFORE BELIEVING ANY CAPTURE.
#
# And use `adb exec-out screencap -p > local.png`. The `adb shell screencap -p
# /sdcard/x.png` form FAILS UNDER GIT BASH - MSYS rewrites /sdcard/... into
# C:/Program Files/Git/sdcard/..., so the file is never written and the pull
# reports "No such file" for a path that was never valid. exec-out streams to
# stdout with no device path, so there is nothing to mangle.
#
# ⚠️ NEVER COMMIT THE OUTPUT. Screenshots are game content; scratchpad only.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
GAME='/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso'
SEQ='start@20000:1500;a@28000:1500;a@34000:1500;a@40000:1500;start@46000:1500;a@52000:1500;a@58000:1500;start@64000:1500;a@70000:1500;a@76000:1500;a@82000:1500;start@88000:1500;a@94000:1500;a@100000:1500;a@106000:1500;start@112000:1500;a@118000:1500;a@124000:1500'
OUT=scratchpad/shots
COOL="${COOL:-38500}"     # see bd_lossless_ab.sh: a charging device floors ~38C
MINV="${MINV:-180000}"    # heavy-scene threshold
HOLD="${HOLD:-2}"         # consecutive heavy samples required (kills the cinematic)

temp(){ t=$("$ADB" -s "$DEV" shell 'cat /sys/class/kgsl/kgsl-3d0/temp' 2>/dev/null | tr -d '\r')
        case "$t" in ''|*[!0-9]*) echo 99999;; *) echo "$t";; esac; }

mkdir -p "$OUT"
NATIVE=$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | sed 's/^package://' | tr -d '\r')")/lib/arm64
DRV=$("$ADB" -s "$DEV" shell run-as $PKG ls files/gpu_drivers/ | tr -d '\r' | head -1)
FILES=/data/user/0/$PKG/files

shot_arm(){
  label="$1"; shift
  echo; echo "########## $label ##########"
  for i in $(seq 1 160); do t=$(temp); [ "$t" -le "$COOL" ] && break; sleep 10; done
  t=$(temp); [ "$t" -le "$COOL" ] || { echo "ABORT: no cooldown to $((COOL/1000))C"; return 1; }
  echo "  cold at $((t/1000))C"

  "$ADB" -s "$DEV" shell am force-stop $PKG; "$ADB" -s "$DEV" logcat -c
  "$ADB" -s "$DEV" shell input keyevent KEYCODE_WAKEUP >/dev/null
  "$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity \
    --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm true \
    --ez cpu_aot_maximize true --ez vulkan_trace_draw_outcomes_per_frame true \
    --es hid nop --es hid_nop_button_sequence '$SEQ' $* \
    --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '$FILES/gpu_drivers/$DRV/' \
    --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
    --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1

  best=0; heavy=0
  for i in $(seq 1 20); do
    sleep 6
    tt=$(temp)
    [ "$tt" -ge 70000 ] && { echo "  70C guard"; break; }
    v=$("$ADB" -s "$DEV" logcat -d 2>/dev/null | grep -oE 'total_vertices=[0-9]+' \
        | tail -3 | grep -oE '[0-9]+' | sort -n | tail -1)
    v=${v:-0}
    if [ "$v" -ge "$MINV" ]; then heavy=$((heavy+1)); else heavy=0; fi
    printf "  %3ds verts=%-8s heavy=%d %3dC\n" $((i*6)) "$v" "$heavy" "$((tt/1000))"
    [ "$heavy" -lt "$HOLD" ] && continue

    # Guard 4: a capture is only ours if OUR activity is on top. This device is
    # shared, and a 2.4 MB screencap once turned out to be another emulator.
    fg=$("$ADB" -s "$DEV" shell dumpsys activity activities 2>/dev/null \
         | grep -m1 topResumedActivity | tr -d '\r')
    case "$fg" in *"$PKG"*) ;; *) echo "  SKIP: foreground is not ours: $fg"; continue;; esac

    "$ADB" -s "$DEV" exec-out screencap -p > "$OUT/_try.png" 2>/dev/null
    sz=$(wc -c < "$OUT/_try.png" 2>/dev/null || echo 0)
    # Guard 3: KEEP THE LARGEST, never the last. ~15 KB is a black/blank grab,
    # a real frame is hundreds of KB.
    if [ "$sz" -gt "$best" ]; then
      best=$sz; cp "$OUT/_try.png" "$OUT/$label.png"
      echo "    kept ${sz}B verts=$v"
    fi
  done
  rm -f "$OUT/_try.png"
  "$ADB" -s "$DEV" shell am force-stop $PKG
  if [ "$best" -lt 100000 ]; then
    echo "  VOID: best capture ${best}B - that is a blank grab, not a frame"
    return 1
  fi
  echo "  $label -> $OUT/$label.png (${best}B)"
}

for a in ${ARMS:-baseline f2 b2}; do
  case "$a" in
    baseline) shot_arm baseline ;;
    f2)       shot_arm f2 --ei gpu_foliage_thin_factor 2 ;;
    f4)       shot_arm f4 --ei gpu_foliage_thin_factor 4 ;;
    b2)       shot_arm b2 --ei gpu_blended_thin_factor 2 ;;
    r2)       shot_arm r2 --ei gpu_vrs_foliage_rate 1 --ei gpu_vrs_heavy_pass_rate 2 --ei gpu_vrs_heavy_pass_draws 16 ;;
    *) echo "unknown arm: $a"; exit 1 ;;
  esac
done
