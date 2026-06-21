# Extreme-novel speedup roadmap (2026-06-20) — the MAJOR levers the conservative hunt missed

39-agent arxiv + Turnip-internals + NEON workflow (3M tokens). The prior "zero survivors" hunt was wrong
because it audited *extensions vs current bottleneck*; this sweep found (a) half-built in-tree machinery and
(b) the structural insight that you attack a TBDR by moving work *before* binning or *below* full resolution.
Re-grounded against the actual tree. KEY framing correction (mine): the coverage-collapse A/B (868->39ms = 22x)
proves BD is **~95% per-covered-fragment overdraw**, ~5% binning. The 360's cheap alpha-test foliage maps onto
Adreno's worst case: xenia emits `OpKill` (spirv_shader_translator_rb.cc:465) -> Turnip `has_kill` -> LRZ off
-> every overlapping foliage+blended layer fully rastered/depth-tested/shaded. THAT is the major lever domain.

## ⭐ #1 MAJOR LEVER — Fragment Density Map (FDM) foliage. GATE GREEN, ~1-WEEK PATH CONFIRMED.
Renders overdraw-heavy screen regions at LOWER RESOLUTION -> cuts raster+depth+sample+texture per fragment
(the actual 95% floor), unlike the shipped VRS which only coarsens shader invocations. STACKS on VRS.
- **Device-confirmed (commit 02c8194fb):** Turnip enumerates `VK_EXT_fragment_density_map` + `_offset` +
  QCOM/VALVE variants. Boot audit: `fragmentDensityMap=1 nonSubsampledImages=1 dynamic=0`.
- **nonSubsampledImages=1** => attach a density map to the EXISTING render targets (~1wk), NOT subsampled-RT
  rearch (~1mo). **_offset = A740 "LRZ-space"** => FDM and LRZ COEXIST on this silicon (a6xx couldn't; a740
  added offset/scale regs so viewport/scissor/LRZ run in LRZ-space, rasterization in render-space).
- **BUILD (next unit):** (1) request VK_EXT_fragment_density_map(+_offset) + enable the feature in
  vulkan_device.cc (mirror the VRS/ROAA pattern); (2) create a small R8G8_UNORM density image (1 texel /
  ~32x32 region); (3) attach via VkRenderPassFragmentDensityMapCreateInfoEXT on the main render pass + the
  FDM image-usage/layout on the RTs (vulkan_render_target_cache.cc); (4) AUTHOR density: low (~0.5-0.75) over
  the gameplay viewport, FULL (1.0) over HUD/UI regions (dynamic=0 so a static/per-frame-uploaded map; HUD is
  usually a separate pass or screen-edge -> keep full). (5) cvar-gated default-off; validate via the single-run
  gpu_freeze_ab_alternate_vrs-style validator on BD field + BTTF. Expect > the VRS -22% (adds raster/depth/
  sample on top of shading). dynamic=0 => map can't be patched mid-pass; upload per-frame or use a static map.
- COMBO B (later): author the density map from the cull's per-draw screen-space AABBs (exact foliage regions).

## #2 GPU compute pre-cull -> compacted index -> indirect draw (PRE-binning). Cheap derisk.
The one binning lever a TBDR can't beat post-binning: a compute dispatch rewrites the index buffer BEFORE the
draw, so culled verts never reach Turnip's binning-VS clone. Source: Imagination GPU-Controlled Rendering
(mobile TBDR tiler 8.73->4.55ms ~48%). All primitives exist in-tree (CmdVkDispatch, CmdVkDrawIndexedIndirect
Count, cull_index_buffer_pool_, shared-mem vertex SSBO, SetupFastAffineReplay). **Honest:** binning is ~5% of
BD's frame, so this is SMALL on fragment-bound BD but LARGER on geometry-bound titles (BTTF 378k verts).
DERISK (1 fire): upgrade the existing read-only cullable-tri counter to the fast-affine replay path; log the
cullable-vert fraction on BD's heavy field. Kill if <25%.

## #3 LRZ-feedback revival for alpha-test foliage. Overdraw. Gated on a740 has_lrz_feedback.
Turnip LRZ feedback lets a discard+depth-write draw feed low-res Z DURING the pass (LRZ_FEEDBACK_ZMODE_MASK)
so later foliage layers depth-reject — no global prepass-composition (why the killed depth-prepass couldn't).
A7xx LRZ is bidirectional + GPU-tracked (web-confirmed). GATE: grep Mesa freedreno_devices.py has_lrz_feedback
for a730/a740 (GitLab bot-blocked this session; retry via a mirror or device TU_DEBUG=lrz). Bounded by
inter-foliage occlusion (only rejects SUBSEQUENT draws). Shares the LRZ-space hardware story with #1-FDM.

## #4 Finish the ROAA transfer-elision stub. Bandwidth. Small-medium.
`edram_roaa_` is set (vulkan_render_target_cache.cc:377) but read NOWHERE — a live unconsumed stub
(roaa_color=1/roaa_depth=1 confirmed). Wire it into the resolve/transfer site = read prior RT via input
attachment, skip the EDRAM ownership copy (~9ms/~22% of a BTTF resolve). A/B on a BTTF-class resolve-bound
title (NOT heavy BD — pass-reduction measured fps-neutral there). Skip the from-scratch FSI-ROP-to-ROAA rewrite.

## #5 UBWC-forced RT compression. Bandwidth. TRIVIAL build, small win.
RT cache sets VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT (vulkan_render_target_cache.cc:1994) which disables UBWC on
pre-750 Adreno (740 is pre-750). Fix: add a 2-entry VkImageFormatListCreateInfo to the RT image pNext (copy
vulkan_texture_cache.cc:1174-1185) to keep UBWC with mutable format. Derisk (no code): default vs
TU_DEBUG=noubwc capture; if flat, UBWC is irrelevant.

## DROPPED — CPU-JIT cluster (#8-#11): code refutes novelty / project measured the class flat.
PIC-IT (code-cache beyond ARM bl +-128MB; RAS already fed), register-resident superblocks (ALREADY SHIPPED:
arm64_context_value_cache_fallthrough + pinned-r1, a64_emitter.cc:330), leaf memoization (no per-context mem
isolation). One sanity A/B of the existing superblock infra on Burnout, else drop. CPU codegen stays exhausted.

## Build order
1. **FDM (#1)** — the major lever, gate green, ~1wk. BUILD NEXT.  2. #2 pre-cull derisk (1 fire, cheap).
3. #3 LRZ-feedback gate (Mesa source).  4. #4 ROAA stub finish.  5. #5 UBWC (trivial).
Full agent output: tasks/w2kvgfyp7.output. Each ships cvar-gated default-off, validated via the single-run
alternation validator, as a stacking XeniaOptimizations toggle.
