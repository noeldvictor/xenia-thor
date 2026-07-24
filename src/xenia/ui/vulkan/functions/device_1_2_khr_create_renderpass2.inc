// VK_KHR_create_renderpass2 functions used in Xenia.
// Promoted to Vulkan 1.2 core.
// Needed for VkSubpassDescriptionDepthStencilResolve: a DEPTH resolve can only be
// attached to a subpass through the RenderPass2 structures - the legacy
// VkSubpassDescription has no pDepthStencilResolveAttachment, and
// vkCmdResolveImage is color-only. This is the TBDR-correct way to serve BD's MSAA
// depth conversions (the resolve rides the pass's existing GMEM tile store instead
// of costing a separate render pass).
XE_UI_VULKAN_FUNCTION_PROMOTED(vkCreateRenderPass2KHR, vkCreateRenderPass2)
