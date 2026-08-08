#!/usr/bin/env bash
# Three-arm affinity A/B + VMX pressure census, in one unattended run.
#
# WHY THIS EXISTS: the 2026-08-07 manual review produced four structural
# findings, of which one is fixed (guest threads were HARD-PINNED to the 2.0GHz
# A510 little cores while the Cortex-X3 sat idle) and three are hypotheses about
# vector register pressure. All four need the device, and the goal is a POWER
# target (rpcs3: ~5W, 50C), so throughput alone answers the wrong question.
#
# ARMS
#   A  baseline    guest CPU N -> host cpu N   (the old 1:1 map; guest 0-2 on A510)
#   B  prime       guest 0 -> X3 prime, rest -> perf/legacy tiers   (current default)
#   C  mid-tier    guest 0 -> A715, rest round-robin                (efficiency control)
#
# Arm A is reproduced with thor_guest_thread_affinity_mask=0 ONLY IF the build
# still has the 1:1 path; after 759e2b59d the default IS arm B, so arm A needs
# the pre-fix build. Use: git stash / build / install, or check out 759e2b59d^.
# Arms B and C are selectable at runtime:
#   B = default (no cvar)
#   C = --ei thor_guest_thread_affinity_mask <A715 mask>   (see below)
#
# ⚡ WATTS ARE THE POINT AND THEY NEED THE CABLE OUT.
# Plugged in, dumpsys reports status=Charging and current_now flips sign between
# consecutive idle samples (measured: -36988, +225591, +165897, +224859, -16846
# uA). Any wattage from a USB-attached session is fiction. This script REFUSES
# to report power unless the device says Discharging, and connects over wifi so
# the cable can be removed:  adb connect <ip>:5555   (already configured once at
# 192.168.1.33:5555)
set -u
ADB="C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe"
DEV="${THOR_SERIAL:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
DRV=mesa-turnip-v26.3.0-20260803-r7-vulkan-1.4.354-7
GAME='/storage/2664-21DE/Roms/xbox360/Gears of War (World) (En,Fr,De,Es,It,Zh,Ko).iso'
# Route that reaches Act 1 gameplay at ~125-150s (device-verified 2026-08-07).
SEQ='start@40000:1200;start@47000:1200;start@53000:1200;start@59000:1200;start@65000:1200;start@71000:1200;a@79000:1200;a@86000:1200;a@93000:1200;a@100000:1200;a@107000:1200;a@114000:1200'
COOL_TO=45000     # milli-C; the route needs a cold start or it guards out
GUARD=72000       # force-stop above this
RUN_S=170

say() { echo "[$(date +%H:%M:%S)] $*"; }

native_dir() {  # MUST be re-read after every install - the APK dir is randomised
  echo "$(dirname "$("$ADB" -s "$DEV" shell pm path $PKG | head -1 | sed 's/^package://' | tr -d '\r')")/lib/arm64"
}

preflight() {
  local t b st
  t=$("$ADB" -s "$DEV" shell cat /sys/class/kgsl/kgsl-3d0/temp | tr -d '\r')
  b=$("$ADB" -s "$DEV" shell dumpsys battery | grep -m1 level | grep -oE '[0-9]+')
  st=$("$ADB" -s "$DEV" shell dumpsys battery | grep -m1 status | grep -oE '[0-9]+')
  say "pre-flight: temp=$((t/1000))C battery=${b}% battery_status=${st} (2=Charging 3=Discharging)"
  [ "$b" -lt 30 ] && { say "BATTERY TOO LOW - refusing"; return 1; }
  return 0
}

cooldown() {
  say "cooling to <$((COOL_TO/1000))C..."
  for _ in $(seq 1 60); do
    local t; t=$("$ADB" -s "$DEV" shell cat /sys/class/kgsl/kgsl-3d0/temp | tr -d '\r')
    [ "$t" -lt "$COOL_TO" ] && { say "  cooled to $((t/1000))C"; return 0; }
    sleep 15
  done
  say "  did not reach target - aborting arm"; return 1
}

# Samples power ONLY when genuinely discharging; otherwise reports UNMEASURABLE.
power_sample() {
  local st c v
  st=$("$ADB" -s "$DEV" shell dumpsys battery | grep -m1 status | grep -oE '[0-9]+')
  if [ "$st" != "3" ]; then echo "UNMEASURABLE(plugged)"; return; fi
  c=$("$ADB" -s "$DEV" shell cat /sys/class/power_supply/battery/current_now | tr -d '\r')
  v=$("$ADB" -s "$DEV" shell cat /sys/class/power_supply/battery/voltage_now | tr -d '\r')
  awk -v c="$c" -v v="$v" 'BEGIN{ if (c<0) c=-c; printf "%.2fW", (c/1e6)*(v/1e6) }'
}

run_arm() {           # $1 = arm label, $2..$n = extra am-start args
  local label="$1"; shift
  cooldown || return 1
  local idle_w; idle_w=$(power_sample)
  local t0; t0=$("$ADB" -s "$DEV" shell cat /sys/class/kgsl/kgsl-3d0/temp | tr -d '\r')
  local nat; nat=$(native_dir)
  "$ADB" -s "$DEV" shell am force-stop $PKG; "$ADB" -s "$DEV" logcat -c
  "$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity \
    --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm true \
    --ez cpu_aot_maximize true --ez cpu_llvm_target_features_native true \
    --ei arm64_speed_profile_interval_ms 5000 \
    --es hid nop --es hid_nop_button_sequence '$SEQ' \
    --es gpu_vulkan_driver turnip \
    --es gpu_vulkan_driver_path '/data/data/$PKG/files/gpu_drivers/$DRV/' \
    --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
    --es gpu_vulkan_driver_hooks_path '$nat' $*" >/dev/null 2>&1
  say "arm $label running (${RUN_S}s)..."
  local peak=0 run_w="n/a"
  for i in $(seq 1 $((RUN_S/5))); do
    local t; t=$("$ADB" -s "$DEV" shell cat /sys/class/kgsl/kgsl-3d0/temp 2>/dev/null | tr -d '\r')
    [ -n "$t" ] && [ "$t" -gt "$peak" ] && peak=$t
    [ -n "$t" ] && [ "$t" -gt "$GUARD" ] && { say "  70C+ guard"; break; }
    [ "$i" -eq $((RUN_S/10)) ] && run_w=$(power_sample)   # sample mid-run, in gameplay
    sleep 5
  done
  "$ADB" -s "$DEV" shell am force-stop $PKG
  # entry_delta: average the last 4 windows, they are noisy in gameplay
  local ed
  ed=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null \
       | grep -oE "entry_delta=[0-9]+" | tail -4 | cut -d= -f2 \
       | awk '{s+=$1; n++} END{ if(n) printf "%d", s/n; else print "n/a" }')
  printf '%-10s start=%sC peak=%sC idle=%s run=%s entry_delta_avg=%s\n' \
    "$label" "$((t0/1000))" "$((peak/1000))" "$idle_w" "$run_w" "$ed" | tee -a /tmp/thor_ab.txt
}

census() {
  cooldown || return 1
  local nat; nat=$(native_dir)
  "$ADB" -s "$DEV" shell am force-stop $PKG; "$ADB" -s "$DEV" logcat -c
  # No route needed - the AOT precompile alone produces the whole distribution.
  "$ADB" -s "$DEV" shell "am start -n $PKG/jp.xenia.emulator.EmulatorActivity \
    --es target '$GAME' --es cpu arm64 --ez cpu_backend_llvm true \
    --ez cpu_aot_maximize true --ez a64_vmx_pressure_census true \
    --ez arm64_register_allocation_audit true \
    --es gpu_vulkan_driver turnip \
    --es gpu_vulkan_driver_path '/data/data/$PKG/files/gpu_drivers/$DRV/' \
    --es gpu_vulkan_driver_lib libvulkan_freedreno.so \
    --es gpu_vulkan_driver_hooks_path '$nat'" >/dev/null 2>&1
  say "census running (AOT pass only, ~60s)..."
  sleep 75
  "$ADB" -s "$DEV" shell am force-stop $PKG
  say "VMX pressure histogram:"
  "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep "VMXpressure" | tail -3
  say "register-allocator spill pressure - the metric that actually decides it:"
  # spill_requests>0 on set=int means 7 GPRs is a real constraint (review #1);
  # on set=vec means 28 vectors is (review #2). max_active_registers is peak
  # SIMULTANEOUSLY-live, which is what determines spilling.
  "$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep -oE "set=[a-z]+ .*max_active_registers=[0-9]+" | tail -6
}

preflight || exit 1
: > /tmp/thor_ab.txt
say "=== arm B: current default (guest 0 -> prime, MIDR-derived) ==="
run_arm "B-prime"
say "=== arm C: mid-tier control (guest threads -> A715/A710 only) ==="
# 0x78 = cpu3-6, the mid cluster, excluding the X3. Keeps the hot thread off the
# prime core so the DVFS-voltage question can be separated from issue width.
run_arm "C-midtier" "--ei thor_guest_thread_affinity_mask 120"
say "=== VMX pressure census ==="
census
echo
say "SUMMARY (arm A needs the pre-759e2b59d build - see header):"
cat /tmp/thor_ab.txt
