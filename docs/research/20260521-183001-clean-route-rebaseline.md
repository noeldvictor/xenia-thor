# Clean Route Rebaseline After Reverted Permute Probe

Date: 2026-05-21 18:30 EDT

Branch: `master`

## Goal

Verify the Thor was restored to a route-clean APK after the reverted
`PERMUTE_I32` lane-replace probe.

## Validation

After reverting the local code change, redeployed clean `master` and ran:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture: `scratch/thor-debug/20260521-182630-*`

Metadata:

- HEAD: `5aaf0d776`
- APK SHA:
  `FE1CA12C2B572C5643775702DF68D3DEA4A45661B2A2DCD7A8694EAF45F26312`
- Target: Blue Dragon Disc 1 on the Thor SD card.
- `arm64_permute_i32_zip_fastpath=true`

## Result

The route recovered. Delayed body-time activated and `82282490` stayed the
main generated-code body-time target:

- 18:28:49: `82282490 body_ticks_delta=5599336`,
  `entries_delta=18337`, `ticks_per_entry=305`, `code_size=87168`.
- 18:29:04: `82282490 body_ticks_delta=3656819`,
  `entries_delta=16488`, `ticks_per_entry=221`, `code_size=87168`.
- 18:29:19: `82282490 body_ticks_delta=10909838`,
  `entries_delta=83104`, `ticks_per_entry=131`, `code_size=87168`.
- 18:29:34: `82282490 body_ticks_delta=7401721`,
  `entries_delta=59360`, `ticks_per_entry=124`, `code_size=87168`.

Final thread sample:

- Main Thread: about `92.3%` CPU.
- XMA Decoder: about `46.1%` CPU.
- GPU Commands: about `11.5%` CPU.

## Decision

The device is back on a route-clean build. Continue from the `82282490` /
state-traffic baseline, not from the reverted lane-replace APK.
