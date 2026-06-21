# Wave-2 extreme-novel sweep result (2026-06-21) — frame extrapolation is the standout; 4 of 6 candidates die at free gates

34-agent arxiv/Turnip-patch/NEON/multicore/Hexagon workflow (wbeld7ld2), targeting the resources wave-1
underweighted (the 7 idle cores, Turnip-source patching, Hexagon, async rendering, 2025-26 arxiv). Brutally
honest: most "use the idle silicon" ideas die at their own free de-risk gate. ONE transformative lever survives.

## ⭐ RANK 1 — PURSUE: GPU frame EXTRAPOLATION (frame generation), guest-pace-decoupled
- **Idea:** present a synthesized in-between frame so a floor-bound title (BD ~6fps, Burnout Main-thread-
  serialized/GPU-40%-idle) PRESENTS at ~2x perceived smoothness. Does NOT lower any floor - it BREAKS the
  present-pace = guest-pace coupling, converting idle GPU headroom + idle cores into perceived speed on exactly
  the titles that have no floor headroom left. The only lever that helps the un-speedup-able titles.
- **The emulator's unfair advantage (FEASIBILITY-GROUNDED this session):** native frame-gen (FSR3/DLSS-FG)
  can't get motion vectors from forward-rendered 360 games; xenia CAN recover them. `DrawExtentEstimator::
  SetupFastAffineReplay` already recovers each draw's full object->clip 4x4 (`FastAffineReplay.m[4][4]`,
  draw_extent_estimator.h:98 / .cc:1117) for affine draws - the camera view-projection DELTA between frames is
  derivable from this. CAVEAT: skinned/multi-leaf draws (characters) fail affine recovery -> their motion isn't
  captured -> minor character-warp artifacts, but the dominant camera/background motion IS captured.
- **Infra already exists:** the presenter runs an INDEPENDENT PaintAndPresent loop fed by a 3-deep
  guest_output mailbox (presenter.h:362 kGuestOutputMailboxSize=3; RefreshGuestOutput/CaptureGuestOutput) -
  cadence decoupling is built. Source: GFFE G-buffer-free extrapolation (arxiv 2406.18551).
- **BUILD (major, multi-session, quality-iteration-heavy):** (a) per-frame, recover/extract the camera VP from
  the affine Ms (or reproject via depth + dominant-VP); store the VP delta at IssueSwap. (b) snapshot prior
  frame depth+color. (c) GPU compute: reproject depth through VP_curr/VP_prev -> forward-warp color -> cheap
  disocclusion hole-fill. (d) present the warped frame off the paint thread on the half-cadence slot. cvar
  `present_frame_extrapolation` default-off. DE-RISK (one fire, GPU-only, no NPU): build the warp, JUDGE
  perceptual quality on BD slow-pan AND Burnout fast-camera; if fast-camera artifacts are unacceptable it dies
  here for free. UPGRADE path (only if the warp costs too much GPU): port warp+inpaint to INT8 on the idle
  Hexagon NPU (QuickSRNet/GFFE-lite, arxiv 2303.04336 ~2.24ms 1080p-2x) - the NPU is the optimization of a
  proven win, NOT the bet. COMBO: extrapolation also RAISES THE CEILING on every shipped GPU lever - with it
  underneath, push gpu_foliage_thin_factor/blended to MAX (3.4x BD, the "cutting too much" setting) and let
  extrapolation backfill the lower guest cadence.

## RANK 2 — cheap pre-flight (DERISK-FIRST): Turnip GMEM-bias via STOCK env vars (no patch)
- `TU_AUTOTUNE_ALGO=prefer_gmem` / `TU_AUTOTUNE_FLAGS=big_gmem` / `TU_DEBUG=gmem`, set via the EXISTING
  setenv-before-dlopen plumbing (gpu_vulkan_driver_debug -> setenv(TU_DEBUG) in vulkan_instance.cc). 5-line
  mirror cvar + one matched BD/Burnout A/B. Likely small/null (BD floor = intra-tile overdraw, mode-invariant;
  edram_buffer_ is a host SSBO not GMEM - render_target_cache.cc:497), but retires the whole "patch Turnip
  render-mode policy" class cheaply, AND is the free pre-flight for Rank-1 (keeps prior-frame depth tiles in
  GMEM so the warp's depth read is on-die). Use Mesa MR !15376 autotune logging to count real mis-picks.

## DROPPED — die/shrink at their FREE de-risk gates (do NOT spend build budget; each is one free probe):
- **BV/BR concurrent-binning overlap:** the SYNC knob is in the KGSL kernel ringbuffer (CP_THREAD_CONTROL
  SYNC_THREADS; freedreno patch Connor Abbott 2025-10-28) = UNPATCHABLE without root on the retail Thor; AND
  xenia already runs kMaxFramesInFlight deep = no API fence to drop. Probe: read KGSL cmdbatch submitted->retired
  deltas; either overlaps already (nothing to win) or serializes in-kernel (park for custom-kernel). DROP/park.
- **VK_EXT_external_memory_host (in-place guest-mirror import):** the memcpy is on NO measured floor (CPU 85%
  idle on BD; lock-free+VRS already fps-neutral on Burnout) + worsens the unsolved deferred-tiler-read-vs-CPU-
  write TDR race + collides with write-watch dirty-tracking. DROP.
- **Async XMA decode on cDSP/HVX:** XMA ALREADY runs on a dedicated host worker (xma_decoder.cc:145), batched,
  off the render/guest threads + off the X3. Highest integration cost for an indirect tiny win. DROP.
- **Ghost-threading helper-core prefetch:** MICRO'25's 1.33x is SMT-bound (shared L1/L2 + a throttle insn with
  no ARMv9 equiv); Thor is non-SMT; AND xenia has no tier-2/hot-block JIT to host the slice generator
  (processor.cc:224 single-tier). DROP.

## Honest meta-conclusion
The "use the 7 idle cores / idle NPU / patch the driver" ideas mostly die because the floors are either
unpatchable-below-us (KGSL kernel) or not where the idle silicon can reach (single-thread guest logic, already-
async audio). The ONE real answer to "turn idle silicon into speed" is FRAME EXTRAPOLATION - it sidesteps every
wall (operates on completed read-only frames, off the present thread: no guest-state sharing, no parallel-JIT
lock, no FastRPC on the critical path). Plus the major GPU-floor lever from wave-1 (FDM) still stands. Full
agent output: tasks/wbeld7ld2.output.
