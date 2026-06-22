# Extreme-novel research (2026-06-22, workflow wm9qcuaon) — the BD overdraw floor is REOPENED

3-track research (arxiv + FULL Turnip/Mesa + NEON), synthesis verified key claims in-tree. Headline: the
project's "BD overdraw floor is a740-irreducible because alpha-test OpKill defeats LRZ" verdict is likely a
CONFOUND, and the floor is reopened. Full output: tasks/wm9qcuaon.output.

## THE finding — LRZ is structurally dead BEFORE alpha-test matters
Turnip disables LRZ when EITHER: (a) the depth attachment has no LOAD_OP_CLEAR at pass start, OR (b) depth is
cleared mid-pass via vkCmdClearAttachments. **xenia trips BOTH on every pass** (the EDRAM load/clear model):
- VERIFIED in-tree: `LOAD_OP_CLEAR` has ZERO occurrences in src/xenia/gpu/vulkan/ — depth/color loadOp is only
  LOAD or DONT_CARE (vulkan_render_target_cache.cc:1700-1744); depth is cleared mid-pass via vkCmdClearAttachments.
- VERIFIED: the FS emits spv::OpKill (spirv_shader_translator_rb.cc:465, gated on kills_pixels()).
- Turnip: a discard FS only forces TU_LRZ_FORCE_DISABLE_WRITE (late-Z, can't WRITE LRZ) — it does NOT set
  FORCE_DISABLE_LRZ. So foliage can still be EARLY-REJECTED against a VALID LRZ.
=> "alpha-test killed LRZ" measured a pass where LRZ was ALREADY dead for the load/clear reason. The
LRZ-feedback attempt (bounded -8.9%) kept OpKill AND ran on an LRZ-dead pass, so it's not disproof.
a740 has the most capable LRZ HW Adreno ships (has_lrz_feedback + has_lrz_dir_tracking + enable_lrz_fast_clear)
yielding the project ZERO value today.

## Ranked NOVEL levers (impact x feasibility)
1. ⭐ **Restore Turnip LRZ validity (render-pass restructure) — THE NEW BIG LEVER.** Make the heavy depth pass
   use LOAD_OP_CLEAR at pass start (not LOAD + mid-pass ClearAttachments), one compare direction, then resurrect
   the PARKED opaque depth pre-pass on a now-LRZ-valid pass -> foliage/alpha-test draws early-reject occluded
   layers (raster+depth+shade+sample ALL skipped). ELIMINATES fragments (vs VRS/FDM which cheapen them) ->
   stacks multiplicatively. MEDIUM, use-feature (no driver patch), prepass machinery exists parked. Honestly
   bounded by how much BD overdraw is genuinely OCCLUDED vs co-planar/blended (blended=34%, LRZ can't help).
   GATE (cheapest highest-info test in the report, device-only no code): TU_DEBUG=perf -> grep "Disabling LRZ
   because '<reason>'" (tu_lrz.cc:90) names the exact disabler; TU_DEBUG=nolrz vs default A/B -> if flat, LRZ is
   already dead = thesis confirmed. Build: loadOp LOAD->CLEAR for the heavy depth pass (vulkan_render_target_
   cache.cc:1737) + drop mid-pass depth ClearAttachments + single direction + re-enable parked prepass; cvar-
   gated, screenshot-validated.
2. **FDM (VK_EXT_fragment_density_map)** — confirmed a740 HW backing (has_hw_bin_scaling = bin-merge HW;
   FDM offset/scale regs so FDM+LRZ COEXIST = validates the "_offset a740 LRZ-space" memory note). Cheapens the
   per-fragment cost LRZ can't remove (co-planar/blended). MEDIUM ~1wk, device gate GREEN. Layers under #1.
3. **SGSR2 (Snapdragon GSR 2)** — render BD's 3D below native -> reconstruct with Qualcomm's Adreno-tuned
   temporal upscaler. FS variant ~0.9-1.0ms at 2.0x (8 Gen 3). VERY HIGH feasibility: BSD-3 public GLSL
   (github.com/SnapdragonGameStudios/snapdragon-gsr), built for this GPU. Inputs color+depth+MV = the
   SetupFastAffineReplay plumbing (shared with frame-gen). SGSR1 (color-only) = even-lower-risk first cut.
   Composite HUD at native AFTER upscale.
4. **Mob-FGSR (SIGGRAPH 2024, NN-free)** — supersedes GFFE as the reference for the in-flight presenter
   frame-gen build. Depth-aware motion splatting from 2 frames -> warp+blend/extrapolate + LUT-SR. Pure shader
   math (no NPU/INT8 risk), 8 Gen 3-targeted. ~2.3x perceived. Same affine-MV caveat already accepted.
   mob-fgsr.github.io.
5. **alpha-test -> alpha-to-coverage / hashed-alpha (Wyman&McGuire I3D2017)** — orthogonal crack at the floor:
   convert OpKill foliage to A2C (opaque-with-coverage-mask, NO discard) -> has_kill false -> LRZ early-Z fully
   restored for the cutout layers. Shader-translator change (emit A2C/hashed instead of OpKill) + MSAA. Helps
   only opaque-cutout foliage (not blended 34%); needs pixel-correct threshold validation. Gate: TU_DEBUG=lrz,
   does removing OpKill flip has_kill/LRZ-on?
6. **Parallel PM4->DeferredCommandBuffer translation on idle mid-cores (Burnout CPU lever, BD-irrelevant)** —
   split the draw stream into N segments, translate per-core into thread-local DeferredCommandBuffer, replay in
   order. ~+15-30% Burnout IF the independent-record fraction dominates shared-cache-mutation (the crux risk;
   hoist mutations serial). NOT the parallel-JIT deadlock (GPU translate never takes the code lock). Gate:
   profile the record-vs-mutation split on one frozen Burnout frame.

## Best NEXT build: #1 (restore LRZ validity). Gated by the TU_DEBUG=perf/nolrz probe (1 device fire, no code).

## DEAD / do-not-re-propose (validated this sweep)
visibility-buffer/deferred-texturing (needs renderer re-arch, can't touch alpha-test/blended); GPU-driven
clustered culling (binning ~5% of BD); async-spacewarp (subset of Mob-FGSR); learned-rule DBT / SIMD-widening
(NEON = same 128-bit as VMX128; only big DBT lever = cross-block flag elision = the proven-unsafe wall, though
in-block dead-CR elimination is a cheap safe check); Hexagon/HVX offload (FastRPC blocking, nothing batchable
on the CPU critical path); TU_DEBUG=sysmem (black-screened; cheap re-check on the changed EDRAM path).
