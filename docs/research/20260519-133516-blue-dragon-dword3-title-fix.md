# Blue Dragon Dword3 Texture Exponent Title Fix

Date: 2026-05-19 13:35 EDT

## Question

Why did Blue Dragon reach the title prompt only when
`vulkan_debug_texture_fetch_disable_exp_adjust=true`, and can we replace that
coarse bypass with a real targeted fix?

## Finding

The SPIR-V texture fetch translator was applying the result exponent adjustment
from fetch constant dword 4. That is the wrong word for this field.

For Xenos texture fetch constants:

- dword 3 carries the result exponent adjustment in bits 13:18.
- dword 4 carries LOD/gradient exponent bias in the same bit range.

Local source reference: `src/xenia/gpu/xenos.h` defines `exp_adjust` on the
texture fetch dword 3 layout, while dword 4 contains `lod_bias` and
`grad_exp_adjust_*`.

The earlier bypass worked because Blue Dragon's B02 title-chain fetch constant
has dword 4 bits 13:18 set to signed `-16`, while dword 3's result exponent
adjust is `0`.

Observed B02 fetch dwords:

```text
8A004802 1DC140B6 0059E4FF 00A80C14 003E0003 00000200
```

Decoded:

- dword 3: `00A80C14`, result exponent bits decode to `0`.
- dword 4: `003E0003`, the same bit range decodes to `-16`, but that belongs
  to LOD/gradient bias, not result exponent adjustment.

## Change

`src/xenia/gpu/spirv_shader_translator_fetch.cc` now loads fetch constant dword
3 for the `GLSLstd450Ldexp` result exponent adjustment. Word 4 remains available
for the LOD and stacked-texture paths.

The debug cvar `vulkan_debug_texture_fetch_disable_exp_adjust` remains
default-off as an emergency research switch, but the validated title run did not
use it.

## Validation

Build/deploy:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
```

Launch proof:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 75 -HideAndroidOsd true -EmitInlineMmioChecks true -GpuTraceSwap true -GpuTracePacketBudget 260 -GpuInterruptOnRingIdle false -GpuBlueDragonKickWaitToken false -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 90 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 160 -VulkanTraceDrawShaderFilter "0ABADD9DA4373CBA,57B736C8B5D4E953,B02CC5F55AD0D140,9567C79307ACC6F5" -VulkanTraceTextureSourceChecksum true -VulkanTraceTextureSourceChecksumBudget 140 -VulkanTraceTextureSourceShaderFilter "0ABADD9DA4373CBA,57B736C8B5D4E953,B02CC5F55AD0D140,9567C79307ACC6F5" -VulkanTraceSwapSharedMemoryChecksum true -VulkanTraceSwapSharedMemoryChecksumBudget 40 -VulkanForceSigned2101010UnormFallback true -VulkanForce2101010Rgba8Fallback false
```

Proof capture:

- `scratch/thor-debug/20260519-132943-screenshot.png`
- `scratch/thor-debug/20260519-132943-meta.txt`
- `scratch/thor-debug/20260519-132943-live-logcat-filtered.txt`
- APK SHA-256:
  `09F480292F913D6132F3A288C7FCAB758E02FF6BAD519B1DF089BB3EF40B1224`

Important log evidence:

- `disable_fetch_exp_adjust=false`
- `force_signed_2101010_unorm=true`
- `process_alive_after_capture=true`
- No `AndroidRuntime`, `FATAL EXCEPTION`, native fatal signal,
  `VK_ERROR_DEVICE_LOST`, or GPU-hung lines in the filtered title capture.

Visual result:

- Blue Dragon Disc 1 reaches the visible `press START` title prompt on AYN Thor
  with Android OSD hidden.

## Post-Push Reproduction

After committing and pushing `d9576ec4c`, the same APK SHA reproduced the title
screen from current `master`.

Build/deploy:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
```

Capture:

- `scratch/thor-debug/20260519-141908-screenshot.png`
- `scratch/thor-debug/20260519-141908-meta.txt`
- `scratch/thor-debug/20260519-141908-live-logcat-filtered.txt`

Metadata:

- `head=d9576ec4c`
- `apk_sha256=09F480292F913D6132F3A288C7FCAB758E02FF6BAD519B1DF089BB3EF40B1224`
- `disable_fetch_exp_adjust=false`
- `force_signed_2101010_unorm=true`
- Android process `5368` stayed alive after the 75 second capture.
- Filtered log search found no `AndroidRuntime`, `FATAL EXCEPTION`, native fatal
  signal, `VK_ERROR_DEVICE_LOST`, or GPU-hung lines.

## Status

This is a real title-screen progress fix, not a compatibility or playability
claim. Next work should press START / drive the menu path, wire usable Android
input, and keep reducing research-only Vulkan fallback knobs.
