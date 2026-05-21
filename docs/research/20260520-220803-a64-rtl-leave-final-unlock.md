# A64 RtlLeaveCriticalSection Final Unlock Inline

Date: 2026-05-20 22:08 EDT

## Goal

Reduce guest-to-host churn in Blue Dragon's high-frequency Xbox kernel thunks
without touching the heavier wait/event path. The previous A64 inline handled
recursive `RtlLeaveCriticalSection` only; the common final release
(`recursion_count == 1`) still fell back to native HLE.

## Change

- Added `a64_inline_rtl_leave_final_unlock`, default-on.
- Forwarded it through `EmulatorActivity` and `tools/thor/thor_xenia_debug.ps1`
  as `-A64InlineRtlLeaveFinalUnlock true|false`.
- Inlined only the uncontended final release shape:
  - owner must match the current guest thread;
  - `recursion_count` must be exactly `1`;
  - `lock_count` must be `0` before release.
- Clears `recursion_count` and `owning_thread`, then uses an exclusive store to
  publish `lock_count = -1`.
- If another thread races in and changes `lock_count`, restores
  `recursion_count` and `owning_thread` and falls back to the native export so
  the event wake path is preserved.
- Cleaned codegen so `-A64InlineRtlLeaveFinalUnlock false` emits the old compact
  448-byte `RtlLeaveCriticalSection` body rather than unreachable fastpath code.

## Validation

Commands:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 55 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 10 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true
```

Final proof:

- Capture: `scratch/thor-debug/20260520-220613-*`
- APK SHA-256:
  `F7FF71C3F191128C01AB7F974F6B8275C00836B017B0F744A928058E2A061B02`
- Settings:
  - `a64_inline_kernel_spinlock_exports=true`
  - `a64_inline_rtl_leave_final_unlock=true`
  - `a64_inline_kf_lower_irql=false`
  - `arm64_speed_profile_thread_snapshot_on_idle=true`
- Result: reached the Blue Dragon Voice Language screen, stayed active through
  the final speed-profile interval, and produced no searched fatal, Android
  runtime, GPU hang, or Vulkan device-lost markers.

Important route note:

- `scratch/thor-debug/20260520-220245-*` black-idled with the final-unlock
  inline enabled.
- `scratch/thor-debug/20260520-220425-*` also black-idled with the final-unlock
  inline disabled and the old 448-byte `RtlLeaveCriticalSection` code shape.
- Treat those as another instance of the known timing-sensitive speed route, not
  as proof that this fastpath is the idle cause.

## Metrics

Compared with the prior healthy spinlock baseline
`scratch/thor-debug/20260520-193232-*`:

| Interval | Baseline `g2h` | New `g2h` | Delta |
| --- | ---: | ---: | ---: |
| ~15-30s | 200837 | 158986 | -41851 |
| ~30-45s | 247447 | 201818 | -45629 |
| ~45-60s | 273426 | 226082 | -47344 |

Generated code size:

| Path | `RtlLeaveCriticalSection` code size |
| --- | ---: |
| Baseline recursive-only inline | 448 bytes |
| New final-unlock inline on | 528 bytes |
| New toggle off after cleanup | 448 bytes |

The new path trades 80 bytes in the high-frequency export thunk for fewer native
guest-to-host exits. This is a host-transition cleanup, not a standalone FPS
breakthrough.

## Next

- Keep `a64_inline_rtl_leave_final_unlock=true` for default speed captures, but
  roll back with `-A64InlineRtlLeaveFinalUnlock false` if a future one-variable
  repro implicates it.
- Add counters for final-unlock taken, waiter-race restore, and native fallback
  if this path becomes suspicious.
- Continue reducing high-frequency HLE exits: `RtlEnterCriticalSection`,
  `KfLowerIrql` with APC-safe semantics, and eventually a real cross-block PPC
  state cache.
