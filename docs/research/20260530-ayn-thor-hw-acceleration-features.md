# AYN Thor hardware acceleration features for xenia-thor (device-confirmed)

Goal (user): Thor is ~10-20x an Xbox 360; exploit its CPU/GPU custom features to hit
100-200% emulation speed. This maps the ACTUAL silicon features (read from device,
not assumed) to concrete acceleration levers, ranked by impact on our measured
bottlenecks.

## The actual silicon (device-read, c3ca0370)
- SoC: Snapdragon 8 Gen 2 / QCS8550 (board "kalama"). GPU: Adreno 740 (Vulkan 1.1+,
  ray tracing). CPU: 1x Cortex-X3 @3.19GHz + 4x A715/A710 @2.8GHz + 3x A510 @2.0GHz.
- CPU ISA features (/proc/cpuinfo): fp asimd aes pmull sha1/2/3/512 sm3/sm4 crc32
  atomics fphp asimdhp asimdrdm jscvt fcma lrcpc dcpop asimddp(dot-product) dit
  uscat ilrcpc flagm/flagm2 frint **i8mm bf16** bti paca/pacg. (ARMv9-class.)
- Adreno Vulkan extensions PRESENT (113 total) relevant to draw overhead:
  - **VK_KHR_push_descriptor** <- push descriptors inline, no alloc+update+bind/draw
  - **VK_EXT_extended_dynamic_state** + **_state2** <- set viewport/scissor/cull/
    depth-bias/stencil dynamically, fewer pipeline variants/binds
  - **VK_EXT_vertex_input_dynamic_state** <- dynamic vertex input, fewer pipelines
  - **VK_KHR_draw_indirect_count**, draw_indirect (GPU-driven draws)
  - VK_KHR_maintenance1-4, VK_ANDROID_external_memory_android_hardware_buffer
  - NOT present: VK_EXT_multi_draw, VK_EXT_descriptor_buffer, VK_EXT_external_memory_host

## What is ALREADY optimized (don't re-do)
- byte_swap uses __builtin_bswap -> single ARM64 REV (optimal scalar).
- copy_and_swap_16/32/64 ARM64 path is ALREADY NEON-vectorized (vqtbl1q_u8 table
  lookup, 16 bytes/iter; a comment notes vrev32q_u8 was benchmarked SLOWER). So the
  endian-conversion hot path is done.
- A64 JIT backend inlines V128 ops with NEON (per upstream Canary work + our a64_seq_
  vector.cc). vector ops are not C-thunked.

## The REAL bottleneck (from this session's profiling) and the matching HW levers
Measured: Blue Dragon field = 10,597 REAL draws/frame, 1.19M verts, ~2.4fps. Cost
split ~25% Adreno driver + ~10% our flat PM4 parse. descriptor_binds=14,749/frame,
pipeline_binds=1,086/frame. It is DRAW-CALL-CPU-BOUND (~38us/draw), NOT vertex/fill
bound (2.86M verts/sec is trivial for Adreno 740). So acceleration = cut PER-DRAW
host+driver cost. Ranked levers, all using device-confirmed HW features:

1. **VK_KHR_push_descriptor** (HIGHEST). Replace per-draw {allocate transient
   descriptor set + vkUpdateDescriptorSets + CmdVkBindDescriptorSets} with one
   vkCmdPushDescriptorSetKHR. Kills the 14,749 binds/frame alloc+bind overhead - the
   biggest measured per-draw driver cost. Needs the descriptor set layouts created
   with VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT and a push path in
   IssueDraw. Big, but THE win.
2. **VK_EXT_extended_dynamic_state(1/2) + vertex_input_dynamic_state**. Move
   viewport/scissor/cull/depth-bias/stencil/primitive-topology/vertex-input out of
   the baked pipeline into dynamic state. Cuts pipeline VARIANTS and the 1,086
   pipeline binds/frame + pipeline-cache lookups per draw. Many of these may already
   be dynamic - audit vulkan_pipeline_cache dynamic-state list vs what's available.
3. **i8mm / bf16 / asimddp** CPU features: not for the draw path, but for any guest
   vector/matrix-heavy code the JIT emits (VMX128). Lower priority - GPU draw cost
   dominates now.

## CRITICAL CONTEXT (do not lose sight of)
The 10,597-draw scene PRODUCES NO VISIBLE OUTPUT (black-3D; flat blue/yellow/black per
present mode - see worklog B21/B22). We are spending all this draw cost on geometry
that never composites to screen. So: (a) the present/composite bug must be fixed for
the work to be worth anything, AND (b) push-descriptor/dynamic-state will speed up the
draw submission regardless. Both are real; push_descriptor is the cleanest pure-speed
win and is independent of the visual bug.

## Recommended next action
Implement VK_KHR_push_descriptor for the per-draw texture+constants descriptor sets
(lever 1). It directly attacks the measured 14,749 descriptor binds/frame and uses a
device-confirmed Adreno feature. Gate behind a cvar (default off until A/B'd), measure
fps + simpleperf delta on the 10,597-draw scene.
