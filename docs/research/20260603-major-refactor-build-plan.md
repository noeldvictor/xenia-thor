# Thor Hyperopt BUILD PLAN — R2-R5 + CPU multicore-JIT (back-to-back)

From the major-refactor-impl-plan workflow (w0cr700f4). All anchors re-verified against live code.
Every refactor cvar-gated default-OFF; default path stays best-known-good. ONE device test at the end.

## Ground-truth corrections (do not skip)
1. **R2: keep `uniform_buffer_pool_` (vulkan_command_processor.h:599)** as the cvar-OFF fallback; ADD the
   rings alongside, gate which path runs. Removing it breaks the off-path.
2. **Multicore: `entry_table.cc:51-58` already implements the STATUS_COMPILING spin-wait interlock** -
   reuse it as the worker/demand publish point. The missing piece is only a parallel *producer*, not a lock.

## Sequenced build order (dependency-correct)
- STEP 0  Shared infra (ring-arena helper + Thor topology router)   [enables R2/R4 + multicore]
- STEP 1  R2  `vulkan_dynamic_constants_arena`        [must precede R5]
- STEP 2  R3  `arm64_use_flat_membase`                [independent]
- STEP 3  MC  `thor_parallel_jit` / topology router    [independent]
- STEP 4  R4  `gpu_precull_cache_enable`              [needs STEP-0 ring]
- STEP 5  R5  `vulkan_r5_draw_concat_state_fingerprint` [needs R2]

Each refactor "done when": compiles + links + cvar OFF = byte-identical to today.

## STEP 0 — shared infra
### 0a `VulkanDynamicBufferRing` (new src/xenia/ui/vulkan/vulkan_dynamic_buffer_ring.{h,cc}, ~120 LOC)
UMA ring arena. R2 = HOST_VISIBLE|DEVICE_LOCAL (CPU writes, GPU reads); R4 = DEVICE_LOCAL. API:
Initialize(device, capacity, usage, memory_props, alignment) / Allocate(size,&ok)->offset /
host_mapping() / buffer() / FrameAdvance(frame_current) / Grow(new_capacity). Persistent vkMapMemory
once. Allocate aligns + per-frame segments by frame_current_ % kMaxFramesInFlight(3) so frame N+1 never
stomps in-flight frame N. If HOST_VISIBLE|DEVICE_LOCAL unavailable -> Init false -> caller stays on
uniform_buffer_pool_. Reuse vulkan_device memory_types helpers (as VulkanUploadBufferPool).
### 0b `ThorTopology` (new src/xenia/base/thor_topology.{h,cc}, ~60 LOC)
kPrimeCore=7 (X3), kBigMask=0xF8 (cpu3-7), kWorkerMask=0x78 (cpu3-6, leave X3 for guest), kLittleMask=0x07.
IsThor() (/proc/cpuinfo detect, cached); PrimeCore(); WorkerCoreMask(). command_processor.cc:344 (R1) +
xthread.cc:893 (guest mask) read defaults from it (sentinel-only; explicit cvars override). #if XE_ARCH_ARM64 && XE_PLATFORM_ANDROID.

## Consolidated checklist (execute top-to-bottom)
STEP 0: (1) create vulkan_dynamic_buffer_ring.{h,cc} + add to xenia-ui-vulkan premake. (2) create
thor_topology.{h,cc} + add to xenia-base. (3) point command_processor.cc:344 + xthread.cc:893 defaults at ThorTopology.
STEP 1 R2 (vulkan_dynamic_constants_arena, gpu_flags + allowlist): (5) .h:599 keep uniform_buffer_pool_,
ADD constant_buffer_rings_[kConstantBufferCount=5], constants_dynamic_descriptor_set_, current/prev_constant_offsets_.
(6) .h:612 descriptor_set_layout_constants_dynamic_ + constants_dynamic_pool_. (7) .cc:340-350 init 5 rings
(HOST_VISIBLE|DEVICE_LOCAL, UNIFORM_BUFFER_BIT, align=minUniformBufferOffsetAlignment, 256KB, grow-on-fail; fail->fallback).
(8) .cc:395-437 dup layout as _dynamic_ with 5x UNIFORM_BUFFER_DYNAMIC; create pool+set; one-time vkUpdateDescriptorSets.
(9) .cc:6609-6740 wrap pool path in if(!cvar){existing}else{ring.Allocate+memcpy; current_constant_offsets_[i]}.
(10) .cc:6920-6958 cvar ON: skip transient alloc/write; set current_graphics_descriptor_sets_[kDescriptorSetConstants]=dynamic set.
(11) .cc:7083-7087 cvar ON + constants set: CmdVkBindDescriptorSets(...,1,&dynamic_set,kConstantBufferCount,offsets); else 0,nullptr.
(12) .cc:6550-6556 elision: cvar ON + merge_consts_same_ + all 5 unchanged -> skip alloc/memcpy, prev->current offsets, bind only.
(13) frame-start ring.FrameAdvance; on Grow re-run rebind. (14) pipeline cache key += dynamic_constants bit; invalidate on flip.
STEP 2 R3 (arm64_use_flat_membase, cpu/memory flags + allowlist): (16) memory.h flat_membase_high_offset_.
(17) memory.cc Initialize set high_offset = granularity>0x1000?0x1000:0. (18) MapViews 4-entry flat layout when cvar,
entry[3] bakes +high_offset at 0xE0000000; keep 8-entry off-path; test 0xDFFFFFFF/0xE0000000. (19) a64_emitter.h cache high_offset.
(20) a64_seq_util.h:271-296 ComputeMemoryAddress: cvar -> mov(x0,addr)/UXTW no range-check; else existing. MMIO unaffected.
STEP 3 MC (thor_parallel_jit + thor_parallel_jit_workers): (22) processor ParallelTranslationPool (workers pinned WorkerCoreMask),
lazy on first ResolveFunction. (23) ResolveFunction: keep synchronous path for demanded addr; REUSE entry_table.cc:51-58
STATUS_COMPILING interlock (worker GetOrCreate wins STATUS_NEW or skips). (24) prediction: enqueue direct-call targets.
(25) IC coherence: worker __builtin___clear_cache + generation counter; dispatch thunk ISB on counter advance. All gated.
STEP 4 R4 (gpu_precull_cache_enable + max_entries/max_memory_mb/frustum_margin=0.10/trace_stats): (27) PrecullCacheEntry +
precull_cache_ map + DEVICE_LOCAL ring + watch handles + stats. (29) BuildCulledIndexList add out_object_space_positions.
(30) draw_extent_estimator.cc:1176/1271 project survivors via exact FP32 NEON FMLA (NO bf16/i8mm); -O2 not -Ofast.
(31) vulkan_shared_memory RegisterPrecullCacheWatch -> InvalidatePrecullCacheEntry; hook ALL guest-write paths.
(32) IssueDraw ~3100 before UpdateBindings: key=FNV1a64(vertex_base,index,vs_hash,const_hash); hit+frustum-changed ->
re-project AABB 8 corners DP4 + 6-plane test margin -> draw cached count; miss -> cull + store. (33) frame-end prune.
STEP 5 R5 (vulkan_r5_draw_concat_state_fingerprint): (35) .h merge_pending_render_pass_/framebuffer_/descriptor_set_validity_
+ merge_mdi_* trio. (36) .cc:4346 concat can_extend AND render_pass==pending && framebuffer==pending && (validity & pending)==pending.
(37) .cc:4366 run-head always snapshot. (38) .cc:4418 MDI can_extend same 3 clauses. (39) .cc:4482 MDI run-head snapshot.
FINAL: (40) flip Thor device-config defaults ON (R2,R3,R5,MC; R4 gated until reuse>=40%); compiled defaults stay false. END TEST once.

## End test (ONCE, after all built + default-on)
thor_gpu_capture.ps1 -NoDump -TopProfile. Trust GPU busy% + per-thread top, NOT gpu_frame_us. Read png every fire.
Match guest_ms AND rendered for A/Bs. Games: (1) Blue Dragon - R2+R5 ON together (per-draw knockout): watch rendered
draw count drop (1100-2180 -> 800-1200?) + GPU busy% at matched guest_ms; then +R4, read reuse-rate, ship R4 if >=40%.
(2) Lost Odyssey (CPU-bound) - R3 + thor_guest_thread_affinity_mask=248 + thor_parallel_jit: per-thread top, JIT-stall shrink.
(3) Banjo - cross-game correctness (R2/R5 must not corrupt). (4) Burnout - transfer-heavy, no present-hang.
Pass: PNG identical to default-off at matched guest_ms; BD rendered down + busy% down no visual loss; LO better thread dist;
no crash/hang/thermal trip. Any failure -> FIX FORWARD (new commit), never revert.
