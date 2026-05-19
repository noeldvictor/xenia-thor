# Blue Dragon EDRAM Dump and FSI Probe

Date: 2026-05-18 23:19 EDT

Fork status: unofficial xenia-thor research experiment only. This is not an
upstream Xenia compatibility claim.

## Question

The previous draw-state capture proved real Vulkan color-writing draws happen,
but all scored present candidates were flat or clear-like. This probe asked
whether pixels are lost during final shared-memory resolve, or whether the
intermediate EDRAM dump is already flat.

## Changes

- Added `vulkan_trace_edram_checksum` and
  `vulkan_trace_edram_checksum_budget`.
- Added a bounded debug readback of the Vulkan EDRAM dump span immediately
  after host render targets are dumped and before the shared-memory resolve
  copy runs.
- Reopened the Vulkan submission after the debug wait so the normal resolve
  continues.
- Added `render_target_path_vulkan` Android/Thor launch plumbing.
- Added startup logging for requested and selected Vulkan render-target path,
  including FSI fallback reasons.

## Validation

Commands:

```powershell
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\thor\thor_build.ps1 -Mode FullDeploy
```

Builds passed after a visibility fix that scoped `VulkanRenderTargetCache`
as a friend of `VulkanCommandProcessor` for this mid-frame debug readback.

Captures:

- FBO EDRAM/readback split:
  - `scratch\thor-debug\20260518-231029-live-logcat.txt`
  - `scratch\thor-debug\20260518-231029-live-logcat-filtered.txt`
  - `scratch\thor-debug\20260518-231029-meta.txt`
  - `scratch\thor-debug\20260518-231029-screenshot.png`
- FSI request route / fallback:
  - `scratch\thor-debug\20260518-231807-live-logcat.txt`
  - `scratch\thor-debug\20260518-231807-live-logcat-filtered.txt`
  - `scratch\thor-debug\20260518-231807-meta.txt`
  - `scratch\thor-debug\20260518-231807-screenshot.png`

APK SHA-256 values:

- `20260518-231029`: `8EE5737024E667B157F7AD5D1DEDFF34CC1964E6A258B9C792A944FE675220D2`
- `20260518-231807`: `99A5D37D6B6A50E8347FD45F0F238BE154B3C33206A7CC777192D99BB2ABEDD9`

## Findings

- The EDRAM checksum probe was active and stable:
  - `24` EDRAM-after-dump checksum lines.
  - `0` EDRAM readback wait failures.
  - `0` EDRAM readback map failures.
- The run stayed alive:
  - `1762` `VdSwap` lines.
  - `880` `XE_SWAP` lines.
  - `0` fatal signal, AndroidRuntime crash, GPU-hung line, or ARM64 fallback.
- For main color resolves, EDRAM-after-dump was already flat or near-uniform:
  - 640x360 color0 source at `base_tiles=0` often sampled as all zero or
    almost entirely `C0000000`.
  - 1280x720 color0 source at `base_tiles=0` sampled as all `C0000000` or all
    zero depending on the moment.
  - Depth-like white candidates sampled as repeated `FFFFFF00` in EDRAM.
- Shared-memory resolve output matched the same flat pattern after byte/format
  conversion, for example `C0000000` in EDRAM becoming `000000C0` in shared
  memory.
- Requesting `render_target_path_vulkan=fsi` on Thor is not currently useful:
  - The launch routed the cvar correctly.
  - Thor logged `render_target_path_vulkan='fsi' selected=fbo`.
  - FSI fell back because both `fragmentShaderSampleInterlock` and
    `fragmentShaderPixelInterlock` are false on the device.

## Interpretation

The final shared-memory resolve copy is not the first place pixels disappear.
For the checked Blue Dragon candidates, the EDRAM dump range is already flat.
That leaves two likely classes:

- The host render-target image is already flat because draw output, constants,
  textures, depth/scissor, or guest state are wrong.
- The host render-target image has useful pixels, but the host-RT-to-EDRAM dump
  shader / format path reads or converts it incorrectly.

FSI cannot be used as a quick bypass on AYN Thor because the Adreno Vulkan
driver does not expose the required fragment shader interlock features.

## Next Actions

- Add a host render-target image sample/readback or a RenderDoc/AGI capture to
  prove whether the RT image itself is flat before the EDRAM dump.
- Log enough RT key / Vulkan format / source descriptor detail around the dump
  path to correlate format `3`, format `12`, and format `0` candidates.
- Keep `vulkan_trace_edram_checksum` budgeted tightly; it waits on the GPU and
  is intentionally slow.
