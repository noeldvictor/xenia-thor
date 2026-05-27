# Blue Dragon Surface Latency Capture Path

Date: 2026-05-27 12:35 EDT

## Status

This slice adds the missing present-attribution capture path after
`docs/research/20260527-110500-blue-dragon-route-engine-sampler-capture.md`.
It does not change emulator generated behavior or APK code.

The previous route-engine sampler capture proved:

- no sustained 30 FPS proof;
- Main Thread / A64 remained the wall;
- Main Thread was mostly resident on core 7, so core placement is not the
  first suspect;
- `gfxinfo` only reported three app frames, so it is not a valid proxy for the
  Vulkan SurfaceView present stream.

## Tooling Changes

`tools/thor/thor_android_frame_cpu_sampler.ps1` now:

- accepts `-AutoSurfaceLayer true` by default;
- selects the actual game layer by preferring
  `SurfaceView[...](BLAST)#...`, then non-BLAST `SurfaceView[...]#...`, then a
  package layer fallback;
- clears SurfaceFlinger latency for the selected layer at capture start;
- reads `dumpsys SurfaceFlinger --latency <layer>` at capture end;
- writes raw `surface_latency=` rows plus summary fields:
  `surface_latency_valid_frames`, `surface_latency_interval_count`,
  `surface_latency_span_ms`, `surface_latency_interval_avg_ms`,
  `surface_latency_interval_max_ms`,
  `surface_latency_intervals_over_33ms`, and
  `surface_latency_intervals_over_50ms`.

`tools/thor/thor_xenia_debug.ps1` now:

- records `frame_cpu_sampler_auto_surface_layer` in capture metadata;
- passes `-AutoSurfaceLayer` to the frame/CPU sampler when
  `-FrameCpuSampler true` is used.

`tools/thor/thor_blue_dragon_route_engine_reset_report.ps1` now:

- parses surface latency summary rows from the sampler output;
- emits those rows into the route-engine report;
- reports `route_engine_gap=missing_frametimeline_jank_source_attribution`
  when SurfaceFlinger latency is valid;
- reports
  `decision=join_surface_latency_with_kernel_hle_churn` when valid latency and
  material kernel/HLE churn coexist.

If a future capture still lacks valid SurfaceFlinger latency rows, the report
keeps the decision at
`capture_surface_latency_or_frametimeline_then_kernel_hle_churn_audit`.

## Harness Updates

Updated the current planning surfaces so the next loop does not repeat the
completed sampler capture:

- `AGENTS.md`
- `.agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md`
- `.agents/skills/xenia-a64-speed-hotpath/SKILL.md`
- `.agents/skills/xenia-codex-goal-loop/SKILL.md`
- `.agents/skills/xenia-continual-harness-refiner/SKILL.md`
- `tools/thor/thor_codex_goal_loop.ps1`

## Validation

- PowerShell parser checks passed for:
  - `tools/thor/thor_android_frame_cpu_sampler.ps1`
  - `tools/thor/thor_xenia_debug.ps1`
  - `tools/thor/thor_blue_dragon_route_engine_reset_report.ps1`
- The route report still parses the existing
  `scratch/thor-debug/20260527-105906-speed-logcat.txt` capture and now
  correctly reports:
  `decision=capture_surface_latency_or_frametimeline_then_kernel_hle_churn_audit`.
- Sampler smoke against the stopped app exits cleanly with
  `decision=process_not_running`.

## Next Capture

Run the same route-engine capture again only after this tooling commit is in
place. This is not a behavior A/B.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 `
  -Mode LaunchBlueDragonSpeedCapture `
  -DeviceSerial c3ca0370 `
  -LiveCaptureSeconds 180 `
  -PerfSampleSeconds "120,150,175" `
  -Arm64OffsetMemoryAddressFastpath true `
  -Arm64SpeedProfileIntervalMs 15000 `
  -Arm64SpeedProfileTopFunctions 30 `
  -Arm64SpeedProfileMinDelta 1 `
  -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" `
  -Arm64SpeedProfileBodyTimeAfterMs 120000 `
  -FrameCpuSampler true `
  -FrameCpuSamplerAutoSurfaceLayer true `
  -FrameCpuSamplerIntervalMs 1000 `
  -DisassembleFunctions false `
  -StopAppAfterCapture true
```

Then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_blue_dragon_speed_capture_summary.ps1 `
  -LogPath <new-speed-logcat> `
  > <new-speed-proof-summary>

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_blue_dragon_route_engine_reset_report.ps1 `
  -LogPath <new-speed-logcat> `
  -OutPath <new-route-engine-reset-report>
```

Decision gate:

- if SurfaceFlinger latency is valid, join present interval spikes against the
  speed-profile route rows before touching kernel/HLE behavior;
- if latency is still missing, add a Perfetto FrameTimeline capture path or fix
  layer selection before another speed run;
- do not reopen closed A64 micro-lanes from this tooling-only slice.
