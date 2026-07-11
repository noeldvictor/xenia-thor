The root cause is a render-pass state-machine bug, not special `VK_EXT_custom_resolve` layout semantics.

## Q1 — Root cause

The deterministic failure is:

1. RT-A begins the two-subpass custom-resolve pass in subpass 0.
2. BD switches to another RT.
3. The RT-switch path calls raw `CmdVkEndRenderPass()` at [vulkan_command_processor.cc:4648](</C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/gpu/vulkan/vulkan_command_processor.cc:4648>).
4. That bypasses the only path that records `vkCmdNextSubpass` plus the resolve draw at [line 5139](</C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/gpu/vulkan/vulkan_command_processor.cc:5139>) and clears `bd_custom_resolve_render_pass_` at [line 5148](</C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/gpu/vulkan/vulkan_command_processor.cc:5148>).

Ending while still in subpass 0 violates `VUID-vkCmdEndRenderPass-None-00910`: the current subpass must be the last subpass. [Vulkan render-pass command rules](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)

Worse, the stale custom-resolve handle remains armed. On the next RT:

- It overrides the next render-pass choice.
- It may be paired with an unrelated ordinary framebuffer.
- Pipeline creation also receives the stale CR render pass.
- The previous resolve output may be published even though subpass 1 never wrote it.

That exactly explains the apparently contradictory same-image reports:

- One recorded use treats the image as a color attachment.
- Another descriptor expects it shader-readable.
- The global layout state is still undefined because the valid final-subpass/end transition never occurred.

So, among your choices: primarily **(c)/(d)**—RT interleaving exposes stale/bypassed pass state. **(a)** is a resulting symptom: the composite can be handed an output generation that was never produced. **(b)** is false.

Also, `07752` is independent, but not harmless noise. `Arrayed=1` must use a compatible array view, normally `VK_IMAGE_VIEW_TYPE_2D_ARRAY`, even with one layer. Prior success only means Turnip tolerated the undefined use. [VUID 07752](https://docs.vulkan.org/spec/latest/chapters/drawing.html)

## Q2 — Correct legacy custom-resolve declaration

Your attachment declaration is correct:

- MSAA producer: subpass-0 `pColorAttachments`, subpass-1 `pInputAttachments`.
- Single-sample A2B10 output: subpass-1 normal `pColorAttachments`.
- `pResolveAttachments = nullptr` or all `VK_ATTACHMENT_UNUSED`.

For a legacy custom-resolve subpass, putting A2B10 in `pResolveAttachments` would be invalid; `VUID-VkSubpassDescription-flags-03341` explicitly requires custom-resolve subpasses not to use fixed-function resolve attachments. [VkSubpassDescription](https://docs.vulkan.org/refpages/latest/refpages/source/VkSubpassDescription.html)

`finalLayout=SHADER_READ_ONLY_OPTIMAL` is honored normally. Vulkan defines `finalLayout` as the transition performed when the render-pass instance ends, with no custom-resolve exception. [Attachment and automatic-transition semantics](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)

One nuance: because this pipeline has a valid legacy `VkRenderPass`, the spec says `VkCustomResolveCreateInfoEXT` parameters are ignored. The subpass flag controls the legacy path; that `pNext` structure is primarily dynamic-rendering format metadata. It may be removed for clarity. [VkCustomResolveCreateInfoEXT](https://docs.vulkan.org/refpages/latest/refpages/source/VkCustomResolveCreateInfoEXT.html)

Turnip’s source implements the optimization by not marking a custom-resolve subpass’s output color attachment as GMEM-backed—implying direct memory output—while the prior producer remains GMEM-backed. That does not bypass Vulkan store or layout rules. [Turnip `tu_pass.cc`](https://chromium.googlesource.com/external/gitlab.freedesktop.org/mesa/mesa/+/bc649a02d4c1ca2148ba6eb2e0a83fa774b4d037/src/freedreno/vulkan/tu_pass.cc)

## Q3 — Concrete fix

First, fix every pass-ending path:

```cpp
if (current_render_pass_ != VK_NULL_HANDLE) {
  ++rt_pass_break_rt_change_;
  EndRenderPass();  // Must perform CR NextSubpass/draw/end/clear too.
}
```

Remove the duplicated raw-end sequence at line 4648. Make `EndRenderPass()` the sole guest render-pass terminator.

Then:

1. Track a dedicated active CR framebuffer/pass, not just one global render-pass handle. Reset/assert-null before arming every new pass.

2. Ensure `vkCmdNextSubpass` is recorded exactly once even if pipeline/descriptor allocation fails. If the resolve draw fails, legally enter subpass 1, end the pass, mark the output invalid, and retain the LLE fallback.

3. Publish/redirect/drop only after a successful subpass-1 draw and legal pass end. Use a per-generation `cr_output_valid`, not merely “this framebuffer supports CR.” Until valid, the composite and transfer-drop path must stay LLE.

4. Narrow CR to the exact main-field resolve edge initially. The current broad gate at [vulkan_render_target_cache.cc:3720](</C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/gpu/vulkan/vulkan_render_target_cache.cc:3720>) catches bloom too, while the resolve finalizer hardcodes field-specific `exp_bias=0.25` and `swap=1`. Gate by learned source RT key/destination resource and copy parameters, not only dimensions/format/MSAA.

5. Fix interleaved-surface persistence. With A-B-A rendering, `att0 loadOp/storeOp=DONT_CARE` loses A’s earlier MSAA color. For correctness bring-up, use:

   - Intermediate/reentered producer: `LOAD`, `STORE`, initial/final `COLOR_ATTACHMENT_OPTIMAL`.
   - First use: a compatible `UNDEFINED/DONT_CARE` variant or explicit initialization.
   - Preserve depth similarly if later A passes reuse it.
   - A2B10 output remains `DONT_CARE`, `STORE`, final shader-read.

After correctness, optimize the terminal A pass to `LOAD + storeOp=DONT_CARE` and run custom resolve there. You cannot keep MSAA contents solely in GMEM across a real A→B framebuffer switch; an intermediate store, pass fusion, or safe reordering is fundamentally required.

6. Keep the 0→1 dependency `BY_REGION`. For 1→external, remove `BY_REGION` unless the later composite access is proven same-pixel framebuffer-local; scaled/filtering sampling needs a global dependency.

7. Change the sampled HLE views used by `xe_texture*_2d_*` to `VK_IMAGE_VIEW_TYPE_2D_ARRAY`. The framebuffer/input-attachment view can remain the type required by its shader.

Do not add a manual att1 layout transition as the primary fix. Once the pass reaches subpass 1 and ends legally, its `finalLayout` performs that transition. A barrier would only mask one symptom while the stale-pass bug remains.

The first clean test should be field-only CR, LLE transfer still retained, zero validation errors, and counters proving `CR begin == NextSubpass == CR draw == end`. Then enable native sampling, and only finally drop the EDRAM transfer.
271,737
The root cause is a render-pass state-machine bug, not special `VK_EXT_custom_resolve` layout semantics.

## Q1 — Root cause

The deterministic failure is:

1. RT-A begins the two-subpass custom-resolve pass in subpass 0.
2. BD switches to another RT.
3. The RT-switch path calls raw `CmdVkEndRenderPass()` at [vulkan_command_processor.cc:4648](</C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/gpu/vulkan/vulkan_command_processor.cc:4648>).
