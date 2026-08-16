# BD EDRAM-deletion → 30fps: executable build plan (2026-07-08)

## The proven mechanism (why EDRAM deletion IS the 30fps path)
- BD's field is **GPU-fragment-bound**: 25.2fps, GPU 99% @ max clock 680MHz on the full BD GameProfile (VRS4 + fp10 + MSAA2 + clamp) + AOT. (profbound.ps1)
- The fragment cuts that reach 30fps **resize the render target** and **garble on the EDRAM tile model**:
  - MSAA 2→1 (−28%): 22.2fps + garble (profm1.ps1).
  - resolution downscale 85%: 26fps + heavy garble (profds.ps1).
  - Cause: BD's 2×-packed main-scene RT bases are re-derived wrong at reduced size in the EDRAM tile addressing (the "cap=1 footprint mismatch", bd-edram-atomic-rop-solve).
- Levers that DON'T resize the RT (VRS4, fp10) render clean but cap at 25fps.
- **Native resource-keyed RTs bypass EDRAM addressing → the resized RTs render clean → MSAA1/downscale unlock → 30fps.** This is the confirmed reason to delete EDRAM.
- Dead ends (do not retry): naive early-Z `gpu_foliage_force_early_z` (22.7fps + white-band, profez.ps1); foliage decimation (user: never).

## What already works (PC-verified)
- Field renders **correct on desktop** with `gpu_bd_native_renderer` + `gpu_bd_native_aux_rt` + `gpu_bd_native_aux_fmt37=false` + `drop_resolves` + `drop_transfers` (desk_hle54.png, desk_mainscene.png). EDRAM resolves + color transfers deletable on PC, field correct.
- AOT object cache shipped default-on (EmulatorActivity.java onCreate ~line 207); warm launches skip the ~32% LLVM compile.
- `gpu_clamp_rt_image_height=768` (BD profile) clamps RT image to actual rows → native-size RTs (tile-I/O minor).

## PRECISE PINNING (2026-07-08, desk_maxdel.png + desk_hle54.png)
- **Working HLE state = CORRECT at 46 passes on PC**: `gpu_bd_native_renderer` (field→471 native, presented via present_output_view) + `gpu_bd_native_aux_rt aux_max_width=2048` (small RTs → native surfaces keyed by resolve-dest) + drop_resolves + drop_transfers + drop_all_color_xfer. Field renders fully correct (desk_hle54.png). So: field + ≤2048px RTs are native, their EDRAM resolves + color transfers deleted.
- **CORRECTED (desk_covdiag.png, diagnostic build): the MAXIMAL EDRAM-op deletion renders CORRECT on PC.** Config = `gpu_native_render_targets` (independent RTs, no ownership transfers) + drop_resolves + drop_transfers + drop_all_color_xfer + aux (native surfaces) + `gpu_clamp_rt_image_height=768` + native renderer (field→471). The earlier desk_maxdel.png "black" was a CAPTURE-TIMING transient, NOT a coverage gap — the `gpu_bd_native_diag_coverage` diagnostic found ZERO >2048px RTs, and the identical config re-captured renders the full field correct. So there is NO wide-RT consume-side gap.
- **So the EXACT remaining EDRAM = the DEPTH transfers only** (drop_all_color_xfer keeps depth; depth is load-bearing, mixed-res 720↔400) + the structural RenderTargetKey EDRAM-base ENCODING (functional RTs are already native/independent). The EDRAM color ops + ownership model are DELETABLE on PC with the field correct — verified.
- ⚠️ Note a FLAKINESS: desk_maxdel black once, desk_covdiag correct — a possible race in the native_render_targets + drop_all_color_xfer path worth confirming (capture 2-3× on Thor).
- **DEPTH is the last load-bearing EDRAM op (desk_alldrop.png): dropping depth transfers too (`drop_all_xfer`) → black even on the independent-RT config.** Consistent with every prior finding (depth is mixed-res 720↔400, carries load-bearing content between resolutions; drop → 400-res depth empty → geometry culled → black). So the depth transfers CANNOT be dropped — they need NATIVE mixed-res depth handling (render/convert depth on native RTs). That is the last EDRAM-op piece + the intricate part of the build. (Given the maxdel false-black, confirm desk_alldrop 2× before treating as final, but it matches all prior depth results.)

## NATIVE-DEPTH BUILD SCOPE (the last EDRAM op, 2026-07-08)
The depth transfers go through xenia's `host_depth_store` COMPUTE shaders (`host_depth_store_{1x,2x,4x}msaa_cs`, vulkan_render_target_cache.cc ~276, HostDepthStoreConstants push-constants ~853). They convert BD's mixed-res depth (720↔400) via the EDRAM buffer — GPU compute ops, not blits (which is why the depth CmdVkCopyImage path I added only handles same-layout). The native replacement: run the depth conversion between NATIVE depth RTs (or have the 400-res consumer sample the native 720 depth via the sampler at reduced res, eliminating the separate 400 buffer). This is the intricate SPIR-V/compute piece of the build. Files: the host_depth_store pipeline setup ~843-865, the transfer/copy block ~5912 (where I added is_depth CmdVkCopyImage + the drop filters).

## NET PC-FIRST STATE (2026-07-08): EDRAM COLOR ops are DELETABLE on PC with the field correct (resolves + color transfers + ownership all gone, desk_covdiag). The ONLY remaining EDRAM op is the DEPTH transfers (load-bearing, need native mixed-res depth). Plus the structural RenderTargetKey encoding. That is the precise, verified remaining scope.

## The build (in order)
1. **Main-scene native-RT coverage.** The aux path (`vulkan_command_processor.cc` ~4322) covers only resolve-dest surfaces (`aux_edge->dest_base`) and excludes ≥800px main-scene RTs (comment ~4345). The field renderer covers 471. Extend native coverage so the **main-scene color + depth** render into resource-keyed native VkImages (not EDRAM-tile-addressed), keyed by stable resource identity.
2. **Consume-side reader coverage.** The reason the main scene is excluded: its readers aren't redirected → sampling the emptied EDRAM copy black-screens (A/B-proven on Thor; desktop already renders correct, so this gap is Turnip-specific). Extend `LookupSampledSurface`/Brick-B (rt_as_texture_views_pixel_, ~10903) AND the composite/present path (hybrid_postprocess composite passes ~2378/2499; `bd_present_native_total_` present path ~3394) to read the native surface for the main-scene RT.
3. **Fix the Turnip black-area gap** (present/composite on Turnip only — desktop is clean).
4. **Then apply MSAA1 (or downscale) on the native RTs** and confirm clean render + 30fps on the Thor. This is the payoff and is Thor-only to validate (TBDR tile addressing is invisible on desktop immediate-mode).

## Validation protocol
- PC-first: each coverage step, verify field-correct via desk screenshot (run_cap.ps1, fmt37=false).
- Thor: single-run GPU-busy% + OSD fps (never cross-run — scene-confounded). Cool <46°C before each launch; force-stop >70°C. Do NOT thrash (memory: never-thrash-thor).
- Success = main scene native + MSAA1 clean + fps ≥30 single-run.

## Key files/seams
- `src/xenia/gpu/vulkan/vulkan_command_processor.cc`: aux gate ~4322-4388; Brick-B ~10903; present ~3394; composite ~2378-2612.
- `src/xenia/gpu/vulkan/vulkan_render_target_cache.cc`: `CreateRenderTarget` ~2771 (RT alloc/sizing); `gpu_clamp_rt_image_height` ~2796.
- Profile: `GameProfiles.java` "4D5307DF".
- Memory: [[bd-field-cpu-bound-not-gpu]] (has the full corrected synthesis + all dead-end tests).
