# Blue Dragon 8228252C STVEWX Lane Fastpath

Date: 2026-05-24 04:04 EDT

## Hypothesis

If the two proven `stvewx` stack-lane dynamic `EXTRACT_I32` sites in
`82282490` are folded to constant lanes, then the hot `8228252C-822825C4`
slice should emit less A64 code and may reduce local generated-code body time.

Primary variable:

- `arm64_blue_dragon_stvewx_stack_lane_fastpath`

Rollback switches:

- `arm64_blue_dragon_stvewx_stack_lane_fastpath=false`
- `arm64_blue_dragon_stvewx_stack_lane_audit=false`

## Patch

Added a default-off Blue-Dragon/function/PC-gated A64 `EXTRACT_I32` fastpath
for only:

- guest PC `82282580` -> lane `0`;
- guest PC `82282584` -> lane `1`.

The first patch deliberately excludes `82282588` because
`docs/research/20260524-034103-8228252c-stvewx-lane-provenance.md` traced it
to `r6 + 0x8`, not the proven `r1 + constant` stack shape.

Audit mode compares the runtime lane with the folded lane and falls back to the
generic dynamic `TBL` lowering on mismatch. The speed profiler reports audit
counters as:

```text
A64 Blue Dragon stvewx stack-lane audit: fastpath=<delta>/<total> fallback=<delta>/<total>
```

Thor launcher support:

- `-Arm64BlueDragonStvewxStackLaneFastpath true|false`
- `-Arm64BlueDragonStvewxStackLaneAudit true|false`

Both are default-off in the backend and forced false by Blue Dragon speed/title
presets unless explicitly overridden.

## Validation

Build/deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Results:

- NativeCore passed.
- FullDeploy passed and installed on `c3ca0370`.
- APK SHA-256:
  `A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8`.

## Audit Capture

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter "82282490" -Arm64SpeedProfileBlockBodyTime true -Arm64BlueDragonStvewxStackLaneFastpath true -Arm64BlueDragonStvewxStackLaneAudit true
```

Capture:

- `scratch/thor-debug/20260524-035227-*`
- final screenshot:
  `scratch/thor-debug/20260524-035227-screenshot.png`
- visible route: opening sky/dragon-wing frame
- fatal-marker search: clean

Final audit rows:

```text
03:55:14 fastpath=622432/892542 fallback=0/0
03:55:29 fastpath=446272/1338814 fallback=0/0
```

Interpretation:

- The two folded sites executed heavily on the real route.
- Runtime lane validation found no mismatches.
- The fastpath is correctness-plausible for these two PCs only.

## Quiet Same-APK A/B

Control command used the same APK and route settings with:

```text
arm64_blue_dragon_stvewx_stack_lane_fastpath=false
arm64_blue_dragon_stvewx_stack_lane_audit=false
```

Control capture:

- `scratch/thor-debug/20260524-035623-*`
- visible route: `Microsoft Game Studios Presents`
- fatal-marker search: clean
- final `82282490` code size: `87168`
- final `82282490` body row:
  `body_ticks_total=50714740`, `entries_delta=49396`,
  `ticks_per_entry=233`

Experiment command changed only:

```text
arm64_blue_dragon_stvewx_stack_lane_fastpath=true
arm64_blue_dragon_stvewx_stack_lane_audit=false
```

Experiment capture:

- `scratch/thor-debug/20260524-035944-*`
- visible route: opening sky/dragon-wing frame
- fatal-marker search: clean
- final `82282490` code size: `87088`
- final `82282490` body row:
  `body_ticks_total=24575490`, `entries_delta=65296`,
  `ticks_per_entry=126`

## Decision

Status: inconclusive for speed, route-clean for correctness.

The patch shrinks `82282490` generated code by 80 bytes and the audit proves
the two lane folds are dynamically stable on this route. However, the quiet A/B
does not prove a speed win because the control reached a later visible route
frame than the fastpath-on run. Do not enable this in the Blue Dragon speed
preset yet, and do not repeat the exact A/B unchanged.

Next useful work:

- keep `arm64_blue_dragon_stvewx_stack_lane_fastpath` default-off;
- treat the lane fold as a safe opt-in probe, not a full-speed win;
- move to a larger body-backed target, especially the `8228252C -> 82287788`
  child path or the broader `8228252C-822825C4` state/vector/FPR traffic;
- if revisiting this micro-optimization, use a control-sandwich or a route
  stabilizer before judging FPS.
