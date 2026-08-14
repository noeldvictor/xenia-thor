#!/usr/bin/env bash
# Does the Android `wrap.<pkg>` mechanism work AT ALL on this device?
#
# WHY: the Mesa/Turnip tracing recipe in CLAUDE.md depends on wrap. to inject
# MESA_GPU_TRACES into the emulator, and a real, getprop-confirmed value
# produced ZERO xenia log lines - the process forked and never attached. Before
# blaming the value, find out whether any wrap value launches at all.
#
# Candidate causes, in order of suspicion:
#   - property NAME length: "wrap.jp.xenia.emulator.github.debug" is 35 chars
#   - the value must be an executable COMMAND, not just env assignments
#   - the quoted form '"..."' vs the bare form
#
# Each arm: set the property, launch, wait, and report whether the process
# survived AND whether the emulator actually logged. Both matter - a live pid
# with no log lines is the "failed to attach" shape.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${THOR_SERIAL:-c3ca0370}"
PKG=jp.xenia.emulator.github.debug
ACT=$PKG/jp.xenia.emulator.EmulatorActivity

busy=$("$ADB" -s "$DEV" shell 'ps -A -o NAME 2>/dev/null | grep -icE "rpcs|rpcsx"' | tr -d '\r')
[ "${busy:-1}" = "0" ] || { echo "ABORT: rpcs3 running - device is SHARED"; exit 1; }

# ALWAYS leave the property genuinely empty. Setting it to '""' sets the literal
# 2-char value "" and bricks every launch of the package until reboot.
cleanup() {
  "$ADB" -s "$DEV" shell "setprop wrap.$PKG ''" >/dev/null 2>&1
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1
}
trap cleanup EXIT

echo "wrap.$PKG is ${#PKG} + 5 = $(( ${#PKG} + 5 )) characters"

try() { # $1 = human label, $2 = raw setprop argument
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1
  "$ADB" -s "$DEV" logcat -c
  "$ADB" -s "$DEV" shell "setprop wrap.$PKG $2" >/dev/null 2>&1
  got=$("$ADB" -s "$DEV" shell "getprop wrap.$PKG" | tr -d '\r')
  "$ADB" -s "$DEV" shell "am start -n $ACT" >/dev/null 2>&1
  sleep 9
  pid=$("$ADB" -s "$DEV" shell "pidof $PKG" | tr -d '\r')
  xl=$("$ADB" -s "$DEV" shell "logcat -d -s xenia:*" 2>/dev/null | grep -c "xenia")
  att=$("$ADB" -s "$DEV" shell "logcat -d" 2>/dev/null | grep -c "failed to attach")
  printf "%-26s prop=[%s] pid=%-7s xenia_lines=%-5s failed_to_attach=%s\n" \
         "$1" "$got" "${pid:-none}" "$xl" "$att"
  "$ADB" -s "$DEV" shell am force-stop $PKG >/dev/null 2>&1
}

try "control (empty)"        "''"
try "bare env"               "env"
try "quoted env"             "'\"env\"'"
try "bare VAR=x"             "MESA_GPU_TRACES=print"
try "quoted VAR=x"           "'\"MESA_GPU_TRACES=print\"'"
try "env + VAR=x quoted"     "'\"env MESA_GPU_TRACES=print\"'"
# THE FIX: single-quote at the SHELL level so setprop receives one argument, but
# put NO literal double quotes in the property VALUE. `env VAR=.. VAR=.. <cmd>`
# is then a real command. The '"..."' form in the old recipe embeds literal
# quotes, so the shell tries to EXECUTE a program named "MESA_GPU_TRACES=print".
try "env + 2 vars, no quotes" "'env MESA_GPU_TRACES=print MESA_GPU_TRACEFILE=/sdcard/u.txt'"
