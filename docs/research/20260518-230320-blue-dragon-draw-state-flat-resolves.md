# Blue Dragon Draw-State and Flat Resolve Probe

Date: 2026-05-18 23:03 EDT

Fork status: unofficial xenia-thor research experiment only. This is not an
upstream Xenia compatibility claim.

## Question

After the format-6 scored-present probe, Blue Dragon could show guest resolve
memory on the Android surface, but the visible sources were white or dark
clear-like surfaces. The next question was whether the selector was choosing
bad addresses or whether the available render output was already flat before
presentation.

## Changes

- Added `vulkan_present_scored_resolve_reject_clear_like`.
- Extended shared-memory readback stats with first sample, first-sample match
  count, low-variation, and clear-like classification.
- Added `vulkan_trace_draw_state` and `vulkan_trace_draw_state_budget`.
- Traced skipped draw causes, raster/pixel state, shader hashes, color masks,
  render-target formats, render-target bases, viewport, surface pitch, and
  copy-source color/depth state.
- Routed the new flags through `EmulatorActivity` and
  `tools/thor/thor_xenia_debug.ps1`.

## Validation

Commands:

```powershell
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\thor\thor_build.ps1 -Mode FullDeploy
```

All passed. The APK installed on Thor serial `c3ca0370`.

Capture:

- `scratch\thor-debug\20260518-225610-live-logcat.txt`
- `scratch\thor-debug\20260518-225610-live-logcat-filtered.txt`
- `scratch\thor-debug\20260518-225610-meta.txt`
- `scratch\thor-debug\20260518-225610-screenshot.png`

APK SHA-256:

- `8ACE64ADED334A4390CA5794169CA87C9EE6D82845BDF3FC4171D53DD24745F2`

Launch shape:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 90 -TimeScalar 1.0 -MountCache true -MmapAddressHigh 8 -GpuTraceSwap true -GpuTraceSwapFrontbufferChecksum true -GpuTraceSwapFrontbufferChecksumBudget 4 -GpuTraceSwapRenderTargets true -GpuTraceSwapRenderTargetsBudget 12 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 180 -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 80 -VulkanPresentScoredResolveOnSwap true -VulkanPresentScoredResolveRejectClearLike true -VulkanPresentScoredResolveMinWidth 320 -VulkanPresentScoredResolveMinHeight 180 -VulkanPresentScoredResolveBudget 800 -HidNopConnected true -HidNopButtons 'START,A' -HidNopButtonsDelayMs 50000 -HidNopButtonsHoldMs 5000
```

## Findings

- Blue Dragon stayed alive through the run.
- No fatal signal, AndroidRuntime crash, GPU hung line, or ARM64 fallback line
  appeared in the valid draw-state capture.
- The run logged `2292` `VdSwap` lines and `1524` `XE_SWAP` lines.
- Draw-state tracing logged real color-writing draws:
  - `raster=true`
  - `pixel=true`
  - `ps_writes=1`
  - `normalized_color_mask=000F`
  - render target 0 active with format `3` and write mask `F`
- The scored selector sampled `800` resolve outputs.
- All sampled scored-present resolve outputs were classified as clear-like
  after the low-variation rule was enabled.
- No scored resolve candidate survived the clear-like rejection in the valid
  run.
- Official `VdSwap` frontbuffers remained zero.
- The Android screenshot remained black behind the Xenia/Android UI.

## Interpretation

The immediate wall is no longer Android presentation or blind present-source
selection. The app can present guest resolve memory, and the GPU command path
executes real pixel-shader color draws. However, the resolved shared-memory
outputs available to the presenter are still zero or near-uniform.

The next split must happen at the Vulkan render-target dump / resolve boundary:

- If the host render target or EDRAM buffer is already flat after color-writing
  draws, the bug is in draw state, shader output, RT binding, viewport/scissor,
  depth, or textures/constants.
- If the host render target / EDRAM buffer has nonflat pixels but shared memory
  resolves are flat, the bug is in dump-to-EDRAM, resolve-copy constants,
  format conversion, or shared-memory copy.

## Next Actions

- Add a targeted debug checksum around the Vulkan render-target dump path.
- Prefer one capture that checks host RT / EDRAM content and final shared
  memory content in the same frame.
- Keep RenderDoc/AGI as the parallel visual lane, but current `thor_renderdoc`
  status did not show an installed or configured RenderDoc layer package.
