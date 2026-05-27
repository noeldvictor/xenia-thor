# Blue Dragon Route-Engine Sampler Capture

Date: 2026-05-27 11:05 EDT

## Status

This slice ran the route-engine reset capture on Thor. It is a fresh evidence
capture, not an A64 behavior patch.

The result is still no sustained 30 FPS proof. The route reached the visible
opening sky / dragon-wing, but the route remained Main Thread / A64
generated-code bound and emitted no FPS proof markers.

## Capture

Build/deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 `
  -Mode FullDeploy `
  -DeviceSerial c3ca0370
```

Route command:

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
  -FrameCpuSamplerIntervalMs 1000 `
  -DisassembleFunctions false `
  -StopAppAfterCapture true
```

Artifacts:

- Log: `scratch/thor-debug/20260527-105906-speed-logcat.txt`
- Metadata: `scratch/thor-debug/20260527-105906-meta.txt`
- Screenshot: `scratch/thor-debug/20260527-105906-screenshot.png`
- Final perf: `scratch/thor-debug/20260527-105906-perf-final.txt`
- Sampler: `scratch/thor-debug/20260527-105906-frame-cpu-sampler.txt`
- Proof summary: `scratch/thor-debug/20260527-105906-speed-proof-summary.txt`
- Route report: `scratch/thor-debug/20260527-105906-route-engine-reset-report.txt`

Capture metadata:

```text
head=16f08b654
apk_sha256=5320F722699CE8B9659BC3A942E5E892CCD01622AE2233F4C1E4D86CB7E3158B
live_capture_seconds=180
arm64_offset_memory_address_fastpath=true
target=/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso
```

The screenshot was manually checked and shows the visible opening sky /
dragon-wing route.

## Proof Summary

```text
fatal_marker_count=0
fatal_marker_raw_count=3
fatal_marker_excluded_count=3
fps_marker_count=0
choreographer_skipped_events=14
choreographer_skipped_max_frames=117
thread_cpu main_thread=96.1
thread_cpu gpu_commands=11.5
thread_cpu draw_thread=3.8
thread_cpu xma_decoder=50.0
body fn=82282490 total=32940122 delta=10030365 entries_delta=57876 ticks_per_entry=173 code_size=86452
body fn=82281D28 total=8122412 delta=2634506 entries_delta=27612 ticks_per_entry=95 code_size=95724
body fn=82287788 total=3528896 delta=1143850 entries_delta=229987 ticks_per_entry=4 code_size=35712
wall=main_thread_a64_generated_code
sustained_30fps_proof=false
proof_reason=no_fps_markers
```

The raw fatal marker hits were unrelated noise:

- Zygote process exit due to signal 9 for another process.
- Two `iportPostJsonRequest` lines with `"reason": "check failed"`.

`tools/thor/thor_blue_dragon_speed_capture_summary.ps1` now counts fatal
markers line-by-line, excludes those known false positives, and reports raw
versus excluded marker counts.

## Route Report

The route report now joins the frame/CPU sampler output:

```text
summary_intervals=13
dispatch entry_delta=67513404
dispatch direct_delta=56599271
dispatch indirect_delta=10870304
dispatch extern_delta=5237684
class class=background_route_function top_delta_sum=14329762 share=0.2123
class class=route_hot_unknown top_delta_sum=13585058 share=0.2012
class class=tiny_hot_leaf_or_helper top_delta_sum=10749239 share=0.1592
class class=kernel_hle_churn top_delta_sum=3689336 share=0.0546
class class=closed_wall_cluster top_delta_sum=952892 share=0.0141
sampler_decision=sampler_complete
sampler_samples=59
sampler_gfxinfo_total_frames=3
sampler_gfxinfo_janky_frames=1 (33.33%)
kernel_hle_churn_share=0.0546
tiny_helper_share=0.1592
route_engine_gap=missing_frametimeline_present_attribution
decision=add_frametimeline_present_attribution_then_kernel_hle_churn_audit
```

Top dynamic route candidates remain split between old helper/leaf traffic,
unknown route hot functions, and kernel/HLE churn:

```text
827294CC share=0.0900 tiny_hot_leaf_or_helper
8272A3A4 share=0.0642 route_hot_unknown
826C5620 share=0.0604 route_hot_unknown
826BF770 share=0.0419 tiny_hot_leaf_or_helper
8273EF84 RtlEnterCriticalSection share=0.0177 kernel_hle_churn
8273EF74 RtlLeaveCriticalSection share=0.0177 kernel_hle_churn
8273F7B4 KeRaiseIrqlToDpcLevel share=0.0054 kernel_hle_churn
```

The body-time wall remains familiar:

```text
82282490 total=32940122 delta=10030365 ticks_per_entry=173
82281D28 total=8122412 delta=2634506 ticks_per_entry=95
82287788 total=3528896 delta=1143850 ticks_per_entry=4
```

## Sampler Interpretation

The sampler found the Main Thread consumed by far the most jiffies:

```text
sampler_thread tid=8261 name=Main Thread (F8 jiffies=12611
sampler_thread tid=8251 name=XMA Decoder (F8 jiffies=2822
sampler_thread tid=8249 name=GPU Commands (F jiffies=2756
sampler_thread tid=8315 name=Draw Thread (F8 jiffies=349
```

Core residency does not make Android scheduling the first suspect: the Main
Thread was sampled mostly on core 7.

```text
sampler_core name=Main Thread (F8 core=3 samples=4
sampler_core name=Main Thread (F8 core=4 samples=2
sampler_core name=Main Thread (F8 core=5 samples=1
sampler_core name=Main Thread (F8 core=6 samples=3
sampler_core name=Main Thread (F8 core=7 samples=49
```

`gfxinfo` is not enough for this game route: it reported only three frames for
the app process, so it is not a reliable proxy for the Vulkan SurfaceView
present stream. The next measurement gap is FrameTimeline / SurfaceFlinger
present attribution, not another closed A64 micro-audit.

## Decision

Do not rerun this same quiet route capture and do not reopen the closed A64,
VMX, fastmem, fast-entry, nonclosed-cache, scalar-context, or static-superblock
lanes from this evidence.

Next useful slice:

1. add a route-clean FrameTimeline / present-attribution capture path, or a
   SurfaceFlinger latency fallback for the actual game SurfaceView layer;
2. keep `-Arm64OffsetMemoryAddressFastpath true` for future Blue Dragon speed
   captures, globally default-off;
3. audit kernel/HLE churn only after the frame/present trace can say whether
   `RtlEnterCriticalSection`, `RtlLeaveCriticalSection`, and
   `KeRaiseIrqlToDpcLevel` align with missed frames or are just route-entry
   background cost.

## Tooling Updates

- `tools/thor/thor_blue_dragon_speed_capture_summary.ps1` now excludes the
  known false-positive fatal marker lines while preserving raw/excluded counts.
- `tools/thor/thor_blue_dragon_route_engine_reset_report.ps1` now reads
  `*-frame-cpu-sampler.txt`, reports sampler thread/core rows, handles CRLF
  raw text, and changes the decision after sampler evidence exists to
  `add_frametimeline_present_attribution_then_kernel_hle_churn_audit`.

## Validation

- `FullDeploy` completed successfully on Thor `c3ca0370`.
- The 180-second route capture completed and stopped the app after capture.
- The screenshot was visually checked.
- PowerShell parser checks passed for:
  - `tools/thor/thor_blue_dragon_speed_capture_summary.ps1`
  - `tools/thor/thor_blue_dragon_route_engine_reset_report.ps1`
- `git diff --check` passed with only the existing CRLF replacement warnings.
- The updated proof summary reported `fatal_marker_count=0`.
- The updated route report emitted Main Thread sampler rows and
  `decision=add_frametimeline_present_attribution_then_kernel_hle_churn_audit`.
