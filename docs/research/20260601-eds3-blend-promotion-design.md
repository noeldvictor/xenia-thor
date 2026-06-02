# EDS3 blend-state promotion — design + device-gating (2026-06-01)

Lever 1b of the binning re-arch: promote blend state to dynamic via
VK_EXT_extended_dynamic_state3 (EDS3), collapsing pipeline variants that differ only in
blend equation / blend enable / color write mask. Extends the shipped EDS1/core-1.3
promotion (cull/front-face/depth/stencil/topology). Goal: fewer pipeline variants →
longer eligible draw-merge runs → less Adreno binning-front-end churn.

## DO NOT build this blind — it is device-gated on THREE facts (build it WITH the device)
Unlike the shipped EDS states, this is NOT safe to build blind:
1. **Device-creation risk.** The shipped EDS states are Vulkan 1.3 CORE (already enabled, zero
   device-creation change). EDS3 is a SEPARATE extension that is NOT enabled in the provider
   today (no reference to VK_EXT_extended_dynamic_state3 anywhere). Promoting blend requires
   ENABLING the extension + chaining VkPhysicalDeviceExtendedDynamicState3FeaturesEXT in device
   creation. A subtle error in that pNext/feature chain breaks device creation → EVERY game
   black-screens, unvalidatable with the device gone. This is the riskiest possible blind change.
2. **Premature.** This lever only helps if pipeline-STATE-variant churn is a real merge blocker.
   The merge_miss[state] counter (shipped a1897f3d6) measures exactly that. READ IT FIRST: if
   merge_miss[state] is small relative to merge_miss[noncontig]/[topo], blend promotion is not
   worth the risk. Building the lever before reading the signal that justifies it is backwards.
3. **Unconfirmed support.** Adreno 740 EDS3 blend-sub-feature support could not be confirmed
   (gpuinfo.org is Cloudflare-bot-blocked, HTTP 403). The three sub-features are independently
   optional; the extension being present does NOT guarantee ColorBlendEquation specifically.

**Correct sequence (on reconnect):** (a) read merge_miss[state] on the Blue Dragon heavy scene;
(b) if state-churn is a significant blocker, confirm the device exposes the EDS3 blend
sub-features; (c) only then build the promotion below, WITH the device to validate the
device-creation change + rendering equivalence.

## Implementation plan (when device-gated greenlit)
Follow the shipped EDS pattern (cvar + pipeline-key-zero + dynamic emit + deferred-buffer method
+ function loading), but with the added EDS3 extension/feature/function plumbing.

1. **Provider device-creation (the risky part):** detect VK_EXT_extended_dynamic_state3 in the
   enumerated device extensions; if present, enable it + chain
   VkPhysicalDeviceExtendedDynamicState3FeaturesEXT and enable ONLY the sub-features the device
   reports: extendedDynamicState3ColorBlendEnable, extendedDynamicState3ColorBlendEquation,
   extendedDynamicState3ColorWriteMask. Store a per-sub-feature support flag. (Also needs
   independentBlend for per-RT differing blend, which the static path already relies on.)
2. **Function loading:** add a functions/*.inc for vkCmdSetColorBlendEnableEXT,
   vkCmdSetColorBlendEquationEXT, vkCmdSetColorWriteMaskEXT (mirror the existing EDS .inc).
3. **Mapping (the error-prone part — reuse existing code):** the Xenos→host blend mapping ALREADY
   exists for the static path in vulkan_pipeline_cache.cc:452-495: kBlendFactorMap[32] (:457) maps
   RB_BLENDCONTROL color/alpha src/dst factors → PipelineBlendFactor; plus the blend-op map and
   the constantAlphaColorBlendFactors device quirk (:488-495). For the dynamic path, translate the
   SAME PipelineBlendFactor + blend-op + write_mask into per-attachment VkColorBlendEquationEXT
   {srcColorBlendFactor,dstColorBlendFactor,colorBlendOp,srcAlphaBlendFactor,dstAlphaBlendFactor,
   alphaBlendOp}, a VkBool32 colorBlendEnable[], and a VkColorComponentFlags colorWriteMask[], for
   RT 0..3. Do NOT re-derive the mapping — factor the existing table so static + dynamic share it.
4. **Pipeline key (GetCurrentStateDescription):** when (cvar && all-3-sub-features-supported),
   zero the per-RT blend equation / enable / write-mask fields in the key so variants differing
   only in blend collapse to one VkPipeline (mirror the topology/cull key-zeroing).
5. **EnsurePipelineCreated:** append VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
   _COLOR_BLEND_EQUATION_EXT, _COLOR_WRITE_MASK_EXT to the dynamic_states array (gated).
6. **UpdateDynamicState:** emit the per-RT blend via the deferred buffer when gated, with the
   update-needed/changed tracking like the other EDS states.
7. **Deferred buffer:** add CmdVkSetColorBlendEnableEXT/EquationEXT/WriteMaskEXT.
8. **Gating:** new cvar vulkan_dynamic_state_blend (default false) + EmulatorActivity --ez
   allowlist. Use ONLY if (cvar && all three sub-features detected); else fall back to the static
   path (default bit-identical).

## Blend constants note
RB_BLEND_RED/GREEN/BLUE/ALPHA (the constant-color blend factors) map to vkCmdSetBlendConstants,
which is CORE dynamic state (not EDS3) — check whether the fork already emits it dynamically; if
the static path bakes constants into the pipeline, promoting the equation to dynamic may require
also making the constants dynamic (otherwise constant-only changes still spawn variants). Include
this in the variant-collapse accounting.

## Device validation (on reconnect, with device)
After the safety gate (temp<60C / busy-low; tools/thor/thor_evidence.ps1):
1. Confirm the EDS3 blend sub-features are detected (log) - if not, the lever is DEAD on Adreno
   740; stop.
2. Correctness gate: Blue Dragon at matched guest_ms, A = vulkan_dynamic_state_blend off vs B = on
   (--ez). Screenshots MUST be bit-identical (blend affects pixels - any divergence = emit/mapping
   bug = hard stop+revert).
3. Effect: with B on, merge_miss[state] should DROP and elig_runlen should LENGTHEN (the whole
   point); then the +vulkan_merge_draws step should convert that into a gpu_frame_us win. If
   merge_miss[state] doesn't drop, blend wasn't the variant driver - revert.

## Honest caveats
Device-gated + unvalidatable blind (device-creation change). Unconfirmed Adreno-740 support.
PREMATURE until merge_miss[state] (a1897f3d6) shows state-variant churn is a real blocker - this
lever is the natural payoff of that scaffolding, in that order. Not math; "approx" does not apply
(pipeline-state management). Per-RT independent blend depends on independentBlend (already used by
the static path). Sources: vulkan_pipeline_cache.cc:452-495 (existing static blend mapping);
Khronos VK_EXT_extended_dynamic_state3; research docs/research/20260601-arm64-adreno-speed-techniques.md.
