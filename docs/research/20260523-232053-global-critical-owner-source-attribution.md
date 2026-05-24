# Global Critical Owner Source Attribution

Date: 2026-05-23 23:20 EDT

## Goal

Add one more diagnostic layer for the Blue Dragon black-idle path before any
generated-code experiment. The previous owner-native-TID work proved that the
processor debug lock owner hint can point at a dead native task / zombied
XThread. This slice tries to answer the next question: which global critical
region acquisition path last touched the owner bookkeeping, and how old is that
record when the A64 idle snapshot is skipped?

## Patch

- Added `xe::global_critical_region::DebugSnapshot`.
- Added `last_owner_sequence`, `last_owner_host_uptime_ms`, and
  `last_owner_source` to global critical-region diagnostics.
- Tagged the common RAII acquire helpers:
  - `Acquire`
  - `AcquireDirect`
  - `TryAcquire`
- Tagged direct lock paths that bypass the helper:
  - `XThread::LockApc`
  - `ObjectTable::LookupObject`
- Extended the A64 idle-snapshot skip line with:
  - `global_lock_owner_seq`
  - `global_lock_owner_age_ms`
  - `global_lock_owner_source`

This is diagnostic-only. It does not change ARM64 generated code, scheduling,
kernel semantics, HLE behavior, Vulkan behavior, or timing cvars.

## Validation

Build and deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
Get-FileHash android\android_studio_project\app\build\outputs\apk\github\debug\app-github-debug.apk -Algorithm SHA256
```

Result:

- `NativeCore` passed.
- `FullDeploy` passed.
- APK SHA256:
  `D68ED877D6C265420CD6853AB6C108C18F17E23911C2A1135016797D345C4823`.
- `MANAGE_EXTERNAL_STORAGE` remained allowed.

Short validation capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "60,95" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227F1D8,82490030" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Capture artifacts:

- `scratch/thor-debug/20260523-231636-speed-logcat.txt`
- `scratch/thor-debug/20260523-231636-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260523-231636-meta.txt`
- `scratch/thor-debug/20260523-231636-screenshot.png`
- `scratch/thor-debug/20260523-231636-perf-final.txt`

Findings:

- Fatal-marker search was clean.
- The final screenshot still showed the loading overlay, not the opening route.
- The run did not reproduce the prior black-idle signature in the 100-second
  window.
- A64 speed profile counters kept moving through the final interval:
  - `23:16:52` entry delta `3768608`
  - `23:17:07` entry delta `1544225`
  - `23:17:22` entry delta `1911459`
  - `23:17:37` entry delta `1993698`
  - `23:17:52` entry delta `4641534`
  - `23:18:07` entry delta `26904338`
  - `23:18:22` entry delta `3499495`
- No `A64 thread snapshot skipped` line appeared, which is expected because the
  run stayed active and never exercised the new black-idle fields.
- Final perf still showed CPU/JIT-side activity, with Main Thread at `30.7%`
  and GPU Commands at `0.0%` in the final sample. This is a loading/route
  diagnostic, not FPS proof.

## Decision

Keep this patch. It is narrow, route-safe in the short validation window, and
adds the missing owner-source/age evidence needed for the next black-idle.

Do not use this capture as `82490030` HIR/body-time evidence. It did not reach
the delayed body-time window and did not reach the visible opening route.

## Limitations

- The patch records last acquisition source, not release source/depth.
- Full RAII release tracking is higher risk because `global_critical_region`
  widely exposes `std::unique_lock<std::recursive_mutex>`.
- Manual direct lock paths can still bypass `NoteOwner` unless tagged. This
  slice tags the known APC and ObjectTable direct paths involved in the recent
  black-idle evidence.

## Next

Run a longer route-stabilized attribution capture on the patched APK. If it
black-idles, inspect `global_lock_owner_seq`, `global_lock_owner_age_ms`, and
`global_lock_owner_source` alongside native-owner liveness. If it reaches the
visible route and stays active, resume performance profiling from the latest
route-clean hotpath evidence instead of repeating the failed `82490030`
captures unchanged.
