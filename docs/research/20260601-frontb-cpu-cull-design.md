# Front B (CPU advanced-math + UMA) feasibility — verdict of record (2026-06-01)

From the design+adversarial-verify workflow (tools/thor/wf_frontb_design.mjs, run
wf_23b97622-da6, 10 agents). Goal: reduce the GPU per-TRIANGLE binning floor (the
NON-coalescable cost Lever 2 + Front A do not touch). Device disconnected - design only.

## Verdict: only a real CULL can help, and it is HOLD-FOR-DEVICE
Of the three CPU+UMA options, only **binning FEWER triangles (a real cull)** can lower the
per-triangle floor:
- **CPU pre-transform — REJECTED.** The GPU re-runs the full guest VS per vertex
  unconditionally: gl_Position is written by the translated guest ALU export
  (spirv_shader_translator.cc:2646-2653), driven by gl_VertexIndex + manual in-shader
  vfetch. No host path to inject precomputed positions; the VS still runs for varyings; the
  binning pass still position-shades and bins every triangle. Bypassing it = per-shader
  SPIR-V dataflow-slicing = a research re-arch, and it still wouldn't bin fewer triangles.
- **CPU compaction — REJECTED (for Front B's goal).** Dedup/reorder preserves the triangle
  set → bins the same triangles → only touches per-DRAW cost, already Lever 2's domain.
- **CPU cull — VIABLE but HOLD-FOR-DEVICE.** Architecturally friendly: idle CPU (~84%),
  UMA zero-copy, index-driven vfetch (kGuestDMA passthrough, primitive_processor.cc:825-832)
  so a culled index buffer needs NO shader change and the GPU bins strictly fewer triangles.
  BUT not provably-equivalent blind: (1) needs per-vertex CLIP-SPACE positions, which only
  exist by replaying the guest VS transform (guest ucode ALU + float constants) + the host
  W/NDC fixup (spirv_shader_translator.cc:1625-1726); (2) must EXACTLY match the GPU on
  winding (PA_SU_SC_MODE_CNTL, registers.h:459-462), the rectangle-list exclusion (broke
  title 4D53082D, draw_util.h:65-92), and especially the UNMODELED hardware guard-band/
  near-plane (the fork leans on the host Adreno's guard-band inside a huge clip_disable
  viewport, draw_util.cc:362-545). One mismatch silently drops a VISIBLE triangle - a
  correctness bug invisible to build validation, only catchable on the device (OFF, must not
  be thrashed). So the geometry-altering cull is HELD for a device A/B.

## The ONE safe blind step: a read-only cullable-triangle COUNTER
Provably rendering-neutral (mutates NO index buffer), build-verifiable with the device off,
gated default-off. It produces the go/no-go number for whether the on-device cull is worth a
device A/B. NOT a speedup itself.
- For a sampled draw, on the idle CPU: replay the guest VS position transform with the
  EXISTING ShaderInterpreter (shader_interpreter.h:29-62; respect CanInterpretShader, which
  returns false for texture-fetch VS → those draws skipped/uncounted), apply the host W/NDC
  fixup (spirv_shader_translator.cc:1625-1726), apply the EXACT backface rule
  (PA_SU_SC_MODE_CNTL) gated by IsPrimitivePolygonal incl. the kRectangleList exclusion, in
  the non-Y-flipped clip space the host rasterizes in (origin_bottom_left=false,
  vulkan_command_processor.cc:3686-3690); for frustum, count ONLY prims provably FULLY
  outside one axis (conservative - no guard-band assumption).
- COUNT (do not drop) the would-cull triangles; log via vulkan_trace_draw_outcomes_per_frame.
- Code home: extend the whole-draw IsRasterizationPotentiallyDone pattern (draw_util.cc:42-65,
  already reads PA_SU_SC_MODE_CNTL + the both-faces-culled check) with a per-triangle count path.

### Incremental plan for the counter (gated default-off, build-verified each step)
- C1: scaffolding - cvar (e.g. gpu_trace_cullable_tris) + a per-frame counter logged in the
  draw-outcomes line + reset at swap + a CountCullableTriangles() hook (initially counts 0 =
  rendering-neutral, build-verifiable).
- C2: wire the ShaderInterpreter VS-position replay (gated by CanInterpretShader) + the W/NDC
  fixup for sampled draws -> per-vertex clip positions.
- C3: apply the exact backface rule + conservative fully-outside frustum, count would-cull tris.

## What NEEDS the device (do not attempt blind)
- The geometry-altering cull itself (writes the GPU-drawn index buffer; a mismatch silently
  drops geometry).
- Validation that the CPU-replayed clip coords match what the GPU rasterizes.
- The real guard-band/near-plane boundary (unmodeled in the fork).
- The actual gpu_frame_us payoff (content-matched harness on device).
- Coverage gap: CanInterpretShader=false on texture-fetch VS - the counted fraction is a lower
  bound over interpretable draws only.

## Honest caveats
The counter is a DECISION instrument, not a speedup - it will not move framerate; its only
value is the droppable-fraction number that decides whether to spend a device A/B on the cull.
It is an APPROXIMATION (must reproduce the W/NDC + Z mapping; cannot model the guard-band, so
its frustum count under-counts) with partial coverage. Even a large droppable fraction does NOT
de-risk the cull's correctness - the cull stays HOLD-for-device. Pre-transform/compaction do
not reduce the floor and must not be blurred into "Front B progress".

Source: wf_23b97622-da6 (full output in the task temp file). Cited file:line verified in-repo.

---

## C2/C3 DE-RISKED (2026-06-01): mirror DrawExtentEstimator (proven in-repo template)
C1 scaffolding shipped (afa40d932). The concern that the VS-position replay would be a
rabbit hole is RESOLVED: src/xenia/gpu/draw_extent_estimator.{h,cc} ALREADY does exactly the
per-vertex VS replay this counter needs, and is the template to mirror:
- PositionYExportSink : ShaderInterpreter::ExportSink (draw_extent_estimator.h:42-64) captures
  position_y/position_w/point_size/vertex_kill from the position export. C2 = a sink capturing
  FULL clip-space x,y,z,w (extend the same Export() override).
- The vertex loop (draw_extent_estimator.cc:171-246): SetShader(vertex_shader);
  SetExportSink; for each i in vgt_draw_initiator.num_indices: read vertex_index from the index
  buffer (16/32-bit, GpuSwap(index_endian) & 0xFFFFFF, reset-index via multi_prim_ib_ena,
  +index_offset clamped to [min,max]); temp_registers()[0]=float(vertex_index); Execute();
  honor vertex_kill; perspective divide x,y,z by position_w when !pa_cl_vte_cntl.vtx_xy_fmt.
  CanInterpretShader gate already handled (returns false for texture-fetch VS -> count 0).
- Index buffer CPU pointer + reg setup: mirror draw_extent_estimator.cc:~95-170.

### Precise C2/C3 plan (each gated gpu_trace_cullable_tris, read-only, build-verified)
- C2: add a ShaderInterpreter + a full-position CullExportSink (or add a CountCullable method to
  DrawExtentEstimator, which already owns the interpreter + setup - CLEANER, prefer this:
  add DrawExtentEstimator member to the cull path OR a sibling method). Replay per-vertex,
  store NDC positions (x/w,y/w,z/w) into a reusable scratch std::vector for the draw. Return 0
  (positions computed, not yet tested). LOW RISK (count-only, mutates nothing).
- C3: for LIST topology only (kTriangleList; lines/points/rect non-cullable per IsPrimitivePolygonal
  + kRectangleList exclusion), for each triangle (indices 3i,3i+1,3i+2): backface = signed area
  of the 3 NDC xy vs the PA_SU_SC_MODE_CNTL winding/cull bits; frustum = all 3 verts beyond one
  clip plane (conservative, no guard-band). Count would-cull tris -> return. Log via cullable_tris.
RECOMMENDATION: prefer reusing DrawExtentEstimator's machinery over reimplementing the vertex
loop (avoids duplicating the index/vfetch/divide handling, which is the error-prone part).
