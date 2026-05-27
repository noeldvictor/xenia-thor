# Blue Dragon Route-Engine Reset

Date: 2026-05-26 21:07 EDT

## Status

This is a reset slice after the quiet proof refresh still reported no sustained
30 FPS proof. It does not change emulator generated behavior.

The immediate problem is not "which one more A64 micro-fastpath?". The problem
is that the sprint has been deciding from static hot blocks and sparse final
perf snapshots while the game route is a time-varying system: frame production,
Android scheduling/core residency, thermal/frequency state, generated-code hot
regions, kernel/HLE helper churn, and old loading-route helpers all overlap.

## Online Rethink Sources

Accessed 2026-05-26.

- QEMU's TCG docs describe QEMU as a dynamic translator and call out CPU-state
  optimization as a translator concern:
  <https://www.qemu.org/docs/master/devel/tcg.html>.
- Perfetto's FrameTimeline docs define Android jank as presentation not matching
  the scheduler's predicted present time, and identify SurfaceFlinger as the
  module reporting the jank source:
  <https://android.googlesource.com/platform/external/perfetto/+/refs/heads/main/docs/data-sources/frametimeline.md>.
- Spink, Wagstaff, Franke, and Topham's region-based DBT work argues for
  translation units containing multiple basic blocks and for using branch type
  plus control-flow profiling to improve common-case code quality:
  <https://doi.org/10.1145/2597809.2597810>.
- Hsu et al.'s early-exit guided region formation work is the right warning for
  us: if hot regions take early exits often, optimized traces lose their value,
  so region quality has to be measured before behavior work:
  <https://doi.org/10.1145/2517326.2451519>.

## New Tooling

Added `tools/thor/thor_blue_dragon_route_engine_reset_report.ps1`.

It reads a Blue Dragon speed log plus optional metadata, final perf, and proof
summary, then emits:

- proof gate status;
- speed-profile dispatch totals;
- route top-function aggregates;
- body-time wall rows;
- class shares for closed wall cluster, old tiny helper/leaf traffic,
  kernel/HLE churn, and unknown route hot functions;
- a decision line for the next route-engine slice.

Run on the latest quiet proof refresh:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_blue_dragon_route_engine_reset_report.ps1 `
  -LogPath scratch\thor-debug\20260526-124549-speed-logcat.txt `
  -OutPath scratch\thor-debug\20260526-132500-route-engine-reset-report.txt
```

Key output:

```text
sustained_30fps_proof=false
proof_reason=no_fps_markers
wall=main_thread_a64_generated_code
summary_intervals=13
dispatch entry_delta=65915643
dispatch direct_delta=55176739
dispatch indirect_delta=10694938
dispatch extern_delta=5121266
dispatch resolves_delta=6115
class class=tiny_hot_leaf_or_helper top_delta_sum=10293210 share=0.1562
class class=kernel_hle_churn top_delta_sum=3634852 share=0.0551
class class=closed_wall_cluster top_delta_sum=822747 share=0.0125
decision=investigate_kernel_hle_churn_with_frame_cpu_residency_sampler
```

The top entry-count candidates include old loading-route helpers
`827294CC`, `8272A3A4`, `826C5620`, and `826BF770`, but final delayed body-time
still points at the `82282490` / `82281D28` / `82287788` wall. That mismatch is
exactly why another isolated body-block patch is the wrong next move.

Added `tools/thor/thor_android_frame_cpu_sampler.ps1`.

It samples a live Android process for:

- process and SurfaceFlinger layer identity;
- per-sample CPU frequencies;
- thermal zones;
- per-thread jiffies and CPU core residency from `/proc/<pid>/task/*/stat`;
- brief `dumpsys gfxinfo` frame/jank counters;
- optional SurfaceFlinger latency output for a named layer.

Smoke test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_frame_cpu_sampler.ps1 `
  -DeviceSerial c3ca0370 `
  -PackageName jp.xenia.emulator.github.debug `
  -Seconds 1 `
  -OutPath scratch\thor-debug\20260526-132500-frame-cpu-sampler-smoke.txt
```

The smoke test found the launcher process and wrote CPU frequency, thermal,
thread residency, SurfaceFlinger layer, and `gfxinfo` rows. It is not a Blue
Dragon speed capture.

Updated `tools/thor/thor_xenia_debug.ps1` so future
`LaunchBlueDragonSpeedCapture` runs can enable the sampler in parallel:

```powershell
-FrameCpuSampler true -FrameCpuSamplerIntervalMs 1000
```

Metadata now records `frame_cpu_sampler`,
`frame_cpu_sampler_interval_ms`, and `frame_cpu_sampler_surface_layer`.

## Interpretation

The latest capture does not justify reopening the closed A64 micro-lanes. It
does reveal a better split:

- body-time wall: still the `82282490` / `82281D28` / `82287788` cluster;
- route entry-count traffic: substantial old helper/leaf and kernel/HLE churn;
- proof gap: no FPS markers, and no time-series frame/present data;
- Android gap: no route-aligned main-thread core residency, frequency, thermal,
  or jank attribution.

This reframes the sprint as a route-engine measurement problem. Before another
behavior patch, the next capture must join:

1. quiet route proof summary;
2. A64 speed-profile top/body rows;
3. frame/jank/present evidence;
4. per-thread CPU residency/frequency/thermal data.

## Next Capture Command

Use this as the next evidence slice, not as a behavior experiment:

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

Then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_blue_dragon_speed_capture_summary.ps1 `
  -LogPath <new-speed-logcat> `
  > <new-speed-proof-summary>

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_blue_dragon_route_engine_reset_report.ps1 `
  -LogPath <new-speed-logcat> `
  -OutPath <new-route-engine-reset-report>
```

## Decision Gates

- If frame/jank evidence is absent or useless, add a Perfetto/FrameTimeline
  capture lane before any new behavior patch.
- If Main Thread burns a full core while CPU frequency/residency shows poor
  scheduling, investigate Android thread priority/affinity/core residency.
- If kernel/HLE churn stays above roughly 5% of route top-entry traffic and
  aligns with jank, audit kernel/HLE lock/IRQL/event helper churn as a system
  lane.
- If old helper/leaf traffic dominates entries but not body-time, do not patch
  it for speed without body-weighted proof.
- If a new body-dominant region outside the closed wall cluster appears, run a
  region-quality/early-exit audit before designing region or alternate-entry
  behavior.
- If GPU/present/submit/pipeline work overtakes Main Thread, only then pivot to
  Vulkan/frame pacing.

## Validation

- PowerShell parser check passed for:
  - `tools/thor/thor_blue_dragon_route_engine_reset_report.ps1`
  - `tools/thor/thor_android_frame_cpu_sampler.ps1`
  - `tools/thor/thor_xenia_debug.ps1`
- Route-engine reset report ran against
  `scratch/thor-debug/20260526-124549-speed-logcat.txt`.
- Frame/CPU sampler smoke ran against the live launcher process.
- Start-Process quoting smoke for the same sampler path passed, matching the
  way `thor_xenia_debug.ps1` launches it during future speed captures.
