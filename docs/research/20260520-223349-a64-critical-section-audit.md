# A64 Critical Section Audit And RtlEnter Reorder Test

Date: 2026-05-20 22:33 EDT

## Goal

Use counters instead of guesswork for the high-frequency
`RtlEnterCriticalSection` / `RtlLeaveCriticalSection` pair. The previous commit
made the final `RtlLeaveCriticalSection` unlock fast; this pass checks whether
that path is actually hot and tests a tempting `RtlEnterCriticalSection`
free-lock-first reorder.

## Change

- Added default-off `a64_rtl_leave_fastpath_audit`.
- When enabled, the A64 speed profiler logs per-interval counters for:
  - recursive `RtlLeaveCriticalSection` inline;
  - final-unlock `RtlLeaveCriticalSection` inline;
  - restore-to-native race path;
  - native fallback path.
- Added `a64_rtl_enter_free_first`, but kept it default-off after proof. It
  tries the free-lock CAS before checking the recursive owner.
- Forwarded both toggles through `EmulatorActivity` and
  `tools/thor/thor_xenia_debug.ps1`.

## Validation

Commands:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 55 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true -A64RtlLeaveFastpathAudit true
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 55 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 55 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true -A64RtlEnterFreeFirst false
```

Audit proof:

- Capture: `scratch/thor-debug/20260520-222648-*`
- APK SHA-256:
  `9857978CC0CCEEA4DFAA64783EEE0604B84C85E5D3C754551EBF46A21544C00F`
- Settings:
  - `a64_rtl_leave_fastpath_audit=true`
  - `a64_inline_rtl_leave_final_unlock=true`
  - `a64_inline_kf_lower_irql=false`
- Result: reached the Blue Dragon Voice Language screen, no searched fatal
  markers.

Final default proof after making `a64_rtl_enter_free_first=false`:

- Capture: `scratch/thor-debug/20260520-223442-*`
- APK SHA-256:
  `B73738FC950D237ACECE281F6A75CCAAE7397E78BAA7756BF709F6FB36857A64`
- Settings:
  - `a64_rtl_enter_free_first=false`
  - `a64_inline_rtl_leave_final_unlock=true`
  - `a64_rtl_leave_fastpath_audit=false`
  - `a64_inline_kf_lower_irql=false`
- Result: reached the Blue Dragon Voice Language screen with active speed
  counters through the final interval.

## Findings

`RtlLeaveCriticalSection` audit totals by the final interval:

| Path | Total |
| --- | ---: |
| final unlock inline | 195628 |
| recursive inline | 23189 |
| native fallback | 3028 |
| restore-to-native race | 14 |

That is a strong confirmation that the final-unlock inline is the correct
default. It handles most `RtlLeaveCriticalSection` calls, while the contended
restore race is extremely rare in this route.

`a64_rtl_enter_free_first=true` was not safe as a default:

- `scratch/thor-debug/20260520-223025-*` black-idled after the early burst with
  `entry_delta=0`; the idle snapshot could not acquire the processor debug
  lock.
- The same APK with `-A64RtlEnterFreeFirst false`,
  `scratch/thor-debug/20260520-223155-*`, reached the Blue Dragon Voice
  Language screen.
- Therefore the reorder stays present only as an opt-in experiment.

## Next

- Do not default-enable `a64_rtl_enter_free_first` without a deeper lock-state
  proof.
- If revisiting `RtlEnterCriticalSection`, add explicit enter-path counters
  first: free CAS success, recursive success, busy native fallback, and failed
  CAS retry.
- The next default-on speed work should move back to APC-safe `KfLowerIrql` or
  cross-block PPC state caching, not another blind critical-section reorder.
