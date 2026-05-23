# 82490030 Filtered Capture Black-Idle

Date: 2026-05-23 15:41 EDT

## Goal

Run the first filtered `82490030` HIR plus delayed block body-time capture after
`8227F1D8 -> 82490030` proved to be the hot child edge.

## Capture

- Capture prefix: `scratch/thor-debug/20260523-153726-*`
- Logcat: `scratch/thor-debug/20260523-153726-speed-logcat.txt`
- Screenshot: `scratch/thor-debug/20260523-153726-screenshot.png`
- HEAD: `b06530e75`
- APK SHA-256:
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`
- Route result: black-idle before opening route
- Fatal-marker search: clean

Launch lane:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -DisassembleFunctions true -DisassembleFunctionFilter 82490030 -Arm64SpeedProfileBodyTimeFilter "8227F1D8,82490030" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter "82490030" -Arm64SpeedProfileBlockBodyTime true -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

## Result

This is not usable `82490030` HIR/body-time evidence. The route black-idled
before the target function reached the opening route.

Evidence:

```text
15:37:45 entry_delta=3803069
15:38:00 entry_delta=230348
15:38:15 entry_delta=0
15:39:30 body-time profile activated after 120000ms
15:39:30..15:40:30 entry_delta=0 repeatedly
```

No `82490030` filtered function dump, body-time row, or block body-time row
landed. The final screenshot was black. The fatal-marker search returned no
matches.

Idle owner attribution fired:

```text
A64 idle thread snapshot trigger: counters went flat after previous activity
A64 thread snapshot skipped: processor debug lock busy
last_global_owner_sys_tid=15739
last_global_owner_thread_id=F80002E8
owner_hint=hit
owner_hint_source=thread_id_or_handle
owner_hint_sys_tid=802753712
owner_hint_tid=00000016
owner_hint_handle=F80002E8
owner_hint_state=zombie
```

The useful read is that this run stalled around a busy processor debug lock
with a zombied XThread owner hint, not that `82490030` generated code is safe to
patch.

Final perf is also an idle signature rather than a speed result:

```text
Main Thread 0.0%
GPU Commands 0.0%
Draw Thread 0.0%
```

## Decision

Do not repeat this exact filtered capture unchanged and do not start a codegen
experiment from it.

Next useful slice:

1. Re-prove route stability on the same APK with a no-disassembly delayed
   body-time control for `8227F1D8,82490030`.
2. If that reaches visible opening and emits comparable `8227F1D8` /
   `82490030` body rows, rerun the filtered `82490030` HIR/block capture.
3. If the no-disassembly control also black-idles, improve the idle attribution
   around zombie owner hints and native system TID mapping before changing
   generated-code behavior.

Suggested control:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227F1D8,82490030" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```
