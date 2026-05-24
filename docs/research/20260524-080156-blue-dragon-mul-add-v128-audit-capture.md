# Blue Dragon MUL_ADD_V128 Audit Capture

Date: 2026-05-24 08:01 EDT

## Summary

The first runtime audit capture was route-clean but did not actually enable the
native audit because Android Java did not forward
`arm64_blue_dragon_mul_add_v128_audit` into the native launch cvar bundle.
`EmulatorActivity.java` now copies that extra, `FullDeploy` passed, and the
rerun produced real `MUL_ADD_V128` audit rows on the Blue Dragon opening route.

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64BlueDragonMulAddV128Audit true -StopAppAfterCapture true
```

## Captures

Pre-plumbing capture:

- `scratch/thor-debug/20260524-075059-speed-logcat.txt`
- `scratch/thor-debug/20260524-075059-screenshot.png`
- Result: visible opening sky/dragon-wing route and clean fatal-marker search,
  but no `A64 Blue Dragon MUL_ADD_V128 audit` rows.

Post-plumbing capture:

- `scratch/thor-debug/20260524-075713-speed-logcat.txt`
- `scratch/thor-debug/20260524-075713-screenshot.png`
- APK SHA-256:
  `7BBAB603A9931918867E9AAE0869193F3A2049329ED7FB448D36C4E66B97EE85`
- Result: visible opening sky/dragon-wing route and clean fatal-marker search.

## Runtime Counters

Final audit row:

```text
A64 Blue Dragon MUL_ADD_V128 audit: total=722256/2131533 pc82282568=240752/710511 pc8228256C=240752/710511 pc82282570=240752/710511 fpcr_switch=240752/710511 sw_flush_path=0/0 input_denorm=0/0 output_denorm=0/0 nan_entry=0/0 nan_lane=0/0 src_copy=2166768/6394599 dest_copy=722256/2131533
```

The format is `delta/total` for each counter. Across the opening route:

- The three target `vmaddfp` PCs executed `2,131,533` total times.
- Each PC executed `710,511` times.
- The VMX FPCR switch happened `710,511` times, roughly once per three-op group.
- The software denormal flush path did not run.
- Input denormal, output denormal, NaN fixup entry, and repaired NaN lane
  counters all stayed at zero.
- Source vector copies totaled `6,394,599`, exactly three source copies per
  target op.
- Destination vector copies totaled `2,131,533`, exactly one destination copy
  per target op.

Final body-time rows kept the known wall:

```text
82282490 body_ticks_total=31263617 entries_delta=60844 ticks_per_entry=157 code_size=89128
82281D28 body_ticks_total=7981873 entries_delta=29028 ticks_per_entry=93 code_size=96908
82282388 body_ticks_total=2318358 entries_delta=5535 ticks_per_entry=141 code_size=88088
82282410 body_ticks_total=2230187 entries_delta=4059 ticks_per_entry=186 code_size=88088
```

The audit build increased `82282490` code size versus quiet controls, so this is
not speed evidence.

## Decision

The expensive semantic slow paths were cold for this route, but the current
lowering still pays source copies, scratch-stack saves, PPC NaN fixup code, and
destination copies on a very hot path. This justifies one default-off,
Blue-Dragon/function/PC-gated `MUL_ADD_V128` fastpath experiment for
`82282568`, `8228256C`, and `82282570`.

The fastpath should stay off in presets and should be proven in two phases:

1. Route-clean capture with the fastpath enabled and normal fatal-marker search.
2. Quiet same-APK A/B with audit, body-time block tracing, and disassembly off.

Do not generalize this to `MUL_ADD_V128` globally. The evidence only covers the
Blue Dragon opening route and these three guest PCs.
