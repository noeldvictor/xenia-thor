# Lever 2 Step 2 design — CPU/NEON triangle cull (emit a compacted index list)

Goal: on `gpu_cull_compaction` draws that qualify, drop backface/frustum triangles BEFORE the Adreno bins
them, by writing a reduced index list into the transient buffer the Step 1 pipe already feeds. Target ~21%
frame-time on BD (76.4% of verts are affine-MVP; ~56% of their triangles cullable; per-triangle binning is
~half the per-draw cost).

## The big discovery — the cull logic ALREADY EXISTS as a counter
`DrawExtentEstimator::CountCullableTriangles` (draw_extent_estimator.cc:382-615) already does, correctly:
- index read: DMA base, endian swap (GpuSwap), 24-bit mask, primitive-restart (reset_indx), VGT_INDX_OFFSET
  add, VGT_MIN/MAX clamp (lines 432-515);
- per-vertex position replay via ShaderInterpreter -> clip-space (x,y,z,w), honoring vertex-kill and missing
  position (lines 479-537);
- per-triangle FRUSTUM test (conservative same-side clip plane, w>0) + BACKFACE test (homogeneous signed-area
  determinant, divide-free) with cull_front/cull_back/front_is_ccw from PA_SU_SC_MODE_CNTL and strip
  alternating winding via `winding_reversed` (lines 540-613).
The real cull is THIS logic with `++cullable` replaced by "emit the surviving triangle's indices." Reuse it
verbatim - it already encodes every bit-exact gate the prompt lists.

## Ordering: correctness FIRST (reuse the slow interpreter), NEON speed SECOND
The position replay is the only slow part (ShaderInterpreter ~9.5us/vert -> ~2s/frame over 222k verts:
correctness-OK, perf-unusable). So:
- **2a (correctness)** — emit the culled index list using the EXISTING ShaderInterpreter replay. Validate the
  cull RENDERS CORRECTLY (no missing front faces) and REDUCES triangles. Net fps may not improve yet (CPU
  replay cost), but gpu_frame_us at matched content must DROP (fewer triangles binned) and the screenshot
  must stay correct. This de-risks the hard parts (emit, strip->list, topology, winding sign) on known-correct
  position math.
- **2b (speed)** — replace the per-vertex ShaderInterpreter with a NEON position-slice micro-interpreter
  (~0.02-0.1us/vert) so the cull is net-positive. Needs the position-slice op list (below).

## THE HARD PART — strip culling needs strip->list + a topology change
BD is strip-dominated (prim[ts~2000, tl=0]); a list-only cull drops nothing. You cannot remove a middle
triangle from a strip without breaking it, and a degenerate (collapsed) triangle is still BINNED (no saving).
So surviving strip triangles must be emitted as a triangle LIST (3 indices each, winding-corrected: swap two
indices for odd `winding_reversed` triangles so the list has consistent CW/CCW), and the draw issued with
TRIANGLE_LIST topology.
- Topology is per-pipeline UNLESS VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY (EDS1, core in Vulkan 1.3 - present on
  Adreno 740) is enabled; list<->strip is the same topology CLASS, so vkCmdSetPrimitiveTopology(LIST) is legal
  on a dynamic-topology pipeline. The cull therefore REQUIRES dynamic topology for strip draws. Options:
  (a) make gpu_cull_compaction force the qualifying pipeline to be built with dynamic topology (couple to the
      vulkan_dynamic_state_topology path), then emit CmdVkSetPrimitiveTopology(TRIANGLE_LIST) before the
      compacted draw; OR
  (b) Step 2a scope = LIST draws only first (emit fewer list triangles, no topology change) to prove the
      emit/cull machinery, then add strip->list + dynamic topology as Step 2a'. (b) validates plumbing but
      culls ~0 on BD, so (a) is required for a BD win.
- Risk if topology is wrong: strip data drawn as list (or vice-versa) scrambles geometry. Gate carefully;
  off-path and non-qualifying draws keep the Step 1 verbatim copy + original topology.

## Position-slice micro-interpreter (for 2b)
Replace ShaderInterpreter with a tiny interpreter over ONLY the ops feeding gl_Position:
- Op-list extraction (during AnalyzeUcode): backward-slice from the position export's source registers to the
  vfetch of position + the float-const-indexed MVP ops ({dp4,dp3,mad,mul,add,max,min}). Record a compact op
  list (opcode, dest reg+mask, src operands incl. which vfetch attribute and which const reg) on the Shader.
  The taint pass at shader_translator.cc:GatherAluInstructionInformation already visits every op - extend it
  to also append qualifying-op records, then keep only those backward-reachable from kPosition.
- The vfetch position source: Shader::vertex_bindings()[].attrib.fetch_instr gives the fetch constant, offset,
  stride, format. The fetch constant -> guest address via regs.GetVertexFetch (as IssueDraw already does at
  line 3931). Decode the vertex format (the existing vertex format decoders) to a float4.
- NEON kernel: load const-MVP rows once per draw (vld1q_f32), per vertex load position (vld1q), 4x vfmaq/vdot
  -> clip xyzw. ~4-8 NEON ops/vert. Constants are read from the float constant register file at draw time
  (regs / the bound constant buffer source the GPU uses).
- Precision: NOT required to be bit-exact (see safety below). FP32 NEON matching VMX128 lane order is more
  than enough for the cull decision.

## Why approx math is SAFE here (the key insight)
The cull DECISION only chooses whether to SKIP binning a triangle; the GPU still does the real transform for
anything kept. A CONSERVATIVE backface test - drop a triangle only when its signed-area determinant is
CLEARLY one-sided (|det| > margin) and clearly backface - NEVER drops a visible front triangle (a wrong-but-
small det near 0 keeps the triangle, which the GPU then bins+tests as today: no visual change, just a missed
saving). So approximate NEON math is safe (aligns with approx-math-guest-visible-vs-heuristics: this is a
heuristic, not guest-visible math). Add a small |det| margin to CountCullableTriangles' `det != 0` test for
the emit path.

## Bit-exact / correctness gates (carry from the counter; do NOT relax these)
- vertex-kill: a killed vertex's triangles must be KEPT (the GPU may still use them) - the counter already
  skips kill>0 verts as "invalid" which DROPS them; for the EMIT path, an invalid/killed vertex's triangle
  must be EMITTED (kept), not dropped. THIS IS A REQUIRED CHANGE from the counter (the counter is a lower
  bound; the culler must be conservative the OTHER way).
- frustum: keep the conservative same-side test (never over-cull) but likewise EMIT (keep) any triangle with a
  not-clearly-outside or invalid vertex.
- W: reconstruct/skip when vtx_xy_fmt (pre-divided positions) - the counter already bails (returns 0) on
  cull_vtx_xy_fmt_; the culler must EMIT-ALL (no cull) for those draws.
- winding from origin_bottom_left / front_is_ccw - reuse the counter's mapping; if a device A/B shows inverted
  culling (front faces vanish), flip the det sign (the counter notes this same risk).
- bail (emit verbatim, no cull): clip_disable, tessellation, non-DMA/auto index, non-list/strip prim,
  texture-fetch VS (CanInterpretShader false), and anything is_position_affine_mvp_candidate rejects.

## Steps + exact locations
- 2a: `DrawExtentEstimator::BuildCulledIndexList(vertex_shader, out_indices, out_is_list)` - refactor of
  CountCullableTriangles (draw_extent_estimator.cc) that emits KEPT triangles as a list (conservative: keep on
  any doubt). Wire into vulkan_command_processor.cc IssueDraw gpu_cull_compaction branch (~line 4078): build
  the list, memcpy into the transient buffer, set TRIANGLE_LIST topology (dynamic), CmdVkDrawIndexed(reduced
  count). Validate: screenshot correct + gpu_frame_us drop at matched rendered/avg_vertices + cullable_tris
  oracle agrees.
- 2b: position-slice op-list extraction (shader.h + shader_translator.cc) + NEON micro-interpreter replacing
  the ShaderInterpreter replay inside BuildCulledIndexList. Validate the NEON output vs a scalar reference
  (offline unit check) + device fps win at matched content.

## Validation plan (device)
gpu_freeze_at_guest_ms HALTS rendering (cannot test a render change). Fresh launches desync, so content-match
by rendered+avg_vertices, not guest_ms. Per checkpoint: (1) screenshot CORRECT (front geometry intact); (2)
gpu_frame_us DROPS at matched content (fewer triangles binned); (3) the cullable_tris oracle's count roughly
matches the triangles removed. Force-stop + cool after each; never thrash.
