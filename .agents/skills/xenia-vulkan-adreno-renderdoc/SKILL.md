---
name: xenia-vulkan-adreno-renderdoc
description: Debug xenia-thor GPU bring-up on AYN Thor Adreno 740, including Xenos PM4 packets, command processor ring pointers, GPU writebacks, VdSwap, PM4_XE_SWAP, Vulkan IssueSwap, texture cache, presenter output, Android Vulkan layers, RenderDoc, AGI, and black-screen-after-draws failures.
---

# Xenia Vulkan Adreno RenderDoc

Use this skill when logs show real GPU traffic, PM4 packets, draw calls,
writeback waits, or black output after Vulkan setup.

## First Rule

RenderDoc is a GPU frame tool, not proof that guest CPU execution is correct.
Only use it after normal logcat proves a meaningful guest GPU frame path.

## Normal Evidence Search

```powershell
rg -n "VdSwap|PM4_XE_SWAP|XE_SWAP|Vulkan IssueSwap|guest output callback|RequestSwapTexture|EVENT_WRITE_SHD|WAIT_REG_MEM|COND_WRITE|DRAW_INDX|CommandProcessor ExecutePrimaryBuffer|read pointer writeback|GPU is hung" scratch\thor-debug\*-logcat.txt
```

Classify:

- no `VdSwap`: guest/kernel/video path has not requested present.
- `VdSwap` but no `PM4_XE_SWAP`: ring buffer or command generation issue.
- `PM4_XE_SWAP` but no `Vulkan IssueSwap`: command processor dispatch issue.
- `Vulkan IssueSwap` but black screen: texture cache, render target, presenter,
  or Adreno/Vulkan path.
- watchdog with real PM4 draws: guest-visible completion/writeback semantics or
  shader/pipeline compile latency.

## Files To Inspect

- `docs/gpu.md`
- `src/xenia/gpu/command_processor.cc`
- `src/xenia/gpu/command_processor.h`
- `src/xenia/gpu/gpu_flags.*`
- `src/xenia/gpu/registers.h`
- `src/xenia/gpu/xenos.h`
- `src/xenia/gpu/vulkan/vulkan_command_processor.cc`
- `src/xenia/gpu/vulkan/vulkan_texture_cache.cc`
- `src/xenia/ui/vulkan/vulkan_presenter.cc`
- `src/xenia/kernel/xboxkrnl/xboxkrnl_video.cc`

## Thor GPU Run

Start with:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragon -DeviceSerial c3ca0370 -GpuTraceSwap true -GpuTracePacketBudget 260 -GpuInterruptOnRingIdle false -GpuBlueDragonKickWaitToken false
```

Only enable experimental knobs one at a time:

- `-GpuEarlyPrimaryReadPointerWriteback true`
- `-ClearMemoryPageState true`
- `-GpuInterruptOnRingIdle true`
- `-GpuBlueDragonKickWaitToken true`

Document why each knob is enabled and what invariant it tests.

## RenderDoc Lane

Use the repo script:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Status
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode EnablePerApp
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Disable
```

Pair every RenderDoc run with a normal non-RenderDoc capture. Disable the layer
immediately after the capture attempt.

## Adreno Caution

- Keep Vulkan validation and RenderDoc runs separate from normal performance
  captures.
- Treat shader/pipeline compile latency as a possible guest watchdog trigger.
- If the screen is black, prove whether the guest frontbuffer contains useful
  data before blaming Android presentation.
- Do not report fork-specific Vulkan findings upstream unless reproduced on an
  official Xenia build.

## Output

End with:

- first missing GPU milestone,
- PM4/writeback/presenter evidence,
- exact capture path,
- next GPU experiment and cvars,
- whether RenderDoc or AGI is justified now.
