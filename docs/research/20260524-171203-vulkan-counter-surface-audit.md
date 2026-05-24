# Vulkan Counter Surface Audit

Date: 2026-05-24

Goal: convert the mature Vulkan-port design lane into repo-local source
evidence, without changing renderer behavior. This answers where xenia-thor can
add PCSX2-style GPU counters before doing broad Vulkan rewrites.

## Inputs

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_vulkan_counter_surface_audit.ps1
```

Related design note:

- `docs/research/20260524-165737-mature-vulkan-port-speed-design.md`

Tool added:

- `tools/thor/thor_vulkan_counter_surface_audit.ps1`

## Result

The audit found all planned source surfaces, so the next GPU slice can be a
default-off counter skeleton instead of a renderer behavior patch.

Key output:

```text
safe_behavior_patch=none
recommended_next_patch=default_off_vulkan_counter_skeleton
counter_groups=pipeline,submission,present,barrier,copy_upload_readback,descriptor,render_pass,resolve,completion_wait,milestone
capture_metadata=cold_warm_pipeline_cache,pipeline_creates,pipeline_create_us,queue_submit_count,queue_submit_us,present_us,barriers,readbacks,uploads,copies,descriptor_updates,resolve_counts,fence_wait_us
do_not=broad_renderer_rewrite,renderdoc_fps_capture,swappy_before_present_timing
surfaces_missing=0
vulkan_speed_lane_status=diagnostics_ready_no_behavior_change
blue_dragon_lane_bias=stay_on_a64_while_main_thread_is_wall
next_gpu_slice=default_off_counters_then_thor_route_capture
```

## Counter Surface Map

- Pipeline creation: `src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc:352`,
  `src/xenia/ui/vulkan/vulkan_util.cc:190`, and render-target cache pipeline
  creation sites. Counters should separate graphics and compute pipeline
  creation, cold/warm cache behavior, and shader translation time.
- Queue submission: `src/xenia/gpu/vulkan/vulkan_command_processor.cc:4278`
  and `src/xenia/ui/vulkan/vulkan_gpu_completion_timeline.cc`. Counters should
  measure submit count, submit time, command buffers, and wait semaphores.
- Present/swapchain: `src/xenia/gpu/vulkan/vulkan_command_processor.cc:1817`
  and `src/xenia/ui/vulkan/vulkan_presenter.cc`. Timing should split
  `IssueSwap`, acquire, paint-submit, UI submit, and `vkQueuePresentKHR`.
- Completion waits: `src/xenia/gpu/vulkan/vulkan_command_processor.cc:1174`
  and `src/xenia/ui/vulkan/vulkan_gpu_completion_timeline.cc`. Counters should
  record fence wait count/time and await-all versus single-submission waits.
- Barriers: `src/xenia/gpu/vulkan/vulkan_command_processor.cc:1413` and
  deferred command buffer `vkCmdPipelineBarrier`. Count buffer barriers, image
  barriers, and forced render-pass breaks by subsystem/caller.
- Render passes: `src/xenia/gpu/vulkan/vulkan_command_processor.cc:2273`,
  render-target cache render-pass entry, deferred `vkCmdBeginRenderPass`, and
  presenter passes. Counters should separate game render-target passes from
  guest-output/presenter passes.
- Copies, uploads, and readbacks:
  `src/xenia/gpu/vulkan/deferred_command_buffer.cc:151`,
  `src/xenia/ui/vulkan/vulkan_upload_buffer_pool.cc`, and Vulkan memory-purpose
  sites. Count copy commands, upload bytes, readback bytes, and map time.
- Descriptor churn: `src/xenia/gpu/vulkan/vulkan_command_processor.cc:528`,
  render-target/texture cache descriptor updates, and the descriptor set pools.
  Count transient descriptor allocations, updates, and pool growth.
- Resolve/EDRAM: `src/xenia/gpu/vulkan/vulkan_command_processor.cc:3691`,
  `src/xenia/gpu/vulkan/vulkan_render_target_cache.cc`, and
  `src/xenia/gpu/draw_util.*`. Tag resolve copies, clears, readback fallback,
  copy shader, and resolved pixel volume.
- Milestones: existing GPU swap trace strings can support a compact ladder:
  `VdSwap` -> `PM4_XE_SWAP` -> `Vulkan IssueSwap` -> `RequestSwapTexture` ->
  `RefreshGuestOutputImpl` -> present.

## Decision

Do not start a broad Vulkan rewrite or RenderDoc/FPS-only GPU capture from the
current evidence. Blue Dragon captures still show the Main Thread near one full
core and GPU Commands low, so A64 remains the active speed wall.

The useful GPU step is a default-off Vulkan counter skeleton plus one Thor
route capture that records pipeline creates, queue submit/present timing,
barriers, readbacks/uploads/copies, descriptor churn, resolve counts, and
completion waits in capture metadata. If those counters show GPU wait, present
stall, resolve/readback, pipeline creation, or descriptor churn, then pick the
specific Vulkan optimization lane from that evidence.
