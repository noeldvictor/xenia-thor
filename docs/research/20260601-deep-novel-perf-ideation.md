# Deep + novel GPU-performance opportunities — research-grounded ideation (2026-06-01)

From the deep+novel research-grounded workflow (tools/thor/wf_deep_codebase_eval.mjs, run
wf_929d25d7-e07, 17 agents: 4 web-research → 9 ideate → 3 adversarial verify → synth). Target:
AYN Thor, Snapdragon 8 Gen 2, Adreno 740v2 (TBDR/FlexRender, no VK_EXT_fragment_shader_interlock).

## Bottleneck (device-measured; everything is ranked against it)
Blue Dragon heavy 3D scene is GPU-bound on the geometry/per-draw/BINNING front-end: gpu_busy
77-79%@615MHz; ~2000 tiny draws/frame, ~136 verts avg, ~2000 descriptor binds; gpu_frame_us
pixel-independent + super-linear in draw+vertex count. Tile load/store ~3%, render-pass breaks
~12%, fill 0%. CPU ~84% idle. Two cost classes (prior research): per-DRAW fixed cost = COALESCABLE
(EDS + concat); per-TRIANGLE binning floor = NOT coalescable (only a real cull lowers it; the
Adreno binning pass runs a position-only VS for every vertex of every draw — HPG2019/freedreno).

## Hard guardrail
Guest-visible math/geometry must reproduce Xenos bit-exactly (proven: arm64_vmx_dot_f32_fastpath
+ fp16 FCVTL/FCVTN broke Blue Dragon). Every UPHELD item below is internal/bit-identical OR
explicitly HELD as guest-visible/device-gated. Device-feature-chain changes can black-screen ALL
games unvalidatable-blind (the UMA present-hang burned this once) → device-gated.

## Ranked opportunities (verifier-upheld: bit-exact-or-internal, grounded, honest)

### 1. Constants set → UNIFORM_BUFFER_DYNAMIC  [HIGH · bottleneck · safe-blind · medium]
**Code:** vulkan_command_processor.cc:369-405 (layout = UNIFORM_BUFFER @:375), 6132-6262 (UBO ring
writes exist), 6443-6482 (transient alloc + 5 VkWriteDescriptorSet + vkUpdateDescriptorSets), ~6606
(rebind). **What:** the constants set (System+FloatVertex+FloatPixel+BoolLoop+Fetch) is
re-allocated + fully rewritten + rebound whenever ANY guest constant changes — float-vertex
constants churn every draw, so this fires ~once/draw = dominant share of the ~2000 binds. Convert
the 5 bindings to UNIFORM_BUFFER_DYNAMIC, allocate ONE persistent set per frame-in-flight over the
existing uniform_buffer_pool_ ring, write once, and per-draw advance 5 dynamic byte-offsets in
CmdVkBindDescriptorSets. Textures already use push descriptors — this closes the gap.
**Bit-exact:** identical constant bytes reach the same shader; only plumbing changes. **Grounded:**
Adreno two-register-bank context model; ARM/Khronos descriptor-reuse 38% frame-time; Roblox GDC
one-buffer + pDynamicOffsets ~50% fewer updates. **First step:** gate at init on
maxDescriptorSetUniformBuffersDynamic>=5 + 256B dynamic-UBO alignment, fallback to current path;
build persistent-set + dynamic-offset path FIRST, verify independently, before touching merge.
**Discipline add:** also gate behind a NEW default-off cvar so the default path is bit-identical
and it's a device A/B (the descriptor change is unvalidatable-blind otherwise).

### 2. Constant-only changes survive concatenation (dynamic-offset re-issue)  [MED · bottleneck · safe · medium · depends on #1]
**Code:** :3970-3993 (merge_cannot_extend_this_draw_ set by any recorded command), 4056-4093
(can_extend+concat), ~4238 (merge_miss_state_). With #1, a constant change records no new set bind;
carry the 5 dynamic offsets into the deferred draw so a same-pipeline run re-issues only the cheap
offset → recovers concat runs constants currently break. Bit-identical. **First step:** measure
merge_miss_state_ before/after #1 to confirm the run-breaker is gone, then implement offset carry.

### 3. Lifetime-aware per-attachment LOAD_OP/STORE_OP (real EDRAM liveness)  [MED · bottleneck · safe · medium]
**Code:** vulkan_render_target_cache.cc:1558-1606 (hard-coded LOAD/STORE; only escape = blanket
gpu_edram_passes_dont_care cvar), RenderPassKey 1391-1469, transfer/ownership bookkeeping ~4561.
Drive loadOp=DONT_CARE only when the pass PROVABLY fully overwrites the EDRAM tile range,
storeOp=DONT_CARE only when nothing reads before next clear; bake op into RenderPassKey. Bit-exact
when liveness is a proven predicate (discarded contents never observed). Targets the ~12%+~3%.
**First step:** land a READ-ONLY counter of provably-dead attachments/pass first; DONT_CARE only
where proven.

### 4. Coalesce same-config EDRAM ownership transfers into ONE render pass  [MED · bottleneck · safe · medium]
**Code:** vulkan_render_target_cache.cc:5087-5088/5454-5455 (transfer = own pass), ~2887 (break),
rt_pass_break_rt_change_. Each transfer opens its own pass + a break back; on TBDR each switch =
GMEM store+reload. Batch same-config destinations into one pass. Bit-identical (same transfers).
**First step:** read rt_pass_break_rt_change_ / xfer_same_fmt on a content-matched frame to confirm.

### 5. GPU-driven multi-draw-indirect for same-state NON-contiguous list draws  [MED · bottleneck · safe-build/device-perf · medium]
**Code:** :3996-4090 (draw emit), 4140-4168 (scattered sizing). ZERO DrawIndexedIndirect in the
vulkan backend (novel-for-codebase). For same-pipeline/layout/index-type list draws that are NOT
byte-contiguous (concat can't fuse), build VkDrawIndexedIndirectCommand[] in the per-frame ring +
one CmdVkDrawIndexedIndirect drawCount=N → captures the merge_vf_scattered_ population concat
leaves behind. Bit-identical. **First step:** read merge_vf_scattered_ to size it; gate on
multiDrawIndirect; reproduce vgt_indx_offset per-draw exactly; device-measure (TBDR per-draw win
may be smaller than desktop).

### 6. Disk-persisted VkPipelineCache + pre-warm  [LOW · NOT bottleneck (spikes) · safe · small]
**Code:** vulkan_pipeline_cache.cc:2304-2305 (vkCreateGraphicsPipelines passes VK_NULL_HANDLE — no
cache object exists!), descriptions persisted ~1881-1883. Create a VkPipelineCache, pass it in,
serialize to a driver+title-keyed file, pre-warm from persisted descriptions. Bit-identical. Attacks
first-encounter compile hitches / scene-transition jank, not the steady-state floor. **First step:**
add the cache object immediately (zero-risk), then wire load/store keyed by pipelineCacheUUID+driver.

### 7. Redundant index-buffer / descriptor-bind filter in DeferredCommandBuffer  [LOW · bottleneck(small) · safe · small]
**Code:** deferred_command_buffer.{h,cc} (linear replay, no redundant-command filtering). Track
last-bound IB/descriptors, drop matching re-binds. Observably a no-op (bit-exact); also lengthens
merge runs. **First step:** add last-bound-handle tracking to the IB bind path, early-return on
unchanged handle+offset+type.

### 8. VkDescriptorUpdateTemplate for texture/sampler pushes  [LOW · NOT bottleneck · safe · medium]
**Code:** vulkan_command_processor.cc:6374-6424/6490-6502/6633-6671 (signature cache ~6297 already
skips most redundant pushes). Describe image-info layout once per (tex,sampler) key via
vkCmdPushDescriptorSetWithTemplateKHR. Bit-identical. CPU-only hygiene; small residual win.

## HELD / device-gated (NOT safe to build blind — do not re-pitch as safe)
- **GPU/CPU compute cull → compacted index buffer + indirect.** The ONLY per-triangle-floor lever,
  but it DECIDES which triangles rasterize → must bit-exactly match the GPU on clip-space replay,
  PA_SU_SC_MODE_CNTL winding, kRectangleList exclusion, the UNMODELED Adreno guard-band/near-plane.
  A mismatch silently drops a visible triangle. HOLD-for-device (frontb-cpu-cull-design). Only the
  read-only cullable_tris counter (already shipped) is safe-blind.
- **State-sorted draw reordering.** Reordering changes rasterization order (blend/alpha/depth-equal/
  stencil/overlap are order-significant). REJECT blind; a read-only reorderable-opaque counter is OK.
- **Point/rect/quad → triangle pre-expansion to retire geometry shaders.** Primitive expansion
  defines rasterized triangles; the fork already expands rect/point via host VS types
  (primitive_processor.cc:379-460); a rect mismatch already broke 4D53082D. HOLD-for-device.
- **Subpass-fusion of EDRAM transfer into the guest draw pass.** Valid only for same-res/same-
  location/format-compatible transfers; input-attachments read only the current fragment. Device-
  gated on Adreno by-region/input-attachment behavior. (Rank 4 is the safe subset.)
- **VK_EXT_descriptor_buffer for constants.** Feature-chain change; Thor ships the PROPRIETARY
  Qualcomm driver (not Turnip), exposure unconfirmed → can black-screen all games. Device-gated;
  UNIFORM_BUFFER_DYNAMIC (#1) is the safe baseline.
- **VK_QCOM_tile_shading on-tile EDRAM (the FSI-free single-pass EDRAM the 740 lacks).** Exposure
  UNCONFIRMED (extension newer than the device's 12/2023 driver — likely absent). Enumerate on
  device first; device-gated.

## Secondary (CPU-bound titles / load / audio — NOT the BD GPU bottleneck; all bit-exact/internal)
Thread pinning to X3/A715; JIT direct block linking; per-thread indirect-branch/return cache;
fastmem [x21,Wguest,UXTW] addressing; UMA per-write coherent staging + touched-page flush scoping;
buffer_device_address vertex/index pulling; diagnostic-first UMA present-hang bisection. Honest
NEGATIVES recorded: the single GPU CP thread is NOT the binning wall; multi-threaded/secondary
command-buffer recording is counter-productive on this TBDR; CPU vertex pre-transform is a non-starter.

## Build order (safe-blind, gated, device-validate on reconnect)
1 (constants dynamic UBO, gated default-off) → 6 (VkPipelineCache object, zero-risk) → 7 (redundant
bind filter) → 2 (concat survives constants, after #1) → 3/4 (EDRAM, counter-first) → 5 (MDI,
counter-first). Each: gated/bit-exact, build-verified, committed; perf validated by device A/B.

Source: wf_929d25d7-e07 (full output in the task temp file). Hardware claims cited to HPG2019,
Mesa/freedreno docs, ARM/Roblox GDC 2020 (zeux.io/data/gdc2020_arm.pdf), Khronos Vulkan-Samples,
chipsandcheese Adreno-7xx; code claims verified file:line this session.
