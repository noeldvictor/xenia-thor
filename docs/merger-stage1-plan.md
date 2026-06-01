# Deep architecture rethink — RESULT: merger premise is in doubt; measure the lever first

Workflow wf_b2976618-bfb (probe + 3 deep designs + adversarial review). Adversary verdict: NEEDS-REWORK.
This supersedes the earlier "cache constants descriptor set" dead-end (also rejected) and the
instancing Stage-1 idea.

## The decisive finding (caught on paper, before building)
The proposed Stage 1 — collapse consecutive geometry-identical + constant-identical draws into one
INSTANCED vkCmdDrawIndexed (instance_count=N) — would NOT move gpu_frame_us, and could mis-render:
- On the Adreno 740 TBDR, instancing the SAME index range N times RE-RUNS the vertex shader and
  RE-BINS/RE-RASTERIZES the identical triangles N times. Total GPU binning + vertex + raster work is
  UNCHANGED. It only collapses the host draw-COMMAND count — which we measured is NOT the bottleneck
  (GPU ~99% busy, CPU ~84% idle). So it proves the wrong lever.
- The guest VS derives geometry from gl_VertexIndex + manual vfetch (spirv_shader_translator.cc:
  1306-1310); there is ZERO gl_InstanceIndex path. Confirmed.
- Pixel-identity is FALSE for blended/stencil/occlusion draws (particles/UI/transparency): drawing the
  same primitives N times != once under alpha blending. Stage 1 would have to exclude exactly those,
  likely gutting the population.
- Eligible population is probably ~0 anyway: descriptor_binds ~= rendered (~2000) => constants change
  almost every draw => constant-identical run length ~1.

## The reframe (the real question, unanswered)
NO draw-merging architecture (instanced / multiDraw / indirect) reduces the TOTAL triangles binned on a
tiler — it only reduces the number of draw RECORDS (per-draw fixed binning + command overhead). The
project's own data is super-linear in BOTH draw count AND vertex/triangle count. So the win is capped
by the (UNMEASURED) split between per-draw fixed binning overhead vs per-triangle binning cost:
- If per-DRAW-fixed dominates -> merging draws (Stage 2 constant-indexed instancing / Stage 3
  multiDrawIndirect with distinct firstIndex/vertexOffset, NOT exact instancing) can help.
- If per-TRIANGLE dominates -> NO merging architecture hits the framerate target; the lever is
  reducing triangle/draw COUNT (culling/LOD) or a different approach entirely.

## NEXT (cheap, evidence-rails, decides the whole direction) — do BEFORE any merger build
1. CPU-vs-GPU lever control (adversary required): re-confirm with gpu_skip_draws_below_verts (exists,
   line ~3879) on the heavy scene that REDUCING DRAW/GEOMETRY count moves gpu_frame_us, and get the
   gpu_frame_us-per-draw slope. (Content-matched A/B, thor_evidence.ps1.)
2. Command-count control: a legitimate per-draw descriptor/constant-bind cache (skip re-Request+
   vkUpdateDescriptorSets when nothing the shader reads changed) -> if collapsing ~2000 binds to a few
   hundred does NOT move gpu_frame_us (expected, CPU idle), that DIRECTLY kills the "per-draw command
   cost" hypothesis, cheaper than instancing.
3. Per-stage GPU split (the real unknown): Snapdragon Profiler / AGI capture (skill
   xenia-snapdragon-profiler-gpu-metrics) on the heavy scene -> % time vertex vs fragment vs binning,
   and whether binning is per-draw-fixed or per-triangle. THIS decides whether any merger is worth
   building. User-run (GUI); or headless KGSL perfcounter if feasible.

## Genuinely safe wins to ship regardless of the lever question (additive, no risk)
- Populate the dead merge_vf_* counters (vulkan_command_processor.h:801-805, declared+reset, never set)
  and add a run-length histogram in IssueDraw (full-state runs AND constant-DIFFERING-but-geometry-
  shared runs) behind vulkan_trace_draw_outcomes_per_frame. Cheapest highest-value data; tells whether
  ANY merger (Stage 2/3) is even possible. Ship this.
- frames-in-flight RING discipline for any future CPU-written/GPU-read UMA array (the correct TDR fix
  vs the brute gpu_uma_serialize_before_write; dead direct_last_read_submission_ confirmed). Needed
  only when a UMA batch array is introduced (Stage 2+), not before.

## Stage order IF the measurements green-light merging
Stage 1a (probe Adreno caps: drawIndirectFirstInstance/multiDrawIndirect/maxDrawIndirectCount/
shaderDrawParameters - gate the roadmap; gl_InstanceIndex is core SPIR-V 1.0, gl_DrawIndex needs
shaderDrawParameters - confirm which Stage 2 uses) -> Stage 2 (SPIR-V per-object constants by
gl_InstanceIndex from a UMA SSBO ring, so constant-DIFFERING geometry-shared runs coalesce - the deep,
least-reversible change) -> Stage 3 (true multiDrawIndirect for non-shared geometry + linearized
indices) -> Stage 3b (make gpu_uma_direct_shared_memory shippable via the ring discipline).
DO NOT start Stage 2 (irreversible SPIR-V) until 1+2+3 above answer the lever question.

## Honest bottom line
The deep rethink's most valuable output is a NEGATIVE: the obvious "merge the draws" instinct (incl.
the user's and mine) does not, by itself, reduce tiler geometry work, and we have NOT measured whether
the bottleneck is per-draw-fixed (mergeable) or per-triangle (not). Measuring that split is the gate
for the entire multi-month rewrite. No fabrication: everything here is code-grounded analysis +
adversarial review; the deciding numbers must come off the device next.
