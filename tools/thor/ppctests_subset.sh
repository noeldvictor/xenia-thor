#!/usr/bin/env bash
# RUN A SUBSET OF THE PPC HARDWARE CORPUS ON THE DEVICE, AGAINST THE a64 BACKEND.
#
# WHY A SUBSET: the full suite reserves ~17 GB of guest address space and is
# SIGKILLed unless the device is otherwise idle, and it takes >10 minutes. A
# targeted directory answers "did my a64 codegen change break this instruction"
# in seconds, which is the question an emitter change actually raises.
#
# WHY THE DEVICE AT ALL: the a64 backend is NOT built on desktop, so the Windows
# corpus run cannot see it, and the qemu harness models ISA semantics rather
# than our emitter. This binary is the only thing that executes our a64 output
# against hardware-captured expectations.
set -u
ADB="${ADB:-C:/Users/leanerdesigner/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
DEV="${DEV:-192.168.1.33:5555}"
BIN="${BIN:-obj/local/arm64-v8a/xenia-cpu-ppc-tests}"
TESTS="${TESTS:?set TESTS to a local dir containing testing/ and testing/bin/}"
REMOTE=/data/local/tmp/ppcsub
adb_(){ timeout 120 "$ADB" -s "$DEV" "$@"; }
say(){ echo "[$(date +%H:%M:%S)] $*"; }

busy=$(adb_ shell "ps -A -o NAME | grep -icE rpcs" 2>/dev/null | tr -d '\r')
[ "$busy" = "0" ] || { say "ABORT: rpcs3 running - shared device"; exit 1; }
adb_ shell "am force-stop jp.xenia.emulator.github.debug" >/dev/null 2>&1
[ -f "$BIN" ] || { say "ABORT: $BIN missing"; exit 1; }

adb_ shell "rm -rf $REMOTE && mkdir -p $REMOTE" >/dev/null
# MSYS_NO_PATHCONV: Git Bash rewrites a POSIX-looking adb DESTINATION into a
# Windows path and adb still prints "1 file pushed". cygpath -m keeps the local
# source readable to adb.exe at the same time.
MSYS_NO_PATHCONV=1 adb_ push "$(cygpath -m "$BIN")" "$REMOTE/ppctests" >/dev/null
MSYS_NO_PATHCONV=1 adb_ push "$(cygpath -m "$TESTS/testing")" "$REMOTE/" >/dev/null
adb_ shell "chmod 755 $REMOTE/ppctests" >/dev/null
say "pushed $(adb_ shell "ls $REMOTE/testing | wc -l" | tr -d '\r') test files"

# Lead with cd: an adb shell argument that STARTS with / is path-converted even
# inside double quotes.
adb_ shell "cd $REMOTE && ./ppctests --test_path=testing/ --test_bin_path=testing/bin/ --break_on_unimplemented_instructions=false" 2>&1 | tr -d '\r' | tail -25
adb_ shell "rm -rf $REMOTE" >/dev/null
