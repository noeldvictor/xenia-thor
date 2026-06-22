# Extreme-novel 3-track research (arxiv + FULL Turnip/Mesa + NEON), red-teamed (2026-06-22, wf wj4d4rur9)

22-agent workflow (14 research, wave-batched 4-wide to dodge a transient server rate-limit that killed the
first run; synthesis; 6 red-teamed 3-wide; final). Mandate: NOVEL BD/Burnout levers that fit the CONFIRMED
floor (BD = co-planar foliage + 34% blended + alpha-test OVERDRAW, NOT occlusion; Burnout = CPU-JIT), each
with the CHEAPEST falsifying device GATE (the LRZ lesson). 5 of 6 first-cut candidates were red-team-KILLED.

## Ranked roadmap (survivors)
1. **gpu_vulkan_rt_keep_ubwc (ALREADY BUILT, lossless) — GATE RUN 2026-06-22: FLAT (negative).** Keep Adreno
   UBWC framebuffer compression on the MUTABLE_FORMAT color RT via a 2-entry VkImageFormatListCreateInfo
   (vulkan_render_target_cache.cc:81/2045/2061). Occlusion-independent. Cheap gate = one frozen A/B, zero code.
   **DEVICE RESULT (frozen guest_ms=18000 BD credits field, 267 draws, opaque=25/alphatest=93/blended=5, n=30
   each): UBWC off mean 26301us vs on 26306us = +0.02% = FLAT.** Both rendered identically (matched A/B; lossless
   so flat = inert here). => the color-RT-bandwidth slice does NOT move BD's gpu_frame_us, CONSISTENT with the
   prior finding that BD is raster/coverage/depth-bound not color/ROP-bound (constant-color FS = no change,
   [[bd-real-bottleneck-overdraw]]). The "~10-18ms b2 byte-inflation slice" was a misattribution. NOT defaulted
   on. CAVEAT: this is the ~26ms LIGHT scene; the heavy 123ms field (blended~386) has ~5x overdraw so a small
   slice COULD emerge there, but the raster-bound prior makes it unlikely + the heavy field is thermally
   watchdog-blocked (~t=110s reach vs 64C watchdog ~t=100s). Re-test on the heavy field ONLY if rank 3 unblocks
   it cheaply. Color-bandwidth angle (incl. the killed R32_UINT 7e3 variant) is BOUNDED.
2. **FDM (VK_EXT_fragment_density_map) — the one TRANSFORMATIVE-headroom lever, gated behind 2 device-free
   checks BEFORE the ~1-2wk build.** Per-bin viewport/scissor DOWNSCALE = cuts the COUNT of rasterized+depth-
   tested+sampled fragments (not just shading rate) over the 3D viewport, FULL density on HUD. Occlusion-
   INDEPENDENT and it attacks RASTER/DEPTH/COVERAGE = exactly BD's confirmed bound (unlike UBWC). probe already
   in vulkan_device.cc (fragmentDensityMap=1 nonSubsampledImages=1). GATE A (rate-value oracle, ~5 lines at
   vulkan_command_processor.cc:5963-5967 to alternate VRS rate 2-vs-4 NOT on/off): if 4x4~=2x2 the floor is
   NOT shading => FDM's raster cheapening is the lever. GATE B (overlap falsifier, DEVICE-FREE RE, do FIRST):
   get actual Turnip/Mesa C SOURCE (only .so vendored) + grep the bin-offset/GRAS_VRS_CONFIG path to confirm
   a740 applies FDM as REAL viewport/scissor bin-scale that lowers fragment COUNT, NOT a coarse-raster ALIAS of
   the shipped VRS. If same HW as VRS => redundant => dead pre-build. The "stacks on VRS / -20-40%" claim is
   UNVERIFIED extrapolation. Artifact risk (blockiness) on blended+alpha-test.
3. **Save-state deadlock fix OR guest_ms-anchored BD-field reach — the META-BLOCKER for ALL BD-heavy-field
   A/Bs.** SaveToFile hangs in kernel_state_->Save (emulator.cc:1094). Without it, every BD GPU number except
   UBWC's is extrapolated from a light scene (the heavy field free-running A/B is scene-confounded + VRS-on
   white-screens the menu nav). Cheap diagnosis FIRST (device-free): RE the hang stack — is Save spinning on a
   guest-held lock Pause didn't quiesce, or blocking on GetObjectsByType? Fallback = a deterministic guest_ms-
   anchored TAS nav to the field. Highest-INFO unblock; gpu_freeze already partly mitigates (it gives matched
   frozen frames, as the UBWC A/B just used).
4. **Burnout spin-to-block / WFE-park — THERMAL/POWER ONLY, NOT fps (red-team FAILED the fps premise).** Kept
   only as a battery/thermal hygiene option; do NOT build expecting fps.

## NEW DEAD entries (red-team-killed + source-confirmed — do NOT re-propose)
- **Foliage alpha-test CUTOFF escalation** (raise RB_ALPHA_REF): the RB alpha test + OpKill run in the FS
  EPILOGUE (CompleteFragmentShaderInMain, spirv_shader_translator_rb.cc:445/522/619) AFTER full texture-sample
  + ALU shading, so a late-killed fragment already paid sample+shade; raising the cutoff only suppresses the
  ROP color write = a fragment-OUTPUT suppressor mislabeled as a fragment-COUNT reducer = late-Z/occlusion
  class = dead for BD's floor. Likely fully inert if BD uses guest texkill (kills_pixels) not the HW RB test.
- **Bit-exact 32bpp 7e3 color via R32_UINT integer-alias + in-shader blend**: Adreno 740 render path is always
  kHostRenderTargets with FIXED-FUNCTION blend (FSI device-absent); R32_UINT forbids fixed-function blend, so
  all ~386 blended draws would need in-FS RMW = building the absent-FSI software-blend EDRAM backend, NOT a
  bounded change. Forfeits UBWC too. SUPERSEDED by (now-flat) rank 1. Could only ride on ROAA/local_read FB-
  fetch if that's ever wired.
- **Spin-to-block as an FPS lever**: top -H shows 5/8 cores idle (no contention to relieve); TWO existing A/Bs
  prove cutting Main-thread CPU burn moves nothing (cpu_lockfree_entry_lookup 8.43=8.43 VdSwap/s;
  gpu_early_primary_read_pointer_writeback gpu_frame 46256=46263) — Burnout is GPU-PACED. Thermal-only.
- **Camera-VP-delta depth-reproject warp** (for frame-gen motion): DrawExtentEstimator recovers per-draw
  OBJECT->CLIP (MVP, draw_extent_estimator.cc:1044), folding each object's model matrix in => NO coherent shared
  camera VP to read; xenia never sees V or P separately. SURVIVING reframe = a 2D global-motion HOMOGRAPHY warp
  of the color history (no depth/matrices), A/B vs the shipped cross-fade on a slow-pan; only if it beats
  cross-fade consider per-draw motion-splatting (Mob-FGSR), never a single recovered VP.
- **VRS 4x4-vs-2x2 as a "free zero-code FDM oracle"**: the shipped validator alternates vrs_active ON-vs-OFF at
  a fixed rate (vulkan_command_processor.cc:5963-5967), NOT 2x2-vs-4x4 — the "one free fire decides both" story
  is false; the rate-value comparison needs new code (=FDM gate A). FDM itself survives (rank 2).

## Honest ceiling (the LRZ lesson restated)
No proposal here is a clean path to "BD full speed". BD's per-covered-fragment overdraw on co-planar/blended/
alpha-test remains a hard wall; these are stacking chips. Rank-1 UBWC just GATED FLAT (the cheap gate paid off
by killing a default-on cheaply). FDM (rank 2) is the only TRANSFORMATIVE-upside survivor but carries the most
validation risk and must clear the device-free Turnip-source overlap check (gate B) before any build. Burnout
has NO surviving GPU/spin fps lever in this batch (its levers stay Main-JIT codegen + fewer verts/draws +
bindless vertex-fetch). NEXT device-free step: FDM gate B (RE Turnip/Mesa source for FDM-vs-VRS HW overlap) +
rank-3 hang diagnosis — both fillable during device cooldown.
