# Patched No-Filter Route Control Black-Idle

Date: 2026-05-22 23:09 EDT
Branch: `master`
HEAD: `0ec440af6`

## Goal

Prove whether the patched APK from `0ec440af6` can still reach the Blue Dragon
opening route without `-DisassembleFunctionFilter`, before spending another
run on filtered `8227FEE8` HIR.

## Capture

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "82282490,8227FEE8" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Artifacts:

- `scratch/thor-debug/20260522-230518-speed-logcat.txt`
- `scratch/thor-debug/20260522-230518-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260522-230518-meta.txt`
- `scratch/thor-debug/20260522-230518-screenshot.png`
- `scratch/thor-debug/20260522-230518-perf-120s.txt`
- `scratch/thor-debug/20260522-230518-perf-150s.txt`
- `scratch/thor-debug/20260522-230518-perf-175s.txt`
- `scratch/thor-debug/20260522-230518-perf-final.txt`

Metadata:

- APK SHA-256:
  `89086669EF6CC19A028049FBF5957827CF7CFA97C85F0083021A739B9C16FFAF`
- `disassemble_functions=false`
- `disassemble_function_filter=`
- `arm64_speed_profile_body_time_filter=82282490,8227FEE8`
- `arm64_speed_profile_body_time_after_ms=120000`
- `arm64_speed_profile_call_edge_filter=`
- `arm64_speed_profile_call_edge_audit_only=false`
- `arm64_context_promotion_gpr_livein_r1=false`

## Result

This patched no-filter control did not reach the opening route. The final
screenshot was black.

Speed counters showed the same early flatline shape as the previous patched
filtered run:

```text
23:05:35 funcs=3539 entry_delta=3834195
23:05:50 funcs=3866 entry_delta=222587
23:06:05 funcs=3866 entry_delta=0
23:06:20 funcs=3866 entry_delta=0
...
23:08:20 funcs=3866 entry_delta=0
```

Body-time activated at `23:07:20` after `120001ms`, but no
`A64 speed profile body top` rows for `82282490` or `8227FEE8` appeared.

The idle snapshot trigger fired when counters went flat, but the processor
debug lock was busy:

```text
A64 idle thread snapshot trigger: counters went flat after previous activity
A64 thread snapshot skipped: processor debug lock busy after_retries=20 last_global_owner_sys_tid=21741 global_lock_count=0 owner_tid=00000000 owner_lr=00000000 owner_ctr=00000000 owner_r1=00000000 owner_r3=00000000 owner_r4=00000000
```

Fatal-marker search found no `Fatal signal`, `AndroidRuntime`, `SIGSEGV`,
`SIGABRT`, `VK_ERROR_DEVICE_LOST`, `GPU is hung`, or native tombstone marker in
the speed logs.

Perf samples confirm a low-CPU idle/stall rather than a hot generated-code
loop. At `120s`, all Xenia guest, GPU, audio, and file threads were sleeping.
At final, only the Android app thread showed about `7.6%` while the emulator
process as a whole was effectively idle.

## Decision

The warning-level filtered-dump patch stays useful as tooling, but this control
does not prove the patched APK route-safe. Do not run the exact filtered
`8227FEE8` capture next.

The next useful slice should add route-stability or idle attribution before
another HIR capture. Good options:

- Make the idle snapshot retry longer or less lock-dependent, and log the guest
  thread owning `last_global_owner_sys_tid` when the processor debug lock is
  busy.
- Add a low-overhead route-progress breadcrumb before the known flatline window,
  so captures can distinguish input/title timing from generated-code stalls.
- Run a control-sandwich only after the idle attribution can explain why
  same-cvar patched controls sometimes black-idle before `82282490`.
