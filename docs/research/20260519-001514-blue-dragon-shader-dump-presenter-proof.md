# Blue Dragon Shader Dump And Presenter Proof

Timestamp: 2026-05-19 00:15:14 EDT

Repo head for tested build: `6a48d6428` plus local shader-dump automation
changes.

## Scope

Add a faster shader-debug loop for Thor runs:

- route `dump_shaders` through Android `EmulatorActivity` extras,
- make Vulkan dump translated SPIR-V binaries alongside the existing Xenos
  ucode binaries/disassembly,
- add `-DumpShaders auto` to `tools/thor/thor_xenia_debug.ps1`,
- create a timestamped device shader dump directory,
- pull shader dumps into the matching `scratch/thor-debug/<stamp>-shader-dumps`
  folder after live capture.

## Validation Commands

```powershell
git diff --check
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -Arm64MiniJit false -MmapAddressHigh 8 -DumpShaders auto -GpuTraceSwap true -GpuTracePacketBudget 120 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 260 -VulkanTraceShaderConstants true -VulkanTraceShaderConstantsBudget 48 -VulkanTraceTextureSourceChecksum true -VulkanTraceTextureSourceChecksumBudget 48 -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 24 -VulkanTraceResolveChecksum true -VulkanTraceResolveChecksumBudget 16 -VulkanPresentScoredResolveOnSwap true -VulkanPresentScoredResolveRejectClearLike true -VulkanPresentScoredResolveBudget 100 -LiveCaptureSeconds 60
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -Arm64MiniJit false -MmapAddressHigh 8 -GpuTraceSwap true -GpuTracePacketBudget 40 -VulkanDebugSolidGuestOutput true -LiveCaptureSeconds 25
```

## Captures

- Shader dump capture: `scratch/thor-debug/20260519-001033-*`
- Shader dumps: `scratch/thor-debug/20260519-001033-shader-dumps`
- Solid presenter capture: `scratch/thor-debug/20260519-001412-*`

APK SHA-256 for tested APK:

- `9B11EE7998FECBCE637B7866A0E73CB9C70B06E2E00BD185C4313A0A6747CCB2`

## Shader Dump Results

The shader dump pull succeeded and produced 41 files:

- 14 Xenos ucode binary files,
- 14 Xenos ucode text disassembly files,
- 13 Vulkan translated SPIR-V binary files.

Key visible pixel shader dumps:

- `05775DE8A2B0B3F5`: 7 texture fetches, exports `r1`.
- `0ABADD9DA4373CBA`: branch ladder over up to 9 texture fetches.
- `2A0674C564A8A8C5`: simple single-texture color transform/downscale.
- `57B736C8B5D4E953`: four taps of a single texture.
- `1B132051B5504DA9`: 13-tap style single-texture filter.
- `B02CC5F55AD0D140`: conditional texture/color correction path.

No local `spirv-dis` executable was found, so the SPIR-V side is currently
captured as binary evidence only. The Xenos ucode text is available immediately.

## Key Evidence

The `0ABADD9DA4373CBA` ucode can sample non-clear texture fetch 7, but the
captured constants make that path inactive for the observed draw:

- `c26.x` / guest constant `c282.x` is `2.0`.
- The shader's threshold ladder exits before fetch 7 contributes.
- Fetch 7 has non-clear packed DXT data, but it is not currently part of the
  exported color for the observed draw.

The final visible chain is therefore mostly consuming prior flat render-chain
sources, not losing a known-good texture in the last composite pass.

The solid presenter capture proves the Android/Vulkan presenter is not the
current black-frame blocker:

- `GPU swap trace: Vulkan guest output callback submitted solid debug color`:
  `123`
- `GPU swap trace: Vulkan IssueSwap begin`: `123`
- no `AndroidRuntime`, fatal signal, GPU hung, or skipped swap lines.
- Screenshot `scratch/thor-debug/20260519-001412-screenshot.png` shows the
  full magenta guest-output frame behind the Android OSD.

## Interpretation

The current Blue Dragon wall is upstream of Android presentation. The Vulkan
guest-output path can present, shaders and constants are available, and shader
dumps are now reproducible. The bad content is produced earlier in the render
chain, likely in EDRAM/render-target generation, resolve behavior, or an earlier
guest GPU/CPU correctness issue before the final fullscreen composite.

## Next Probe

Trace earlier color-producing passes before the first fullscreen composite:

- capture source/destination checksums around the first non-clear 80x45 resolves,
- inspect color/depth/stencil state for passes feeding `1F2E6000` and
  `1F2E0000`,
- add an optional lower threshold for scored-present candidate logging without
  changing the present source,
- if SPIR-V text is needed, build or install a local `spirv-dis` helper and
  disassemble the pulled `.vulkan.bin.*` files.
