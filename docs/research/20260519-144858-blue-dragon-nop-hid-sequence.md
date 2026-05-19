# Blue Dragon Nop HID Sequence

Date: 2026-05-19 14:48 EDT

## Question

Can the Thor build drive Blue Dragon past the `press START` title prompt without
manual Android/controller input?

## Change

Added a research-only nop HID sequence cvar:

```text
hid_nop_button_sequence=start@45000:3000;a@68000:3000
```

Format:

- Entries are separated by `;` or `|`.
- Each entry is `buttons@delay_ms:hold_ms`.
- Multiple buttons inside an entry may use `+` or `,`.
- Example: `start@45000:3000;a@68000:3000;a@86000:3000`.

The older single-pulse cvars still work:

- `hid_nop_buttons`
- `hid_nop_buttons_delay_ms`
- `hid_nop_buttons_hold_ms`

Android launch extras and `tools/thor/thor_xenia_debug.ps1` now forward
`hid_nop_button_sequence` into the native cvar bundle.

Also fixed the XAM input path to pass the normalized `actual_user_index` into
`GetState`, `SetState`, and `GetKeystroke`. Those functions already calculated
the normalized user index, but some calls still forwarded the raw argument.

## Validation

Build/deploy:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
```

First proof that normal nop HID START/A works:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 120 -HideAndroidOsd true -HidNopConnected true -HidNopButtons "start,a" -HidNopButtonsDelayMs 45000 -HidNopButtonsHoldMs 7000 -EmitInlineMmioChecks true -GpuTraceSwap true -GpuTracePacketBudget 260 -GpuInterruptOnRingIdle false -GpuBlueDragonKickWaitToken false -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 90 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 160 -VulkanTraceDrawShaderFilter "0ABADD9DA4373CBA,57B736C8B5D4E953,B02CC5F55AD0D140,9567C79307ACC6F5" -VulkanTraceTextureSourceChecksum true -VulkanTraceTextureSourceChecksumBudget 140 -VulkanTraceTextureSourceShaderFilter "0ABADD9DA4373CBA,57B736C8B5D4E953,B02CC5F55AD0D140,9567C79307ACC6F5" -VulkanTraceSwapSharedMemoryChecksum true -VulkanTraceSwapSharedMemoryChecksumBudget 40 -VulkanForceSigned2101010UnormFallback true -VulkanForce2101010Rgba8Fallback false
```

Result:

- `scratch/thor-debug/20260519-144021-screenshot.png`
- Reached Blue Dragon's `Voice Language` screen with English selected.

Sequenced proof:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 150 -HideAndroidOsd true -HidNopConnected true -HidNopButtonSequence "start@45000:3000;a@68000:3000;a@86000:3000;a@106000:3000" -EmitInlineMmioChecks true -GpuTraceSwap true -GpuTracePacketBudget 260 -GpuInterruptOnRingIdle false -GpuBlueDragonKickWaitToken false -VulkanTraceCopyState true -VulkanTraceCopyStateBudget 90 -VulkanTraceDrawState true -VulkanTraceDrawStateBudget 160 -VulkanTraceDrawShaderFilter "0ABADD9DA4373CBA,57B736C8B5D4E953,B02CC5F55AD0D140,9567C79307ACC6F5" -VulkanTraceTextureSourceChecksum true -VulkanTraceTextureSourceChecksumBudget 140 -VulkanTraceTextureSourceShaderFilter "0ABADD9DA4373CBA,57B736C8B5D4E953,B02CC5F55AD0D140,9567C79307ACC6F5" -VulkanTraceSwapSharedMemoryChecksum true -VulkanTraceSwapSharedMemoryChecksumBudget 40 -VulkanForceSigned2101010UnormFallback true -VulkanForce2101010Rgba8Fallback false
```

Result:

- `scratch/thor-debug/20260519-144529-screenshot.png`
- Reached the opening scene after language selection.
- APK SHA-256:
  `8F3CF8C24DDD76CA1BFD11116A3B186BCF5D74145129A17A0DA2049B82D91556`
- `disable_fetch_exp_adjust=false`
- `force_signed_2101010_unorm=true`
- Android process `18239` stayed alive after the 150 second capture.
- Filtered log search found no `AndroidRuntime`, `FATAL EXCEPTION`, native fatal
  signal, `VK_ERROR_DEVICE_LOST`, or GPU-hung lines.

## Status

Blue Dragon now progresses past the title prompt and language selection into
the opening scene on Thor using scripted nop HID pulses. This is still not a
playability or compatibility claim. The next lane is real Android controller
mapping, longer scripted input, and checking whether the opening sequence
continues into a controllable menu/gameplay state.
