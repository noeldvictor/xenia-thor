# Lever 2: zero-copy draw concatenation — design of record (2026-06-01)

From the design+adversarial-verify workflow (wf_concat_design.mjs, run wf_07744894-2fe,
10 agents). Feasibility: **build-with-fixes**. Builds blind, gated `vulkan_merge_draws`
(default false, bit-identical off); device A/B deferred to reconnect. Front A EDS lengthens
same-pipeline runs; this lever turns same-state runs into ONE vkCmdDrawIndexed.

## Core idea
Today: 1 guest draw → 1 IssueDraw → exactly 1 host draw (vulkan_command_processor.cc
~3910-3953). For **kGuestDMA** indexed draws the index buffer is always
`shared_memory_->buffer()` and `guest_index_base` is folded into the CmdVkBindIndexBuffer
offset (firstIndex=0, vertexOffset=0). N consecutive same-state draws indexing a
**contiguous** byte run are equivalent to ONE `CmdVkBindIndexBuffer(buffer, run_base, type)`
+ ONE `CmdVkDrawIndexed(sum_of_counts, 1, 0, 0, 0)` — pays the Adreno per-draw fixed cost
once, bins each triangle once. NOT instancing.

**Defer-and-coalesce:** IssueDraw runs ALL state setup unchanged; only the trailing
bind-index+draw pair (3925-3953) is deferred. A same-state run collapses to one pair.
Equivalence holds because, by the predicate, every state command between two merged draws
is a no-op.

## CRITICAL adversarial corrections (must implement)
1. **HARD LIST-ONLY topology gate.** Merge only `host_primitive_type` ∈ {kTriangleList,
   kLineList, kPointList}. Strips/fans/quads/polygons + any primitive-restart-enabled draw
   → flush, run length 1. Strips would stitch primitives across draw boundaries. Do NOT
   trust "EDS promotes strips to lists": vulkan_pipeline_cache.cc:600-602 normalizes only
   the pipeline KEY; UpdateDynamicState emits the REAL VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
   so list+strip share a VkPipeline but render as their true topology. Concatenation is
   list-only. (BD heavy scene ~931 triangle-list draws — still high value.)
2. **Cursor-snapshot for the dynamic-state hole.** Snapshot the deferred_command_buffer_
   emitted-command cursor at IssueDraw entry; compare just before the emit block (~3910).
   ANY appended command other than the predicate-gated CmdVkBindPipeline → cannot-extend.
   This covers ALL dynamic state (viewport/scissor/depth-bias/blend/stencil/cull/front/
   topology/restart/depth), descriptor binds+pushes, render-pass begin/end, barriers —
   comprehensively. Preferred over hand-enumerated flags (blend constants are pure dynamic
   state on no-FSI Adreno-740, invisible to any system-const signal).
3. **Low-level chokepoint flushes** (NOT the IssueDraw:3906 call site): the RT cache breaks
   the live pass from INSIDE render_target_cache_->Update() at IssueDraw:~3561, BEFORE the
   interceptor. Put FlushPendingMergeRun() at the TOP of: SubmitBarriers (~2784, before its
   EndRenderPass), SubmitBarriersAndEnterRenderTargetCacheRenderPass (~2849), EndSubmission
   (~4572, before teardown EndRenderPass + before the submission-begin pipeline-NULL reset),
   first statement of IssueCopy (~4046) and IssueSwap (~1852), and inside
   BindExternalGraphicsPipeline (~3167) before it NULLs the pipeline (~3202).
4. skip_tiny_draw (3914-3919) + the primitive index-count-clamp case = HARD non-extending
   boundaries (advance the guest index pointer with no 1:1 host draw); flush, never extend
   across them, never update next_byte for a skipped draw.
5. vfetch-constants gate: the per-draw vfetch RequestRange loop keys off
   regs.GetVertexFetch() (XE_GPU_REG_SHADER_CONSTANT_FETCH), NOT in kConstantBufferFloatVertex.
   Gate explicitly OR prove a fetch-constant change dirties a tracked descriptor set (caught
   by the cursor snapshot).
6. Use POST-normalization ProcessingResult values (endian/base get rewritten/aligned).
7. The `vertex_index_load_address` "run base reconstructs addressing" rationale is WRONG —
   that constant is dead in the mergeable path (set only for shader_32bit_index_dma, the
   non-mergeable CmdVkDraw path). True invariant: gl_VertexIndex walks the contiguous
   concatenated range from the single bind offset, with byte-identical vertex_base_index +
   vertex_index_endian applied uniformly. Do NOT add a load_address gate.
8. Reset all merge_pending_* at the submission-begin reset block REGARDLESS of cvar (no
   stale carryover on mid-run toggle); the EndSubmission-entry flush MUST run BEFORE that reset.

## Merge predicate (extend the pending run only if ALL hold)
same VkPipeline; same PipelineLayout; index_buffer_type==kGuestDMA; same VkIndexType
(stride); stride-correct CONTIGUITY (guest_index_base == pending_next_byte, the merge_vf_contig_
test); same vertex_base_index (VGT_INDX_OFFSET); same vertex_index_endian; LIST-only topology
+ same prim type + restart class; VS float constants unchanged
(current_constant_buffers_up_to_date_ & (1<<kConstantBufferFloatVertex)); NOT memexport
(Use was kRead); cursor-snapshot shows no command appended (covers descriptors/dynamic-state/
render-pass/barriers); vfetch constants stable.

## Incremental build plan (each gated, build-verified, committed)
1. **Scaffolding (NO behavior change)** ← FIRST. cvar + EmulatorActivity allowlist; merge_pending_*
   members + last-bound-IB cache in .h; FlushPendingMergeRun() (emits one bind+draw from
   captured fields, else early-return). Interceptor at 3925-3953: cvar OFF → existing path
   verbatim; cvar ON → capture head then immediately FlushPendingMergeRun() (run length
   always 1 = identical stream). Reset merge_pending_* at submission-begin regardless of cvar.
2. **Cursor signal**: add a command-count getter to DeferredCommandBuffer; snapshot at
   IssueDraw entry + before emit; compute cannot_extend (cursor moved by anything but the one
   CmdVkBindPipeline). Still flush every draw.
3. **Chokepoint flushes**: insert FlushPendingMergeRun() at the chokepoints (#3 above). Inert
   while flush-every-draw, but proven to compile + early-return.
4. **Enable coalescing (list-only kGuestDMA)**: full predicate incl. list-only gate,
   contiguity, vertex-addressing, vfetch-constant gate, kRead-only, cursor check. EXTEND or
   FLUSH+new-head. FlushPendingMergeRun emits one bind + one CmdVkDrawIndexed(sum,1,0,0,0).
   cvar off byte-identical; cvar on = coalescing, correctness UNVERIFIABLE w/o device.
5. **Device A/B (deferred)**: content-matched off vs on, read merge_run_hist_/merge_vf_contig_
   + screenshot for no dropped/stitched geometry; only then consider default-on.

## Top risks
Missed flush = dropped/mis-ordered geometry, invisible to validation, only catchable on
device (currently off). Highest-risk miss = the barrier-induced render-pass break (not
visible from render-pass handle equality) — covered by the cursor snapshot + chokepoint
flushes. Off-path bit-identity must be preserved at every increment.

Source: wf_07744894-2fe (full output was in the task temp file).
