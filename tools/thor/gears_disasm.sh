#!/usr/bin/env bash
# DUMP GUEST DISASSEMBLY FOR NAMED FUNCTIONS - NO ROUTE, NO THERMAL BUDGET.
#
# WHY THIS IS CHEAP AND NOBODY WAS USING IT: the dumps land during LOAD, so a
# disassembly run costs ~20 SECONDS and never reaches gameplay. It is the
# cheapest instrument in the tree for a guest-code question, and this project
# spent eight instruments on the Gears freeze before reaching for it.
#
# --es disassemble_function_filter takes a COMMA LIST and matches any address
# INSIDE a function, so a return address from a backtrace works directly.
#
# TRAPS CARRIED (all previously paid for in this repo):
#  - every adb call is bounded: a dropped USB endpoint once hung an unwrapped
#    call so the polling loop never came back round and THE 70C GUARD NEVER
#    EVALUATED (device reached 78.1C).
#  - the driver name is read off the DEVICE (a stale path silently falls back to
#    the Qualcomm blob), and nativeLibraryDir is derived fresh after any install.
#  - the remote `cat` is QUOTED: unquoted, Git Bash rewrites /sys/... into a
#    Windows path and the temperature parses EMPTY, so the guard is inert.
#  - a64 backend by default: it needs no LLVM AOT, so the dump appears in
#    seconds instead of after a multi-minute compile.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
PKG=jp.xenia.emulator.github.debug
GAME="${GAME:-/storage/2664-21DE/Roms/xbox360/Gears of War (World) (En,Fr,De,Es,It,Zh,Ko).iso}"
FILTER="${FILTER:?set FILTER to a comma list of guest addresses, e.g. 827A7B08,82444EF0}"
LABEL="${LABEL:-disasm}"
SECS="${SECS:-25}"
COOL="${COOL:-50000}"
OUT=scratchpad/gears
mkdir -p "$OUT"
adb_(){ timeout 25 "$ADB" -s "$DEV" "$@"; }
say(){ echo "[$(date +%H:%M:%S)] $*"; }
temp(){ t=$(adb_ shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d '\r')
        case "$t" in ''|*[!0-9]*) echo 999999;; *) echo "$t";; esac; }

busy=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '\r')
[ "$busy" = "0" ] || { say "ABORT: rpcs3 running - shared device"; exit 1; }
BAT=$(adb_ shell "dumpsys battery" | tr -d '\r')
LVL=$(echo "$BAT" | sed -nE 's/^ *level: ([0-9]+)/\1/p')
CHG=$(echo "$BAT" | sed -nE 's/^ *status: ([0-9]+)/\1/p')
say "battery=${LVL}% status=${CHG} (2=charging)"
[ "${LVL:-0}" -ge 30 ] || [ "$CHG" = "2" ] || { say "ABORT: battery ${LVL}% and not charging"; exit 1; }
T=$(temp); say "temp=$((T/1000))C"
[ "$T" -le "$COOL" ] || { say "ABORT: too hot"; exit 1; }

adb_ shell "am force-stop $PKG" >/dev/null
adb_ logcat -G 64M >/dev/null 2>&1; adb_ logcat -c >/dev/null 2>&1
adb_ shell "input keyevent KEYCODE_WAKEUP" >/dev/null
NATIVE=$(dirname "$(adb_ shell "pm path $PKG" | sed 's/^package://' | tr -d '\r')")/lib/arm64
DRV=$(adb_ shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)
[ -n "$DRV" ] || { say "ABORT: no Turnip driver"; exit 1; }
say "driver=$DRV  filter=$FILTER"

adb_ shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm false --ez cpu_aot_maximize true --es disassemble_function_filter '$FILTER' --ei gpu_frame_limit_fps 30 --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '/data/user/0/$PKG/files/gpu_drivers/$DRV/' --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1

for i in $(seq 1 $((SECS/5))); do
  sleep 5; tt=$(temp)
  say "  $((i*5))s $((tt/1000))C"
  [ "$tt" -ge 70000 ] && { say "  70C guard"; break; }
done
adb_ logcat -d -s xenia:* > "$OUT/${LABEL}.log" 2>/dev/null
adb_ shell "am force-stop $PKG" >/dev/null
say "force-stopped; log -> $OUT/${LABEL}.log"

echo
for a in $(echo "$FILTER" | tr ',' ' '); do
  echo "================ $a ================"
  grep "Filtered function dump $a " "$OUT/${LABEL}.log" | sed -E 's/^.*Filtered function dump [0-9A-F]+ PPC: //' | cut -c1-100
done
