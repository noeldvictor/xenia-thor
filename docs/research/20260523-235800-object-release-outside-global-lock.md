# Object Release Outside Global Lock

Date: 2026-05-23 23:58 EDT

## Goal

Fix the black-idle path identified by the tagged-lifecycle attribution capture.
Commit `11747d104` reproduced the idle with:

```text
global_lock_owner_source='Processor::OnThreadDestroyed'
native_owner_live=false
owner_hint_state=zombie
```

That pointed to an `XThread` destructor running while the object table/global
critical-region lock was still held.

## Patch

`ObjectTable::ReleaseHandle` and `ObjectTable::RemoveHandle` now detach the
handle/table entry while holding the global critical region, but defer the final
`object->Release()` until after that lock is dropped.

Implementation details:

- Added `LookupTableLocked`.
- Added `RemoveHandleLocked`.
- `ReleaseHandle` uses `ObjectTable::ReleaseHandle` as the source tag.
- `RemoveHandle` uses `ObjectTable::RemoveHandle` as the source tag.
- Name-table removal still happens while the table is locked.
- Destructor-running `object->Release()` happens outside the table/global lock.

This is a behavioral fix, not just instrumentation. The intent is to prevent
`XThread::~XThread -> Processor::OnThreadDestroyed` from running under the
object table's global critical-region ownership.

## Validation

Build and deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
Get-FileHash android\android_studio_project\app\build\outputs\apk\github\debug\app-github-debug.apk -Algorithm SHA256
adb -s c3ca0370 shell cmd appops get jp.xenia.emulator.github.debug MANAGE_EXTERNAL_STORAGE
```

Result:

- `NativeCore` passed in 2m 41s.
- `FullDeploy` passed and installed successfully.
- APK SHA256:
  `FE5CBCF23E832807E51547D66387C3680DF6DB4B4802C9DB5BB64ACB46B8489B`.
- `MANAGE_EXTERNAL_STORAGE` remained allowed.

Route validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227F1D8,82490030" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Artifacts:

- `scratch/thor-debug/20260523-235417-speed-logcat.txt`
- `scratch/thor-debug/20260523-235417-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260523-235417-meta.txt`
- `scratch/thor-debug/20260523-235417-screenshot.png`
- `scratch/thor-debug/20260523-235417-perf-final.txt`

Findings:

- Fatal-marker search was clean.
- Final screenshot reached the visible Blue Dragon sky/wing opening route.
- No `A64 thread snapshot skipped` line appeared.
- A64 counters stayed active through the full capture.
- Delayed body-time rows returned:
  - `8227F1D8 body_ticks_total=2956364`, final delta `547564`,
    `entries_delta=10184`, `ticks_per_entry=53`, `code_size=20180`
  - `82490030 body_ticks_total=2877652`, final delta `524512`,
    `entries_delta=10184`, `ticks_per_entry=51`, `code_size=6716`
  - `8227EFC8 body_ticks_total=39202`, final delta `11440`,
    `ticks_per_entry=5`, `code_size=25752`
- Final perf was CPU/JIT-heavy again:
  - Main Thread `92.3%`
  - XMA Decoder `50.0%`
  - GPU Commands `11.5%`
  - Draw Thread `3.8%`

## Decision

Keep the patch. It directly addresses the measured
`Processor::OnThreadDestroyed` lock-death path and restored the visible route in
the validation capture.

This is not full-speed proof. It removes a route-stability blocker and returns
us to the measured CPU/JIT hotpath lane.

## Next

Commit this stability fix, then resume performance profiling from the restored
route. The next measured hotpath is still the `8227F1D8 -> 82490030` path, with
`82490030` now small enough to dump/profile again under a route-stable capture.
