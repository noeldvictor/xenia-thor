# Blue Dragon Zero Frontbuffer / Resolve Split

Date: 2026-05-18 22:16 EDT

Device: AYN Thor Max (`c3ca0370`)

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

APK SHA-256:
`FCE19F9A549C67E40F4DF3860C55F43C01C77C3FD254CE9F7525A262F78AD321`

## Captures

- `scratch/thor-debug/20260518-215112-*`
- `scratch/thor-debug/20260518-215656-*`
- `scratch/thor-debug/20260518-220223-*`
- `scratch/thor-debug/20260518-220457-*`
- `scratch/thor-debug/20260518-220838-*`
- `scratch/thor-debug/20260518-221334-*`

## What Changed

Added names to ARM64 compiled call trace entries so hot guest PCs include known
function labels, LR labels, and CTR labels without declaring new functions from
the trace path. The tracer now uses `QueryFunction` /
`FindFunctionsWithAddress`, avoiding accidental function creation during
sampling.

## Evidence

The named A64 trace run was stable:

- `700` A64 call trace lines.
- `220` Xboxkrnl wait trace lines.
- `100` Xboxkrnl event trace lines.
- `1632` `VdSwap` lines.
- `1632` `XE_SWAP` lines.
- `0` AndroidRuntime, fatal, GPU-hung, or ARM64 fallback lines.

Top meaningful hot guest PCs after helper prolog/epilog functions:

- `822870D8 sub_822870D8`
- `82170B90 sub_82170B90`
- `82473D78 sub_82473D78`
- `82474388 sub_82474388`
- `82485DD8 sub_82485DD8`
- `8248B040 sub_8248B040`
- `82212EC0 sub_82212EC0`

The targeted copy/resolve run proved the official swap frontbuffers are empty:

- Frontbuffers `1CA1C000` and `1CDB4000`.
- Size `1280x720`, `3686400` bytes.
- Sampled nonzero count: `0`.
- First nonzero offset: `-1`.
- Checksum: `C0F40A2C0EC1FB33`.

At the same time, Vulkan `IssueCopy` is producing nonzero resolves:

- `1D87C000+00385000`, `720x720`, pitch `1280`, format `6`, score `2305`.
- `1DC27000+00385000`, `720x720`, pitch `1280`, format `7`, score `3026`.
- `1C340000+000F0000`, `640x360`, pitch `640`, format `7`, score `1084`.
- `1DC14000+00398000`, `1280x720`, pitch `1280`, format `7`, score `3064`,
  but later the same target resolves to all zero and is cleared as stale.

The large-minimum scored run (`640x360`) avoided the tiny `80x45` candidate and
kept presenting `1C340000`, but the screenshot stayed flat dark/black.

The longer low-trace run stayed alive for 180 seconds:

- `5119` `VdSwap`
- `3408` `XE_SWAP`
- `14803` XMA kicks
- `0` AndroidRuntime, fatal, GPU-hung, or ARM64 fallback lines

It still did not reach title.

The forced 8_8_8_8 probe changed the visible output:

- Forced source: `1D88F000+00385000`, `720x720`, pitch `1280`, format `6`.
- Screenshot became white instead of black.
- This confirms the Android/Vulkan presenter can show guest resolve memory when
  forced to a visible source/format.

## Interpretation

This is no longer an Android activity, APK install, or native Vulkan presenter
bring-up problem. Blue Dragon is alive, swapping, and feeding XMA, while the
official frontbuffers passed through `VdSwap` / `XE_SWAP` are zero.

The useful split is:

- Official swap frontbuffer path: alive but empty.
- Resolve path: contains nonzero data, but the current debug present selector
  can choose visually useless targets.
- Forced format-6 presentation: proves the presenter path is capable of showing
  nonblack/non-dark data.

The next real fix is not another blind wait. It should link render target state,
resolve command, and present candidate with better content classification, then
trace why the game's selected frontbuffers are cleared or never populated.

## Next Probes

- Add a visual-content scorer that penalizes near-uniform full-screen clears,
  not just zero/nonzero samples.
- Log format, first words, variance, and chosen candidate at swap with the last
  actual `IssueCopy` state nearby.
- Compare format-6 and format-7 candidate presentation explicitly; the format-7
  path may be technically valid but visually black for this debug usage.
- Keep `time_scalar=1.0` for progress probes now that the watchdog is not the
  immediate wall in the latest long run.
