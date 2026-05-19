# Blue Dragon Texture Source Checksum Probe

Timestamp: 2026-05-18 23:55:01 EDT

Repo head for tested build: `4da6dd619` plus local source-checksum probe
changes.

Capture path:

- `scratch/thor-debug/20260518-235253-*`

APK SHA-256:

- `9858BD6E67BE57C04CBA6B7FD205E05E0430D7282A013C2989268AA2EFCDFCD7`

Launch target:

- `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Scope

Add `vulkan_trace_texture_source_checksum` and
`vulkan_trace_texture_source_checksum_budget` so active pixel texture source
ranges can be read back from Vulkan shared memory before the draw samples them.

This probe splits two possibilities:

- missing or invalid texture descriptors,
- valid descriptors sampling source memory that is already flat or clear-like.

The probe is intentionally slow and research-only. It is routed through:

- Android activity cvar extras,
- `tools/thor/thor_xenia_debug.ps1`,
- native Vulkan draw handling.

## Validation Commands

```powershell
git diff --check
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -Arm64MiniJit false -MmapAddressHigh 8 -GpuTraceSwap true -GpuTracePacketBudget 160 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 280 -VulkanTraceTextureSourceChecksum true -VulkanTraceTextureSourceChecksumBudget 64 -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 40 -VulkanTraceResolveChecksum true -VulkanTraceResolveChecksumBudget 24 -VulkanTraceEdramChecksum true -VulkanTraceEdramChecksumBudget 12 -VulkanPresentScoredResolveOnSwap true -VulkanPresentScoredResolveRejectClearLike true -VulkanPresentScoredResolveBudget 160 -LiveCaptureSeconds 65
```

## Capture Counts

- `GPU texture-source trace:`: `128`
- `GPU texture-source trace: shared-memory checksum`: `64`
- `GPU texture trace:`: `68`
- `GPU draw trace: prim=`: `183`
- `used_textures=0000007F`: `1`
- `used_textures=000001FF`: `1`
- `pixel=true`: `25`
- `valid=false`: `0`
- `has_unsigned=false`: `0`
- `scaled=true`: `0`
- `GPU resolve trace: shared-memory checksum`: `166`
- `Vulkan IssueSwap`: `1587`
- `VdSwap`: `1591`
- `XE_SWAP`: `1058`
- no AndroidRuntime, fatal signal, native tombstone, GPU-hung, ARM64 fallback,
  or failed-to-reopen-submission lines.

## Key Evidence

The visible 1280x720 pass with pixel shader `05775DE8A2B0B3F5` samples seven
valid source ranges before drawing:

- fetch 0: `1D87C000+00398000`, format `22`, `1280x720`,
  `first_sample=00FFFFFF`, `score=3064`, `clear_like=true`
- fetch 1: `1DC14000+00398000`, format `7`, `1280x720`,
  `first_sample=00000040`, `score=3064`, `clear_like=true`
- fetch 2: `1C340000+000F0000`, format `7`, `640x360`,
  `first_sample=000000C0`, `score=1084`, `clear_like=true`
- fetch 3: `1C304000+0003C000`, format `7`, `320x180`,
  `first_sample=000000C0`, `score=120`, `clear_like=true`
- fetch 4: `1C661000+0000F000`, format `7`, `160x90`,
  `first_sample=000000C0`, `score=30`, `clear_like=true`
- fetch 5: `1F2E6000+00006000`, format `7`, `80x45`,
  `score=92`, `clear_like=false`
- fetch 6: `1F2E0000+00006000`, format `7`, `80x45`,
  `score=92`, `clear_like=false`

That draw still resolves `1DC14000+00398000` as clear-like:

- `first_sample=000000C0`
- `first_sample_matches=1800`
- `score=3064`
- `clear_like=true`

The later `used_textures=000001FF` shader `0ABADD9DA4373CBA` also samples
valid source ranges. Most render-chain sources are clear-like, but the packed
DXT2/3 source is not:

- fetch 7 base `1F190000+00010000`: `score=452`, `clear_like=false`
- fetch 7 mips `1F1A0000+00010000`: `score=251`, `clear_like=false`
- fetch 8 cube `1E8EC000+00180000`: all zero in the sampled range.

## Interpretation

Texture binding is alive and the source readback path now proves the visible
fullscreen shaders are sampling real guest memory ranges. The important shift is
that the large render-chain sources are already clear-like before the draw.

That moves the current wall away from descriptor setup and toward:

- earlier render passes producing mostly flat content,
- pixel shader constants or translation selecting clear-like sources,
- sampling/conversion of the non-clear DXT source not affecting final color,
- or game progression/timing feeding the same clear-like frame repeatedly.

This still does not prove Blue Dragon is rendering useful title output. It
proves the next GPU question is shader/source semantics, not texture descriptor
existence.

## Next Probe

Trace shader constants for the visible fullscreen pixel shader family:

- `05775DE8A2B0B3F5`
- `0ABADD9DA4373CBA`
- `2A0674C564A8A8C5`
- `1E70EB9513D670C9`
- `1B132051B5504DA9`

The goal is to see whether constants, bools, loop parameters, or fetch weights
are selecting the clear-like render-chain sources and ignoring the non-clear
asset texture data.

