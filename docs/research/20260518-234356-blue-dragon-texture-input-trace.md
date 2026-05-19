# Blue Dragon Texture Input Trace

Timestamp: 2026-05-18 23:43:56 EDT

Repo head for tested build: `331a9ff58`

Capture paths:

- `scratch/thor-debug/20260518-233843-*`
- `scratch/thor-debug/20260518-234028-*`

APK SHA-256:

- `12356C0F7F0BB274CF43F547BB2D05F1898AD4FB60B497395B521616BFE689FB`

Launch target:

- `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Scope

Add a budgeted texture-input trace under the existing
`vulkan_trace_draw_state` path so the Blue Dragon fullscreen/composite draws
can prove whether texture descriptors, texture keys, loaded objects, and pixel
shader texture masks are sane.

The trace logs per used fetch:

- vertex versus pixel stage ownership,
- raw fetch base, mip, dimension, format, pitch, tiling, endian, swizzle,
  clamps, filters, and fetch dwords,
- active Vulkan texture binding key,
- loaded unsigned/signed texture availability and host memory use.

## Validation Commands

```powershell
git diff --check
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -Arm64MiniJit false -MmapAddressHigh 8 -GpuTraceSwap true -GpuTracePacketBudget 80 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 80 -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 40 -VulkanTraceResolveChecksum true -VulkanTraceResolveChecksumBudget 24 -VulkanTraceEdramChecksum true -VulkanTraceEdramChecksumBudget 12 -VulkanPresentScoredResolveOnSwap true -VulkanPresentScoredResolveRejectClearLike true -VulkanPresentScoredResolveBudget 120 -LiveCaptureSeconds 45
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -Arm64MiniJit false -MmapAddressHigh 8 -GpuTraceSwap true -GpuTracePacketBudget 160 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 260 -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 40 -VulkanTraceResolveChecksum true -VulkanTraceResolveChecksumBudget 24 -VulkanTraceEdramChecksum true -VulkanTraceEdramChecksumBudget 12 -VulkanPresentScoredResolveOnSwap true -VulkanPresentScoredResolveRejectClearLike true -VulkanPresentScoredResolveBudget 160 -LiveCaptureSeconds 55
```

## Capture Counts

The first run, `20260518-233843`, used too small a draw-state budget. It
captured only early skipped draws:

- `GPU texture trace:`: `0`
- `GPU draw trace: prim=`: `7`
- `skipped no rasterization/no memexport`: `72`
- `pixel=true`: `0`
- no AndroidRuntime, fatal signal, or GPU-hung lines.

The second run, `20260518-234028`, reached the visible textured passes:

- `GPU texture trace:`: `68`
- pixel-stage texture traces: `68`
- vertex-stage texture traces: `0`
- `GPU draw trace: prim=`: `164`
- `pixel=true`: `25`
- `used_textures=0000007F`: `1`
- `used_textures=00000001`: `16`
- `valid=false`: `0`
- `has_unsigned=false`: `0`
- `scaled=true`: `0`
- `GPU resolve trace: edram checksum`: `12`
- `GPU resolve trace: shared-memory checksum`: `166`
- `Vulkan IssueSwap`: `1202`
- `VdSwap`: `1207`
- `XE_SWAP`: `802`
- no AndroidRuntime, fatal signal, native tombstone, GPU-hung, or ARM64 fallback
  lines.

## Key Evidence

The 1280x720 fullscreen draw at log line `2263` is a real pixel-writing draw:

- `prim=13`
- `raster=true`
- `pixel=true`
- `ps_writes=1`
- `normalized_color_mask=000F`
- `viewport=1280x720+0,0`
- `scissor=1280x720+0,0`
- `z_enable=false`
- `used_textures=0000007F`
- `vs_textures=00000000`
- `ps_textures=0000007F`
- `ps_hash=05775DE8A2B0B3F5`

Its seven pixel textures are all valid and loaded:

- fetch 0: `1D87C000`, format/key format `22`, `1280x720`
- fetch 1: `1DC14000`, raw format `54`, key format `7`, `1280x720`
- fetch 2: `1C340000`, key format `7`, `640x360`
- fetch 3: `1C5F4000`, key format `7`, `320x180`
- fetch 4: `1C331000`, key format `7`, `160x90`
- fetch 5: `1F2E6000`, key format `7`, `80x45`
- fetch 6: `1F2E0000`, key format `7`, `80x45`

All seven have `valid=true`, `has_unsigned=true`, and `scaled=false`.

Immediately after the draw, the resolve to `1DC14000+00398000` is still
clear-like:

- `samples=1840`
- `nonzero=1800`
- `varying=79`
- `clear_like=true`
- `low_variation=true`
- `first_sample=000000C0`
- `first_sample_matches=1800`

Later shader variants also have valid bindings, including the
`used_textures=000001FF` draw with:

- fetch 7: `1F190000`, key format `19`, packed mips, `256x256`
- fetch 8: `1E8EC000`, cube, key format `7`, `256x256x6`

That draw also resolves `1DC14000+00398000` to all zero in the sampled shared
memory range.

## Interpretation

Texture descriptor plumbing is probably not the immediate Blue Dragon wall.
The visible composite/downscale passes have valid pixel texture masks, valid
raw fetch constants, live Vulkan texture bindings, loaded unsigned texture
objects, sane sizes, and no scaled-resolve marker surprises.

The remaining wall moved earlier:

- the source textures may already contain clear-like data when sampled,
- pixel shader translation or constants may be producing flat output,
- the earlier draw pass that should populate the 1280x720 source may be wrong,
- or a format/load path may be creating a valid texture object from flat source
  data.

This run does not prove the title is rendering. It proves the failure is no
longer explained by missing texture descriptors on the visible fullscreen pass.

## Next Probe

Add a targeted source-content/shader-output split:

1. Log shared-memory checksums for active texture source ranges before the
   visible composite draws sample them.
2. Add a small targeted pixel-shader or constant trace for hashes
   `05775DE8A2B0B3F5`, `0ABADD9DA4373CBA`, and `2A0674C564A8A8C5`.
3. If needed, add a debug-only solid/gradient output override for one known
   fullscreen pixel shader to prove the render target path can carry non-flat
   output through EDRAM dump and resolve.

