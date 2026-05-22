# GPR Local-Slot Promotion Probe

Date: 2026-05-22 11:30 EDT

Branch: `master`

Starting HEAD: `aca4e53ab`

## Goal

Turn the `82282490` HIR GPR promotion audit into a guarded runtime experiment
without creating unsafe cross-block SSA values in `ContextPromotionPass`.

## Patch

Added default-off cvars:

- `arm64_context_promotion_gpr_local_slots`
- `arm64_context_promotion_gpr_local_slots_function`

The experiment targets only whole INT64 PPC GPR slots `r[1]` and `r[11]`.
For dominated single-predecessor HIR blocks, predecessor values are stored to
HIR locals before the tail branch and successor first loads are replaced by
`LOAD_LOCAL` plus `ASSIGN`. Stores to `PPCContext` are not elided.

Android forwarding and `tools/thor/thor_xenia_debug.ps1` launch/metadata support
were added. Blue Dragon speed/title defaults force the cvar off unless the run
explicitly opts in.

## Validation

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLocalSlots true -Arm64ContextPromotionGprLocalSlotsFunction 0x82282490 -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture:

- `scratch/thor-debug/20260522-113303-*`
- screenshot: visible opening sky/dragon-wing route
- APK SHA-256: `AC6ED13CCA87548392BFC2AFCF459A9A4F388EF170DBD79AD716C4B39B854AE8`
- fatal marker search: no matches for Android fatal/crash/guest crash/GPU-hung markers

## Result

The route is clean, but this is not a speed win yet.

Compared with clean route capture `scratch/thor-debug/20260521-182630-*`,
`82282490` code size increased from `87168` to `87660`. Comparable body-time
intervals are flat to slightly worse:

- clean `20260521-182630`: `83104` entries, `ticks_per_entry=131`, `code_size=87168`
- local-slot probe `20260522-113303`: `83104` entries, `ticks_per_entry=139`, `code_size=87660`
- clean final interval: `59360` entries, `ticks_per_entry=124`, `code_size=87168`
- local-slot final interval: `56392` entries, `ticks_per_entry=141`, `code_size=87660`

## Decision

Keep `arm64_context_promotion_gpr_local_slots` default-off. It is useful as a
route-clean scaffold for testing pre-RA GPR state promotion, but HIR locals add
enough load/store/code-size overhead that this shape should not be treated as a
speed preset.

## Next

Add exact promotion counters/code-size attribution for this pass, or move to a
lower-overhead pinned-GPR experiment for `r[1]`/`r[11]` with explicit helper,
exit, branch, and exception flush rules.
