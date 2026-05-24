# Blue Dragon All-Three STVEWX Lane Fastpath

Date: 2026-05-24 06:56 EDT

## Hypothesis

The previous `stvewx` stack-lane fastpath folded only `82282580` and
`82282584` because `82282588` lacked a proven stack lane. After
`docs/research/20260524-064118-82282588-stvewx-provenance.md`, all three
dynamic `EXTRACT_I32` sites in `82282490` span `8228252C-822825C4` have lane
provenance under the normal 16-byte PPC stack-pointer alignment assumption:

- `82282580 -> lane 0` from `r1 + 0x50`;
- `82282584 -> lane 1` from `r1 + 0x54`;
- `82282588 -> lane 2` via `r6 = r1 + 0x50`, then `r6 + 0x8`.

If the default-off fastpath folds all three PCs and audit mode sees no runtime
mismatches on the opening route, then this lane is safe enough for a later
quiet speed A/B.

## Patch

Changed the existing default-off cvar path:

- `arm64_blue_dragon_stvewx_stack_lane_fastpath`
- `arm64_blue_dragon_stvewx_stack_lane_audit`

The PC gate now covers:

- `82282580 -> lane 0`;
- `82282584 -> lane 1`;
- `82282588 -> lane 2`.

Audit mode still compares the runtime lane against the folded lane and falls
back to the generic dynamic `EXTRACT_I32` lowering on mismatch. The fastpath
remains Blue-Dragon/function-gated to `82282490` and default-off.

## Validation

Build/deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Results:

- `NativeCore` passed.
- `FullDeploy` passed and installed on `c3ca0370`.
- APK SHA-256:
  `8A3DA22B2208AA67DE13C07383490F3BEBA14E2DC1538AAB5CE08FD035626771`.

## Audit Capture

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter "82282490" -Arm64SpeedProfileBlockBodyTime true -Arm64BlueDragonStvewxStackLaneFastpath true -Arm64BlueDragonStvewxStackLaneAudit true
```

Capture:

- `scratch/thor-debug/20260524-065132-*`
- final screenshot:
  `scratch/thor-debug/20260524-065132-screenshot.png`
- visible route: opening sky/dragon-wing frame
- fatal-marker search: clean
- metadata APK SHA:
  `8A3DA22B2208AA67DE13C07383490F3BEBA14E2DC1538AAB5CE08FD035626771`

Final audit row:

```text
06:54:34 A64 Blue Dragon stvewx stack-lane audit: fastpath=722256/2008221 fallback=0/0
```

Final delayed body row:

```text
82282490 body_ticks_total=50120439 entries_delta=60844 ticks_per_entry=276 code_size=107420
```

Final block body rows kept the known shape:

- `822825E0` first at `body_ticks_total=35575683`;
- `8228252C` second at `body_ticks_total=4053809`;
- `822825C8` third at `body_ticks_total=3945645`;
- `822824F0` fourth at `body_ticks_total=1268521`.

## Decision

Status: route-clean correctness proof for the all-three-site fold, not a speed
win yet.

The third folded site executed under audit and no fallback mismatches occurred.
Because this capture enabled audit counters and block body-time instrumentation,
it is not a quiet FPS run and should not be used to declare a speed win. Keep
`arm64_blue_dragon_stvewx_stack_lane_fastpath` default-off in presets.

Next useful work:

- run a quiet same-APK A/B or control sandwich with audit off before enabling
  this path anywhere by default;
- if the quiet A/B is inconclusive again, stop spending more slices on narrow
  `stvewx` lane folds and move to `MUL_ADD_V128` cost for
  `82282568/8228256C/82282570`.
