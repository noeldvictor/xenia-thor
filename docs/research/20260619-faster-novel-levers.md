# Faster xenia-on-Thor — ranked buildable speed levers (latest-techniques research, 2026-06-19)

User goal: "get this emulator FASTER, novel hardware tricks, latest research." 6-agent WebSearch-backed
workflow + code-grounded. >1.3x BD win IS available (GPU overdraw track). Three levers, all code-verified.

## Lever A (#1, BIGGEST) — Foliage front-to-back reorder to revive Adreno LRZ-TEST reject  [BD, GPU-bound]
- **Insight (latest):** xenia emits `OpKill` (discard) for the Xenos alpha-test (spirv_shader_translator_rb.cc:619).
  On Adreno/Turnip `discard` disables LRZ-**WRITE** only — **LRZ-TEST (reject) SURVIVES** (ARM Early-Z 2024,
  Mesa/Igalia A7XX LRZ-feedback, Qualcomm Best-Practices Oct-2025). xenia replays guest draws in guest order
  with NO sort, so the foliage layer never builds a front-to-back depth field to reject against - THAT missing
  sort is the bug. Buffer the contiguous same-RT alpha-test run, sort by per-draw bbox depth front-to-back,
  replay -> near cards prime depth, far cards LRZ-reject.
- Win: attacks the 43% foliage self-overdraw the coverage-collapse A/B isolated; plausibly **1.3-1.7x BD GPU
  frame**. Pixel-exact (alpha-test writes aren't blended -> reordering same-state alpha-test draws is identical).
- GATE FIRST (one fire, near-zero build): `gpu_foliage_lrz_gate` enum guest/reverse/sorted; for the alpha-test
  run force depth-test-on/write-off (CmdVkSetDepthWriteEnable dynamic @:7638) + submit in the chosen order at a
  frozen gpu_freeze_at_guest_ms. sorted << reverse -> LRZ ordering live -> build the sorter. ALSO read
  z_write_enable for the alpha-test class (:4485/:7618 - the load-bearing unknown). All tie / depth never
  written -> Lever A': foliage depth-only self-prime (extend EmitOpaquePrepassDraw :7258 to the alpha-test
  class with colorWriteMask=0 + depth-write, one is_depth_only pipeline-key bit).
- Files: vulkan_command_processor.cc:5794-5886 (classifier + prepass scaffolding already exists), :7258, :7638.

## Lever B — VRS coarse-shading the foliage pass  [BD, GPU-bound] — NEWLY-CONFIRMED PRESENT
- **VK_KHR_fragment_shading_rate IS enumerated on BOTH Thor drivers** (Turnip scratch/banjo_gameplay.txt +
  Qualcomm scratch/burnout_base.txt) but requested NOWHERE in src - corrects the standing "unconfirmed". 2x2
  coarse rate on the classified alpha-test/blended foliage draws cuts per-covered-fragment FS + the
  alpha-test-discard/texture-fetch path up to 4x. Unlike a depth-prepass it does NOT duplicate the alpha-test.
- Win: honest caveat - constant-color-FS showed shading ALU is DEAD on BD, so VRS only helps via the
  per-invocation discard/texture-fetch path: realistic **10-25% on the foliage pass** (Qualcomm/Popov 2024
  ~30% GPU power). Quality-toggle class (coarsens foliage edges).
- Build: add VK_KHR_fragment_shading_rate to vulkan_device.cc:140-213 request map + feature link; log
  vkGetPhysicalDeviceFragmentShadingRatesKHR tiers (one boot line); cvar gpu_vrs_foliage_rate ->
  CmdSetFragmentShadingRate when the classifier flags the draw alpha-test/blended (1x1 otherwise).

## Lever C — ADPF: register the frame-critical GUEST thread, not just the CP worker  [CPU-bound titles]
- The shipped ADPF session (command_processor.cc:1444-1450) registers ONLY the CP-worker tid + reports the
  swap interval. CPU-bound titles bottleneck on a DIFFERENT guest XThread (Burnout = one branchy Main XThread
  99% in top-H), invisible to ADPF -> scheduler boosts the wrong core. Register {cp_tid, guest_frame_tid} +
  report max(thread work).
- Win: 2024 multi-thread-hint stories 10-50% sustained/anti-throttle (UNISOC +50%/+3% power). ~0 on GPU-bound BD.
- CAVEAT (corrects research): there is NO single "hot guest tid" tracked - only affinity MASKS
  (thor_guest_thread_affinity_mask, thor_topology.h kWorkerMask leaves X3 for the guest hot path). Need to
  IDENTIFY the frame-critical guest thread (the VdSwap-driver / the X3-pinned one) to register it. Bigger than
  "4 lines". ADPF create_session takes thread_ids[]+size (command_processor.cc:67) so multi-tid is supported.

## Honest ceiling
- BD: >1.3x available, ONLY from the GPU overdraw track (Lever A 1.3-1.7x, +B 1.1-1.25x stacked). ~77% of
  BD's GPU frame is per-covered-fragment raster+depth+alpha-test overdraw; foliage self-overdraw (43%) is the
  only big content-preserving wall-free slice.
- CPU-bound titles: ~1.3-1.6x total ceiling (RPCS3 hand-JIT-vs-LLVM gap / Rosetta translation-tax floor).
  Un-shipped CPU levers, priority order: (1) BL/RET return-address-stack pairing (Rosetta-2 trick; guest
  returns use plain `br x9` defeating the X3 RAS predictor; ~1 day; device-free qemu-testable), (2) in-block
  partial-dead-CR / compare->branch FLAGM2 fusion, (3) hot superblock/trace formation (2nd-tier). Lever C
  (ADPF) is the cheapest CPU-side win + stacks.
- DEAD (don't re-propose): mesh shaders (hard-absent both drivers), QCOM tile_shading (840+), VRS-as-ALU-fix
  (ALU dead), Hexagon FastRPC (slower than the GPU), generic opaque depth-prepass (primes wrong layer),
  alpha-to-coverage on BD (inert at msaa=0). ROAA: Turnip enumerates it + dynamic_rendering_local_read but the
  EXT flag reads false; narrow ~172ms resolve-elision bandwidth lever, NOT the overdraw unlock - low priority.

## Build order (device-rest-conscious; device fired ~9x this session)
Build device-free + cvar-gated now, validate in a rested session: Lever B (VRS, cleanest novel-hardware) ->
Lever A gate+reorder (the #1, needs the gate fire) -> the BL/RET RAS pairing (CPU, qemu-testable) -> Lever C
(ADPF, needs the frame-critical-tid identification). Each ships as a stacking XeniaOptimizations toggle.
