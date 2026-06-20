# Adreno 740 / Turnip feature-gap audit — what the Thor exposes that xenia does NOT use (2026-06-20)

Triggered by the user's (correct) certainty that "we are missing GPU enhancements from Adreno." Audited the
LIVE device extension list (from the Burnout capture logcat — `vkEnumerateDeviceExtensionProperties`) against
what xenia enables (`vulkan_device.h`/`.cc`). **Turnip exposes ~80 device extensions; xenia enables ~15.**

## ✅ PROBE RESULTS (device, 2026-06-20) — ALL high-value FEATURE bits are ENABLED
Shipped a one-time `GPU feature-gap audit` log in vulkan_device.cc; device init reports:
`shaderFloat16=1 bufferDeviceAddress=1 descriptorIndexing=1 runtimeDescriptorArray=1
sampledImageArrayNonUniformIndexing=1 descriptorBindingPartiallyBound=1 dynamicRendering=1
synchronization2=1 roaa_color=1 roaa_depth=1`. So EVERY candidate FEATURE is usable (not just the extension).
⭐ **BIGGEST: ROAA color+depth = 1** (memory roaa-edram-path recorded it FALSE on 2026-06-17 → Turnip flipped
it ON). The single-pass EDRAM / FSI-alternative path is now FEASIBLE. Full bindless is confirmed
(bufferDeviceAddress + descriptorIndexing + runtimeDescriptorArray + partiallyBound all = 1). FP16 shaders
(shaderFloat16=1) confirmed. No feature-flag gamble remains — every lever below builds on a confirmed feature.

## ⚠️ CORRECTIONS to stale "ABSENT" beliefs (CLAUDE.md / memories) — these ARE exposed now
The Thor's Turnip build has advanced. The following, previously documented ABSENT, are in the device list:
- **VK_EXT_descriptor_buffer** (CLAUDE.md said absent) — exposed.
- **VK_EXT_multi_draw** (CLAUDE.md said absent) — exposed.
- **VK_EXT_rasterization_order_attachment_access** (ROAA; memory roaa-edram-path said feature=false 2026-06-17)
  — the EXTENSION is exposed; the FEATURE flag (rasterizationOrderColorAttachmentAccess) still needs a probe.
- **VK_KHR_dynamic_rendering + VK_KHR_dynamic_rendering_local_read** — exposed (the cleaner FSI-alternative).
- **VK_KHR_buffer_device_address + VK_EXT_descriptor_indexing** — exposed (bindless enablers).
NOTE: extension-listed != feature-usable. A one-fire feature-flag probe (vkGetPhysicalDeviceFeatures2 chain)
should confirm the FEATURE bits for ROAA / descriptor_buffer / dynamic_rendering_local_read / EDS3 sub-bits
before building on them. Most (buffer_device_address, descriptor_indexing, multi_draw, shaderFloat16,
extended_dynamic_state3, load_store_op_none[no feature]) are reliably ON in Mesa/Turnip when advertised.

## High-value EXPOSED-but-UNUSED extensions (0 refs in src/xenia), mapped to our bottlenecks
| Extension | Attacks | Verdict |
|---|---|---|
| **VK_KHR/EXT shader_float16_int8** (+RelaxedPrecision) | fragment SHADER ALU | the bd-recomp "STEAL"; **but BD overdraw is fixed-function raster/depth/alpha-test, NOT shader ALU** → likely small on BD; helps ALU-heavy pixel shaders only. Medium build (SPIR-V translator emits no RelaxedPrecision today), risky (approx-math), default-off + pixel-validate. |
| **VK_KHR_fragment_shading_rate (VRS)** | foliage fragment+texture invocations | **already SHIPPED** as `gpu_vrs_foliage_rate` (default-off) but **perf UNMEASURED** → validate on BD via gpu_freeze-matched A/B = the cheapest potential immediate win. |
| **buffer_device_address + descriptor_indexing/buffer** | Burnout shape-level shader-variant pipeline churn | **bindless vertex-fetch** = the format-churn escape (xenia uses in-shader vfetch w/ per-format SPIR-V variants → 260 pipeline_binds/frame on Burnout). Collapsing variants → fewer TBDR pipeline-switch context-rolls (a real GPU per-draw cost). LARGE rearch; the one big GPU lever for Burnout. GATE: confirm pipeline-switch context-roll is a measurable GPU cost first. |
| **VK_EXT_load_store_op_none** | EDRAM tile LOAD/STORE traffic (~20% of BD GPU per the skip-transfers probe) | mark transient/clear-only attachments NONE → skip redundant tile traffic. No feature flag. Bounded; applicability limited (most EDRAM tiles round-trip). Medium. |
| **dynamic_rendering(_local_read) / ROAA** | render-pass breaks + single-pass blend (EDRAM) | FSI-alternative. ⚠️ pass-reduction was fps-NEUTRAL on HEAVY BD (B35, thor-gpu-pipeline-rearch) → likely neutral on the heavy scene; for light/menu + hygiene. Large rearch. |
| **VK_EXT_multi_draw** | per-draw CP submission | batches draws → cuts CPU issue. **But Burnout CPU is neutral (GPU-paced, lock-free A/B); TBDR bins all verts regardless** → GPU-neutral unless per-DRAW (not per-vertex) overhead dominates (bd-draw-batching gate, UNPROVEN). Likely CPU-only/neutral. |
| **VK_KHR_synchronization2** | barrier precision | hygiene; cleaner/cheaper barriers. Low risk, low-moderate value. |
| **VK_EXT_graphics_pipeline_library / pipeline_creation_cache_control** | pipeline-create stutter | async/faster pipeline creation → less first-encounter hitching (stutter, not steady fps). |

## Honest bottleneck reality (why "more extensions" ≠ automatic fps)
BD = GPU-bound on per-vertex BINNING (Adreno wall: xenia binds 1 VS, Turnip derives the binning clone — a
cheaper binning VS is architecturally impossible; mesh shaders NOT exposed) + per-covered-fragment OVERDRAW
that is **fixed-function** (raster/depth/alpha-test defeating LRZ), not shader-ALU. Burnout = GPU-paced
(CPU micro-opts neutral, proven by the lock-free A/B). So the extensions that move these specific floors are
narrow: (1) VRS (cut foliage fragment/texture invocations — validate), (2) bindless (cut pipeline-switch
context-rolls on Burnout — gate then build), (3) reducing submitted verts/fragments (game-side LOD/cull, the
shipped foliage thinning). The rest are CPU/pass/stutter hygiene = neutral on these GPU-bound floors but real
for lighter/CPU-bound titles.

## Recommended build order
1. ✅ **Feature-flag probe** — DONE (2026-06-20): all FEATURE bits = 1, ROAA color+depth flipped to TRUE.
2. **ROAA single-pass EDRAM** (NOW UNBLOCKED, roaa_color/depth=1): the previously-blocked roaa-edram-path —
   read-modify-write the framebuffer in-tile WITHOUT render-pass breaks/EDRAM transfers for the cases that
   currently force them. ⚠️ gate: pass/transfer reduction was fps-NEUTRAL on HEAVY BD (B35) — target the
   blended-transparency path (34% of BD) + verify it cuts gpu_frame, not just passes. Medium-large.
3. **Validate VRS on BD** (gpu_freeze-matched A/B): shipped, Adreno-native, unmeasured — cheapest potential win.
   ⚠️ FREEZE APPROACH FAILED (2026-06-20 fire bdvrsoff): `gpu_freeze_at_guest_ms=151000` locked a WHITE
   loading/error screen (rendered=172, alphatest=0, 0.0 FPS) because this build hit `XamShowDirtyDiscErrorUI`
   ~21s in — BD's scene-vs-guest_ms mapping is non-deterministic (intermittent dirty-disc/load-timing race),
   so a fixed guest_ms freeze is unreliable. CORRECTED PLAN: FREE-RUNNING (no freeze), DurationSec ~280 (give
   BD time past any dirty-disc + into the field), `--ez gpu_uma_direct_shared_memory false`. The heavy field's
   gpu_frame_us is STABLE (~129ms ±0.3% across frames) so NO frame-match is needed — just confirm BOTH runs
   reach the field (rendered>800, comp alphatest>0) via the png, then compare the field's gpu_frame_us:
   `--ei gpu_vrs_foliage_rate 0` vs `2` (2x2) vs `4`. WILDCARD: BD intermittently hits the dirty-disc error
   (white screen) → retry until a clean boot reaches the field. The real fix to unblock all BD A/Bs is BD boot
   reliability (the dirty-disc race). Both cvars allowlisted. Needs a cool device.
4. **Bindless vertex fetch** (Burnout): gate (is pipeline-switch context-roll a measurable GPU cost?) → build.
   The one big GPU lever for Burnout's churn; buffer_device_address + descriptor_indexing CONFIRMED = 1.
5. Sync2 + pipeline_creation_cache_control hygiene (low-risk stacking toggles).
6. RelaxedPrecision/FP16 pixel shaders (shaderFloat16=1) + load_store_op_none: bounded/risky, lower priority.
