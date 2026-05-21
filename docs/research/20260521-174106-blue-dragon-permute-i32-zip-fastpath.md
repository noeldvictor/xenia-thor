# Blue Dragon PERMUTE_I32 Zip Fastpath

Date: 2026-05-21 17:41 EDT

Branch: `master`

## Question

Can the A64 backend remove generic TBL lowering from Blue Dragon's VMX merge
word hot path without touching fragile FP semantics?

## Rationale

The `82282490` HIR classification in
`docs/research/20260521-172826-blue-dragon-vmx-dot-negative.md` showed many
`vmrghw` / `permute.2` operations. The PPC frontend lowers:

- `vmrghw` to `MakePermuteMask(0, 0, 1, 0, 0, 1, 1, 1)`, control
  `0x05010400`, result `[src2.x, src3.x, src2.y, src3.y]`
- `vmrglw` to `MakePermuteMask(0, 2, 1, 2, 0, 3, 1, 3)`, control
  `0x07030602`, result `[src2.z, src3.z, src2.w, src3.w]`

The old A64 `PERMUTE_I32` path builds a 16-byte TBL control vector, copies
sources into `v0/v1`, loads the constant, and uses `tbl`. These two exact
merge-word shapes map directly to NEON `zip1 .s4` and `zip2 .s4`.

## Change

Added default-on `arm64_permute_i32_zip_fastpath`, forwarded through Android
launch extras and the Thor debug script. When enabled:

- control `0x05010400` emits `zip1(vd.s4, src2.s4, src3.s4)`
- control `0x07030602` emits `zip2(vd.s4, src2.s4, src3.s4)`
- all other `PERMUTE_I32` controls keep the existing generic TBL path

This is a rollback-able generic A64 lowering improvement, not a Blue
Dragon-only address patch.

## Validation

Native build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both passed. APK SHA:
`9E6A13BD7B8CC0B9A67F5042DA34599BE4F9623A3697350A9FD61D9F71B21BC1`.

Fastpath on:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture `scratch/thor-debug/20260521-173359-*` reached the opening
sky/dragon-wing scene with no searched fatal markers. `82282490` code size
dropped to `87168`, down from the earlier TBL route's `94608`.

Body-time with fastpath on:

- 17:36:17: `82282490 body_ticks_delta=5339060`,
  `entries_delta=15003`, `ticks_per_entry=355`, `code_size=87168`
- 17:36:32: `body_ticks_delta=5364021`, `entries_delta=19822`,
  `ticks_per_entry=270`, `code_size=87168`
- 17:36:47: `body_ticks_delta=9855526`, `entries_delta=69748`,
  `ticks_per_entry=141`, `code_size=87168`
- 17:37:02: `body_ticks_delta=8092394`, `entries_delta=62328`,
  `ticks_per_entry=129`, `code_size=87168`

At 175 seconds the main thread was still the wall at about `88.4%` CPU, with
GPU Commands and XMA around `7.6%` each and Draw Thread around `3.8%`.

Same APK with fastpath off:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath false -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture `scratch/thor-debug/20260521-173734-*` black-idled before body-time
activated, with repeated `entry_delta=0` from 17:38:22 onward and a black final
screenshot. This reinforces that the Blue Dragon route is still
timing-sensitive; it does not by itself prove the old TBL lowering is wrong.

## Decision

Keep `arm64_permute_i32_zip_fastpath=true` by default. It is small,
semantics-preserving for the two exact PPC merge-word masks, route-positive in
the current Blue Dragon opening-scene lane, and has a script rollback:

```powershell
-Arm64PermuteI32ZipFastpath false
```

This is a real codegen shrink, but not enough for 30-60 FPS. The main thread is
still saturated. Next work should attack the same `82282490` cluster at a
larger level: vector context traffic, `stvx` byte-swap/store fusion, branch
linking, and a correctness-proven dot-product replacement.

