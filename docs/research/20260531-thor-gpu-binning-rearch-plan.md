# Thor / Adreno-740 GPU binning re-architecture plan (2026-05-31)

Deep theoretical re-arch for the AYN Thor (Adreno-740v2 TBDR + Snapdragon 8 Gen 2),
produced by a 20-agent design+adversarial-verify workflow while the device was
disconnected. Goal: cut Blue Dragon's proven GPU-bound heavy-scene cost; exploit
UMA + advanced CPU acceleration math (NEON/i8mm/bf16/dotprod/fcma). Every lever is
cvar-gated, reversible, build-verifiable; device A/B deferred until Thor reconnects.

## Binning-cost verdict: MIXED (reasoned from the proven signature, not a profiler)
The heavy-scene cost decomposes into two independent terms, both visible in the
proven on-device signature (gpu_frame_us SUPER-LINEAR in BOTH draw count AND vertex
count, corr ~0.88, and pixel-INDEPENDENT; fill 0%, EDRAM ~3%, breaks ~12%, CPU ~84% idle):

1. **Per-DRAW fixed cost — COALESCABLE.** Context roll / bin restart / state re-latch
   on the TBDR front-end, paid once per vkCmdDraw regardless of size. Fingerprint =
   the *independent* super-linearity in DRAW count. The ~1200-2148 tiny draws/frame
   (avg 3-136 verts) make this a large share, because a fixed per-draw cost is a big
   fraction of a 3-vert draw. This is what merge_run_hist_ / merge_vf_ were built to size.
2. **Per-TRIANGLE binning floor — NOT coalescable.** The binning pass position-shades
   every vertex and writes every primitive into every tile it covers. Fingerprint =
   super-linearity in VERTEX count + fill-independence. Draw concatenation does NOT
   reduce it (each triangle still bins once) — it is a hard floor.

**Why mixed, not pure either:** a pure per-triangle model can't explain independent
super-linearity in draw count at fixed vertex count; a pure per-draw model can't
explain super-linearity in vertex count. Both terms are independently present.

**Instancing is confirmed DEAD:** it re-bins identical triangles N times on a TBDR →
the dominant per-triangle term is untouched; only host draw count drops (irrelevant,
CPU is idle). The math signature is exactly why.

## Deep re-arch: a TWO-FRONT attack, UMA + CPU-math as connective tissue
- **FRONT A — kill the per-draw fixed cost.** Promote the pipeline-key state that
  forces an Adreno context roll (cull/front-face, primitive topology/restart, depth
  test/write/compare, stencil enable/ops via EDS/EDS2; per-RT blend via EDS3) to
  Vulkan **dynamic state**, so shader-identical draws collapse onto ONE VkPipeline →
  fewer context rolls AND longer same-pipeline runs → then **zero-copy draw
  concatenation** of consecutive same-state, byte-contiguous index ranges over
  `shared_memory_->buffer()` into one vkCmdDrawIndexed.
- **FRONT B — lower the per-triangle floor (what coalescing structurally can't touch).**
  Use the ~84%-idle Cortex-X3/A715 + NEON/i8mm/bf16/dotprod/fcma to pre-transform/skin
  guest VMX128/PPC vertex math on the CPU and hand the GPU fewer/bigger, more
  contiguous geometry batches via UMA (HOST_VISIBLE|DEVICE_LOCAL) — reducing per-vertex
  binning-shader cost AND manufacturing the contiguity that makes Front A applicable to
  previously-scattered draws. UMA's *present* path stays OFF (device-verified hang,
  B74); UMA is used here only as a CPU→GPU geometry feed.

## Sequenced roadmap (each cvar-gated, reversible, build-verified; A/B on reconnect)
- **Lever 0 — DONE (this commit): stride-correct merge_vf contiguity measurement fix**
  (`gpu_merge_vf_index_stride_fix`, default 0, measurement-only, bit-identical when off).
  Fixes the byte-address-vs-vertex-count bug in the contiguity classifier
  (vulkan_command_processor.cc ~3960) and classifies only kGuestDMA sources, so the
  merge_vf_/merge_run_hist_ counters that gate the whole roadmap report trustworthy
  numbers when first read on device.
- **Lever 1 — EDS/EDS2 dynamic-state promotion (the real GPU-work reducer + Lever-2
  prerequisite).** Feature-detect+enable VK_EXT_extended_dynamic_state(2) (core in VK
  1.3), grow the dynamic-state array (vulkan_pipeline_cache.cc), zero the promoted
  fields in GetCurrentStateDescription so topology/cull/depth-stencil variants collapse
  to one pipeline key, emit CmdVkSet* at the UpdateDynamicState site
  (vulkan_command_processor.cc ~5051), each field its own cvar.
- **Lever 1b — EDS3 blend promotion** (feature-gated; Adreno-740 support uncertain).
- **Lever 2 — zero-copy draw concatenation** (depends on Lever 1 producing runs >1).
  Defer CmdVkDrawIndexed, accumulate firstIndex/indexCount across a run passing the
  FULL same-state predicate. **Correctness graft (adversary):** the predicate MUST also
  require identical `vertex_base_index` (VGT_INDX_OFFSET), `vertex_index_load_address`,
  and `vertex_index_endian` (the in-shader vfetch depends on them) or vertex addressing
  silently corrupts. Flush the run on ANY state/render-pass break, frame/submission end,
  or IssueCopy. Also add a last-bound-IB cache (kills the unconditional
  CmdVkBindIndexBuffer rebind — CPU hygiene only, not a GPU lever).
- **Lever 3 — CPU advanced-math pre-transform (Front B; highest risk, last).** NEON/
  i8mm/bf16/dotprod/fcma skinning/transform of guest geometry into a UMA buffer.

## Hard limits / honest uncertainties
- Max concatenation win = the per-DRAW component's share. The per-TRIANGLE floor is NOT
  recoverable by any draw-count reduction — do not promise frame-time ∝ draw-count cut.
- The per-draw vs per-triangle SPLIT is reasoned (medium confidence on magnitude), not
  measured — the gating counters are coded but unmeasured (device disconnected). Lever 0
  exists to size it on reconnect.
- merge_run_hist_ may be length-1-dominated (constants churn ~every draw); if so Lever 2
  finds ~0 eligible runs until Lever 1 lengthens runs — Lever 2 is NOT standalone.
- No GPU effect or frame correctness is verifiable now; all Lever 1-3 GPU claims are
  predictions to validate on content-matched frames after reconnect. Only Lever 0's
  zero-effect + clean-link is verifiable today (it is).
- Stale device xenia.config.toml can silently override new cvar defaults — confirm the
  value took effect in logcat before trusting any A/B.

Source: workflow run wf_8facd5a2-4cc (binning verdict + 5 designs + 5 adversarial
verdicts + synthesis). Script: tools/thor/wf_gpu_rearch.mjs.
