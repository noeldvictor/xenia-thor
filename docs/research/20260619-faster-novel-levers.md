# Faster xenia-on-Thor — ranked buildable speed levers (latest-techniques research, 2026-06-19)

User goal: "get this emulator FASTER, novel hardware tricks, latest research." 6-agent WebSearch-backed
workflow + code-grounded. >1.3x BD win IS available (GPU overdraw track). Three levers, all code-verified.

## ⭐ VALIDATION UNBLOCK (2026-06-19): reorder levers are gpu_freeze-validatable; save-states are deadlocked
The recurring BD GPU-lever validation wall is boot-nondeterminism (a free-running A/B races into different
scenes - it sank the VRS A/B). Two findings this session:
- **Save-state matched-A/B path is DEEPLY blocked** (deferred): SaveToFile hangs in (1) StepToGuestSafePoint
  stepping a spinning thread - FIXED + shipped (6e4da5a97, bounded waits/scan/recursion + soft-skip), AND
  (2) a global-lock DEADLOCK - KernelState::Save -> ObjectTable::GetObjectsByType does
  global_critical_region_.Acquire() while a Pause()-suspended guest thread holds the global lock (SaveToFile
  runs lockless by design, emulator.cc:1111). Per-object Save likely adds more. Multi-layer, uncertain payoff.
- **KEY INSIGHT - a draw-REORDER lever is GUEST-TRANSPARENT**, so it needs NO save-state: it changes only GPU
  draw-replay order, not guest CPU execution, so under gpu_freeze_at_guest_ms both gate variants run the guest
  IDENTICALLY and freeze on the SAME scene = a clean deterministic A/B. (VRS could NOT - it changed GPU work
  -> frame time -> VdSwap pacing -> different nav.) So **Lever A below is validatable NOW via gpu_freeze**;
  confirm the frozen composition (rendered/alphatest counts) matches across the two launches before trusting
  the gpu_frame_us delta.

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

## Lever B — VRS coarse-shading the foliage pass  [BD, GPU-bound] — ⚙️ BUILT + INFRA-VALIDATED, perf A/B BLOCKED 2026-06-19
**DEVICE-FIRE RESULT (2026-06-19, commit 39e185ec0):** VRS infrastructure PROVEN working - boot log
`* VK_KHR_fragment_shading_rate (pipelineFragmentShadingRate: yes)` (extension requested+enabled on Turnip),
runs with gpu_vrs_foliage_rate=2 CLEAN (zero VK_ERROR/validation/device-lost, 4412 VdSwaps, guest_ms
advancing normally = guest healthy). **BUT the perf A/B is UNMEASURED - scene-confound.** Baseline (VRS off)
reached the BD town heavy field (rendered~1158, alphatest~470 + blended=387 draws, gpu_frame_us median
~127-129ms, guest_ms truncated to "1" by logcat line-length on the longer slow-frame lines = actually
~1xxxxx). VRS-on fired TWICE (155s + 185s) and BOTH times diverged to a LIGHT menu/load scene (max
alphatest=93, max rendered=279, white screen, 28-31fps) - NEVER hit the foliage scene. Root cause: the
wall-clock-timed auto-nav (hid_nop start@/a@ at fixed wall-ms) + VRS-on's faster light-scene pacing land the
guest in different content; BD boot is NOT reproducible across the two configs (guest_ms is not a reliable
content anchor here - at the same guest_ms the two runs show different scenes). **A free-running launch A/B
cannot isolate a GPU lever on BD.** FIX FOR NEXT TIME: a SAVE-STATE anchored at the foliage scene loaded in
both configs (save_state_at_guest_ms cvar + the TAS/save-state system) = identical scene = clean matched A/B.
Until then VRS stays default-off (perf unvalidated). This scene-confound also blocks Lever A's free-running
gate - build the save-state A/B harness FIRST before any further BD GPU-lever device validation.

### (build map) — ⚙️ BUILT 2026-06-19
**STATUS: fully built end-to-end, cvar-gated default-off (gpu_vrs_foliage_rate=0 = inert), compile-verify
in progress.** Sites landed: (1) functions/device_khr_fragment_shading_rate.inc; (2) vulkan_device.h ext
flag + Functions-struct .inc include; (3) vulkan_device.cc VulkanFeatures decl + STRUCT_EXTENSION request
(unconditional when supported = layering option (a)) + Link gate + manual pipelineFragmentShadingRate
enable (NOT via FEATURE_2 - no properties_ member) + honest-flag downgrade + boot log line + loader .inc;
(4) deferred_command_buffer.h Command::kVkSetFragmentShadingRate + ArgsVkSetFragmentShadingRate + Cmd write
method; (5) deferred_command_buffer.cc replay case; (6) vulkan_pipeline_cache.cc dynamic_states[16]->[18] +
VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR appended (cvar+device gated); (7) vulkan_command_processor.cc
:5827 per-draw consumer (foliage = alpha-test OR blended -> {r,r} KEEP,KEEP, else 1x1); (8) EmulatorActivity
allowlist + XeniaOptimizations opt_vrs_foliage toggle. **DEVICE FIRE PLAN:** BD heavy field, confirm boot log
"VK_KHR_fragment_shading_rate (pipelineFragmentShadingRate: yes)", A/B gpu_vrs_foliage_rate 0 vs 2 at matched
guest_ms+rendered, compare gpu_frame_us, read png (foliage intact, edges may be blockier). Expect 10-25% on
the foliage pass (ALU is dead on BD; the win is the per-invocation discard/texture-fetch path).

### (original analysis) — NEWLY-CONFIRMED PRESENT
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
- **VRS FULL SITE MAP (2026-06-19, all verified; cvar `gpu_vrs_foliage_rate` ALREADY ADDED to gpu_flags.cc/.h):**
  1. NEW `src/xenia/ui/vulkan/functions/device_khr_fragment_shading_rate.inc`: `XE_UI_VULKAN_FUNCTION(vkCmdSetFragmentShadingRateKHR)`.
  2. vulkan_device.h: `bool ext_KHR_fragment_shading_rate=false;` in Extensions (after :199 region) + `#include` the
     new .inc in the Functions struct (after :227 push_descriptor include).
  3. vulkan_device.cc: (a) `VulkanFeatures<VkPhysicalDeviceFragmentShadingRateFeaturesKHR, VK_STRUCTURE_TYPE_
     PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR>` decl near :325; (b) request via
     `XE_UI_VULKAN_STRUCT_EXTENSION(KHR_fragment_shading_rate)` in the with_gpu_emulation block (sets
     device->extensions_.ext_KHR_fragment_shading_rate, like ROAA :199); (c) `.Link()` near :361 gated on the
     ext flag; (d) enable line after the query (~:372): `features.enabled.pipelineFragmentShadingRate =
     features.supported.pipelineFragmentShadingRate;`; (e) loader `if(ext_KHR_fragment_shading_rate){#include
     the .inc}` after :826.
  4. deferred_command_buffer.h: `Command::kVkSetFragmentShadingRate` (enum :638), `struct ArgsVkSetFragment
     ShadingRate{VkExtent2D size; VkFragmentShadingRateCombinerOpKHR ops[2];}` (:837 region), `CmdVkSet
     FragmentShadingRate(...)` write method (:501 pattern).
  5. deferred_command_buffer.cc: replay case (:320 pattern) -> `dfn.vkCmdSetFragmentShadingRateKHR(cb,&size,ops)`.
  6. vulkan_command_processor.cc :5827: per-draw consumer -> if `cvars::gpu_vrs_foliage_rate && is_alphatest_draw`
     set {rate,rate} else {1,1} (combiners KEEP,KEEP).
  7. EmulatorActivity copyIntExtra("gpu_vrs_foliage_rate") + XeniaOptimizations IntCvar toggle.
  **LAYERING NUANCE:** vulkan_device.cc (xenia-UI) can't read the xenia-GPU cvar gpu_vrs_foliage_rate. Either
  (a) request the extension UNCONDITIONALLY when supported (low-risk: enabling a supported feature is inert
  until the consumer sets a rate), or (b) add a separate UI-level enable cvar to gate the request. Prefer (a)
  for simplicity (the gpu consumer cvar default-off = no rate set = inert), OR (b) for a fully-gated request.
  Build on a RESTED device (compile-verify Android, then fire with gpu_vrs_foliage_rate=2 on BD heavy field;
  log the vkGetPhysicalDeviceFragmentShadingRatesKHR tiers + A/B gpu_frame_us).

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
  Un-shipped CPU levers, priority order: ~~(1) BL/RET return-address-stack pairing~~ **DEAD - GATED OUT
  2026-06-19: the a64 backend ALREADY does RAS-friendly call/return.** Regular guest calls emit host `blr`
  (RAS-push, a64_emitter.cc:6160); the function epilog ends in `ret` (RAS-pop, :4027); a guest return (blr to
  LR, CALL_POSSIBLE_RETURN) compares the target to the saved GUEST_RET_ADDR and on match branches to the
  epilog `ret` (:6120) = RAS-predicted; only true CALL_TAIL uses `br` (correct, no return expected). So the
  call/return is RAS-balanced - the Rosetta-2 "guest returns use plain br defeating the RAS" premise is FALSE
  for this backend. Do NOT build it. (2) in-block partial-dead-CR / compare->branch FLAGM2 fusion, (3) hot
  superblock/trace formation (2nd-tier). Lever C (ADPF) is the cheapest CPU-side win + stacks. NOTE: picking
  the highest-impact CPU codegen lever needs DEVICE simpleperf on Burnout's hot guest fn (do NOT guess) per
  [[cpu-track-lockfree-and-thorpack-gate]] - the generic peepholes (known-bits, const-range, rlwinm, CR
  fast-paths, FLAGM ADD_CARRY) are shipped; remaining wins are trace-formation (big) or device-profiled.
- DEAD (don't re-propose): mesh shaders (hard-absent both drivers), QCOM tile_shading (840+), VRS-as-ALU-fix
  (ALU dead), Hexagon FastRPC (slower than the GPU), generic opaque depth-prepass (primes wrong layer),
  alpha-to-coverage on BD (inert at msaa=0). ROAA: Turnip enumerates it + dynamic_rendering_local_read but the
  EXT flag reads false; narrow ~172ms resolve-elision bandwidth lever, NOT the overdraw unlock - low priority.

## Build order (device-rest-conscious; device fired ~9x this session)
Build device-free + cvar-gated now, validate in a rested session: Lever B (VRS, cleanest novel-hardware) ->
Lever A gate+reorder (the #1, needs the gate fire) -> the BL/RET RAS pairing (CPU, qemu-testable) -> Lever C
(ADPF, needs the frame-critical-tid identification). Each ships as a stacking XeniaOptimizations toggle.
