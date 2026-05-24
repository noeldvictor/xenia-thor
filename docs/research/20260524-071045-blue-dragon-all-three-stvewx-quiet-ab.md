# Blue Dragon All-Three STVEWX Quiet A/B

Date: 2026-05-24 07:10 EDT

## Goal

Test whether the route-clean all-three `stvewx` lane fold from
`docs/research/20260524-065602-blue-dragon-all-three-stvewx-fastpath.md`
actually improves the opening-route speed when audit and block/body-time
instrumentation are off.

Only one variable changed between runs:

- control: `arm64_blue_dragon_stvewx_stack_lane_fastpath=false`;
- experiment: `arm64_blue_dragon_stvewx_stack_lane_fastpath=true`.

Both runs kept:

- `arm64_blue_dragon_stvewx_stack_lane_audit=false`;
- no body-time filter;
- no block body-time;
- no disassembly dump;
- same commit `a924aba56`;
- same APK SHA-256
  `8A3DA22B2208AA67DE13C07383490F3BEBA14E2DC1538AAB5CE08FD035626771`.

## Commands

Control:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64BlueDragonStvewxStackLaneFastpath false -Arm64BlueDragonStvewxStackLaneAudit false
```

Experiment:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64BlueDragonStvewxStackLaneFastpath true -Arm64BlueDragonStvewxStackLaneAudit false
```

## Captures

Control:

- `scratch/thor-debug/20260524-070253-*`
- final screenshot:
  `scratch/thor-debug/20260524-070253-screenshot.png`
- visible route: opening sky/dragon-wing frame
- fatal-marker search: clean
- final perf: Main Thread `96.1%`, XMA Decoder `50.0%`, GPU Commands `11.5%`

Experiment:

- `scratch/thor-debug/20260524-070613-*`
- final screenshot:
  `scratch/thor-debug/20260524-070613-screenshot.png`
- visible route: opening sky/dragon-wing frame
- fatal-marker search: clean
- final perf: Main Thread `100%`, XMA Decoder `46.1%`, GPU Commands `11.5%`

## Comparison

Both runs reached visually matching route frames. Android UI frame stats still
reported only three rendered UI frames and are not useful game FPS evidence for
this route.

Final speed-profile interval:

| Run | `entry_delta` | Top 1 | Top 2 | Top 3 |
| --- | ---: | --- | --- | --- |
| control | `3640519` | `82274DB0` delta `221593` | `82287788` delta `217929` | `826BF770` delta `154683` |
| fastpath on | `3537838` | `82274DB0` delta `209615` | `82287788` delta `206254` | `826BF770` delta `147712` |

`82282490` rows:

| Run | final `82282490` delta | final `82282490` total | `82282490` code size |
| --- | ---: | ---: | ---: |
| control | `54908` | `183498` | `86828` |
| fastpath on | `51940` | `183498` | `86708` |

The fastpath shrank generated code by `120` bytes and did not perturb the
route, but the speed evidence is not strong enough to call this a win. The
same final `82282490` total with nearly identical route progress means this
micro-optimization is below current noise or masked by larger work.

## Decision

Status: keep default-off; stop spending sprint slices on narrow `stvewx`
lane-fold work unless a future broader state/vector experiment reopens it.

This quiet A/B does not prove a speed win. The next useful lane is
`MUL_ADD_V128` source/cost audit for the shared hot PCs
`82282568/8228256C/82282570`, or a broader state/vector/FPR reduction in
`8228252C-822825C4`.
