#!/usr/bin/env bash
# READ THE GEARS JOB-QUEUE STATE DURING THE FREEZE, FROM GUEST MEMORY.
#
# The freeze is a job-queue drain barrier that never completes. Reasoning about
# WHICH branch of the dispatcher ran is guesswork; the guest words say it.
#
# Addresses come from disassembly (see CLAUDE.md):
#   0x82BFA380  job-system enable flag  (dispatcher AND pump thread both gate on it)
#   0x82BFA38C  the object whose vt[5] returns an owner thread id
#   0x82BFA394  a global main swaps out before dispatching
#   0x82C0CB24  the command buffer: +0x0 base, +0x8 write, +0xC limit, +0x14 read
#
# ⚠ /data/local/tmp IS NOT WRITABLE BY THE APP (it runs as u0_aNNN, not shell).
# The dump goes to the app's own files dir and comes back via run-as.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
PKG=jp.xenia.emulator.github.debug
GAME="${GAME:-/storage/2664-21DE/Roms/xbox360/Gears of War (World) (En,Fr,De,Es,It,Zh,Ko).iso}"
AT_MS="${AT_MS:-35000}"
MB="${MB:-16}"   # default base 0x82000000 covers 0x82BFA380 and 0x82C0CB24
BASE_DEC="${BASE_DEC:-}"  # DECIMAL only: `am start --ei` rejects >INT32_MAX hex
SECS="${SECS:-60}"
OUT=scratchpad/gears
mkdir -p "$OUT"
adb_(){ timeout 25 "$ADB" -s "$DEV" "$@"; }
say(){ echo "[$(date +%H:%M:%S)] $*"; }
temp(){ t=$(adb_ shell "cat /sys/class/kgsl/kgsl-3d0/temp" 2>/dev/null | tr -d '\r')
        case "$t" in ''|*[!0-9]*) echo 999999;; *) echo "$t";; esac; }

busy=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '\r')
[ "$busy" = "0" ] || { say "ABORT: rpcs3 running"; exit 1; }
BAT=$(adb_ shell "dumpsys battery" | tr -d '\r')
LVL=$(echo "$BAT" | sed -nE 's/^ *level: ([0-9]+)/\1/p')
CHG=$(echo "$BAT" | sed -nE 's/^ *status: ([0-9]+)/\1/p')
say "battery=${LVL}% status=${CHG}"
[ "${LVL:-0}" -ge 30 ] || [ "$CHG" = "2" ] || { say "ABORT: low battery"; exit 1; }
for i in $(seq 1 40); do T=$(temp); [ "$T" -le 50000 ] && break; sleep 10; done
[ "$T" -le 50000 ] || { say "ABORT: no cooldown"; exit 1; }
say "start temp=$((T/1000))C  dump base=0x82000000 (default) ${MB}MB at ${AT_MS}ms"

adb_ shell "am force-stop $PKG" >/dev/null
adb_ shell "run-as $PKG rm -f files/gm.bin" >/dev/null 2>&1
adb_ logcat -G 64M >/dev/null 2>&1; adb_ logcat -c >/dev/null 2>&1
adb_ shell "input keyevent KEYCODE_WAKEUP" >/dev/null
NATIVE=$(dirname "$(adb_ shell "pm path $PKG" | sed 's/^package://' | tr -d '\r')")/lib/arm64
DRV=$(adb_ shell "run-as $PKG ls files/gpu_drivers/" | tr -d '\r' | head -1)
[ -n "$DRV" ] || { say "ABORT: no Turnip driver"; exit 1; }

adb_ shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm false --ez cpu_aot_maximize true --ez vulkan_trace_draw_outcomes_per_frame true --ei gpu_frame_limit_fps 30 --ei dump_guest_mem_at_ms $AT_MS --ei dump_guest_mem_size_mb $MB ${BASE_DEC:+--ei dump_guest_mem_base $BASE_DEC} --es dump_guest_mem_path '/data/data/$PKG/files/gm.bin' --es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '/data/user/0/$PKG/files/gpu_drivers/$DRV/' --es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '$NATIVE'" >/dev/null 2>&1

for i in $(seq 1 $((SECS/10))); do
  sleep 10; tt=$(temp)
  vt=$(adb_ logcat -d -s xenia:* 2>/dev/null | grep -oE "total_vertices=[0-9]+" | grep -oE "[0-9]+" | sort -n | tail -1)
  st=$(adb_ logcat -d -s xenia:* 2>/dev/null | grep -c "has waited")
  say "  $((i*10))s $((tt/1000))C peakverts=${vt:-0} stalls=$st"
  [ "$tt" -ge 70000 ] && { say "  70C guard"; break; }
done
adb_ logcat -d -s xenia:* > "$OUT/memdump.log" 2>/dev/null
say "dump line: $(grep -i 'guest memory' "$OUT/memdump.log" | tail -1 | sed -E 's/^.*xenia[ ]*: //' | cut -c1-140)"
adb_ exec-out "run-as $PKG cat files/gm.bin" > "$OUT/gm.bin" 2>/dev/null
adb_ shell "am force-stop $PKG" >/dev/null
say "force-stopped; $(wc -c < "$OUT/gm.bin") bytes -> $OUT/gm.bin"
