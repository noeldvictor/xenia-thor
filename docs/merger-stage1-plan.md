# Draw-merger Stage 1 — DESIGN FLAW FOUND in code-reading; re-aim to Approach A

Status: scaffolding edited (cvar define/declare, header members, frame-open invalidation — all
gated, default-off, harmless), cache logic NOT wired. Found a likely-fatal flaw BEFORE building.

## The flaw (found by reading the real code, confirmed by grep)
The vetted Stage-1 was: cache the constants descriptor set, reusing it when the 5
`VkDescriptorBufferInfo` (current_constant_buffer_infos_) are byte-identical to the last set.
But:
- The constants descriptor-set write path (vulkan_command_processor.cc ~5853) only runs when the
  constants VALUE bit is clear.
- The ONLY place that clears the constants value bit is the constant-write block at 5548-5551, and it
  clears it precisely when a constant buffer was re-`Request()`ed (grep confirmed no other clear site
  besides frame-open).
- `uniform_buffer_pool_->Request()` returns a FRESH ring offset on every call. So whenever the set is
  rewritten, at least one `current_constant_buffer_infos_[i].offset` is NEW → the byte-identical
  signature CANNOT match the previous frame's.
=> The buffer-info-signature cache hit rate is ~0. (Contrast textures: image-view handles are STABLE,
so the texture-info signature genuinely recurs → that cache hits. The analogy breaks for constants.)
Also: when constants DON'T change, the existing up-to-date tracking ALREADY skips the alloc+write+bind
(the `if (!(values & constants_bit))` at 5853 is false). So there is no redundant work left for a
buffer-info cache to remove. descriptor_binds≈2148/frame means the guest genuinely re-uploads
constants (per-object transforms) almost every draw, each to a fresh offset.

## Correct conclusion
To cut per-draw constants binds for GENUINELY-CHANGING constants, the buffer-info cache is the wrong
tool. The real options:
- **Approach A — dynamic uniform-buffer offsets** (the workflow's first idea, deferred as "risky"):
  one big per-frame constants buffer, bind the constants set ONCE with
  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, vary only the dynamic offset per draw. This removes the
  per-draw descriptor write+bind for changing constants. Cost: pipeline-layout-compat change (set-1
  binding type → _DYNAMIC) → recreate all pipeline layouts + guest pipelines; not live-toggleable
  per-draw (needs pipeline teardown). Must be built supervised + staged, validated under
  VK_LAYER_KHRONOS_validation (dynamic-offset alignment vs minUniformBufferOffsetAlignment).
- **Approach B — constant DATA-hash dedup**: hash the actual constant bytes; if identical to a recent
  upload, reuse that buffer region (and thus the same VkDescriptorBufferInfo → the cache would then
  hit). Catches "same values re-uploaded to a new offset" (over-invalidation). Cheaper than A, no
  layout change, but adds a per-draw hash + a small region cache. Worth measuring how often values
  actually recur first.

## Honest caveat carried from the adversary
Even if a constants-bind cut works, the scene is GPU-bound on geometry/binning (B60), and gpu_frame_us
is super-linear in draw/geometry count, NOT in descriptor binds. A descriptor_binds drop with FLAT
gpu_frame_us is the likely outcome. So constants-bind optimization may be a CPU-command-thread win
(draw_cpu_bindings_ns_) with little GPU/fps movement. The actual GPU per-draw cost is the
CmdVkBindIndexBuffer + draw command + Adreno context roll, which neither A nor B removes. That points
back to true draw MERGING (fewer draw commands) as the real GPU lever — the hard multidraw-indirect /
instancing path.

## Next action (for the loop / next supervised turn)
1. Decide: measure Approach B's potential first (cheap instrumentation: count how often the constant
   DATA actually recurs frame-to-frame) BEFORE building A or B. If values rarely recur, neither helps
   and the only lever is draw-command merging.
2. Keep the gated scaffolding (vulkan_cache_constant_descriptors cvar + members) OR revert it — it
   compiles and is default-off/no-op. If kept, wire it only as a measurement of hit rate to PROVE the
   flaw on-device (expected: ~0 hits, descriptor_binds unchanged), then leave default-off.
3. Do NOT ship a "win" here without an evidence file showing a real descriptor_binds drop AND
   pixel-identical A/B. Per evidence-discipline.
