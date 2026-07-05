/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Blue Dragon native D3D9->Vulkan HLE renderer — Brick 1 (persistent native RT
 * + one render pass). See bd_native_renderer.h + docs/research/20260705-native-
 * vulkan-renderer-plan.md.
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/bd_native_renderer.h"

#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

bool BdNativeRenderer::Initialize(uint32_t width, uint32_t height) {
  if (!width || !height) {
    return false;
  }
  width_ = width;
  height_ = height;
  if (!CreateRenderPass()) {
    XELOGE("BdNativeRenderer: render pass creation failed");
    Shutdown();
    return false;
  }
  // TODO(bd-native, Brick 1b): allocate color_image_/depth_image_ (VkImage +
  // device memory via the provider's memory allocator) + views + framebuffer,
  // sized width_ x height_, single-sample, native tiling (OPTIMAL). The render
  // pass here is deliberately created first (standalone) so it can be validated
  // on desktop `--gpu=vulkan` via RenderDoc before wiring the image/framebuffer.
  XELOGI(
      "BdNativeRenderer: initialized native render pass {}x{} (color=RGBA8, "
      "depth=D24S8, LOAD_OP_CLEAR for TBDR-LRZ validity). Bricks 2-4: images + "
      "native pipelines + captured-draw submit + present.",
      width_, height_);
  return true;
}

bool BdNativeRenderer::CreateRenderPass() {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // One color + one depth attachment. TBDR super-optimization (the 30fps axis on
  // Turnip): LOAD_OP_CLEAR (NOT LOAD — LOAD_OP_LOAD is what kills Turnip LRZ every
  // pass on the LLE path), STORE_OP_STORE for color (presented), DONT_CARE for
  // depth store (transient, GMEM-resident within the pass). This is the one
  // held-open pass that replaces BD's 95-pass EDRAM structure.
  VkAttachmentDescription attachments[2] = {};
  // Color.
  attachments[0].format = kColorFormat;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  // Depth (transient — never stored, GMEM-resident, keeps LRZ valid in-pass).
  attachments[1].format = kDepthFormat;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_ref = {};
  color_ref.attachment = 0;
  color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference depth_ref = {};
  depth_ref.attachment = 1;
  depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;
  subpass.pDepthStencilAttachment = &depth_ref;

  VkRenderPassCreateInfo render_pass_create_info = {};
  render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_create_info.attachmentCount = 2;
  render_pass_create_info.pAttachments = attachments;
  render_pass_create_info.subpassCount = 1;
  render_pass_create_info.pSubpasses = &subpass;

  if (dfn.vkCreateRenderPass(device, &render_pass_create_info, nullptr,
                             &render_pass_) != VK_SUCCESS) {
    render_pass_ = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

void BdNativeRenderer::Shutdown() {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  if (!vulkan_device) {
    return;
  }
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  if (framebuffer_ != VK_NULL_HANDLE) {
    dfn.vkDestroyFramebuffer(device, framebuffer_, nullptr);
    framebuffer_ = VK_NULL_HANDLE;
  }
  if (render_pass_ != VK_NULL_HANDLE) {
    dfn.vkDestroyRenderPass(device, render_pass_, nullptr);
    render_pass_ = VK_NULL_HANDLE;
  }
  if (color_view_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, color_view_, nullptr);
    color_view_ = VK_NULL_HANDLE;
  }
  if (color_image_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, color_image_, nullptr);
    color_image_ = VK_NULL_HANDLE;
  }
  if (color_memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, color_memory_, nullptr);
    color_memory_ = VK_NULL_HANDLE;
  }
  if (depth_view_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, depth_view_, nullptr);
    depth_view_ = VK_NULL_HANDLE;
  }
  if (depth_image_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, depth_image_, nullptr);
    depth_image_ = VK_NULL_HANDLE;
  }
  if (depth_memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, depth_memory_, nullptr);
    depth_memory_ = VK_NULL_HANDLE;
  }
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
