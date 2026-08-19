#!/usr/bin/env bash
# Wait for the shared device to free up, then run a command. User directive
# 2026-08-18: "wait if in use after 15-20 mins kill" - so this polls for the
# other session's rpcs3 to exit, and force-stops it only once the grace period
# has fully elapsed. Never kills early: the device is shared and their run is a
# measurement too.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
GRACE_S="${GRACE_S:-1080}"      # 18 min, mid-point of the 15-20 the user gave
adb_(){ timeout 25 "$ADB" -s "$DEV" "$@"; }
say(){ echo "[$(date +%H:%M:%S)] $*"; }
start=$(date +%s)
while :; do
  busy=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '\r')
  now=$(date +%s); el=$((now-start))
  if [ "${busy:-1}" = "0" ]; then say "device FREE after ${el}s - proceeding"; break; fi
  if [ "$el" -ge "$GRACE_S" ]; then
    say "grace ${GRACE_S}s elapsed and rpcs3 still up - force-stopping it per directive"
    adb_ shell "am force-stop net.rpcsx.easy" >/dev/null 2>&1
    sleep 5
    still=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '\r')
    say "after force-stop: rpcs=$still"
    break
  fi
  say "rpcs3 up, waited ${el}s of ${GRACE_S}s"
  sleep 60
done
exec "$@"
