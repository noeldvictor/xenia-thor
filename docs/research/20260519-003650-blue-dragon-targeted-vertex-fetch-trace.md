# Blue Dragon Targeted Vertex Fetch Trace

Date: 2026-05-19 00:36 EDT
Repo: `xenia-thor`
Branch: `master`
Device: AYN Thor Max, serial `c3ca0370`
Target: `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Purpose

Blue Dragon still presents a flat dark/clear-like frame on the Thor even though
Android/Vulkan presentation is proven alive. The previous shader dump showed a
final fullscreen path using vertex shader `5CF1EE2E3C82546B` and pixel shader
`9567C79307ACC6F5`, with the pixel shader sampling a 1280x720 render-chain
texture and multiplying by vertex color. This run tests whether the final quad
vertex fetches are bad, especially whether the color attribute is zero.

## Changes

- Added `vulkan_trace_vertex_fetch_checksum`.
- Added `vulkan_trace_vertex_fetch_checksum_budget`.
- Added `vulkan_trace_vertex_fetch_shader_filter` so focused probes do not burn
  the trace budget on early setup draws.
- Routed the knobs through Android launch extras and
  `tools/thor/thor_xenia_debug.ps1`.

## Commands

Build and deploy:

```powershell
git diff --check
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
```

Initial broad vertex trace:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -Arm64MiniJit false -MmapAddressHigh 8 -MountCache true -TimeScalar 1.0 -HidNopConnected true -HidNopButtons 'START,A' -HidNopButtonsDelayMs 45000 -HidNopButtonsHoldMs 5000 -GpuTraceSwap true -GpuTracePacketBudget 120 -GpuTraceSwapFrontbufferChecksum true -GpuTraceSwapFrontbufferChecksumBudget 12 -GpuTraceSwapRenderTargets true -GpuTraceSwapRenderTargetsBudget 18 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 140 -VulkanTraceVertexFetchChecksum true -VulkanTraceVertexFetchChecksumBudget 90 -VulkanTraceTextureSourceChecksum true -VulkanTraceTextureSourceChecksumBudget 20 -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 80 -VulkanTraceResolveChecksum true -VulkanTraceResolveChecksumBudget 42 -VulkanPresentScoredResolveOnSwap true -VulkanPresentScoredResolveRejectClearLike true -VulkanPresentScoredResolveMinWidth 80 -VulkanPresentScoredResolveMinHeight 45 -VulkanPresentScoredResolveBudget 160 -LiveCaptureSeconds 75
```

Focused final-blit vertex trace:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -Arm64MiniJit false -MmapAddressHigh 8 -MountCache true -TimeScalar 1.0 -HidNopConnected true -HidNopButtons 'START,A' -HidNopButtonsDelayMs 50000 -HidNopButtonsHoldMs 5000 -GpuTraceSwap true -GpuTracePacketBudget 120 -GpuTraceSwapFrontbufferChecksum true -GpuTraceSwapFrontbufferChecksumBudget 12 -GpuTraceSwapRenderTargets true -GpuTraceSwapRenderTargetsBudget 18 -VulkanTraceVertexFetchChecksum true -VulkanTraceVertexFetchShaderFilter 5CF1EE2E3C82546B -VulkanTraceVertexFetchChecksumBudget 24 -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 70 -VulkanTraceResolveChecksum true -VulkanTraceResolveChecksumBudget 36 -VulkanPresentScoredResolveOnSwap true -VulkanPresentScoredResolveRejectClearLike true -VulkanPresentScoredResolveMinWidth 80 -VulkanPresentScoredResolveMinHeight 45 -VulkanPresentScoredResolveBudget 220 -LiveCaptureSeconds 110
```

## Captures

- Broad trace: `scratch/thor-debug/20260519-002829-*`
  - APK SHA-256: `2F33E8F5F9F7848FDE68C14B144A96D54612F9B1B749C56C36344CCCDFC40B01`
  - `GPU vertex-source trace=270`
  - `shader=5CF1EE2E3C82546B=0`
  - `VdSwap=1816`, `XE_SWAP=1206`
  - No `AndroidRuntime`, `Fatal signal`, `GPU is hung`, or `ARM64 fallback`.
- Focused trace: `scratch/thor-debug/20260519-003312-*`
  - APK SHA-256: `1D66CE51565B842A3C2A203E066AFF1C5F14BE7CFBA1ABBB8423907F7A5E38BE`
  - `GPU vertex-source trace=96`
  - `shader=5CF1EE2E3C82546B=96`
  - `9567C79307ACC6F5=4`
  - `GPU resolve trace: shared-memory checksum=220`
  - `frontbuffer checksum=12`
  - `using scored resolve=963`
  - `VdSwap=2896`, `XE_SWAP=1926`
  - No `AndroidRuntime`, `Fatal signal`, `GPU is hung`, or `ARM64 fallback`.

## Evidence

The focused trace reached the final `5CF1EE2E3C82546B` / `9567C79307ACC6F5`
path. The first observed pass loaded the 1280x720 source at `0x1DC14000` and
then drew the fullscreen quad:

- Vertex buffer: `address=1F68D000`, `length=00000050`, `endian=2`,
  `stride_words=5`, `host_vertices=4`, `attr_count=3`.
- Position attribute had sensible fullscreen values after big-endian swap:
  `00000000,00000000,44A00000,44A00000`.
- Color attribute was not black:
  `FFFFFFFF,FFFFFFFF,FFFFFFFF,FFFFFFFF`.
- UV attribute was sane:
  `00000000,00000000,3F800000,3F800000`.

The following resolve to `1D14C000+00398000` was already empty:

- `nonzero=0`
- `varying=0`
- `score=0`
- `clear_like=true`
- `first_nonzero=-1`

The later copy from `1D14C000` to the official frontbuffer `1CA1C000` also
resolved all zero. Later filtered traces with the same vertex shader showed
color values like `FFFFFF00` / big-endian `00FFFFFF`, also not black.

## Conclusion

The final blit vertex stream is not the immediate black-frame cause. The quad
has sane position and UV data, and the color multiplier is not zero. The source
render-chain texture feeding the final pixel shader is already flat or zero
before the final composite.

The next useful lane is upstream render-target/source population, not Android
presentation or final fullscreen quad setup. Add targeted shader filters for
texture-source and draw-state traces, then focus on the shaders and resolves
that populate `1DC14000`, `1D14C000`, `1CA1C000`, and nearby scored-present
candidates before the final `5CF1/9567` pass.
