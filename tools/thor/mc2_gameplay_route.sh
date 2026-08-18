#!/usr/bin/env bash
# MAGNACARTA 2 GAMEPLAY ROUTE - THE PROJECT'S SECOND MEASURABLE TITLE.
#
# Device-verified 2026-08-17: reaches gameplay-tier geometry at ~t=70s and
# sustains it. Measured over a 150s run:
#     gameplay frames (>100k verts) : n=2,463  median gpu_frame_us=10,130
#     menu/loader     (<50k  verts) : n=1,831  median gpu_frame_us=1,749
#     peak vertices                 : 2,343,400   (Blue Dragon's field is ~263k)
#     thermals                      : 40C -> 67C over 150s, no guard trip
#
# WHY THIS TITLE MATTERS, and it is not "another JRPG":
#   * It is UNCAPPED. GameProfiles gives it no gpu_frame_limit_fps, unlike
#     Burnout (60) / Gears (30) / Banjo (30) / BTTF (30). A capped title cannot
#     show a CPU or GPU win at all - that is why Burnout is useless as a
#     benchmark despite running perfectly.
#   * Its GPU frame is ~10 ms, i.e. the GPU is NOWHERE NEAR the limiter here.
#     Blue Dragon's field is ~63 ms and 89.6% in-pass shading. So MC2 is a
#     structurally DIFFERENT workload from the only title this project could
#     previously measure - which is the entire point of having a second one.
#
# ⚠️ IT REQUIRES THE a64 BACKEND TODAY: on LLVM it dies during AOT at
# ~33,280/47,353 functions with a Scudo map failure (it is the largest title on
# the device). That is a separate, unfixed bug - see the OOM entry.
#
# ⚠️ AND IT ONLY BOOTS AT ALL AFTER THE DefineSymbol FIX (2026-08-17). Before
# that it reached `Title name` and died within ~10s with an anonymous SIGTRAP.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
PKG=jp.xenia.emulator.github.debug
GAME="${GAME:-/storage/2664-21DE/Roms/xbox360/Magna Carta 2.m3u/MagnaCarta 2 (USA) (En,Fr) (Disc 1).iso}"
# Title is up by ~10s on a64, so press early. Extra presses on a screen that
# ignores them are harmless; the sequence is deliberately redundant because a
# JRPG boot is title -> menu -> new game -> cutscenes, and the cutscene lengths
# are not fixed.
SEQ="${SEQ:-start@12000:1200;a@17000:1200;a@22000:1200;start@27000:1200;a@32000:1200;a@37000:1200;start@43000:1200;a@48000:1200;a@54000:1200;start@60000:1200;a@66000:1200;a@72000:1200;start@78000:1200;a@84000:1200;a@90000:1200;start@96000:1200;a@102000:1200;a@108000:1200}"
EXTRA="${EXTRA:-}"
COOL="${COOL:-42000}"
GUARD="${GUARD:-70000}"
SECS="${SECS:-150}"
OUT=scratchpad/titles
mkdir -p "$OUT"
LABEL="${LABEL:-mc2}"
adb_(){ timeout 25 "$ADB" -s "$DEV" "$@"; }   # bounded: a hung transport once disabled the thermal guard
say(){ echo "[$(date +%H:%M:%S)] $*"; }
temp(){ t=$(adb_ shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d '\r')
        case "$t" in ''|*[!0-9]*) echo 999999;; *) echo "$t";; esac; }

busy=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '\r')
[ "$busy" = "0" ] || { say "ABORT: rpcs3 running - shared device"; exit 1; }
for i in $(seq 1 60); do T=$(temp); [ "$T" -le "$COOL" ] && break; sleep 10; done
[ "$T" -le "$COOL" ] || { say "ABORT: no cooldown to $((COOL/1000))C"; exit 1; }
say "cold at $((T/1000))C"

adb_ shell "am force-stop $PKG" >/dev/null
adb_ logcat -G 64M >/dev/null 2>&1; adb_ logcat -c >/dev/null 2>&1
adb_ shell "input keyevent KEYCODE_WAKEUP" >/dev/null
NATIVE=$(dirname "$(adb_ shell "pm path $PKG" | sed 's/^package://' | tr -d '\r')")/lib/arm64
DRV=$(adb_ shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)
[ -n "$DRV" ] || { say "ABORT: no Turnip driver"; exit 1; }
say "driver=$DRV"
adb_ shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm false --ez cpu_aot_maximize true --ez vulkan_trace_draw_outcomes_per_frame true --es hid nop --es hid_nop_button_sequence '$SEQ' $EXTRA --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '/data/user/0/$PKG/files/gpu_drivers/$DRV/' --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1

n=$((SECS/10))
for i in $(seq 1 $n); do
  sleep 10; tt=$(temp); alive=$(adb_ shell "pidof $PKG" | tr -d '\r')
  say "  $((i*10))s $((tt/1000))C alive=$([ -n "$alive" ] && echo Y || echo N)"
  [ -z "$alive" ] && { say "  DIED"; break; }
  [ "$tt" -ge "$GUARD" ] && { say "  70C guard"; break; }
done
adb_ logcat -d -s xenia:* > "$OUT/${LABEL}.log" 2>/dev/null
adb_ shell "am force-stop $PKG" >/dev/null
say "log -> $OUT/${LABEL}.log"

echo
echo "================ $LABEL FRAME TIMES ================"
python - "$OUT/${LABEL}.log" <<'PY'
import io,re,sys,statistics
rows=[]
for l in io.open(sys.argv[1],encoding='utf-8',errors='replace'):
    v=re.search(r'total_vertices=(\d+)',l); g=re.search(r'gpu_frame_us=(\d+)',l)
    if v and g: rows.append((int(v.group(1)),int(g.group(1))))
print("frames: %d   peak verts: %d" % (len(rows), max([v for v,_ in rows]) if rows else 0))
# Bucket by scene complexity, never by the metric under test - filtering frames
# by a function of frame time is selection on the dependent variable and has
# inverted a verdict in this project before.
for lo,hi,name in [(100000,10**9,"gameplay >100k"),(50000,100000,"50k-100k"),(0,50000,"menu/loader <50k")]:
    s=[g for v,g in rows if lo<=v<hi and g>0]
    if len(s)>=5:
        m=statistics.median(s)
        print("  %-18s n=%-5d median gpu_frame_us=%-8d => %.2f fps (GPU-side)" % (name,len(s),m,1e6/m))
PY
echo
echo "NOTE: gpu_frame_us is GPU frame time, NOT wall-clock fps. A ~10ms GPU frame"
echo "means the GPU is not the limiter here - which is what makes this title the"
echo "right vehicle for CPU work, and the opposite of Blue Dragon."
