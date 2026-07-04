# NATIVE RENDER PATH — the BD GPU-pipeline REARCH (not a lever)

**WHY (user-mandated 2026-07-04, "NO MORE INCREMENTAL LEVERS, REARCH"):** BD's ~15-20× emulation gap is a
COMPOUNDING STACK of ~2× emulation taxes, not one bug. Every single-layer lever (native-fetch, blend-bind
collapse, resolution, EDRAM elision) read FLAT because removing ONE layer leaves the other four dominating.
The rearch removes the WHOLE stack → the compound 10-20× → 30-60fps. **RULE: build + measure the COMPOUND,
never one layer (that's the trap that killed every lever).** The Thor is 10-20× the 360; the 360 hit 30 by
rendering NATIVELY. We render the scene natively too.

## The stack (each ~2×, multiplying)
1. **SSBO in-shader vertex fetch** (vs native HW input assembly) — `gpu_hw_vertex_fetch` BUILT (flat alone).
2. **1272 state-changes/frame** (208 pipeline + 1064 descriptor binds) each context-rolling the TBDR — BINDLESS not built.
3. **EDRAM round-trips across 42 render passes** (vs GMEM-resident single pass) — RT-cache exists, not the native path.
4. **Xenos→SPIR-V shader bloat** (~1.5×) + fp32 where fp16 suffices — Adreno FP16 unused for foliage.
5. **Format/bandwidth waste** (fp16 64bpp color, no native format match) — quality-option fp10 only.

## Build order (each brick STACKS on the prior; measure the compound at each stack point)
- **BRICK 0 ✅ native vertex input** — `gpu_hw_vertex_fetch` (buffer bound as VkBuffer, native shader attrs via
  kSysFlag_HwVertexFetch, fixed-function VkVertexInputState in the pipeline). Exists; keep, don't re-measure alone.
- **BRICK 1 🔨 BINDLESS resources** (descriptor_indexing + buffer_device_address, both device-confirmed) — ONE
  global texture array (runtimeDescriptorArray + partiallyBound + VARIABLE_DESCRIPTOR_COUNT), bound ONCE/frame;
  shaders index it by push-constant instead of per-draw descriptor sets. Collapses the 1064 descriptor binds →
  ~0 AND removes the per-texture pipeline variance → fewer of the 208 pipeline binds. Current per-draw path to
  replace: GetTextureDescriptorSetLayout (vulkan_command_processor.cc:4545) + the per-draw CmdVkBindDescriptorSets
  (:3615) built from pixel_shader->texture_bindings() (:5130). Gate: `gpu_native_render_path` (compound cvar).
- **BRICK 2 native render targets** — render the scene to a GMEM-resident image via dynamic_rendering +
  dynamic_rendering_local_read (device-confirmed) once, read composites in-place; bypass the EDRAM tile round-trip
  across the 42 passes. Build on the RT cache.
- **BRICK 3 lean fp16 shaders** — Xenos→SPIR-V with RelaxedPrecision/fp16 for the foliage fragment path (Adreno
  native FP16 throughput) + drop the bloat.

## Measurement discipline (the anti-lever rule)
Enable BRICKS 0+1 TOGETHER, then 0+1+2, then all — measure the COMPOUND frame time + OSD fps at each stack point,
never a brick alone. One-brick A/B is meaningless here (the stack hides it). Single-run, GPU-busy field, screenshot-
correct. The compound is the only honest signal.

## Status
- BRICK 0: shipped (default-off), FLAT alone.
- BRICK 1: BUILT + DEVICE-TESTED 2026-07-04 (gpu_native_render_path). Renders PIXEL-PERFECT (bindless
  architecture correct, no crash) BUT **REGRESSED: 129→161ms / 7.7→6.2fps, and descriptor_binds UNCHANGED
  (~1074), pipeline_binds UNCHANGED (208).** Decisive finding: **the 1064 descriptor binds are NOT texture
  binds — they are set-0 (shared-mem) + set-1 (per-draw CONSTANTS), inherent per-draw data bindless can't
  touch; the 208 pipeline binds are shader/render_pass variants. So the "1272 state-change tax" is NOT
  collapsible via bindless textures** — bindless removed the small texture-bind slice + ADDED push-constant/
  indirect overhead → net regression. Compound (BRICK0+1) REGRESSES.

## 🚨 THE REARCH PREMISE IS REFUTED BY THE FRAME ANATOMY (2026-07-04)
The driver u_trace already said BD's frame = **~90% fragment/draw EXECUTION (the intrinsic foliage rendering),
~6ms EDRAM structure, ~1ms tile-I/O.** BRICK 1 confirms it from the other side: the binds/structure the rearch
removes are only a few ms; there is NO 15-20× of *emulation structure* to remove — the frame is dominated by
the actual RENDERING WORK (262k foliage verts + heavy alpha-test overdraw fill). **Structure-rearch (bindless,
native RT, native input) CANNOT reach 30 — there is only ~7ms of structure to reclaim, and bindless even that
regresses.** The "compounding stack of emulation taxes" model was WRONG: the taxes sum to ~2-3×, not 15-20×;
the rest is intrinsic. The 10-20× hardware figure is PEAK on friendly workloads — BD's alpha-test overdraw
foliage defeats the GPU's early-Z/HSR (every layer shaded), so the Adreno's EFFECTIVE advantage for THIS
workload is ~2-4×, which the emulation's ~2-3× eats. The 360 hit 30 via developer hand-tuning for the Xenos.
⇒ **BD-30 full-foliage NO-gfx-loss is NOT reachable** by rearch OR lever — the rendering work is intrinsic +
near the Adreno's effective limit. Shipped 19.8 (lossy fp10/VRS) is the practical ceiling; 30 needs reduced
foliage (quality option). BRICK 2/3 (native RT ~6ms, lean fp16 shaders ~1.5×) only chip the ~2-3× emulation
slice, not the intrinsic bulk. Bindless kept default-off (correct; may help texture-bind-bound titles).
