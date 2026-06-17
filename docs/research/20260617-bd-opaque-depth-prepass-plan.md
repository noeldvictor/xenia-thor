# BD opaque depth pre-pass — build plan (2026-06-17)

User directive: build the depth pre-pass. Goal: cut Blue Dragon's overdraw cost
(GPU-bound ~800-868ms/frame, per-covered-fragment; see memory bd-real-bottleneck-overdraw).

## Why / expected payoff (honest)
BD draw composition (device-measured, Unit 1 classifier, aa5cf6965): of ~2159 draws/frame,
**opaque=240 (11%, ~73k/290k verts ≈ 25%), alphatest=1316 (61%), blended=459 (21%)**.
A classic depth pre-pass directly helps only opaque draws. BUT the 240 opaque draws are large
(~304 verts/draw = terrain/buildings) and likely OCCLUDE much of the alpha-test foliage behind them.
Mechanism: prime opaque depth FIRST, then the full stream's fragments (incl. alpha-test) early-Z-REJECT
where behind opaque geometry (depth-test reject works even with discard, as long as the alpha-test draw
does not depth-WRITE). Payoff is scene-dependent + UNCERTAIN (not the clean 2-10x of opaque-heavy
overdraw). Cheap-confirmed alternative win: RT-transfer coalescing (~20%, measured 868->696ms).

## Architecture (xenia Vulkan command processor)
- DeferredCommandBuffer (deferred_command_buffer.cc): linear command_stream_ recorded then Execute()'d
  into a real VkCommandBuffer at submit. Has command_stream_size_elements() cursor + RecordStats.
- Render pass lifecycle in vulkan_command_processor.cc: EnterRenderPass (~3650), CmdVkBeginRenderPass
  (~3688), per-draw CmdVkBindPipeline (~4615) + binds + CmdVkDrawIndexed (~5752/5763), EndRenderPass
  (~3706 CmdVkEndRenderPass). Draws are recorded IMMEDIATELY into deferred_command_buffer_ in guest order.
- TBDR note: a same-render-pass prepass (opaque-depth-only draws THEN color draws, one pass) keeps depth
  in GMEM (no resolve). A two-render-pass version costs only ~1 depth store+load (~0.15ms) = negligible
  vs 800ms, so EITHER ordering is acceptable; same-pass is cleanest.

## Unit plan
- [x] UNIT 1 - draw classifier (opaque/alphatest/blended counters + comp[] log). SHIPPED aa5cf6965.
- [ ] UNIT 2 - depth-only pipeline variant: for an opaque draw's pipeline, a variant with
      colorWriteMask=0 on all RTs, depth-write ON, depth-test as-is, same VS + (ideally) no PS / minimal
      PS. Key the pipeline cache on a "depth_only" bit. (vulkan_pipeline_cache.cc ~2281-2306.)
- [ ] UNIT 3 - opaque-draw capture: during a render pass with the prepass cvar on, when an opaque draw
      is issued, record its DEPTH-ONLY commands (bind depth-only variant + vertex/index/descriptor binds
      + draw) into a SECOND DeferredCommandBuffer (prepass_command_buffer_). Needs all per-draw replay
      state, which is already in scope at issue time.
- [ ] UNIT 4 (crux) - stream splice ordering: emit, within one render pass, [BeginRenderPass]
      [prepass_command_buffer_ commands][main color stream][EndRenderPass]. Approach: buffer the render
      pass's COLOR draw commands into a side buffer too, and at EndRenderPass splice
      main_stream += begin; main_stream += prepass; main_stream += color; main_stream += end. Requires a
      DeferredCommandBuffer::AppendFrom(other) primitive (concatenate command_stream_ + arg bytes; verify
      commands are position-independent / self-contained so concatenation is valid). ALT: two render
      passes (prepass pass with depth store, main pass with depth LOAD) - simpler ordering, ~0.15ms extra
      GMEM, avoids AppendFrom but needs the opaque set buffered before pass 1 (still deferral).
- [ ] UNIT 5 - main-pass depth state + gate + validate: ensure opaque draws in the main pass use
      depth-test that benefits (LEQUAL/EQUAL), alpha-test/blended draws keep depth-WRITE off so early-Z
      reject engages. cvar gpu_opaque_depth_prepass (default off). Pixel-validate BD (no holes/z-fight),
      A/B gpu_frame_us at matched guest_ms. KILL if it doesn't beat ~5%.

## Risks
- Correctness: depth precision / z-fighting between prepass and main (use EQUAL or exact same depth
  state); guest depth convention (z direction). Alpha-test draws must NOT depth-write in main pass or
  they'd occlude each other wrongly. Blended draws unaffected (order-locked).
- Payoff uncertain (61% alpha-test). Gate hard on Unit 5 A/B; do not default-on unless device-proven.
- AppendFrom must preserve command validity (position-independence) - verify in deferred_command_buffer.

## Validate
BD heavy field, matched guest_ms (use gpu_freeze_at_guest_ms to kill cross-fire scene noise), compare
gpu_frame_us + VdSwap fps prepass off vs on. Pixel-correctness screenshot. Target: meaningful drop from
~800ms; the one number = gpu_frame_us.
