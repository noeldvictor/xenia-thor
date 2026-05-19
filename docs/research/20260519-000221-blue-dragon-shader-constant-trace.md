# Blue Dragon Shader Constant Trace

Timestamp: 2026-05-19 00:02:21 EDT

Repo head for tested build: `64269f49f` plus local shader-constant probe
changes.

Capture path:

- `scratch/thor-debug/20260519-000002-*`

APK SHA-256:

- `198B4AAEAB8406EB820284CCD3C3C2A28C96801A5FE005991F5BBB05F4AC03EF`

Launch target:

- `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Scope

Add `vulkan_trace_shader_constants` and
`vulkan_trace_shader_constants_budget` so active pixel shader constant maps and
values can be logged next to the draw/texture/source checksum traces.

The trace logs:

- float constant count and bitmap,
- dynamic float addressing flag,
- loop and bool constant bitmaps,
- raw and float-decoded values for up to 32 used float constants,
- raw loop and bool values when used.

## Validation Commands

```powershell
git diff --check
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -Arm64MiniJit false -MmapAddressHigh 8 -GpuTraceSwap true -GpuTracePacketBudget 120 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 280 -VulkanTraceShaderConstants true -VulkanTraceShaderConstantsBudget 64 -VulkanTraceTextureSourceChecksum true -VulkanTraceTextureSourceChecksumBudget 64 -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 32 -VulkanTraceResolveChecksum true -VulkanTraceResolveChecksumBudget 20 -VulkanPresentScoredResolveOnSwap true -VulkanPresentScoredResolveRejectClearLike true -VulkanPresentScoredResolveBudget 120 -LiveCaptureSeconds 65
```

## Capture Counts

- `GPU shader-constant trace:`: `659`
- `05775DE8A2B0B3F5`: `8` lines
- `0ABADD9DA4373CBA`: `28` lines
- `2A0674C564A8A8C5`: `8` lines
- `1E70EB9513D670C9`: `15` lines
- `1B132051B5504DA9`: `544` lines
- `GPU texture-source trace: shared-memory checksum`: `64`
- `GPU draw trace: prim=`: `183`
- `used_textures=0000007F`: `1`
- `used_textures=000001FF`: `1`
- `GPU resolve trace: shared-memory checksum`: `126`
- no AndroidRuntime, fatal signal, GPU-hung, or ARM64 fallback lines.

## Key Evidence

The main `used_textures=0000007F` 1280x720 shader
`05775DE8A2B0B3F5` has a small, sane-looking constant footprint:

- `float_count=3`
- `float_dynamic=false`
- `loop_bitmap=00000000`
- all bool bitmap words are zero
- used float constants: `c283`, `c510`, `c511`
- values include `1.4`, `0.075`, `0.50005`, `711.11115`, `4.0`,
  and `0.125`

The following downscale/composite shader `2A0674C564A8A8C5` also uses only
three float constants and no bool or loop constants:

- used float constants: `c283`, `c510`, `c511`
- values include `0.25`, `10.0`, `4.0`, `0.2125`, `0.0721`, and `0.7154`
- one raw lane is `FFFFFFFF`, which decodes as NaN if interpreted as a float.
  This may be intentional packed/non-float data in a float register.

The `used_textures=000001FF` shader `0ABADD9DA4373CBA` has:

- `float_count=13`
- `float_dynamic=false`
- no bool or loop constants
- used constants `c269`, `c270` through `c277`, `c282`, `c509`, `c510`,
  and `c511`
- repeated raw lanes such as `00000001,8278381C`, which again may be packed
  data rather than true float values.

The repeated downscale shader `1E70EB9513D670C9` has no float, bool, or loop
constants.

## Interpretation

The visible fullscreen shaders are not failing because their constant maps are
empty, fully zero, or missing bool/loop state. Constants are present where
expected, and the no-constant downscale shader is a real shader choice rather
than a missing upload.

The evidence now points past descriptors, source memory readback, and gross
constant upload. The next likely wall is shader translation semantics or the
earlier passes that produce mostly clear-like render-chain inputs.

## Next Probe

Dump or log the Xenos microcode / translated SPIR-V for the target pixel shader
hashes and inspect:

- which texture fetches actually contribute to color,
- whether raw/pseudo-float constants such as `FFFFFFFF` and `8278381C` are used
  as bit patterns,
- whether control flow, predicate, or export code is being translated into a
  path that writes clear-like output,
- whether the non-clear DXT inputs are gated out by shader logic.

