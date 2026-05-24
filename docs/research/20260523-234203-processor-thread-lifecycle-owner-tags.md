# Processor Thread Lifecycle Owner Tags

Date: 2026-05-23 23:42 EDT

## Goal

The previous 180-second capture reproduced the black-idle and showed
`global_lock_owner_source='Acquire'`, which was too generic to identify the
host-side callsite. This slice narrows the diagnostic scope by tagging
processor thread-lifecycle global-critical acquisitions.

## Patch

- Added `global_critical_region::Acquire(const char* source)`.
- Kept the existing `Acquire()` API as `Acquire("Acquire")` so existing
  callsites continue to behave the same.
- Tagged these processor lifecycle callsites:
  - `Processor::OnThreadCreated`
  - `Processor::OnThreadNativeStarted`
  - `Processor::OnThreadExit`
  - `Processor::OnThreadDestroyed`
  - `Processor::OnThreadEnteringWait`
  - `Processor::OnThreadLeavingWait`

This is diagnostic-only. It does not change lock semantics, generated code,
kernel behavior, HLE behavior, or Vulkan behavior.

## Validation

Build/deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
Get-FileHash android\android_studio_project\app\build\outputs\apk\github\debug\app-github-debug.apk -Algorithm SHA256
adb -s c3ca0370 shell cmd appops get jp.xenia.emulator.github.debug MANAGE_EXTERNAL_STORAGE
```

Result:

- Initial `NativeCore` wrapper call outlived the two-minute shell timeout while
  `clang++` workers were still active.
- After workers drained, rerunning `NativeCore` completed successfully in 4s.
- `FullDeploy` passed and installed successfully.
- APK SHA256:
  `862F86C44625B460A5BAB8528E25AB4E946F52CDB30137D7479D24AC3BD50FCB`.
- `MANAGE_EXTERNAL_STORAGE` remained allowed.

Short validation capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "60,95" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227F1D8,82490030" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Artifacts:

- `scratch/thor-debug/20260523-233953-speed-logcat.txt`
- `scratch/thor-debug/20260523-233953-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260523-233953-meta.txt`
- `scratch/thor-debug/20260523-233953-screenshot.png`
- `scratch/thor-debug/20260523-233953-perf-final.txt`

Findings:

- Fatal-marker search was clean.
- Final screenshot still showed the loading overlay.
- The run did not reproduce the black-idle in the 100-second window.
- A64 counters kept moving through the final interval, including
  `entry_delta=6784668` at `23:41:39`.
- No `A64 thread snapshot skipped` line appeared, so the new lifecycle source
  labels were not exercised yet.
- Final perf showed continued activity: Main Thread `15.3%`, GPU Commands
  `7.6%`, Draw Thread `7.6%`, and several XThreads/File/Audio workers active.

## Decision

Keep the patch. It is narrow and the short validation did not show an early
route regression. It still needs a black-idle reproduction to prove the specific
source label.

## Next

Run a longer attribution capture on the tagged-lifecycle APK. If black-idle
reproduces, inspect `global_lock_owner_source`. A result such as
`Processor::OnThreadDestroyed` or `Processor::OnThreadExit` would point the
next patch at thread lifecycle / lock release behavior. If the longer capture
stays active and reaches the visible route, resume measured hotpath profiling.
