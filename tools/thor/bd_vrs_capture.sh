#!/usr/bin/env bash
# Launch BD with the per-pass VRS setting and capture a FIELD screenshot.
# Catches gross corruption only - VRS artifacts on blended particles are
# TEMPORAL (flicker under camera motion) and a still frame cannot show them.
# A human watching the panel is the only check that settles the quality claim.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
OUT="${OUT:-$HERE/../../scratchpad}"
mkdir -p "$OUT"
( EXTRA="--ez vulkan_trace_pass_timestamps true --ei gpu_vrs_foliage_rate 1 \
   --ei gpu_vrs_heavy_pass_rate 2 --ei gpu_vrs_heavy_pass_draws 16" \
  DEV="$DEV" bash "$HERE/bd_gameplay_route.sh" >"$OUT/vrs_capture_route.log" 2>&1 ) &
RP=$!
# Wait for a genuinely heavy frame, then grab it. Gate on CONTENT, not a clock:
# the field is what we need to see, and the route's timing shifts with cache
# state (a cold cache once pushed the title out to 280s).
for i in $(seq 1 60); do
  sleep 5
  v=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null \
      | grep -oE "total_vertices=[0-9]+" | cut -d= -f2 | sort -n | tail -1)
  echo "  t=$((i*5))s peak_verts=${v:-0}"
  # ⚠⚠ VERTEX COUNT CANNOT SEPARATE THE FIELD FROM THE CINEMATIC HERE, measured:
  #   cinematic  ~237,861 verts / ~1,098 draws
  #   FIELD      ~217,721 verts / ~2,007 draws   <- FEWER verts, MORE draws
  # So a ">=N verts" gate is not just imprecise, it is pointed the wrong way.
  # And a fixed elapsed-time gate does not work either: the thermal guard ends
  # the route at ~50-70s, so anything gated at t>=100s never fires.
  # ⇒ Capture REPEATEDLY once the scene is heavy and keep overwriting, so the
  #   file left behind is the DEEPEST frame the route actually reached.
  if [ "${v:-0}" -ge 200000 ] && [ $((i*5)) -ge 30 ]; then
    # exec-out streams the PNG to stdout: no device path for MSYS to rewrite.
    # 🚨 CAPTURE ONLY WHILE THE GUEST IS ALIVE. A screencap taken after the
    # route force-stops returns the ANDROID HOME SCREEN, and with an
    # overwrite-in-place loop that silently DESTROYS the good frame captured
    # moments earlier. This file already records the post-force-stop home-screen
    # trap; the repeating loop is a new way to hit the same one.
    "$ADB" -s "$DEV" shell pidof jp.xenia.emulator.github.debug >/dev/null 2>&1 || {
      echo "  guest gone - stopping capture (keeping the best frame)"; break; }
    "$ADB" -s "$DEV" exec-out screencap -p > "$OUT/vrs_shot.png"
    # Keep the LARGEST frame: a busy field frame compresses far larger than a
    # flat menu, so size is a decent proxy for "most content on screen".
    cur=$(wc -c < "$OUT/vrs_shot.png")
    best=0; [ -f "$OUT/vrs_heavy_field.png" ] && best=$(wc -c < "$OUT/vrs_heavy_field.png")
    [ "$cur" -gt "$best" ] && cp "$OUT/vrs_shot.png" "$OUT/vrs_heavy_field.png"
    d=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null         | grep -oE "rendered=[0-9]+" | cut -d= -f2 | sort -n | tail -1)
    echo "  captured $(wc -c < "$OUT/vrs_heavy_field.png") bytes (peak draws=${d:-0})"
  fi
  kill -0 $RP 2>/dev/null || { echo "route exited before the field was reached"; break; }
done
wait $RP 2>/dev/null
echo "--- route tail ---"; tail -6 "$OUT/vrs_capture_route.log"
