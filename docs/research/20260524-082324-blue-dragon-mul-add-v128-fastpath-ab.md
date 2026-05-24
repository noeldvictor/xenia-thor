# Blue Dragon MUL_ADD_V128 Fastpath A/B

Date: 2026-05-24 08:23 EDT

## Summary

Added a default-off, Blue-Dragon/function/PC-gated A64 fastpath for
`MUL_ADD_V128` at `82282490` PCs `82282568`, `8228256C`, and `82282570`.
The fastpath keeps VMX FPCR mode and requires host hardware denormal-input
flushing, but skips the generic scratch-stack source saves and PPC NaN repair
path that the previous runtime audit proved cold for this route.

The route-clean capture and quiet same-APK A/B both reached the visible opening
sky/dragon-wing route with clean fatal-marker searches. This is safe enough to
keep as a default-off experiment, but it is not a proven speed win.

## Code Change

- New cvar: `arm64_blue_dragon_mul_add_v128_fastpath`, default `false`.
- Gate: guest function `82282490`, guest PCs `82282568`, `8228256C`,
  `82282570`, and `kA64FZFlushesInputs`.
- Android launcher plumbing:
  `EmulatorActivity.java` forwards `arm64_blue_dragon_mul_add_v128_fastpath`.
- Thor script plumbing:
  `-Arm64BlueDragonMulAddV128Fastpath true|false`, metadata, launch args, and
  Blue Dragon preset default-off.

## Validation

Builds:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both passed. APK SHA-256 for the tested APK:

```text
127CFD98C6AFE2AFD5C7EC8D6D0E0A8DB2B9DE7893334ED3F85B7894621B3688
```

Route-safety capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64BlueDragonMulAddV128Fastpath true -StopAppAfterCapture true
```

- Capture: `scratch/thor-debug/20260524-081212-*`
- Result: visible opening sky/dragon-wing route, clean fatal-marker search.
- Final `82282490` body row:
  `body_ticks_total=25372189`, `entries_delta=56392`,
  `ticks_per_entry=120`, `code_size=85764`.

Quiet same-APK control:

- Capture: `scratch/thor-debug/20260524-081601-*`
- `arm64_blue_dragon_mul_add_v128_fastpath=false`
- Result: visible opening sky/dragon-wing route, clean fatal-marker search.
- Final `82282490` speed row:
  `delta=65296`, `total=170142`, `code_size=86828`.
- Final thread sample:
  Main Thread `92.3%`, GPU Commands `11.5%`, XMA Decoder `7.6%`.

Quiet same-APK fastpath:

- Capture: `scratch/thor-debug/20260524-081929-*`
- `arm64_blue_dragon_mul_add_v128_fastpath=true`
- Result: visible opening sky/dragon-wing route, clean fatal-marker search.
- Final `82282490` speed row:
  `delta=60844`, `total=177562`, `code_size=85424`.
- Final thread sample:
  Main Thread `96.1%`, GPU Commands `15.3%`, XMA Decoder `50.0%`.

## Decision

The fastpath shrinks `82282490` generated code by `1404` bytes in the quiet
A/B (`86828 -> 85424`) and keeps the route clean, but the final counters and
thread samples do not prove route-wide speed. Keep
`arm64_blue_dragon_mul_add_v128_fastpath` default-off and do not enable it in
the Blue Dragon speed preset.

Stop spending immediate sprint slices on this narrow three-PC FMA skip unless a
broader `8228252C-822825C4` state/vector/FPR experiment reopens it. The next
useful lane is broader local-span reduction in `8228252C-822825C4`, where the
latest exclusive ranking still shows the largest approximate local work.
