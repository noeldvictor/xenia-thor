# A64 Owner Native TID Attribution

Date: 2026-05-23 16:06 EDT

## Goal

Improve the Blue Dragon black-idle owner diagnostics before changing generated
code. The previous `82490030` filtered and no-disassembly controls both
flatlined with `last_global_owner_thread_id=F80002E8` and a zombied owner hint,
but `owner_hint_sys_tid` looked like a stale `pthread_t` value rather than a
Linux native thread ID.

## Patch

- Added `Processor::OnThreadNativeStarted(thread_id, system_thread_id)`.
- Called it from the XThread host lambda after `set_current_thread_id()` using
  `xe::threading::current_thread_system_id()`.
- This replaces the initial Android `pthread_t`-style `Thread::system_id()`
  hint with the real Linux `gettid()` value after the host thread starts.
- Extended the A64 idle snapshot skip line with `/proc/self/task/<tid>` liveness
  for both:
  - `last_global_owner_sys_tid`
  - `owner_hint.system_thread_id`
- New fields:
  - `native_owner_live`
  - `native_owner_name`
  - `native_owner_state`
  - `owner_hint_native_live`
  - `owner_hint_native_name`
  - `owner_hint_native_state`

## Validation

Build and deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Results:

- `NativeCore`: passed.
- `FullDeploy`: passed.
- Installed APK SHA-256:
  `2E3D88F46BB709AA3A869634C24219FBBA0568695C1F4902693132701CF9EBE5`
- `MANAGE_EXTERNAL_STORAGE`: allowed.

Short attribution capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "60,95" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227F1D8,82490030" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Capture:

- Prefix: `scratch/thor-debug/20260523-160357-*`
- Starting HEAD in metadata: `64a915c52`
- APK SHA-256:
  `2E3D88F46BB709AA3A869634C24219FBBA0568695C1F4902693132701CF9EBE5`
- Final screenshot: black
- Fatal-marker search: clean
- Counters:
  - `16:04:16`: `entry_delta=3820291`
  - `16:04:31`: `entry_delta=195869`
  - `16:04:46` onward: `entry_delta=0`

Idle owner line:

```text
last_global_owner_sys_tid=28245 last_global_owner_thread_id=F80002EC
native_owner_live=false native_owner_name='-' native_owner_state='-'
owner_hint=hit owner_hint_source=system_tid owner_hint_sys_tid=28245
owner_hint_tid=00000016 owner_hint_handle=F80002EC owner_hint_state=zombie
owner_hint_native_live=false owner_hint_native_name='-'
owner_hint_native_state='-'
```

## Interpretation

The patch fixed the stale `pthread_t` hint problem: `owner_hint_source` is now
`system_tid`, and `owner_hint_sys_tid` matches `last_global_owner_sys_tid`.

The stronger signal is that `/proc/self/task/28245` was already gone when the
snapshot fired, while the cached XThread state was `zombie`. The lock owner
record is therefore pointing at a dead native thread, not a live stuck thread.
This makes an `82490030` codegen patch the wrong next move.

## Next Action

Do not rerun the exact `82490030` filtered/no-disassembly captures unchanged,
and do not patch generated code from this evidence.

Next useful slice: instrument or audit global critical-region ownership around
the processor debug lock so a future black-idle can distinguish:

- stale "last owner" bookkeeping after a clean release;
- a native thread exiting while holding the global critical region;
- recursive ownership that survives into XThread exit/destruction;
- another live owner that did not update the last-owner hint.

Keep the next patch diagnostic-only and default-on only for the existing
idle-snapshot path.
