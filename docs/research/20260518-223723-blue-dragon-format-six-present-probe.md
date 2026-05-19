# Blue Dragon Format-6 Present Probe

Timestamp: 2026-05-18 22:37 EDT

## Purpose

The scored resolve selector proved that Android/Vulkan presentation can show
guest resolve memory, but the automatic path was still choosing flat dark
surfaces. This probe added an optional required Xenos texture format so the
selector can compare candidate families without manually forcing stale
addresses.

## Code Slice

- Added `vulkan_present_scored_resolve_required_format`, default `-1`.
- Routed the cvar through Android `EmulatorActivity` launch extras.
- Added `-VulkanPresentScoredResolveRequiredFormat` to
  `tools/thor/thor_xenia_debug.ps1`.
- Included the required format in the Vulkan swap cvar trace.

## Thor Run

Command shape:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 `
  -Mode LaunchBlueDragonLiveCapture `
  -DeviceSerial c3ca0370 `
  -LiveCaptureSeconds 120 `
  -TimeScalar 1.0 `
  -MountCache true `
  -MmapAddressHigh 8 `
  -GpuTraceSwap true `
  -GpuTraceSwapFrontbufferChecksum true `
  -GpuTraceSwapFrontbufferChecksumBudget 6 `
  -VulkanTraceCopyState true `
  -VulkanTraceCopyStateBudget 180 `
  -VulkanPresentScoredResolveOnSwap true `
  -VulkanPresentScoredResolveMinWidth 320 `
  -VulkanPresentScoredResolveMinHeight 180 `
  -VulkanPresentScoredResolveBudget 768 `
  -VulkanPresentScoredResolveRequiredFormat 6 `
  -HidNopConnected true `
  -HidNopButtons 'START,A' `
  -HidNopButtonsDelayMs 50000 `
  -HidNopButtonsHoldMs 5000
```

Evidence:

- Metadata: `scratch/thor-debug/20260518-223301-meta.txt`
- Logcat: `scratch/thor-debug/20260518-223301-live-logcat.txt`
- Filtered log: `scratch/thor-debug/20260518-223301-live-logcat-filtered.txt`
- Screenshot: `scratch/thor-debug/20260518-223301-screenshot.png`
- APK SHA-256:
  `FD0960036316B83DC13E0B2ECC07353FA73669004954A3FF7B0DEF3672ABE32F`

## Findings

- The new cvar is active in the log:
  `scored_present=true scored_min=320x180 scored_budget=768 scored_required_format=6`.
- The selector now follows dynamic format-6 resolves, not a manually forced
  stale address.
- The recurring selected candidate is:
  `source=1D88F000+00385000 size=720x720 pitch=1280 format=6 sequence=2 score=3026`.
- The candidate checksum probe reports many nonzero samples, but the first
  values are clear-like white:
  `first_nonzero_value=00FFFFFF first=00FFFFFF,...`.
- Official `VdSwap` frontbuffers are still zero at swap time:
  `1CA1C000` / `1CDB4000`, `1280x720`, `nonzero=0`.
- The screenshot is white behind Android OSD, matching the selected format-6
  candidate's sampled content.
- There are no `AndroidRuntime`, `FATAL`, `GPU is hung`, or ARM64 JIT fallback
  lines in the capture.

## Interpretation

This proves the format filter and dynamic scored presentation path work. It
also proves format-6 is not the title image. The visible-output wall is now
upstream of Android presentation: Blue Dragon is alive and swapping, resolves
are being written, but the presentable candidates we can see are clear-like
dark or white surfaces while the official frontbuffers remain zero.

The next useful probe should classify resolve content more sharply and attach
render-target/write-mask/clear state to each resolved surface. A RenderDoc or
AGI capture is now justified only if paired with a normal log that names the
candidate address, format, first sampled value, and swap sequence.
