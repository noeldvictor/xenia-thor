# Global Owner Source Black-Idle Reproduction

Date: 2026-05-23 23:29 EDT

## Goal

Run the longer attribution capture requested after
`20260523-232053-global-critical-owner-source-attribution.md`, now that commit
`94fb4546e` is on `master`.

## Capture

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227F1D8,82490030" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Artifacts:

- `scratch/thor-debug/20260523-232432-speed-logcat.txt`
- `scratch/thor-debug/20260523-232432-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260523-232432-meta.txt`
- `scratch/thor-debug/20260523-232432-screenshot.png`
- `scratch/thor-debug/20260523-232432-perf-final.txt`

Metadata:

- HEAD: `94fb4546e`
- APK SHA256:
  `D68ED877D6C265420CD6853AB6C108C18F17E23911C2A1135016797D345C4823`
- Target: Blue Dragon Disc 1 on the known Thor SD-card path.

## Result

The capture reproduced the black-idle before visible opening progress.

- Fatal-marker search was clean.
- Final screenshot was black.
- Final perf showed all xenia threads sleeping, with no CPU-active emulator
  thread.
- A64 counters went flat by `23:25:18` and stayed at `entry_delta=0` through
  the final interval.

The new owner-source fields fired:

```text
A64 thread snapshot skipped: processor debug lock busy after_retries=20
last_global_owner_sys_tid=32582
last_global_owner_thread_id=F80002E8
global_lock_count=0
global_lock_owner_seq=269343
global_lock_owner_age_ms=25053
global_lock_owner_source='Acquire'
owner_hint_source=system_tid
owner_hint_sys_tid=32582
owner_hint_tid=00000016
owner_hint_handle=F80002E8
owner_hint_state=zombie
native_owner_live=false
owner_hint_native_live=false
```

## Interpretation

This is still not generated-code evidence for `8227F1D8` or `82490030`. The
game idled before the delayed body-time window reached useful target rows.

The useful new fact is that the processor debug lock could not be acquired
while the last global critical-region owner record pointed at a dead native
task / zombied XThread, and the PPC guest global-lock snapshot had already
cleared (`global_lock_count=0`). The source field was only the generic
`Acquire`, so it proved the broad path but not the callsite.

## Next

Add narrower diagnostic source labels to processor thread-lifecycle global-lock
acquisitions. The next black-idle should distinguish `Processor::OnThreadExit`,
`Processor::OnThreadDestroyed`, wait-state transitions, and native-start
updates from generic host-side `Acquire` traffic.
