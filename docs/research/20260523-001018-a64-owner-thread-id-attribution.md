# A64 Owner Thread-ID Attribution

Date: 2026-05-23 00:10 EDT

## Goal

Run the delayed `8227FEE8` block body-time capture requested by the previous
slice. If it black-idles, make the idle owner line more actionable before
touching generated-code behavior.

## Block Body-Time Attempt

Capture: `scratch/thor-debug/20260522-235449-*`

- HEAD: `05dbeb7d1`
- APK SHA256:
  `E92DAC2CB4E7080C196DB9656305F372DC20C189E7697A2FCCD47D1E12DA3FA3`
- Flags:
  - `arm64_speed_profile_body_time_filter=8227FEE8`
  - `arm64_speed_profile_body_time_after_ms=120000`
  - `arm64_speed_profile_block_filter=8227FEE8`
  - `arm64_speed_profile_block_body_time=true`
  - `arm64_speed_profile_thread_snapshot_on_idle=true`
  - `arm64_context_promotion_gpr_livein_r1=false`

Result:

- The capture black-idled before it reached useful `8227FEE8` block rows.
- Screenshot was black.
- Fatal-marker search was clean.
- Counters went flat by `23:55:36`.
- Body-time activated after `120001ms`, but there were no `8227FEE8` body or
  block body rows.
- Idle snapshot could not acquire the processor debug lock and reported:
  `last_global_owner_sys_tid=14186`, `global_lock_count=0`, and
  `owner_hint=miss`.

Interpretation:

The delayed block body-time target is still right, but this run did not provide
the block attribution. The repeated `owner_hint=miss` means the blocking native
owner is not reachable through the current native-TID-only XThread hint cache,
or the owner moved/died before the hint lookup.

## Patch

Changed diagnostic attribution only:

- `xe::global_critical_region` now records both:
  - `last_owner_system_thread_id()`
  - `last_owner_thread_id()`
- `Processor` can query the lock-free debug hint cache by guest thread ID or
  handle, not only native system TID.
- The A64 idle snapshot skip line now logs:
  - `last_global_owner_thread_id`
  - `owner_hint_source`
  - `owner_hint_sys_tid`

This does not change guest generated code, guest scheduling, or lock semantics.
It only makes the next black-idle capture easier to classify.

## Validation

Commands:

```powershell
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 90 -PerfSampleSeconds "60,85" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Validation capture: `scratch/thor-debug/20260523-000506-*`

- APK SHA256:
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`
- Fatal-marker search was clean.
- The process stayed active instead of black-idling during the 90-second lane.
- Screenshot showed the loading spinner, not the opening route.
- The idle owner line did not fire, so the new `owner_hint_source` path is
  build/deploy validated but not runtime-exercised yet.
- Final perf sample showed active CPU work, especially Main Thread at about
  `50.0%`, with XMA/GPU/file/audio work still present.

## Next Action

Repeat the delayed `8227FEE8` block body-time capture on the patched APK:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227FEE8" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter "8227FEE8" -Arm64SpeedProfileBlockBodyTime true -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

If it reaches route/body-time, use the block rows to choose a default-off
function/block-gated codegen experiment. If it black-idles, inspect
`last_global_owner_thread_id`, `owner_hint_source`, and `owner_hint_sys_tid`
before making another generated-code change.
