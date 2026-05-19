# Blue Dragon Quiet Speed Lane

Date: 2026-05-19 15:46 EDT

## Context

Blue Dragon reaches title, language selection, loading, and the opening scene on
the AYN Thor Max, but the run is extremely slow. The previous trace-off sample
showed the main guest CPU thread and XMA/audio as the likely bottlenecks, with
GPU command work lower and no Vulkan device-loss evidence.

Goal for this pass: make a repeatable speed lane that separates normal proof
captures from low-overhead CPU/GPU/audio sampling.

## Implementation

- Added `LaunchBlueDragonSpeedCapture` to `tools/thor/thor_xenia_debug.ps1`.
- Speed mode launches the known Blue Dragon target with the known START/A nop
  HID sequence, hides Android OSD, disables trace-heavy flags, captures timed
  `top -H` / process / memory / SurfaceFlinger / thermal snapshots, and only
  dumps logcat after the run.
- Added `log_level` forwarding from Android launch extras and made speed mode
  use `log_level=0` for error-only Xenia logging.
- Added opt-in trace gates for previously unconditional noisy logs:
  - `xma_trace_context_state`
  - `gpu_trace_texture_cache_actions`
  - `vulkan_trace_pipeline_creation`
  - `gpu_trace_swap` now gates the plain `XE_SWAP` log too.
- Added experimental `xma_fast_silence` to advance XMA contexts with silent
  output for bring-up-only comparison runs.
- Hardened `tools/thor/thor_build.ps1` so failed Gradle or ADB commands throw
  instead of installing stale APKs after a native build failure.

## Device Runs

Baseline quiet speed run after log gating:

- Capture: `scratch/thor-debug/20260519-153505-*`
- APK SHA-256:
  `7869AE163103E7F79C50B2FB5D6BBEC25D2FFA127631F87DAEDBB553F2773EB4`
- Result: process stayed alive in `EmulatorActivity`, no AndroidRuntime fatal,
  no native fatal signal, no `VK_ERROR_DEVICE_LOST`, and no GPU-hung line.
- Screenshot at 130 seconds: Blue Dragon loading screen.
- Follow-up after more wall time: `scratch/thor-debug/20260519-154546-*`
  reached the opening scene with `Microsoft Game Studios Presents`.

Noise comparison:

| Run | Log bytes | Filtered bytes | XMA spam | Known GPU/XMA spam |
| --- | ---: | ---: | ---: | ---: |
| `20260519-152249` pre-gating | 2137889 | 2010959 | 2948 | many |
| `20260519-153505` quiet lane | 264909 | 17888 | 0 | 0 |

Thread sample from `20260519-153505`:

- 60 seconds: main guest CPU about `80.7%`, XMA decoder about `38.4%`, GPU
  commands about `19.2%`.
- 120 seconds: main guest CPU about `103%`, XMA decoder about `7.6%`, GPU
  commands about `3.8%`.
- Final: main guest CPU about `103%`; one instantaneous sample caught GPU
  commands at `100%`, but cumulative GPU command time remained much lower than
  main guest CPU time.

Experimental fast-silence run:

- Command included `-XmaFastSilence true`.
- Capture: `scratch/thor-debug/20260519-154141-*`
- APK SHA-256:
  `784039C4306771AE3DF26AAE654757251B1213E3F77A3740262562E5AEB779F1`
- Result: process stayed alive; follow-up `20260519-154546-*` reached the
  opening scene.
- XMA instantaneous cost still appears during early loading and drops later.
  This does not solve the overall speed wall.

## Findings

- The old debug loop was measuring too much logging overhead. Quiet speed mode
  cuts known XMA/GPU log spam to zero and shrinks filtered logcat from megabytes
  to kilobytes.
- Vulkan/Adreno is rendering frames. The current slow path is not explained by
  device loss or logcat alone.
- XMA/audio is a secondary cost, especially early, but the persistent bottleneck
  is the main guest CPU thread.
- `xma_fast_silence` is useful as an experiment knob but not a real performance
  fix. It does not remove the need for a much better AArch64 backend.

## Next Actions

- Use `LaunchBlueDragonSpeedCapture` for speed samples, not live trace captures.
- Keep `xma_fast_silence` opt-in and treat it as a bring-up probe only.
- Focus the next speed pass on A64 backend dispatch, hot guest functions, and
  generated-code profiling rather than Vulkan first.
