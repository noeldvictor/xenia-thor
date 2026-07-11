`R16G16B16A16_SFLOAT` is 64 bpp; `A2B10G10R10_UNORM_PACK32` is 32 bpp. Mutable views still require compatible format classes. They cannot change texel size, and the bit layouts would be meaningless anyway. [Vulkan image-view rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html)

### C. Format-converting on-tile resolve

Yes: `VK_EXT_custom_resolve` is the intended solution.

The extension exists specifically because fixed-function resolves cannot change format and separate shader resolves force tiled GPUs to materialize the MSAA source. It permits a final shader-resolve subpass that reads the multisampled input attachment and writes a different-format single-sampled color attachment. [Khronos custom-resolve proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_custom_resolve.html)

Turnip’s implementation is especially favorable:

- The MSAA input stays GMEM-backed.
- For a custom-resolve subpass, Turnip does not allocate the output color attachment in GMEM.
- The custom-resolve draw writes the A2B10 destination directly to system memory.
- Therefore there is one necessary 32-bpp materialization, no float16 intermediate, and no separate render pass. [Turnip render-pass source](https://chromium.googlesource.com/external/gitlab.freedesktop.org/mesa/mesa/+/bc649a02d4c1ca2148ba6eb2e0a83fa774b4d037/src/freedreno/vulkan/tu_pass.cc)

`VK_EXT_multisampled_render_to_single_sampled` alone cannot perform this cross-format conversion. It is useful only for same-format implicit MSAA, such as the lossy direct-A2B10 path.

## Q3: least-pass wiring

Use a two-subpass legacy render pass:

1. Producer subpass:

   - MSAA `R16G16B16A16_SFLOAT` color.
   - `storeOp=DONT_CARE`.
   - Existing depth attachment as required.

2. Final custom-resolve subpass:

   - Set `VK_SUBPASS_DESCRIPTION_CUSTOM_RESOLVE_BIT_EXT`.
   - Read the float16 source as `subpassInputMS`.
   - Write a 1× `A2B10G10R10_UNORM_PACK32` color attachment.
   - Destination `loadOp=DONT_CARE`, `storeOp=STORE`.
   - `pResolveAttachments = nullptr`; custom-resolve subpasses must not use fixed resolve attachments.
   - One fullscreen triangle, exactly one fragment per pixel; no blend, depth, discard, or overdraw.
   - Use the framebuffer-local `BY_REGION` dependency above.

The shader must reproduce the existing resolve semantics, not blindly average all samples:

- Honor `copy_sample_select`: single sample, `01`, `23`, or `0123`.
- Apply the copy exponent bias after the selected-sample average.
- Apply destination swap.
- Clamp/quantize for the destination format.


Only use this replacement for identity/local resolve edges. Input attachments cannot implement arbitrary source/destination offsets, scaling, or neighboring reads. Non-local guest resolve dispatches must remain fail-closed until their resource mapping is normalized.

Folding conversion into the later composite shader is a fallback, not the preferred general design:

- It eliminates a conversion pass.
- But it still requires the 1× float16 resolve/store at 64 bpp.
- If the composite filters the texture, conversion after filtering is not equivalent to materializing A2B10 before filtering.

The absolute minimum is to make the custom-resolve shader perform the composite itself and write the final composite target. Do that only if the field lookup is 1:1 framebuffer-local and all bloom/other inputs already exist. Otherwise materialize A2B10 once through custom resolve and let the existing composite sample it normally.

## Recommendation

Proceed with C+D under this definition:

- C = custom-resolve shader subpass, not a separate conversion pass.
- D = drop each ownership-transfer pass and resolve dispatch only after its custom-resolve output is live and every consumer is redirected.

Before testing, also fix:

- Begin the actual resolve/custom render-pass variant.
- Match the native framebuffer’s logical extent rather than the base `host_extent`.
- Append the FDM view after the custom output if `use_fdm` is enabled.
- Create/cache producer and custom-resolve pipeline variants for subpasses 0 and 1.

Falsify it with one field publication edge first. Success requires:

- One fewer render-pass begin/end.
- One fewer ownership-transfer pass and matching resolve dispatch.
- Producer remains in Turnip GMEM mode.
- No 64-bpp MSAA or 1× float16 store.
- One 32-bpp A2B10 output write.
- The between-pass gap falls without changing the resolved image.

If the dependency causes Turnip to choose sysmem, or the producer-pass increase consumes roughly the entire deleted transfer cost, stop. Otherwise this is the strongest available route to deleting the measured 30 ms wall.

The external hard-review helper could not initialize because this session is filesystem-read-only; the verdict above comes from the direct Vulkan, Turnip, and repo-source audit.
[2mtokens used[0m
343,318
Proceed with D, but change C. Q1 does not kill the architecture; it kills the claim that float16 `pResolveAttachments` is “free.” Do not build a later conversion pass. Make C an in-render-pass `VK_EXT_custom_resolve` subpass that performs resolve + format conversion together.

## Q1: what Turnip actually does

Turnip does not turn `pResolveAttachments` into a hidden full-frame render pass.

In GMEM mode, Turnip emits resolves from `tu6_emit_gmem_resolves` as part of the per-tile store command stream, calling `tu_store_gmem_attachment`. The final subpass emits this into `tile_store_cs`; afterward, ordinary attachment stores are emitted. [Turnip command-buffer source](https://chromium.googlesource.com/external/gitlab.freedesktop.org/mesa/mesa/+/bc649a02d4c1ca2148ba6eb2e0a83fa774b4d037/src/freedreno/vulkan/tu_cmd_buffer.cc)

But your format matters:

- `R16G16B16A16_SFLOAT` is rejected by Turnip’s fast resolve event because `blit_can_resolve` rejects color channels wider than 10 bits—explicitly all float formats.
- Turnip therefore chooses `TU_GMEM_LAYOUT_AVOID_CCU` and uses its `store_3d_blit` fallback.
- That is a per-tile 3D resolve-store, not another Vulkan render pass. The MSAA source remains in GMEM; only the resolved 1× float16 image is written externally. [Turnip clear/blit source](https://chromium.googlesource.com/external/gitlab.freedesktop.org/mesa/mesa/+/6a7d3020a781a191e21c35ed9410d9da73e0de4a/src/freedreno/vulkan/tu_clear_blit.cc)

So the precise verdict is:

- No hidden separate pass.
- No MSAA external spill and reload.
- `storeOp=DONT_CARE` does prevent the additional ordinary MSAA store in Turnip.
- But it is not zero-cost: it performs a slow-path per-tile resolve and writes 64 bpp at 1×.
- It is still fundamentally cheaper than your proven-dead separate-pass conversion.

One further constraint: a non-framebuffer-local subpass dependency can make Turnip disable GMEM rendering. Keep the producer→resolve dependency strictly `COLOR_ATTACHMENT_OUTPUT/WRITE → FRAGMENT_SHADER/INPUT_ATTACHMENT_READ` with `VK_DEPENDENCY_BY_REGION_BIT`. Mesa documents Adreno’s GMEM/sysmem split and autotuning here: [Freedreno architecture](https://docs.mesa3d.org/drivers/freedreno.html).


## Q2: eliminating the conversion

### A. Render directly into A2B10

Not correctness-equivalent.

Xenos 7e3 and A2B10 UNORM have the same 32-bit footprint but different number systems. For example, the 7e3 encoding `0x180` represents `1.0`; interpreted as 10-bit UNORM it is approximately `0.375`. Conversely, UNORM’s `0x3FF` represents `1.0`, while 7e3 decodes it as `31.875`.


Use it only as:

- An aggressive BD quality/performance mode.
- A performance upper bound.
- Not the correctness-preserving architecture.

It does have one performance advantage: A2B10 passes Turnip’s fast resolve-event test, unlike float16.

### B. A2B10 view of the float16 image

Impossible.

`R16G16B16A16_SFLOAT` is 64 bpp; `A2B10G10R10_UNORM_PACK32` is 32 bpp. Mutable views still require compatible format classes. They cannot change texel size, and the bit layouts would be meaningless anyway. [Vulkan image-view rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewCreateInfo.html)

### C. Format-converting on-tile resolve

Yes: `VK_EXT_custom_resolve` is the intended solution.

The extension exists specifically because fixed-function resolves cannot change format and separate shader resolves force tiled GPUs to materialize the MSAA source. It permits a final shader-resolve subpass that reads the multisampled input attachment and writes a different-format single-sampled color attachment. [Khronos custom-resolve proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_custom_resolve.html)

Turnip’s implementation is especially favorable:

- The MSAA input stays GMEM-backed.
- For a custom-resolve subpass, Turnip does not allocate the output color attachment in GMEM.
- The custom-resolve draw writes the A2B10 destination directly to system memory.
- Therefore there is one necessary 32-bpp materialization, no float16 intermediate, and no separate render pass. [Turnip render-pass source](https://chromium.googlesource.com/external/gitlab.freedesktop.org/mesa/mesa/+/bc649a02d4c1ca2148ba6eb2e0a83fa774b4d037/src/freedreno/vulkan/tu_pass.cc)

`VK_EXT_multisampled_render_to_single_sampled` alone cannot perform this cross-format conversion. It is useful only for same-format implicit MSAA, such as the lossy direct-A2B10 path.

## Q3: least-pass wiring

Use a two-subpass legacy render pass:

1. Producer subpass:

   - MSAA `R16G16B16A16_SFLOAT` color.
   - `storeOp=DONT_CARE`.
   - Existing depth attachment as required.

2. Final custom-resolve subpass:

   - Set `VK_SUBPASS_DESCRIPTION_CUSTOM_RESOLVE_BIT_EXT`.
   - Read the float16 source as `subpassInputMS`.
   - Write a 1× `A2B10G10R10_UNORM_PACK32` color attachment.
   - Destination `loadOp=DONT_CARE`, `storeOp=STORE`.
   - `pResolveAttachments = nullptr`; custom-resolve subpasses must not use fixed resolve attachments.
   - One fullscreen triangle, exactly one fragment per pixel; no blend, depth, discard, or overdraw.
   - Use the framebuffer-local `BY_REGION` dependency above.

The shader must reproduce the existing resolve semantics, not blindly average all samples:

- Honor `copy_sample_select`: single sample, `01`, `23`, or `0123`.
- Apply the copy exponent bias after the selected-sample average.
- Apply destination swap.
- Clamp/quantize for the destination format.


Only use this replacement for identity/local resolve edges. Input attachments cannot implement arbitrary source/destination offsets, scaling, or neighboring reads. Non-local guest resolve dispatches must remain fail-closed until their resource mapping is normalized.

Folding conversion into the later composite shader is a fallback, not the preferred general design:

- It eliminates a conversion pass.
- But it still requires the 1× float16 resolve/store at 64 bpp.
- If the composite filters the texture, conversion after filtering is not equivalent to materializing A2B10 before filtering.

The absolute minimum is to make the custom-resolve shader perform the composite itself and write the final composite target. Do that only if the field lookup is 1:1 framebuffer-local and all bloom/other inputs already exist. Otherwise materialize A2B10 once through custom resolve and let the existing composite sample it normally.

## Recommendation

Proceed with C+D under this definition:

- C = custom-resolve shader subpass, not a separate conversion pass.
- D = drop each ownership-transfer pass and resolve dispatch only after its custom-resolve output is live and every consumer is redirected.

Before testing, also fix:

- Begin the actual resolve/custom render-pass variant.
- Match the native framebuffer’s logical extent rather than the base `host_extent`.
- Append the FDM view after the custom output if `use_fdm` is enabled.
- Create/cache producer and custom-resolve pipeline variants for subpasses 0 and 1.

Falsify it with one field publication edge first. Success requires:

- One fewer render-pass begin/end.
- One fewer ownership-transfer pass and matching resolve dispatch.
- Producer remains in Turnip GMEM mode.
- No 64-bpp MSAA or 1× float16 store.
- One 32-bpp A2B10 output write.
- The between-pass gap falls without changing the resolved image.

If the dependency causes Turnip to choose sysmem, or the producer-pass increase consumes roughly the entire deleted transfer cost, stop. Otherwise this is the strongest available route to deleting the measured 30 ms wall.

The external hard-review helper could not initialize because this session is filesystem-read-only; the verdict above comes from the direct Vulkan, Turnip, and repo-source audit.

================================================================================
# BUILD PLAN — custom-resolve field path (resumable, exact API + step order)
# (appended by the build session 2026-07-11 after 5.6-sol verdict + on-device
#  confirmation + exact upstream API extraction)
================================================================================

## STATUS
- DONE + committed (283fddbab): device-support foundation.
  - `src/xenia/ui/vulkan/vulkan_custom_resolve_ext.h` — shim (structs, sTypes
    1000628000/1/2, VK_SUBPASS_DESCRIPTION_CUSTOM_RESOLVE_BIT_EXT=0x8,
    PFN_vkCmdBeginCustomResolveEXT). Guarded on `#ifndef VK_EXT_custom_resolve`.
  - vulkan_device.h: Extensions::ext_EXT_custom_resolve, Properties::customResolve.
  - vulkan_device.cc: STRUCT_EXTENSION request + features_EXT_custom_resolve
    (VulkanFeatures<...FEATURES_EXT>) + Link + FEATURE_2 enable. COMPILES.
- DONE + committed (b99035aae): pResolveAttachments render-pass variant + producer
  1x resolve image. ** SUPERSEDED for the float field ** (pResolveAttachments is
  Turnip slow-path for float16 per Q1) but reusable machinery (variant key, resolve
  image alloc/cleanup, GetBdNativeColorSwizzledView resolve-sample).
- CONFIRMED on-device: Turnip (Mesa 26.0) exposes VK_EXT_custom_resolve
  (driver .so: tu_emit_custom_resolve_end chip6/7/8). Desktop NVIDIA does NOT ->
  Thor-only validation; desktop keeps the LLE transfer path (fallback when
  !customResolve).
- DEAD (ledger): fp10_color_as_unorm10 (black HDR clamp), pResolveAttachments float16
  (slow store_3d_blit), A2B10-view-of-float16 (bpp mismatch), separate convert pass
  (1.8fps).

## EXACT API (from upstream vulkan_core.h, in the shim)
- VkPhysicalDeviceCustomResolveFeaturesEXT { sType; void* pNext; VkBool32 customResolve; }
    sType = 1000628000
- VkBeginCustomResolveInfoEXT { sType; void* pNext; }   sType = 1000628001  (DYNAMIC-rendering only)
- VkCustomResolveCreateInfoEXT { sType; const void* pNext; VkBool32 customResolve;
    uint32_t colorAttachmentCount; const VkFormat* pColorAttachmentFormats;
    VkFormat depthAttachmentFormat; VkFormat stencilAttachmentFormat; }  sType = 1000628002
- VK_SUBPASS_DESCRIPTION_CUSTOM_RESOLVE_BIT_EXT = 0x00000008  (legacy subpass flag)
- vkCmdBeginCustomResolveEXT — DYNAMIC-rendering path only; NOT needed for the legacy
  two-subpass render pass (the subpass flag drives it). No End command (runs to pass end).

## THE RENDER-PASS REWORK (the remaining fps build)
Target ONE edge first: the main field publication (471/473 -> the A2B10 texture the
composite samples, base=1DC14000-class, fetchfmt=64). Falsify before generalizing.

1. RENDER PASS (vulkan_render_target_cache.cc GetHostRenderTargetsRenderPass, add a
   `bd_custom_resolve` variant alongside the existing bd_color_resolve):
   - TWO subpasses in ONE VkRenderPass (VkRenderPassCreateInfo, subpassCount=2):
     - subpass 0 (PRODUCER): the existing color(MSAA float16 R16G16B16A16_SFLOAT)+depth
       attachment refs. color storeOp=DONT_CARE (only the resolve is kept). depth as today.
     - subpass 1 (CUSTOM RESOLVE): flags |= VK_SUBPASS_DESCRIPTION_CUSTOM_RESOLVE_BIT_EXT.
       pInputAttachments = { the MSAA float16 color, layout SHADER_READ_ONLY }.
       pColorAttachments = { the 1x A2B10G10R10_UNORM_PACK32 output, layout COLOR }.
       No depth. pResolveAttachments = nullptr (custom-resolve forbids fixed resolve refs).
   - Attachments array: [depth(0), MSAA float16 color(1), A2B10 resolve-out(2)].
     A2B10 out: loadOp=DONT_CARE, storeOp=STORE, finalLayout=SHADER_READ_ONLY.
   - Subpass DEPENDENCY 0->1: srcStage COLOR_ATTACHMENT_OUTPUT / srcAccess
     COLOR_ATTACHMENT_WRITE -> dstStage FRAGMENT_SHADER / dstAccess INPUT_ATTACHMENT_READ,
     dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT.  ** MUST be BY_REGION framebuffer-local
     or Turnip disables GMEM (5.6-sol). ** Plus the usual EXTERNAL deps.
2. FRAMEBUFFER (GetBdNativeColorProducerFramebuffer): attachments
   [depth, MSAA float16 producer view, A2B10 1x resolve-out view]. The A2B10 image is the
   resolve image already allocated by piece B (change its format to A2B10 = color_format
   under the composite's expected fetch fmt, and add INPUT_ATTACHMENT usage to the MSAA
   producer image). extent = logical prod_width/height (already done).
3. CONVERT SHADER (new fragment shader, adapt GetBdNativeConvertShader):
   - Read the MSAA float16 via a subpassInputMS (OpTypeImage Dim=SubpassData, MS=1) +
     OpImageRead per sample (NOT a sampled image / texelFetch). Input attachment index 0.
   - Reproduce the EDRAM resolve semantics EXACTLY (5.6-sol): honor copy_sample_select
     (single / 01 / 23 / 0123 average — NOT blind average-all), apply copy exp_bias AFTER
     the sample average, apply dest swap, clamp/quantize. Output float4 -> the A2B10
     attachment (hw packs). Push-constants as today {exp_bias, swap, sample_count} + add
     sample_select.
   - Pipeline for subpass 1: fullscreen triangle, no depth/blend/discard, one frag/pixel.
     Chain VkCustomResolveCreateInfoEXT into the pipeline (fragment-output stage) with
     customResolve=VK_TRUE, colorAttachmentCount=1, pColorAttachmentFormats={A2B10}.
   - Producer pipelines (subpass 0) are the normal BD field pipelines but with
     subpass index 0 in this 2-subpass pass — pipeline cache must key subpass index.
4. DRAW FLOW (vulkan_command_processor): after the field's producer draws (subpass 0),
   vkCmdNextSubpass -> bind the convert pipeline -> vkCmdDraw(3) fullscreen. (Legacy path;
   no vkCmdBeginCustomResolveEXT.) Gate on device customResolve + keep_scissor.
5. CONSUMER: composite already samples the A2B10 producer view (identity-format gate now
   passes: A2B10==fetch 64). The resolve-out image IS what GetBdNativeColorSwizzledView
   returns (piece C already prefers resolve image). native_fmt = A2B10.
6. DROP (piece D, already exists): lifetime_hle>=6 BdL5DropSafe drops the field's EDRAM
   color transfer once every consumer read native. No new code.

## FALSIFY (5.6-sol success criteria, measure on Thor frozen frame):
- one fewer render-pass begin/end; one fewer ownership-transfer pass + resolve dispatch;
- producer stays Turnip GMEM (check no sysmem fallback in a GPU trace);
- NO 64bpp MSAA store, NO 1x float16 store; ONE 32bpp A2B10 write;
- the between-pass GAP falls, resolved image unchanged (screenshot identical to LLE).
- STOP if the BY_REGION dep forces sysmem OR the added producer-subpass cost ~= the
  deleted transfer cost.

## GOTCHAS
- Thor-only: gate everything on device->extensions_.ext_EXT_custom_resolve &&
  properties.customResolve; desktop path unchanged (LLE transfer).
- vulkan_validation=true on first device run (strict Turnip crashes where desktop tolerates).
- Input-attachment reads need the MSAA producer image created with
  VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT (add to the producer image usage).
- Pipeline subpass-index keying: the field producer pipelines must be created for subpass 0
  of this 2-subpass pass (VkGraphicsPipelineCreateInfo::subpass=0), the convert for subpass=1.
