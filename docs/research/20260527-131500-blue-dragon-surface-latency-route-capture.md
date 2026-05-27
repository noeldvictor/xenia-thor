# Blue Dragon Surface Latency Route Capture

Date: 2026-05-27 13:15 EDT

## Status

This slice ran the route-engine reset runtime capture requested after
`docs/research/20260527-123500-blue-dragon-surface-latency-capture-path.md`.
It does not prove sustained 30 FPS and does not change emulator generated
behavior.

Capture prefix:
`scratch/thor-debug/20260527-125840-*`

Capture commit:
`e5425d408`

APK SHA256:
`5320F722699CE8B9659BC3A942E5E892CCD01622AE2233F4C1E4D86CB7E3158B`

Enabled cvars / capture options:

- `arm64_offset_memory_address_fastpath=true`
- `frame_cpu_sampler=true`
- `frame_cpu_sampler_auto_surface_layer=true`
- delayed body-time comparators: `82282490,82281D28,82287788`
- audit/disassembly/block tracing off

Screenshot check reached the visible opening sky / dragon-wing route.

## Proof Summary

`scratch/thor-debug/20260527-125840-speed-proof-summary.txt` reports:

- `fatal_marker_count=0`
- `fatal_marker_raw_count=2`
- `fatal_marker_excluded_count=2`
- `fps_marker_count=0`
- `sustained_30fps_proof=false`
- `proof_reason=no_fps_markers`
- `choreographer_skipped_max_frames=149`
- Main Thread `96.1%`
- GPU Commands `11.5%`
- Draw Thread `3.8%`
- XMA Decoder `46.1%`

Final body rows remain the known Main Thread / A64 generated-code wall:

- `82282490=31126351`
- `82281D28=8135606`
- `82287788=3507105`

## SurfaceFlinger Evidence

The sampler auto-selected:

`SurfaceView[jp.xenia.emulator.github.debug/jp.xenia.emulator.EmulatorActivity](BLAST)#87414`

The first route report showed impossible interval math because SurfaceFlinger
returned one sentinel row:

`1473104118248359 9223372036854775807 9223372036854775807`

That row is not a real present. The sampler and route report now reject
sentinel actual-present timestamps and recompute the route report from raw
`surface_latency=` rows, so old captures can be reinterpreted after parser
fixes.

Corrected route report:
`scratch/thor-debug/20260527-125840-route-engine-reset-report.txt`

Corrected SurfaceFlinger summary:

- `surface_latency_decision=surface_latency_valid`
- `surface_latency_valid_frames=126`
- `surface_latency_interval_count=125`
- `surface_latency_span_ms=55149.561`
- `surface_latency_interval_avg_ms=441.196`
- `surface_latency_interval_max_ms=2361.047`
- `surface_latency_intervals_over_33ms=125`
- `surface_latency_intervals_over_50ms=125`
- `surface_latency_rejected_rows=1`

This proves the actual game SurfaceView has very poor present cadence during
the route. It does not by itself identify whether the missed presents come
from A64 generated-code starvation, kernel/HLE synchronization, XMA churn,
Vulkan submission, or Android frame scheduling.

## Route Engine Report

The corrected route report keeps the wall CPU-bound:

- `kernel_hle_churn_share=0.0547`
- `tiny_helper_share=0.1594`
- `route_engine_gap=missing_frametimeline_jank_source_attribution`
- `decision=join_surface_latency_with_kernel_hle_churn`

Top kernel/HLE candidates remain small but material enough to investigate only
with timing alignment:

- `8273EF84 RtlEnterCriticalSection`, share `0.0176`
- `8273EF74 RtlLeaveCriticalSection`, share `0.0176`
- `8273F7B4 KeRaiseIrqlToDpcLevel`, share `0.0054`

Main Thread residency still does not point to core placement as the first
suspect: the sampler saw Main Thread mostly on core 7 (`48/59` samples).

## Tooling Changes

Changed:

- `tools/thor/thor_android_frame_cpu_sampler.ps1`
- `tools/thor/thor_blue_dragon_route_engine_reset_report.ps1`

The sampler now:

- trims selected SurfaceFlinger layer names;
- rejects sentinel / non-positive actual-present rows;
- sorts actual-present timestamps before interval math;
- emits `surface_latency_rejected_rows`;
- uses non-grouped fixed-point millisecond formatting.

The route report now:

- trims sampler surface-latency values;
- recomputes SurfaceFlinger interval stats from raw `surface_latency=` rows;
- rejects sentinel rows even when older sampler summary values are stale;
- requires both valid frames and valid intervals before treating latency as
  present evidence.

Validation:

- PowerShell parser checks passed for both changed scripts.
- The corrected route report reran successfully on
  `scratch/thor-debug/20260527-125840-speed-logcat.txt`.

## Next Slice

Do not rerun the same capture or patch kernel/HLE behavior from aggregate
evidence.

Next useful work is a time-aligned present/jank attribution slice:

- bucket or timestamp SurfaceFlinger present intervals against the route speed
  profile / sampler timeline;
- identify whether long present gaps align with `RtlEnterCriticalSection`,
  `RtlLeaveCriticalSection`, `KeRaiseIrqlToDpcLevel`, XMA, or A64 wall bursts;
- if SurfaceFlinger latency is not enough for attribution, add a Perfetto /
  FrameTimeline capture path for the game SurfaceView.

Only after that join should kernel/HLE churn become a behavior lane.
