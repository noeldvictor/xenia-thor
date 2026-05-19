# Blue Dragon Raster and Dump-State Probe - 2026-05-18 23:31 EDT

This is experimental xenia-thor research only. It is not an upstream Xenia
compatibility claim.

## Question

The previous EDRAM probe showed the final shared-memory resolve was preserving
flat content. This run asked whether visible Blue Dragon draws are being
discarded by scissor, depth, clipping, culling, or whether the host render
target dump rectangles are routing the wrong render target into EDRAM.

## Change

- Extended `vulkan_trace_draw_state` with derived scissor, raw window/screen
  scissor, clip/VTE/SU state, culling, depth, stencil, and alpha-test fields.
- Added budgeted host-render-target dump routing logs under
  `vulkan_trace_copy_state`, including RT key, base, pitch, width, MSAA, format,
  rectangle rows, dump pipeline key, and compute dispatch group counts.

## Validation

- Branch: `master`.
- Capture build state: dirty after `319c76367` with the probe edits applied.
- APK SHA-256:
  `2944C6B6C26F133660B94F515AD5F757EE194AADF6B8431B8F3AA1A8EC3889DB`.
- Build:
  `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore`
- Deploy:
  `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy`
- Capture:
  `scratch/thor-debug/20260518-232822-*`
- Launch:
  `LaunchBlueDragonLiveCapture` with `-MmapAddressHigh 8`,
  `-GpuTraceSwap true`, `-VulkanTraceDrawState true`,
  `-VulkanTraceCopyState true`, `-VulkanTraceResolveChecksum true`,
  `-VulkanTraceEdramChecksum true`, and scored resolve clear-like rejection.

## Counts

- `GPU draw trace: prim=`: 70
- `GPU draw trace: raster`: 70
- `pixel=true`: 20
- `pixel=false`: 50
- `normalized_color_mask=000F`: 20
- `GPU resolve trace: dump begin`: 40
- `GPU resolve trace: dump rt`: 40
- `GPU resolve trace: dump dispatch`: 40
- `GPU resolve trace: edram checksum`: 20
- `GPU resolve trace: shared-memory checksum`: present throughout the capture.
- `Vulkan IssueSwap`: 4797
- `GPU swap trace: VdSwap`: 3202
- `XE_SWAP`: 3198
- `AndroidRuntime`, `Fatal signal`, `F DEBUG`, `GPU is hung`,
  `ARM64 fallback`: 0

## Findings

- The important textured fullscreen quads use sane viewport/scissor pairs:
  `640x360`, `320x180`, `160x90`, `80x45`, and `1280x720`.
- These draws are not depth-killed. Their raster lines show `z_enable=false`
  on the texture/composite quads, with matching full-surface scissor rectangles.
- Back-face culling is enabled on the textured quads (`cull_back=true`), but
  front culling is not, and the draws still reach the pixel path with
  `pixel=true`, `ps_writes=1`, and color mask `000F`.
- Host render-target dump routing matches the expected render targets:
  - `00C04000`: 640-wide 1xMSAA color format 3.
  - `00C02000`: 320-wide 1xMSAA color format 3.
  - `00C01000`: 160-wide 1xMSAA color format 3.
  - `00C00800`: 80-wide 1xMSAA color format 3.
  - `00C08000`: 1280-wide 1xMSAA color format 3.
- Dumped EDRAM and shared-memory samples are still flat or near-flat:
  `C0000000` / `000000C0`, zero, or white-like depth/color conversions.
- The scored presenter rejected all useful-size candidates as clear-like; the
  screenshot remained black behind the Android/Xenia overlay.

## Interpretation

This rules out a simple Android presenter failure, final shared-memory copy
failure, bogus scissor, obvious depth discard, and wrong dump rectangle routing.
The next wall is earlier: the host render target contents are being produced as
flat data, or the shaders/textures feeding those fullscreen/composite draws are
flat/incorrect before the render-target dump.

## Next Probe

Trace the texture inputs and shader bindings for the visible textured draws,
especially the `used_textures=0000007F` 1280x720 pass and the
`used_textures=00000001` downscale/composite passes. The next question is
whether source textures are missing/zero/constant, sampled with bad format or
addressing, or whether the pixel shader translation is returning a constant.
