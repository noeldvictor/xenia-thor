# PERMUTE_I32 Lane-Replace Negative

Date: 2026-05-21 18:21 EDT

Branch: `master`

## Question

Can the remaining common `PERMUTE_I32` masks in Blue Dragon's `82282490` be
lowered with direct lane moves instead of the generic TBL path?

## Offline Evidence

The existing HIR dump `scratch/thor-debug/20260521-170941-*` shows
`82282490` is still heavy on vector permutes even after the proven zip path:

- `permute.2`: 236 optimized HIR ops.
- `0x05010400`: 123 hits, already lowered as `zip1 .s4`.
- `0x07030602`: 32 hits, already lowered as `zip2 .s4`.
- Other notable masks: `0x07060104` 19 hits, `0x03020504` 13 hits,
  `0x07020100` 11 hits, `0x03060504` 10 hits.

The local experiment added a generic helper for masks representable as one
base vector plus up to two 32-bit lane inserts. It stayed behind the existing
`arm64_permute_i32_zip_fastpath` rollback flag and left the proven zip cases
first.

## Validation

Build and deploy passed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

APK SHA:
`672123AC66A833C6A5B37A2E93B0FA9DA33C1627F075CA418024A4611C4EB417`.

Full 180-second route:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture `scratch/thor-debug/20260521-181513-*` black-idled before delayed
body-time activated. It logged `entry_delta=0` from 18:16:01 onward, never
produced `82282490` body-time rows, and final perf showed the main thread
sleeping.

Short repeat without delayed body-time:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 95 -PerfSampleSeconds "90" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -StopAppAfterCapture true
```

Capture `scratch/thor-debug/20260521-181920-*` repeated the same failure:
`entry_delta=0` from 18:20:09 onward, with no visible route progress.

## Decision

Reverted the local lane-replace helper and did not commit it. The offline count
still says these masks are tempting, but the broad generic lane helper is not
route-clean enough to carry.

If this path is retried, do it as a much narrower exact-mask experiment with a
small correctness harness or a generated-code disassembly audit first. Do not
fold generic lane-replace masks into the default `arm64_permute_i32_zip_fastpath`
without a clean Blue Dragon opening-scene proof.
