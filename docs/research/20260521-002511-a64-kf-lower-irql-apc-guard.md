# A64 KfLowerIrql APC-Guard Probe

Date: 2026-05-21 00:25 EDT

## Summary

`KfLowerIrql` is still too route-sensitive to inline for Blue Dragon. A guarded
no-pending-APC inline saved guest-to-host transitions, but every tested inline
cadence black-idled before the Voice Language menu. Keep all new
`KfLowerIrql` guard flags default-off.

## What Changed

- Added a per-`XThread` APC pending counter and exposed it through `PPCContext`
  for A64-generated code.
- Added default-off `a64_inline_kf_lower_irql_apc_guard`.
- Added default-off `a64_kf_lower_irql_apc_guard_audit`.
- Added `a64_kf_lower_irql_apc_guard_native_poll_interval` so the guarded path
  can force every Nth no-pending lower through the native export.
- Added Thor launcher flags:
  - `-A64InlineKfLowerIrqlApcGuard`
  - `-A64KfLowerIrqlApcGuardAudit`
  - `-A64KfLowerIrqlApcGuardNativePollInterval`

## Proof Commands

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Baseline-off A/B:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 60 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true -A64InlineKfLowerIrql false -A64InlineKfLowerIrqlApcGuard false -A64KfLowerIrqlApcGuardAudit false
```

Guard probes:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 60 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true -A64InlineKfLowerIrql false -A64InlineKfLowerIrqlApcGuard true -A64KfLowerIrqlApcGuardAudit true
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 65 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true -A64InlineKfLowerIrql false -A64InlineKfLowerIrqlApcGuard true -A64KfLowerIrqlApcGuardAudit true -A64KfLowerIrqlApcGuardNativePollInterval 64
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 65 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true -A64InlineKfLowerIrql false -A64InlineKfLowerIrqlApcGuard true -A64KfLowerIrqlApcGuardAudit true -A64KfLowerIrqlApcGuardNativePollInterval 4
```

## Captures

Baseline guard off:

- Capture: `scratch/thor-debug/20260521-001638-*`
- APK SHA-256: `92A6719808758419D8A2E6A5118A08E35613CCF8C7B765208FEE24F08C832364`
- Result: reached the Blue Dragon Voice Language screen.
- Final interval stayed active:
  `entry_delta=2017533 h2g=4080/14358 g2h=225487/597675`.
- `KfLowerIrql` stayed native and hot: final visible delta `40596`,
  `code_size=336`.

Guard on, no native poll:

- Capture: `scratch/thor-debug/20260521-001506-*`
- APK SHA-256: `92A6719808758419D8A2E6A5118A08E35613CCF8C7B765208FEE24F08C832364`
- Result: black-idled after the second interval.
- Guard counters before idle:
  `fastpath=18606`, `pending_fallback=0`, `missing_fallback=0`.
- It reduced guest-to-host calls in the first interval versus the off run
  (`46894` vs `56948`) but did not preserve route progress.

Guard on, native poll every 64:

- Capture: `scratch/thor-debug/20260521-002131-*`
- APK SHA-256: `076279C17960A86A03EAA88A6CA2E618A874EAB0C3035747E8810BD35AB00291`
- Result: black-idled with a noisy half-frame screenshot.
- Guard counters before idle:
  `fastpath=16172`, `poll_fallback=256`, `pending_fallback=0`,
  `missing_fallback=0`.

Guard on, native poll every 4:

- Capture: `scratch/thor-debug/20260521-002312-*`
- APK SHA-256: `076279C17960A86A03EAA88A6CA2E618A874EAB0C3035747E8810BD35AB00291`
- Result: black-idled with the same noisy half-frame shape.
- Guard counters before idle:
  `fastpath=12768`, `poll_fallback=4255`, `pending_fallback=0`,
  `missing_fallback=0`.

## Interpretation

The APC pending counter did not catch any queued APCs during the failed guard
runs, so the loss is probably not a simple "missed queued APC" bug. Native
`KfLowerIrql` appears to matter as a host-side scheduling, synchronization, or
callback cadence point for this Blue Dragon route. Even polling native once per
four lowers is not currently enough.

## Decision

- Keep `a64_inline_kf_lower_irql=false`.
- Keep `a64_inline_kf_lower_irql_apc_guard=false`.
- Use the APC guard only as an audit/probe lane.
- Do not claim the `KfLowerIrql` guard as a speed win.

## Next Actions

- Add a native-path audit for `KfLowerIrql_entry` / `CheckApcs()` that counts
  `HasPending()`, `apc_disable_count`, and possibly host callback queueing.
- If revisiting the inline, preserve a native scheduler/APC pump explicitly
  rather than relying only on a pending-APC guard.
- Return speed work to safer hotpaths: direct block chaining, guest function
  call/return overhead, cross-block PPC state cache, and XMA cost probes.
