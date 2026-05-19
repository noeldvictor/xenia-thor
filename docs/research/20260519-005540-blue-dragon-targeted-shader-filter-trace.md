# Blue Dragon Targeted Shader Filter Trace

Date: 2026-05-19 00:55 EDT

Repo: `xenia-thor`

Branch: `master`

Base commit: `f9a2679ed`

Device: AYN Thor Max, ADB serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

The previous vertex-fetch trace ruled out the final fullscreen blit vertex data
as the immediate black-frame cause. This run asks a narrower question: which
upstream shader pass makes the 1280x720 render-chain source become zero before
the final blit?

## Tooling Added

- `vulkan_trace_draw_shader_filter`
- `vulkan_trace_shader_constants_shader_filter`
- `vulkan_trace_texture_source_shader_filter`

All three accept comma-separated Xenos ucode hashes. The filters are routed
through Android launch extras and `tools/thor/thor_xenia_debug.ps1`, allowing
draw-state, texture-source, and constant budgets to be spent only on the shader
family under investigation.

## Captures

### `scratch/thor-debug/20260519-004401-*`

APK SHA-256:
`482F60FDCB0F2C4E4EF39EB1DD9D08BBA359424D343179DA6B353900E59F68B2`

Key flags:

- `-MmapAddressHigh 8`
- `-MountCache true`
- `-HidNopConnected true`
- `-HidNopButtons 'START,A'`
- `-VulkanTraceDrawState true`
- `-VulkanTraceDrawShaderFilter '05775DE8A2B0B3F5,0ABADD9DA4373CBA,1B132051B5504DA9,9567C79307ACC6F5'`
- `-VulkanTraceTextureSourceChecksum true`
- `-VulkanTraceTextureSourceShaderFilter '05775DE8A2B0B3F5,0ABADD9DA4373CBA,1B132051B5504DA9,9567C79307ACC6F5'`
- `-VulkanTraceResolveChecksum true`
- `-VulkanPresentScoredResolveOnSwap true`
- `-VulkanPresentScoredResolveRejectClearLike true`
- `-LiveCaptureSeconds 95`

Counts:

- `GPU draw trace`: 420
- `GPU texture-source trace`: 160
- `GPU resolve trace: shared-memory checksum`: 220
- `GPU swap trace: frontbuffer checksum`: 12
- `using scored resolve`: 784

Result: no title screen. No fatal signal, AndroidRuntime crash, GPU hang, or
ARM64 execution fallback. The only `fallback` strings are Vulkan format fallback
messages.

### `scratch/thor-debug/20260519-004826-*`

APK SHA-256:
`03F2DD4495DD8E558473377871C3C5232D38BD10768CB639925DFDBB7A91A4D1`

Shader dumps:
`scratch/thor-debug/20260519-004826-shader-dumps`

Key flags:

- `-DumpShaders auto`
- `-VulkanTraceDrawShaderFilter '05775DE8A2B0B3F5,0ABADD9DA4373CBA,2A0674C564A8A8C5,57B736C8B5D4E953,B02CC5F55AD0D140,9567C79307ACC6F5'`
- `-VulkanTraceShaderConstants true`
- `-VulkanTraceShaderConstantsShaderFilter '05775DE8A2B0B3F5,0ABADD9DA4373CBA,2A0674C564A8A8C5,57B736C8B5D4E953,B02CC5F55AD0D140,9567C79307ACC6F5'`
- `-VulkanTraceTextureSourceChecksum true`
- `-VulkanTraceTextureSourceShaderFilter '05775DE8A2B0B3F5,0ABADD9DA4373CBA,2A0674C564A8A8C5,57B736C8B5D4E953,B02CC5F55AD0D140,9567C79307ACC6F5'`
- `-LiveCaptureSeconds 95`

Counts:

- `GPU draw trace`: 540
- `GPU texture-source trace`: 240
- `GPU shader-constant trace`: 402
- `GPU resolve trace: shared-memory checksum`: 240
- `GPU swap trace: frontbuffer checksum`: 12
- `using scored resolve`: 782
- `05775DE8A2B0B3F5`: 85 log hits
- `0ABADD9DA4373CBA`: 190 log hits
- `2A0674C564A8A8C5`: 56 log hits
- `57B736C8B5D4E953`: 73 log hits
- `B02CC5F55AD0D140`: 124 log hits
- `9567C79307ACC6F5`: 198 log hits

Result: no title screen. No fatal signal, AndroidRuntime crash, GPU hang, or
ARM64 execution fallback.

## Findings

The `05775DE8A2B0B3F5` 1280x720 pass samples several already-flat or clear-like
sources and writes nonzero `1DC14000` data:

- Before the draw, fetch 1 from `1DC14000` has first sample `00000040`.
- The draw uses `used_textures=0000007F`, viewport `1280x720`, color writes on,
  and render-target format `3`.
- The following resolve writes `1DC14000+00398000` with first sample
  `000000C0`, nonzero samples, and low variation.

The next `0ABADD9DA4373CBA` 1280x720 pass then samples that nonzero source and
resolves `1DC14000` back to all zero:

- Fetch 0 from `1DC14000` sees first sample `000000C0`.
- The draw uses `used_textures=000001FF`, viewport `1280x720`, color writes on,
  and render-target format `3`.
- The following resolve writes `1DC14000+00398000` with `nonzero=0`,
  `varying=0`, and `first_nonzero=-1`.

Later post passes such as `57B736C8B5D4E953` and `B02CC5F55AD0D140` sample
`1DC14000` after it is already zero. They may still be wrong, but they are not
the first observed zeroing point in this capture.

The final `9567C79307ACC6F5` fullscreen blit continues to receive zero input
from `1DC14000` / `1D14C000`, matching the earlier targeted vertex-fetch proof.

The `0ABADD9DA4373CBA` ucode dump shows a predicate ladder keyed by `c26.x`.
In this run `c26.x` is `2.0`, so the non-clear packed DXT fetch 7 is still
available in descriptors but is gated out by the observed branch path. This
keeps the output dependent mainly on flat render-chain sources and constants.

Blue Dragon also opens `D:\pack\!necessity.ipk` successfully, then later probes
loose font paths under `D:\!necessity\font\...` and misses them. It also touches
`cache:\pack` around `item_rec.ipk` / `item_rec.tmp`. These may be normal game
fallback checks, but they are now worth checking because the GPU path appears
to be rendering a real chain that contains only clear-like assets.

## Decision

The current black-frame wall is upstream of final presentation and upstream of
the final blit. The first concrete GPU suspect is now the 1280x720 composite
step around `0ABADD9DA4373CBA`, but the file/cache path must be checked in
parallel because missing archive or loose asset behavior could also explain
flat early title resources.

## Next Actions

- Audit Blue Dragon VFS/cache behavior for `D:\pack\!necessity.ipk`,
  `D:\!necessity\font\...`, and `cache:\pack`.
- Add a tiny targeted probe for the `0ABADD9DA4373CBA` output path only if the
  VFS/cache check does not explain the flat content.
- Keep shader-filter captures as the default GPU loop for this phase; broad
  trace budgets waste too much time on unrelated setup draws.
