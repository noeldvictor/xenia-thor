# Blue Dragon Present/Jank Attribution Instrumentation

Date: 2026-05-27 13:25 EDT

## Status

This slice follows
`docs/research/20260527-131500-blue-dragon-surface-latency-route-capture.md`.
It does not change emulator generated behavior and does not claim speed.

The previous capture proved valid game `SurfaceView` latency and bad present
cadence, but the evidence was still aggregate. SurfaceFlinger latency rows use
device monotonic nanoseconds, while the sampler only stored host wall time and
elapsed stopwatch milliseconds. That means the old capture cannot rigorously
join long present intervals to thread samples, logcat speed-profile intervals,
kernel/HLE helpers, XMA, A64 wall bursts, or Vulkan frame pacing.

## Tooling Changes

Changed:

- `tools/thor/thor_android_frame_cpu_sampler.ps1`
- `tools/thor/thor_blue_dragon_present_jank_attribution.ps1`

The frame/CPU sampler now records device monotonic time from `/proc/uptime`:

- `start_device_monotonic_ns=...`
- each sample row includes `device_monotonic_ns=...`
- `surface_latency_capture_device_monotonic_ns=...`

The new attribution report:

`tools/thor/thor_blue_dragon_present_jank_attribution.ps1`

Input:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_blue_dragon_present_jank_attribution.ps1 `
  -LogPath <capture>-speed-logcat.txt `
  -OutPath <capture>-present-jank-attribution.txt
```

Behavior:

- parses raw SurfaceFlinger latency rows and rejects sentinel actual-present
  timestamps;
- parses sampler rows and requires `device_monotonic_ns` before time
  attribution;
- parses per-thread sampler deltas for Main Thread, XMA Decoder, GPU Commands,
  Draw Thread, audio, and guest XThreads;
- parses A64 speed-profile timestamps and separates kernel/HLE deltas from the
  known wall cluster;
- when monotonic samples exist, assigns long present intervals to nearest
  sampler buckets and nearest speed-profile buckets;
- when monotonic samples are missing, fails closed with a concrete blocker.

## Old Capture Result

Ran the attribution report on the previous capture:

`scratch/thor-debug/20260527-125840-present-jank-attribution.txt`

Result:

- `sample_count=59`
- `device_monotonic_sample_count=0`
- `surface_latency_valid_frames=126`
- `surface_latency_interval_count=125`
- `surface_latency_rejected_rows=1`
- `long_present_interval_count=125`
- `attribution_status=missing_device_monotonic_samples`
- `blocker=surface_latency_actual_present_ns_cannot_be_aligned_to_sampler_or_logcat_time`
- `decision=rerun_capture_with_device_monotonic_sampler`

This closes the old capture for attribution decisions. It can prove bad present
cadence, but it cannot prove whether the gaps align with kernel/HLE helpers,
XMA, A64 wall bursts, or Vulkan pacing.

## Validation

- PowerShell parser checks passed for:
  - `tools/thor/thor_android_frame_cpu_sampler.ps1`
  - `tools/thor/thor_blue_dragon_present_jank_attribution.ps1`
- The new attribution report ran successfully on
  `scratch/thor-debug/20260527-125840-speed-logcat.txt`.
- Sampler smoke against a stopped app wrote `start_device_monotonic_ns` and
  exited cleanly with `decision=process_not_running`.

## Next Slice

Run a new route capture because the sampler format changed:

- FullDeploy current branch.
- Launch the same quiet 180-second Blue Dragon route with:
  - `-Arm64OffsetMemoryAddressFastpath true`
  - `-FrameCpuSampler true`
  - `-FrameCpuSamplerAutoSurfaceLayer true`
  - audit/disassembly/block tracing off
  - delayed body-time comparators `82282490,82281D28,82287788`
- Then run:
  - `thor_blue_dragon_speed_capture_summary.ps1`
  - `thor_blue_dragon_route_engine_reset_report.ps1`
  - `thor_blue_dragon_present_jank_attribution.ps1`

Behavior work remains blocked until the attribution report shows whether long
present intervals align with `RtlEnterCriticalSection`,
`RtlLeaveCriticalSection`, `KeRaiseIrqlToDpcLevel`, XMA churn, A64 wall bursts,
or Vulkan frame pacing.
