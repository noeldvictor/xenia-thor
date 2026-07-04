# PIPELINE REARCH TO HOST — the plan (user mandate 2026-07-04: "REARCH THE PIPELINE TO HOST")

## Goal
Replace BD's emulation stack — **PM4 decode → register_file → emulated SSBO vertex-fetch → EDRAM round-trips →
shader-translation bloat** — with ONE native host-Vulkan path driven directly by BD's D3D9 device state. The
10–20× hardware means the ~15–30× emulation overhead is DIFFUSE across that stack; no single lever removes it
(see experiment-ledger.md — every one is DEAD/flat), so we remove it wholesale by porting the pipeline.
Model: UnleashedRecomp's "translation layer for the renderer, not GPU emulation" — a per-title D3D9→Vulkan subset.

## What we already have (RE done, 2 agents)
- **Signature-HLE mechanism** (processor.cc `kD3dHleSigs` + `LookupFunction` `SetupExtern`): load-time identify
  an XDK D3D9 fn by 48-byte prologue (mask bytes[4:8] = bl-to-__savegprlr) → install a host handler. PROVEN.
- **Intercepts identified**: EndTiling @0x8248A680 (whole-scene field render+resolve), DrawRects2D @0x82488C10
  (only LIVE draw emitter — 2D/HUD/composite rects), FlushRenderState @0x82488670 (state snapshot),
  BeginTilingSetup @0x824884C8. FUN_82487CC0 = per-tile replay (below EndTiling).
- **BD D3D device layout** (from VdGlobalDevice=*0x820005F4): +0x10/+0x18/+0x20 dirty-state masks, +0x28 PM4
  write ptr, +0x30 limit, +0x30a0 surface-info (>>0x12=pitch), +0x30b8 tile count, +0x32d0 token stream,
  +0x34a4 ring base, +0x28xx shadow render-state arrays. Reg-flush = FUN_82485DD8 (Xenos regs 0x2000..0x2300).
- **CENTRAL CONSTRAINT**: xenia `VulkanCommandProcessor::IssueDraw` (vulkan_command_processor.cc:4891) reads
  EVERYTHING from register_file_; active_vertex/pixel_shader set only by PM4_IM_LOAD (cp.cc:2619). No API to
  draw from D3D params. So the port either (A) populates register_file_ + reuses IssueDraw [keeps SSBO/EDRAM],
  or (B) bypasses IssueDraw and drives Vulkan subsystems directly [the true port]. The big overhead (SSBO
  vfetch, EDRAM) lives INSIDE IssueDraw's path → the real win needs (B) for those stages.

## Architecture — the native path (Path B, driven by D3D state)
Reuse xenia subsystems as LIBRARIES, drive them natively instead of from register_file_/PM4:
- **Vertex**: D3D vertex declaration → `VkVertexInputAttributeDescription` (hardware input assembly), vertex/
  index data uploaded via `shared_memory_`. REMOVES the emulated SSBO/texture vfetch (shader-stats showed the
  foliage binning VS is 12 tex-fetch + 10 sync-wait heavy = the fetch emulation).
- **Shaders**: keep Xenos→SPIR-V (`SpirvShaderTranslator`, pipeline_cache_) — they're the game's shaders (VRS/
  quality options apply). BUT translated for NATIVE INPUT (read attributes, not SSBO) — a translator variant.
- **State**: D3D render state (shadow arrays @device+0x28xx) → `VkPipeline` state (blend/depth/raster/viewport).
- **Render targets**: native `VkImage` RTs, NO EDRAM round-trip (BD field = main 1280x720 + bloom pyramid
  640/320/160/80; render each to a native image, sample directly). REMOVES the EDRAM tile-I/O emulation.
- **Draw**: `vkCmdDrawIndexed` directly. REMOVES PM4 decode + register_file churn.
- **Textures**: `texture_cache_` (reuse).

## Build order (each brick shippable, cvar-gated, validated; grep the ledger before each)
0. **[RE IN FLIGHT]** EndTiling decompile → is the field genuinely multi-TILED (bin-once helps) or 1 tile
   (inert)? + the exact reshape recipe. GATES whether step 1 is bin-once-in-xenia or straight-to-native.
1. **Intercept + state-read foundation**: install EndTiling/DrawRects2D/FlushRenderState signatures; a handler
   that READS + logs BD's D3D scene state (draw list, vertex/index addrs, shader ids, RT config). Proves we can
   see everything the native path needs. (No render change yet — safe.)
2. **Native RT for the field** (biggest structural win): render BD's main scene to a native VkImage instead of
   the EDRAM-emulated path; sample it for the composite. Validate pixel-correct + measure.
3. **Native vertex input** for the foliage (translator variant: attributes not SSBO). The fetch-emulation cut.
4. **Native pipeline/state** from D3D render state.
5. **Native composite/resolve** (bloom pyramid + present) — collapse the round-trips.

## Honesty / risks (do not forget)
- The foliage GEOMETRY (262k verts × wind shader) is intrinsic — native removes the per-vert EMULATION overhead
  (the 15–30×), but the vertex COUNT stays; the 10–20× HW headroom is what makes native fast regardless.
- Path B is dxvk-scale for the general case; we do the per-title SUBSET BD's field needs (a few draws/states).
- Reuse xenia's translator + caches — do NOT rewrite them; drive them.
- Every brick: cvar-gated default-off, screenshot-correct, ledger-logged. No cross-run fps claims.
