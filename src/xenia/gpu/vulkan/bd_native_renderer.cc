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

#include <cstring>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_util.h"

DECLARE_double(gpu_bd_native_depth_clear);

namespace xe {
namespace gpu {
namespace vulkan {

// Desktop Vulkan (NVIDIA/AMD/Intel) is lenient about render-pass<->pipeline
// compatibility; the Adreno driver (Qualcomm proprietary vendorID 0x5143, and
// Mesa Turnip) is strict and CRASHES (null-deref inside vulkan.adreno.so) on the
// native renderer's mismatched pass. This is THE fork point for device-specific
// (Adreno super-optimized) vs desktop (structure/correctness) code paths.
bool BdNativeRenderer::IsAdreno() const {
  const auto* dev = command_processor_.GetVulkanDevice();
  if (!dev) {
    return false;
  }
  const auto& props = dev->properties();
  return props.vendorID == 0x5143u ||  // Qualcomm
         std::strstr(props.deviceName, "Adreno") != nullptr ||
         std::strstr(props.deviceName, "Turnip") != nullptr;
}

bool BdNativeRenderer::Initialize(uint32_t width, uint32_t height,
                                  uint32_t stretch_src_width) {
  if (!width || !height) {
    return false;
  }
  width_ = width;
  height_ = height;
  stretch_src_width_ = stretch_src_width;
  present_width_ = width;
  {
    const auto* dev = command_processor_.GetVulkanDevice();
    if (dev) {
      const auto& props = dev->properties();
      XELOGI(
          "BdNativeRenderer: GPU vendorID={:04X} driverID={} device='{}' "
          "is_adreno={}",
          props.vendorID, uint32_t(props.driverID), props.deviceName,
          IsAdreno());
    }
  }
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

bool BdNativeRenderer::ResolveMsaa(DeferredCommandBuffer& command_buffer) {
  if (!IsAdreno() || samples_ == VK_SAMPLE_COUNT_1_BIT ||
      resolve_image_ == VK_NULL_HANDLE || color_image_ == VK_NULL_HANDLE) {
    return false;  // desktop uses the in-pass resolve; single-sample = nothing.
  }
  // color_image_ is COLOR_ATTACHMENT (render pass finalLayout for MSAA). Resolve
  // the multisampled color -> single-sample resolve_image_ (what the in-pass resolve
  // does on desktop), leaving resolve_image_ SHADER_READ for present / stretch.
  VkImageSubresourceRange range = {};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.levelCount = 1;
  range.layerCount = 1;

  VkImageMemoryBarrier pre[2] = {};
  pre[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  pre[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  pre[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  pre[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  pre[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  pre[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  pre[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  pre[0].image = color_image_;
  pre[0].subresourceRange = range;
  pre[1] = pre[0];
  pre[1].srcAccessMask = 0;
  pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  pre[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  pre[1].image = resolve_image_;
  command_buffer.CmdVkPipelineBarrier(
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
          VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, pre);

  VkImageResolve r = {};
  r.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  r.srcSubresource.layerCount = 1;
  r.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  r.dstSubresource.layerCount = 1;
  r.extent = {width_, height_, 1};
  command_buffer.CmdVkResolveImage(
      color_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resolve_image_,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);

  VkImageMemoryBarrier post = {};
  post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  post.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  post.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  post.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  post.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  post.image = resolve_image_;
  post.subresourceRange = range;
  command_buffer.CmdVkPipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                      nullptr, 0, nullptr, 1, &post);
  return true;
}

bool BdNativeRenderer::StretchToPresent(DeferredCommandBuffer& command_buffer,
                                        uint32_t src_width,
                                        uint32_t src_height) {
  if (present_image_ == VK_NULL_HANDLE || stretch_src_width_ == 0 ||
      src_width == 0 || src_height == 0) {
    return false;
  }
  VkImage src = (samples_ != VK_SAMPLE_COUNT_1_BIT &&
                 resolve_image_ != VK_NULL_HANDLE)
                    ? resolve_image_
                    : color_image_;
  if (src == VK_NULL_HANDLE) {
    return false;
  }
  // The rendered image is SHADER_READ_ONLY (render pass finalLayout); present_image_
  // is UNDEFINED (first) or SHADER_READ (prior frame). Transition src->TRANSFER_SRC,
  // present->TRANSFER_DST, blit STRETCHED [0..src]->[0..present_width_], then both
  // back to SHADER_READ for the gamma present pass.
  VkImageSubresourceRange color_range = {};
  color_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  color_range.levelCount = 1;
  color_range.layerCount = 1;

  VkImageMemoryBarrier pre[2] = {};
  pre[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  pre[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  pre[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  pre[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  pre[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  pre[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  pre[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  pre[0].image = src;
  pre[0].subresourceRange = color_range;
  pre[1] = pre[0];
  pre[1].srcAccessMask = 0;
  pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  pre[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  pre[1].image = present_image_;
  command_buffer.CmdVkPipelineBarrier(
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, pre);

  VkImageBlit blit = {};
  blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.srcSubresource.layerCount = 1;
  blit.srcOffsets[0] = {0, 0, 0};
  blit.srcOffsets[1] = {int32_t(src_width), int32_t(src_height), 1};
  blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.dstSubresource.layerCount = 1;
  blit.dstOffsets[0] = {0, 0, 0};
  blit.dstOffsets[1] = {int32_t(present_width_), int32_t(height_), 1};
  command_buffer.CmdVkBlitImage(
      src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, present_image_,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

  VkImageMemoryBarrier post[2] = {};
  post[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  post[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  post[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  post[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  post[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  post[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  post[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  post[0].image = src;
  post[0].subresourceRange = color_range;
  post[1] = post[0];
  post[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  post[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  post[1].image = present_image_;
  command_buffer.CmdVkPipelineBarrier(
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
      0, nullptr, 0, nullptr, 2, post);
  present_image_ready_ = true;
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
  clear_values[1].depthStencil = {float(cvars::gpu_bd_native_depth_clear), 0};

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

// ---- Native-surface registry (real HLE / EDRAM-deletion substrate) ----------

NativeSurface* BdNativeRenderer::FindSurface(uint32_t key) {
  auto it = surfaces_.find(key);
  return it == surfaces_.end() ? nullptr : &it->second;
}

VkImageView BdNativeRenderer::LookupSampledSurface(uint32_t guest_address) {
  auto it = surfaces_.find(guest_address);
  if (it == surfaces_.end()) {
    return VK_NULL_HANDLE;
  }
  // Only bind the native image once it holds valid rendered content (a completed
  // render pass leaves it SHADER_READ_ONLY). Undefined => fall back to EDRAM.
  if (it->second.color_layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    return VK_NULL_HANDLE;
  }
  // Frontbuffer-sized surfaces are authoritative only for the frame that wrote
  // them. Smaller aux surfaces retain the legacy cross-frame lookup behavior.
  if (it->second.is_main_scene && !it->second.rendered_this_frame) {
    return VK_NULL_HANDLE;
  }
  return it->second.color_view;
}

void BdNativeRenderer::BeginSurfaceFrame() {
  for (auto& kv : surfaces_) {
    kv.second.rendered_this_frame = false;
  }
}

NativeSurface* BdNativeRenderer::AcquireSurface(uint32_t key, uint32_t width,
                                                uint32_t height,
                                                VkFormat color_format,
                                                VkFormat depth_format,
                                                VkSampleCountFlagBits samples) {
  if (!key || !width || !height || color_format == VK_FORMAT_UNDEFINED) {
    return nullptr;
  }
  // MSAA aux surfaces need a resolve to be sampleable — not yet implemented.
  if (samples != VK_SAMPLE_COUNT_1_BIT) {
    return nullptr;
  }
  auto it = surfaces_.find(key);
  if (it != surfaces_.end()) {
    NativeSurface& s = it->second;
    // Reuse if the existing surface is ALREADY BIG ENOUGH + format-compatible.
    // Requiring EXACT dims caused per-frame destroy/recreate churn (BD's scissor
    // height oscillates 180<->184), which freed VkImages still in use by in-flight
    // frames = use-after-free crash during scene transitions. A larger surface
    // renders a smaller draw fine (viewport/scissor bound the area). Only GROW.
    if (s.color_format == color_format && s.depth_format == depth_format &&
        s.samples == samples && s.framebuffer != VK_NULL_HANDLE &&
        s.width >= width && s.height >= height) {
      return &s;
    }
    // Must grow/reformat: rebuild at the MAX of old+new dims so it stabilizes
    // (never shrinks => no ping-pong recreation). Grows are one-time as dims
    // settle, so the per-frame destroy/recreate churn (the crash trigger) stops.
    width = s.width > width ? s.width : width;
    height = s.height > height ? s.height : height;
    DestroySurfaceResources(s);
  }
  NativeSurface& s = surfaces_[key];
  s.key = key;
  s.width = width;
  s.height = height;
  s.color_format = color_format;
  s.depth_format =
      depth_format == VK_FORMAT_UNDEFINED ? VK_FORMAT_D24_UNORM_S8_UINT
                                          : depth_format;
  s.samples = samples;
  s.color_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  s.rendered_this_frame = false;
  if (!CreateSurfaceResources(s)) {
    DestroySurfaceResources(s);
    surfaces_.erase(key);
    return nullptr;
  }
  static uint32_t s_logged = 0;
  if (s_logged < 8) {
    ++s_logged;
    XELOGI(
        "BdNativeRenderer: native surface key={:08X} {}x{} color={} depth={} "
        "(live={})",
        key, width, height, uint32_t(color_format), uint32_t(s.depth_format),
        surfaces_.size());
  }
  return &s;
}

bool BdNativeRenderer::CreateSurfaceResources(NativeSurface& s) {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // Color: SAMPLED (the field fetches it) + COLOR_ATTACHMENT (rendered into) +
  // TRANSFER_SRC (debug/blit). Depth: attachment only.
  VkImageCreateInfo ici = {};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.extent = {s.width, s.height, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = s.samples;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  ici.format = s.color_format;
  ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device, ici, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
          s.color_image, s.color_memory)) {
    return false;
  }
  ici.format = s.depth_format;
  ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device, ici, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
          s.depth_image, s.depth_memory)) {
    return false;
  }

  VkImageViewCreateInfo vci = {};
  vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.subresourceRange.levelCount = 1;
  vci.subresourceRange.layerCount = 1;
  vci.image = s.color_image;
  vci.format = s.color_format;
  vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  if (dfn.vkCreateImageView(device, &vci, nullptr, &s.color_view) !=
      VK_SUCCESS) {
    return false;
  }
  vci.image = s.depth_image;
  vci.format = s.depth_format;
  vci.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  if (dfn.vkCreateImageView(device, &vci, nullptr, &s.depth_view) !=
      VK_SUCCESS) {
    return false;
  }

  // Two render passes (CLEAR first draw / LOAD accumulate) mirroring the primary
  // field RT, but single-sample and with color finalLayout SHADER_READ so the
  // surface is directly sampleable after rendering (that's the whole point —
  // render-to-texture with no EDRAM resolve).
  VkAttachmentDescription att[2] = {};
  att[0].format = s.color_format;
  att[0].samples = s.samples;
  att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  att[1].format = s.depth_format;
  att[1].samples = s.samples;
  att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_ref = {0,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depth_ref = {
      1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;
  subpass.pDepthStencilAttachment = &depth_ref;

  VkRenderPassCreateInfo rpci = {};
  rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpci.attachmentCount = 2;
  rpci.pAttachments = att;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &subpass;
  if (dfn.vkCreateRenderPass(device, &rpci, nullptr, &s.render_pass_clear) !=
      VK_SUCCESS) {
    s.render_pass_clear = VK_NULL_HANDLE;
    return false;
  }
  att[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  att[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  att[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  att[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  if (dfn.vkCreateRenderPass(device, &rpci, nullptr, &s.render_pass_load) !=
      VK_SUCCESS) {
    s.render_pass_load = VK_NULL_HANDLE;
    return false;
  }

  VkImageView fb_attachments[2] = {s.color_view, s.depth_view};
  VkFramebufferCreateInfo fbci = {};
  fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbci.renderPass = s.render_pass_clear;
  fbci.attachmentCount = 2;
  fbci.pAttachments = fb_attachments;
  fbci.width = s.width;
  fbci.height = s.height;
  fbci.layers = 1;
  return dfn.vkCreateFramebuffer(device, &fbci, nullptr, &s.framebuffer) ==
         VK_SUCCESS;
}

void BdNativeRenderer::DestroySurfaceResources(NativeSurface& s) {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  if (!vulkan_device) {
    return;
  }
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  if (s.framebuffer != VK_NULL_HANDLE) {
    dfn.vkDestroyFramebuffer(device, s.framebuffer, nullptr);
    s.framebuffer = VK_NULL_HANDLE;
  }
  if (s.render_pass_load != VK_NULL_HANDLE) {
    dfn.vkDestroyRenderPass(device, s.render_pass_load, nullptr);
    s.render_pass_load = VK_NULL_HANDLE;
  }
  if (s.render_pass_clear != VK_NULL_HANDLE) {
    dfn.vkDestroyRenderPass(device, s.render_pass_clear, nullptr);
    s.render_pass_clear = VK_NULL_HANDLE;
  }
  if (s.color_view != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, s.color_view, nullptr);
    s.color_view = VK_NULL_HANDLE;
  }
  if (s.color_image != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, s.color_image, nullptr);
    s.color_image = VK_NULL_HANDLE;
  }
  if (s.color_memory != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, s.color_memory, nullptr);
    s.color_memory = VK_NULL_HANDLE;
  }
  if (s.depth_view != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, s.depth_view, nullptr);
    s.depth_view = VK_NULL_HANDLE;
  }
  if (s.depth_image != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, s.depth_image, nullptr);
    s.depth_image = VK_NULL_HANDLE;
  }
  if (s.depth_memory != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, s.depth_memory, nullptr);
    s.depth_memory = VK_NULL_HANDLE;
  }
  s.color_layout = VK_IMAGE_LAYOUT_UNDEFINED;
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

  // MSAA resolve target (single-sample) - the field is 2x MSAA; the render pass
  // resolves the multisampled color into this, and THIS is what gets presented
  // (presenting the MSAA image directly bands). Only when samples_ > 1.
  if (samples_ != VK_SAMPLE_COUNT_1_BIT) {
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.format = color_format_;
    image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!ui::vulkan::util::CreateDedicatedAllocationImage(
            vulkan_device, image_create_info,
            ui::vulkan::util::MemoryPurpose::kDeviceLocal, resolve_image_,
            resolve_memory_)) {
      return false;
    }
    view_create_info.image = resolve_image_;
    view_create_info.format = color_format_;
    view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                              &resolve_view_) != VK_SUCCESS) {
      return false;
    }
  }

  // Stretch present target (single-sample, full display width): the rendered field
  // (left stretch_src_width_ px) is blitted STRETCHED into this on present so the
  // narrower guest surface fills the display (BD upscales the field in its resolve).
  if (stretch_src_width_ > 0) {
    image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_create_info.format = color_format_;
    image_create_info.extent.width = present_width_;
    image_create_info.extent.height = height_;
    image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!ui::vulkan::util::CreateDedicatedAllocationImage(
            vulkan_device, image_create_info,
            ui::vulkan::util::MemoryPurpose::kDeviceLocal, present_image_,
            present_memory_)) {
      return false;
    }
    view_create_info.image = present_image_;
    view_create_info.format = color_format_;
    view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                              &present_view_) != VK_SUCCESS) {
      return false;
    }
    present_image_ready_ = false;
  }
  return true;
}

bool BdNativeRenderer::CreateFramebuffer() {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkImageView attachments[3] = {color_view_, depth_view_, resolve_view_};
  // ADRENO FORK: match xenia's field pass (no in-pass resolve) so the pass is
  // render-pass-compatible with BD's pipelines - resolve separately after.
  bool has_resolve = samples_ != VK_SAMPLE_COUNT_1_BIT && !IsAdreno();
  VkFramebufferCreateInfo framebuffer_create_info = {};
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.renderPass = render_pass_;
  framebuffer_create_info.attachmentCount = has_resolve ? 3 : 2;
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
  const bool msaa = samples_ != VK_SAMPLE_COUNT_1_BIT;
  // ADRENO FORK: xenia's field render pass has pResolveAttachments=nullptr (no
  // in-pass resolve). An in-pass RESOLVE attachment makes THIS pass render-pass-
  // INCOMPATIBLE with BD's field pipelines (built against xenia's no-resolve pass)
  // -> the strict Adreno driver null-derefs (desktop tolerates it). On Adreno, drop
  // the in-pass resolve (2-attachment pass = color+depth, matching the pipelines)
  // and resolve MSAA->single-sample SEPARATELY (vkCmdResolveImage) before present.
  const bool has_resolve = msaa && !IsAdreno();
  VkAttachmentDescription attachments[3] = {};
  // Color.
  attachments[0].format = color_format_;
  attachments[0].samples = samples_;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  // MSAA color stays COLOR_ATTACHMENT (stored for LOAD re-begins + resolved: in-pass
  // on desktop, or separately on Adreno). Single-sample presents directly.
  attachments[0].finalLayout =
      msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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

  // Resolve attachment (single-sample) - only when MSAA. loadOp DONT_CARE (written
  // by the resolve), STORE + SHADER_READ final (presented).
  if (has_resolve) {
    attachments[2].format = color_format_;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  VkAttachmentReference color_ref = {};
  color_ref.attachment = 0;
  color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference depth_ref = {};
  depth_ref.attachment = 1;
  depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkAttachmentReference resolve_ref = {};
  resolve_ref.attachment = 2;
  resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;
  subpass.pDepthStencilAttachment = &depth_ref;
  subpass.pResolveAttachments = has_resolve ? &resolve_ref : nullptr;

  VkRenderPassCreateInfo render_pass_create_info = {};
  render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_create_info.attachmentCount = has_resolve ? 3 : 2;
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
  attachments[0].initialLayout =
      has_resolve ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                  : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
  // NOTE: the native-surface registry is NOT torn down here — Shutdown() is also
  // called by EnsureColorFormat() on a field-format change, and the persistent
  // render-to-texture surfaces must survive that. The registry is freed by
  // ShutdownSurfaces() from the destructor.
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
  if (resolve_view_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, resolve_view_, nullptr);
    resolve_view_ = VK_NULL_HANDLE;
  }
  if (resolve_image_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, resolve_image_, nullptr);
    resolve_image_ = VK_NULL_HANDLE;
  }
  if (resolve_memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, resolve_memory_, nullptr);
    resolve_memory_ = VK_NULL_HANDLE;
  }
  if (present_view_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, present_view_, nullptr);
    present_view_ = VK_NULL_HANDLE;
  }
  if (present_image_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, present_image_, nullptr);
    present_image_ = VK_NULL_HANDLE;
  }
  if (present_memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, present_memory_, nullptr);
    present_memory_ = VK_NULL_HANDLE;
  }
  present_image_ready_ = false;
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

// force relink 1783443302
