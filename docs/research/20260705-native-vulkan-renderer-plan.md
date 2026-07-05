# BD native D3D9→Vulkan renderer (DXVK-for-360) — build blueprint (2026-07-05)

## Why (RE2 proof + amplification math, all device-measured this session)
RE2 Remake (far heavier) runs 30fps on the Thor via DXVK → the Adreno 740 is NOT the wall; xenia's LLE
emulation is. BD's heavy field = **n=95 render passes** (16 geometry + 35 EDRAM-xfer + 23 resolve + 18
composite); gpu_pass_us≈4ms yet gpu_frame_us≈123ms. The ~119ms is the foliage fragment work AMPLIFIED by the
emulation. The amplification factors (measured/derived), multiplicative ≈ 9×:

| factor | ×    | source | native renderer removes it by |
|--------|------|--------|-------------------------------|
| per-tile re-execution | 2× | BD's 2 EDRAM tiles each re-shade the region | render ONCE into one full-surface RT (no tiling) |
| foliage self-overdraw | ~3× | 43% overdraw + no early-Z reject (discard defeats LRZ + LOAD_OP_LOAD kills it) | clean single depth pass → forced per-pixel EarlyFragmentTests works (gpu_foliage_force_early_z, built) against a foliage-primed depth |
| ir3 shader bloat | ~1.5× | Xenos→ir3 translation | native/optimized SPIR-V (or accept; smallest factor) |
| pass/barrier/xfer overhead | (part of the above gaps) | 79 EDRAM overhead passes | native RT triggers NO ownership-transfer/resolve passes |

Removing tiling (2×) + enabling reject (~3× on the occluded half) + fewer passes → target ~3× → ~40ms → the
VRS/ir3 headroom closes to 30fps. **This is the whole-pipeline conversion; no single factor alone suffices
(proven — every isolated lever is DEAD in the exp_ledger).**

## What (the renderer)
A cvar-gated (`gpu_bd_native_renderer`, default-off) native Vulkan path for BD's field, PARALLEL to the LLE
back-end (LLE stays the correctness fallback / other games):
1. **Capture** at the RE'd seam **0x82489F40** (SubmitRectDraw — ONE call per game draw, PRE per-tile fan-out).
   State in device-ctx **dev+0x2800..0x2938** (bound shaders/RT/blend), dev+0x2F88 surface/viewport, dev+0x3058
   scissor, dev+(binIdx+0xBE2)*4 per-bin surface. Infra exists (decoupled native-HLE capture + CallInThread
   handoff to the CP worker thread).
2. **Own RT**: one persistent full-surface `VkImage` (720×1280-ish, native, NOT EDRAM-backed) + one depth image.
   Allocated once, reused. This is the change render_target_cache.cc:642 warns against doing IN the EDRAM path —
   so do it OUTSIDE it (a dedicated image the native path owns).
3. **Own render pass**: ONE (or few) VkRenderPass, held open across all captured draws (no per-RT-change breaks =
   no xfer/resolve passes). Optional depth-prepass sub-step so EarlyFragmentTests rejects self-overdraw.
4. **Native pipelines**: reuse xenia's Xenos→SPIR-V translator for the shaders, but bind via native
   VkPipelineVertexInputState (real VkBuffer vertex data, NOT the SSBO vfetch) + native VkPipelineColorBlendState
   (hardware ROP). Cache pipelines by state hash (DXVK model).
5. **Present**: blit the native RT to the swapchain (present_decoupled infra exists).

## Build order (implement on DESKTOP **VULKAN** — `xenia.exe --gpu=vulkan` — SAME backend as Turnip, ships direct; then device)
> User mandate 2026-07-05: "use vulkan on desktop". Dev on the desktop Vulkan backend (RenderDoc to inspect pass
> count/pipelines, iterates in seconds), so the VkImage/VkRenderPass/VkPipeline/SPIR-V code is IDENTICAL to what
> runs on the Thor's Turnip — NO rewrite. D3D12 would be throwaway. Then MSBuild the Android APK + measure fps.
- **Brick 1**: allocate the persistent native color+depth image + one render pass; render a single captured
  triangle into it + present. Proves the native surface + present path. (No correctness of the full scene yet.)
- **Brick 2**: pipe ALL field draws (pitch-720 + siblings) from 0x82489F40 through it with native vertex-input +
  translated shaders + native blend, one pass. Compare pass count (target: 16→~2) + gpu_frame_us.
- **Brick 3**: depth-prepass + gpu_foliage_force_early_z → self-overdraw reject. Measure the 3× on the occluded half.
- **Brick 4**: fold the composite/post (18 passes) into the native path or blit-chain; handle render-to-texture
  reads. Validate render correctness (screenshot) + lock the fps.

## Anti-repeat (do NOT redo — exp_ledger has the data)
Decoupled native-HLE = PERF-FLAT (kept xenia's 95-pass back-end — the whole point is to NOT keep it). Bin-once/
force-1-tile, in-pass, skip-transfers, downscale, MSAA, ROAA, ALL early-Z/LRZ levers on the LLE path = DEAD.
FSI/interlock = ABSENT on Turnip. The renderer is the ONLY remaining axis.
