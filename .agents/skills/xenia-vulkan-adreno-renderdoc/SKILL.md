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
For this fork, AYN Thor Adreno proof matters more than Windows renderer proof,
but CPU/A64 speed evidence still outranks GPU deep dives when the profile says
the guest CPU is the wall.

## Mature Vulkan Port Bias

Use `docs/research/20260524-165737-mature-vulkan-port-speed-design.md` as the
GPU design ladder, and `docs/research/20260524-171203-vulkan-counter-surface-audit.md`
as the repo-local counter surface map. Other mature emulator Vulkan ports
converge on the same shape:

- device and driver profile before optional Vulkan behavior;
- console GPU milestone classification before frame tools;
- deterministic trace/replay before broad renderer edits;
- shader/pipeline cache timing before async compilation or prewarming;
- EDRAM, resolve, texture-cache, and writeback correctness before speed claims;
- explicit queue/fence/present wait attribution before "push more GPU" work;
- validation, RenderDoc, AGI, and quiet FPS captures kept as separate lanes.

For Blue Dragon, do not start broad GPU work while quiet captures still show the
main CPU/JIT thread as the wall. A GPU slice needs fresh evidence such as high
GPU wait, resolve/copy body cost, shader/pipeline stalls, present pacing stalls,
or a real black-frame milestone regression.

Before a GPU speed patch, run the no-behavior audit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_vulkan_counter_surface_audit.ps1
```

The current default-off counter skeleton is documented in
`docs/research/20260524-173639-vulkan-perf-counter-skeleton.md`. Enable it
through `tools\thor\thor_xenia_debug.ps1` with
`-VulkanTracePerfCounters true -VulkanTracePerfCountersLogInterval <N>`. It
already logs IssueSwap, graphics pipeline create/cache-hit, queue submit,
present acquire/submit/present, barriers, and render-pass starts. Prefer a
route-stabilized control/counter sandwich over a renderer rewrite until Thor
route metadata proves whether the GPU wall is pipeline creation, queue/present,
barriers, readbacks/uploads/copies, descriptor churn, resolves, or completion
waits.

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

Run `.agents/skills/xenia-thor-experiment-gate/SKILL.md` first for risky GPU
toggles so the probe has one primary variable and a rollback story.

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
- Treat Adreno as a tile-based mobile target: minimize unnecessary
  external-memory round-trips, use precise load/store intent where the current
  render-target path permits it, and avoid optional format or extension
  assumptions without Thor feature logs.
- Do not report fork-specific Vulkan findings upstream unless reproduced on an
  official Xenia build.

## Output

End with:

- first missing GPU milestone,
- PM4/writeback/presenter evidence,
- exact capture path,
- next GPU experiment and cvars,
- whether RenderDoc or AGI is justified now.
