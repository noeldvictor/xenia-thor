#!/usr/bin/env bash
# Keep the device clear for a measurement run. User directive 2026-08-19:
# "try again shutdown all other apps on device". rpcs3 relaunches every few
# minutes and a mid-run appearance voids an arm, so this force-stops it on a
# short cycle for the duration. Run it ALONGSIDE the measurement, not before.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
SECS="${SECS:-900}"
end=$(( $(date +%s) + SECS ))
while [ $(date +%s) -lt $end ]; do
  timeout 20 "$ADB" -s "$DEV" shell "am force-stop net.rpcsx.easy" >/dev/null 2>&1
  sleep 20
done
