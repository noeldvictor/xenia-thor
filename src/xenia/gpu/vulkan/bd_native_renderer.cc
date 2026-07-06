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
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
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
  if (!CreateRenderPass() || !CreateImages() || !CreateFramebuffer()) {
    XELOGE("BdNativeRenderer: initialization failed ({}x{})", width_, height_);
    Shutdown();
    return false;
  }
  XELOGI(
      "BdNativeRenderer: initialized native RT {}x{} (color=RGBA8 STORE, "
      "depth=D24S8 DONT_CARE-store/GMEM-resident, LOAD_OP_CLEAR for TBDR-LRZ "
      "validity), one held render pass + framebuffer. Bricks 2-4: native "
      "pipelines + captured-draw submit + present.",
      width_, height_);
  return true;
}

void BdNativeRenderer::RenderFrame(DeferredCommandBuffer& command_buffer) {
  if (!initialized() || framebuffer_ == VK_NULL_HANDLE) {
    return;
  }
  // Clear color to a distinct debug magenta (Brick 2a: proves the native RT +
  // one-pass path records end-to-end before the draws land) and depth to the
  // reverse-Z far plane (0.0 - BD uses reverse-Z; Brick 3 depth-prepass).
  VkClearValue clear_values[2] = {};
  clear_values[0].color = {{1.0f, 0.0f, 1.0f, 1.0f}};
  clear_values[1].depthStencil = {0.0f, 0};

  VkRenderPassBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  begin_info.renderPass = render_pass_;
  begin_info.framebuffer = framebuffer_;
  begin_info.renderArea.extent.width = width_;
  begin_info.renderArea.extent.height = height_;
  begin_info.clearValueCount = 2;
  begin_info.pClearValues = clear_values;

  // Record into xenia's deferred stream (replayed on the CP worker thread).
  command_buffer.CmdVkBeginRenderPass(&begin_info, VK_SUBPASS_CONTENTS_INLINE);
  // Brick 2b-3: record the captured 0x82489F40 draws here (bind native pipeline +
  // VkBuffer vertex/index, CmdVkDrawIndexed), depth-prepass first for early-Z
  // self-overdraw reject. One pass, no fan-out, no EDRAM.
  command_buffer.CmdVkEndRenderPass();
}

bool BdNativeRenderer::EnsureColorFormat(VkFormat color_format,
                                        VkFormat depth_format,
                                        VkSampleCountFlagBits samples) {
  if (color_format == VK_FORMAT_UNDEFINED ||
      depth_format == VK_FORMAT_UNDEFINED) {
    return false;
  }
  if (color_format_ == color_format && depth_format_ == depth_format &&
      samples_ == samples && framebuffer_ != VK_NULL_HANDLE) {
    return true;
  }
  // Recreate everything to match the field's color+depth formats + samples so the
  // native render pass is render-pass-COMPATIBLE with the field pipelines (format/
  // sample mismatch = silent skip/black). Called before a field draw, images not
  // in use. Logs once per config for RE.
  Shutdown();
  color_format_ = color_format;
  depth_format_ = depth_format;
  samples_ = samples;
  if (!CreateRenderPass() || !CreateImages() || !CreateFramebuffer()) {
    Shutdown();
    return false;
  }
  static uint64_t s_logged = ~uint64_t(0);
  uint64_t cfg = (uint64_t(color_format) << 32) | uint32_t(depth_format);
  if (s_logged != cfg) {
    s_logged = cfg;
    XELOGI("BdNativeRenderer: matched native RT to field color={} depth={} samp={}",
           uint32_t(color_format), uint32_t(depth_format), uint32_t(samples));
  }
  return true;
}

bool BdNativeRenderer::CreateImages() {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // Native (non-EDRAM) device-local color + depth images, full-surface, single-
  // sample, OPTIMAL tiling. Color is SAMPLED (blit/present) + COLOR_ATTACHMENT;
  // depth is DEPTH_STENCIL_ATTACHMENT only (transient, never sampled/stored).
  VkImageCreateInfo image_create_info = {};
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.extent.width = width_;
  image_create_info.extent.height = height_;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = samples_;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  image_create_info.format = color_format_;
  image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device, image_create_info,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, color_image_,
          color_memory_)) {
    return false;
  }

  image_create_info.format = depth_format_;
  image_create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device, image_create_info,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, depth_image_,
          depth_memory_)) {
    return false;
  }

  VkImageViewCreateInfo view_create_info = {};
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create_info.subresourceRange.levelCount = 1;
  view_create_info.subresourceRange.layerCount = 1;

  view_create_info.image = color_image_;
  view_create_info.format = color_format_;
  view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  if (dfn.vkCreateImageView(device, &view_create_info, nullptr, &color_view_) !=
      VK_SUCCESS) {
    return false;
  }

  view_create_info.image = depth_image_;
  view_create_info.format = depth_format_;
  view_create_info.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  if (dfn.vkCreateImageView(device, &view_create_info, nullptr, &depth_view_) !=
      VK_SUCCESS) {
    return false;
  }
  return true;
}

bool BdNativeRenderer::CreateFramebuffer() {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkImageView attachments[2] = {color_view_, depth_view_};
  VkFramebufferCreateInfo framebuffer_create_info = {};
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.renderPass = render_pass_;
  framebuffer_create_info.attachmentCount = 2;
  framebuffer_create_info.pAttachments = attachments;
  framebuffer_create_info.width = width_;
  framebuffer_create_info.height = height_;
  framebuffer_create_info.layers = 1;
  return dfn.vkCreateFramebuffer(device, &framebuffer_create_info, nullptr,
                                 &framebuffer_) == VK_SUCCESS;
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
  attachments[0].format = color_format_;
  attachments[0].samples = samples_;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  // Depth (transient — never stored, GMEM-resident, keeps LRZ valid in-pass).
  attachments[1].format = depth_format_;
  attachments[1].samples = samples_;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  // STORE depth (was DONT_CARE): the LOAD pass re-begins need the primed depth to
  // persist so the accumulated geometry depth-tests correctly across breaks.
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
  // LOAD variant (accumulate across mid-frame re-begins): color + depth LOAD from
  // their prior content, initialLayout == the CLEAR pass's finalLayout so the
  // image is already in the right layout (no wipe). Same attachments -> compatible
  // with framebuffer_ + the field pipelines.
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  if (dfn.vkCreateRenderPass(device, &render_pass_create_info, nullptr,
                             &render_pass_load_) != VK_SUCCESS) {
    render_pass_load_ = VK_NULL_HANDLE;
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
  if (render_pass_load_ != VK_NULL_HANDLE) {
    dfn.vkDestroyRenderPass(device, render_pass_load_, nullptr);
    render_pass_load_ = VK_NULL_HANDLE;
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
