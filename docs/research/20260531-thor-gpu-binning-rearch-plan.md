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

---

## Implementation status + RECONNECT A/B PROCEDURE (updated 2026-06-01)

Built BLIND while the Thor was disconnected. All gated, default-off, build-verified
(BUILD SUCCESSFUL + cvar/func strings linked in arm64 libxenia-app.so), and the
default path is provably bit-identical (each cvar short-circuits first). NONE has run
on hardware yet — the reconnect A/B below is the gate before trusting/shipping any.

### Done (committed)
- Lever 0 `590084fa9` — `gpu_merge_vf_index_stride_fix` (measurement-only stride-correct
  merge_vf contiguity classifier).
- Lever 1a `5cc773169` — EDS command infrastructure (vkCmdSetCullMode/FrontFace/
  DepthTestEnable/DepthWriteEnable/DepthCompareOp/StencilTestEnable/StencilOp loaded
  via functions/device_1_3_ext_extended_dynamic_state.inc + DeferredCommandBuffer
  record/replay).
- Lever 1b `50c385d52` — `vulkan_dynamic_state_cull_front` (cull mode + front face).
- `73e8d9050` — `vulkan_dynamic_state_depth` (depth test/write/compare).
- `c419b06b7` — `vulkan_dynamic_state_stencil` (stencil test enable + front/back ops).

Each EDS field: key-zeroed in GetCurrentStateDescription (after the real bake) + appended
to the dynamic-state array in EnsurePipelineCreated (now sized 14) + emitted in
UpdateDynamicState reproducing the exact baked value, with cmd-buffer-reset and
external-pipeline-bind re-emission. All gated `cvar && apiVersion>=1.3` (depth/stencil
also `&& !FSI`).

### DEFERRED to post-reconnect (need on-device verification, not safe to do blind)
- Primitive **topology** promotion (HIGH value for Blue Dragon: ~931 triangle-list +
  ~266 strip = same triangle class). Risky blind: intertwined with geometry-shader
  selection (rect/quad lists) + topology-class normalization + the emit site
  (UpdateDynamicState) lacks host_primitive_type (needs a signature change). Do it with
  the device attached so the class handling is verifiable on real frames.
- Lever 2 (zero-copy draw concatenation) — gated on measuring that EDS actually
  lengthened same-pipeline runs (merge_run_hist_); building it before that risks the
  "finds ~0 eligible runs" dead end the binning verdict warned about.
- Lever 3 (NEON/i8mm/bf16/dotprod/fcma CPU pre-transform, Front B).

### TURNKEY RECONNECT A/B (do this first when the Thor is back + cool)
Per never-thrash: confirm temp<60C + idle before launch; use tools/thor/thor_evidence.ps1
(thermal-guarded). The EXISTING merge_run_hist_ instrumentation directly measures EDS
effectiveness (it counts consecutive same-VkPipeline draws — EDS should shift it to
higher buckets), so no new code is needed.
1. BASELINE (cvars off): capture the heavy field scene, freeze content at a fixed
   guest_ms (gpu_freeze_at_guest_ms) so the frame is identical across configs, with
   `--ez vulkan_trace_draw_outcomes_per_frame true`. Read merge_run_hist_, pipeline-bind
   count, gpu_frame_us, fps, + the screenshot.
2. EDS-ON: same launch + `--ez vulkan_dynamic_state_cull_front true --ez
   vulkan_dynamic_state_depth true --ez vulkan_dynamic_state_stencil true` (+ optionally
   `--ei gpu_merge_vf_index_stride_fix 1`). Match the SAME guest_ms.
3. Compare: merge_run_hist_ should move toward longer runs and pipeline binds should
   drop if EDS collapses pipelines; gpu_frame_us should fall if per-draw context-roll
   cost was real. READ both screenshots — rendering MUST be identical (EDS only moves
   state from static to dynamic; any visual difference = a state-reproduction bug in the
   emit path, which is where to debug). Confirm the cvar actually took effect in logcat
   (stale device xenia.config.toml can override).
4. If EDS-on renders identically AND lengthens runs / cuts gpu_frame_us: ship the cvars
   on in the Blue Dragon profile, then build topology + Lever 2 on top (now with run-
   length data). If runs DON'T lengthen: EDS alone won't enable concatenation — the
   state churn is elsewhere (constants), revisit the binning verdict's risk note.
