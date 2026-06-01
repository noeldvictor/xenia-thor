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
