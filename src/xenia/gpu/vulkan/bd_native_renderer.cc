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

#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_util.h"

DECLARE_double(gpu_bd_native_depth_clear);
DECLARE_bool(gpu_bd_native_depth_resolve);

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

bool BdNativeRenderer::ResolveSurface(NativeSurface& s,
                                     DeferredCommandBuffer& command_buffer) {
  if (s.samples == VK_SAMPLE_COUNT_1_BIT || s.resolve_image == VK_NULL_HANDLE ||
      s.color_image == VK_NULL_HANDLE) {
    return false;  // single-sample or no resolve target = nothing to do.
  }
  // Resolve the surface's multisampled color -> its single-sample resolve_image
  // (the sampleable image for MSAA surfaces), leaving resolve_image SHADER_READ.
  // Same shape as the primary ResolveMsaa; call right after rendering into the
  // surface, so color_image is COLOR_ATTACHMENT (the MSAA render-pass finalLayout).
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
  pre[0].image = s.color_image;
  pre[0].subresourceRange = range;
  pre[1] = pre[0];
  pre[1].srcAccessMask = 0;
  pre[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  pre[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  pre[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  pre[1].image = s.resolve_image;
  command_buffer.CmdVkPipelineBarrier(
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
          VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, pre);

  VkImageResolve r = {};
  r.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  r.srcSubresource.layerCount = 1;
  r.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  r.dstSubresource.layerCount = 1;
  r.extent = {s.width, s.height, 1};
  command_buffer.CmdVkResolveImage(
      s.color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.resolve_image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);

  VkImageMemoryBarrier post = {};
  post.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  post.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  post.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  post.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  post.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  post.image = s.resolve_image;
  post.subresourceRange = range;
  command_buffer.CmdVkPipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                      nullptr, 0, nullptr, 1, &post);
  // color_image is now TRANSFER_SRC; the next render into the surface re-begins
  // its pass (which transitions from UNDEFINED/LOAD), so mark it accordingly.
  s.color_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
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

void BdNativeRenderer::LogSurfaceKeys(const char* context) {
  // FIELD-GATED **BEFORE** the rate limit - this ordering is the whole point.
  // Twice now the 8-dump budget was consumed entirely during LOADING (all dumps
  // stamped ~15s after launch, while the field census lines are ~3 minutes in), so
  // the output described the loading screen's surfaces and said nothing about the
  // field. Counting only in-field dumps is what makes the snapshot meaningful:
  // at load time the live surfaces are BD's single-sample bloom pyramid, whereas
  // the consumer we are trying to serve needs a 4X MSAA depth.
  if (xe::Clock::QueryGuestUptimeMillis() <= 135000) {
    return;
  }
  // Rate-limited: this fires from a per-pass site, and BD runs ~2000 passes/frame.
  static std::atomic<uint32_t> s_dumps{0};
  if (s_dumps.fetch_add(1, std::memory_order_relaxed) >= 8) {
    return;
  }
  XELOGI("BD NATIVE SURFACE KEYS [{}]: count={}", context, surfaces_.size());
  for (const auto& kv : surfaces_) {
    const NativeSurface& s = kv.second;
    XELOGI(
        "  key(guest_addr)=0x{:08X} {}x{} color_fmt={} depth_fmt={} samples={} "
        "main_scene={} rendered_this_frame={} has_depth_resolve={} "
        "depth_resolve_ready={}",
        kv.first, s.width, s.height, uint32_t(s.color_format),
        uint32_t(s.depth_format), uint32_t(s.samples), s.is_main_scene ? 1 : 0,
        s.rendered_this_frame ? 1 : 0,
        s.depth_resolve_image != VK_NULL_HANDLE ? 1 : 0,
        (s.depth_resolve_image != VK_NULL_HANDLE && s.rendered_this_frame) ? 1
                                                                          : 0);
  }
}

VkImage BdNativeRenderer::LookupDepthResolveImage(uint32_t guest_address) {
  auto it = surfaces_.find(guest_address);
  if (it == surfaces_.end()) {
    return VK_NULL_HANDLE;
  }
  NativeSurface& s = it->second;
  if (s.depth_resolve_image == VK_NULL_HANDLE) {
    return VK_NULL_HANDLE;  // single-sample, or the depth resolve is disabled.
  }
  // The resolve target is written by the render pass's depth-resolve attachment,
  // so it only holds valid content once this surface has actually rendered THIS
  // FRAME. Unlike the color lookup (where older aux content is still usable), a
  // stale depth generation is exactly the temporal-snapshot hazard that makes
  // wrong depth collapse BD's field, so this is deliberately strict: current
  // frame only, for every surface class.
  if (!s.rendered_this_frame) {
    return VK_NULL_HANDLE;
  }
  return s.depth_resolve_image;
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
  if (!key || !width || !height) {
    return nullptr;
  }
  // Depth-only surface = no color (color_format UNDEFINED) + a real depth
  // format. A color surface needs a color format; a depth-only surface needs a
  // depth format. Reject when neither is present.
  const bool depth_only = (color_format == VK_FORMAT_UNDEFINED);
  if (depth_only && depth_format == VK_FORMAT_UNDEFINED) {
    return nullptr;
  }
  // MSAA (samples>1) surfaces get a single-sample resolve target in
  // CreateSurfaceResources so they stay sampleable.
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

NativeSurface* BdNativeRenderer::AcquireDepthOnlySurface(
    uint32_t key, uint32_t width, uint32_t height, VkFormat depth_format,
    VkSampleCountFlagBits samples) {
  // Depth-only = no color format; AcquireSurface + CreateSurfaceResources treat
  // color_format==UNDEFINED as "skip the color image / color attachment".
  return AcquireSurface(key, width, height, VK_FORMAT_UNDEFINED, depth_format,
                        samples);
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

  const bool depth_only = (s.color_format == VK_FORMAT_UNDEFINED);
  const bool is_msaa = (s.samples != VK_SAMPLE_COUNT_1_BIT);
  if (!depth_only) {
    ici.format = s.color_format;
    // MSAA color is resolved before it can be sampled (an MSAA image sampled
    // directly bands), so it gets COLOR_ATTACHMENT + TRANSFER_SRC but NOT
    // SAMPLED; single-sample color is sampled directly.
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                (is_msaa ? 0u : VK_IMAGE_USAGE_SAMPLED_BIT);
    if (!ui::vulkan::util::CreateDedicatedAllocationImage(
            vulkan_device, ici, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
            s.color_image, s.color_memory)) {
      return false;
    }
    if (is_msaa) {
      // Single-sample resolve target = the sampleable image for MSAA surfaces.
      VkImageCreateInfo rici = ici;
      rici.samples = VK_SAMPLE_COUNT_1_BIT;
      rici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      if (!ui::vulkan::util::CreateDedicatedAllocationImage(
              vulkan_device, rici, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
              s.resolve_image, s.resolve_memory)) {
        return false;
      }
    }
  }
  ici.format = s.depth_format;
  // Depth-only surfaces (prepass/shadow) are sampled by later passes, so add
  // SAMPLED; a paired depth is attachment-only.
  ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
              (depth_only ? VK_IMAGE_USAGE_SAMPLED_BIT : 0u);
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device, ici, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
          s.depth_image, s.depth_memory)) {
    return false;
  }
  // Single-sample DEPTH resolve target for the in-pass depth resolve. Sampled,
  // because the point is to serve BD's converted (1x) depth views natively instead
  // of via an EDRAM depth ownership transfer.
  // Guard on the driver actually exposing SAMPLE_ZERO depth resolve AND on
  // vkCreateRenderPass2 being loaded (core 1.2) - fail closed to the legacy
  // 2-attachment pass rather than creating an unusable surface.
  const ui::vulkan::VulkanDevice::Functions& cap_dfn = vulkan_device->functions();
  const bool depth_resolve_supported =
      cap_dfn.vkCreateRenderPass2 != nullptr &&
      (vulkan_device->properties().apiVersion >=
       VK_MAKE_API_VERSION(0, 1, 2, 0));
  const bool want_depth_resolve =
      is_msaa && cvars::gpu_bd_native_depth_resolve && depth_resolve_supported;
  if (want_depth_resolve) {
    VkImageCreateInfo drici = ici;
    drici.samples = VK_SAMPLE_COUNT_1_BIT;
    drici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!ui::vulkan::util::CreateDedicatedAllocationImage(
            vulkan_device, drici, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
            s.depth_resolve_image, s.depth_resolve_memory)) {
      return false;
    }
  }

  VkImageViewCreateInfo vci = {};
  vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.subresourceRange.levelCount = 1;
  vci.subresourceRange.layerCount = 1;
  if (!depth_only) {
    vci.image = s.color_image;
    vci.format = s.color_format;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (dfn.vkCreateImageView(device, &vci, nullptr, &s.color_view) !=
        VK_SUCCESS) {
      return false;
    }
    if (is_msaa) {
      vci.image = s.resolve_image;  // format/aspect unchanged (single-sample)
      if (dfn.vkCreateImageView(device, &vci, nullptr, &s.resolve_view) !=
          VK_SUCCESS) {
        return false;
      }
    }
  }
  vci.image = s.depth_image;
  vci.format = s.depth_format;
  vci.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  if (dfn.vkCreateImageView(device, &vci, nullptr, &s.depth_view) !=
      VK_SUCCESS) {
    return false;
  }
  if (s.depth_resolve_image != VK_NULL_HANDLE) {
    vci.image = s.depth_resolve_image;  // format/aspect unchanged (single-sample)
    if (dfn.vkCreateImageView(device, &vci, nullptr, &s.depth_resolve_view) !=
        VK_SUCCESS) {
      return false;
    }
  }

  if (depth_only) {
    // Depth-only render passes (CLEAR first / LOAD accumulate) + framebuffer:
    // one depth attachment, no color. The depth image is SAMPLED so later
    // passes read the prepass depth directly (no EDRAM resolve).
    VkAttachmentDescription datt = {};
    datt.format = s.depth_format;
    datt.samples = s.samples;
    datt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    datt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    datt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    datt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    datt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    datt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference dref = {
        0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription dsub = {};
    dsub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    dsub.colorAttachmentCount = 0;
    dsub.pColorAttachments = nullptr;
    dsub.pDepthStencilAttachment = &dref;
    VkRenderPassCreateInfo drpci = {};
    drpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    drpci.attachmentCount = 1;
    drpci.pAttachments = &datt;
    drpci.subpassCount = 1;
    drpci.pSubpasses = &dsub;
    if (dfn.vkCreateRenderPass(device, &drpci, nullptr, &s.render_pass_clear) !=
        VK_SUCCESS) {
      s.render_pass_clear = VK_NULL_HANDLE;
      return false;
    }
    datt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    datt.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (dfn.vkCreateRenderPass(device, &drpci, nullptr, &s.render_pass_load) !=
        VK_SUCCESS) {
      s.render_pass_load = VK_NULL_HANDLE;
      return false;
    }
    VkFramebufferCreateInfo dfbci = {};
    dfbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    dfbci.renderPass = s.render_pass_clear;
    dfbci.attachmentCount = 1;
    dfbci.pAttachments = &s.depth_view;
    dfbci.width = s.width;
    dfbci.height = s.height;
    dfbci.layers = 1;
    return dfn.vkCreateFramebuffer(device, &dfbci, nullptr, &s.framebuffer) ==
           VK_SUCCESS;
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
  // Single-sample color is sampled directly (SHADER_READ); MSAA color is
  // resolved separately first, so it stays a color attachment.
  const VkImageLayout color_final_layout =
      is_msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
              : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  att[0].finalLayout = color_final_layout;
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

  // IN-PASS DEPTH RESOLVE (gpu_bd_native_depth_resolve). When this surface has a
  // single-sample depth-resolve target, build the pass through vkCreateRenderPass2
  // with a VkSubpassDescriptionDepthStencilResolve so the MSAA depth resolves as
  // part of THIS pass's GMEM tile store - no extra render pass on the TBDR. The
  // legacy VkSubpassDescription cannot express this, and vkCmdResolveImage is
  // color-only. Device-confirmed on Turnip: depthModes/stencilModes = 0x21, which
  // includes SAMPLE_ZERO; independentResolve = 0 AND independentResolveNone = 0, so
  // depth and stencil MUST use the SAME mode - hence SAMPLE_ZERO for both.
  if (s.depth_resolve_view != VK_NULL_HANDLE) {
    VkAttachmentDescription2 att2[3] = {};
    for (int i = 0; i < 2; ++i) {
      att2[i].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
      att2[i].format = att[i].format;
      att2[i].samples = att[i].samples;
      att2[i].loadOp = att[i].loadOp;
      att2[i].storeOp = att[i].storeOp;
      att2[i].stencilLoadOp = att[i].stencilLoadOp;
      att2[i].stencilStoreOp = att[i].stencilStoreOp;
      att2[i].initialLayout = att[i].initialLayout;
      att2[i].finalLayout = att[i].finalLayout;
    }
    att2[2].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    att2[2].format = s.depth_format;
    att2[2].samples = VK_SAMPLE_COUNT_1_BIT;
    // The resolve target is fully written by the resolve itself.
    att2[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att2[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att2[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att2[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att2[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Left as a depth attachment; the consumer redirect barriers it to
    // SHADER_READ when it binds it (mirrors the color resolve's handling).
    att2[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference2 color_ref2 = {
        VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT};
    VkAttachmentReference2 depth_ref2 = {
        VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 1,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT};
    VkAttachmentReference2 depth_resolve_ref2 = {
        VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, nullptr, 2,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT};

    VkSubpassDescriptionDepthStencilResolve dsr = {};
    dsr.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE;
    dsr.depthResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    dsr.stencilResolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    dsr.pDepthStencilResolveAttachment = &depth_resolve_ref2;

    VkSubpassDescription2 subpass2 = {};
    subpass2.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    subpass2.pNext = &dsr;
    subpass2.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass2.colorAttachmentCount = 1;
    subpass2.pColorAttachments = &color_ref2;
    subpass2.pDepthStencilAttachment = &depth_ref2;

    VkRenderPassCreateInfo2 rpci2 = {};
    rpci2.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    rpci2.attachmentCount = 3;
    rpci2.pAttachments = att2;
    rpci2.subpassCount = 1;
    rpci2.pSubpasses = &subpass2;
    if (dfn.vkCreateRenderPass2(device, &rpci2, nullptr,
                                &s.render_pass_clear) != VK_SUCCESS) {
      s.render_pass_clear = VK_NULL_HANDLE;
      return false;
    }
    att2[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att2[0].initialLayout = color_final_layout;
    att2[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att2[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (dfn.vkCreateRenderPass2(device, &rpci2, nullptr, &s.render_pass_load) !=
        VK_SUCCESS) {
      s.render_pass_load = VK_NULL_HANDLE;
      return false;
    }
  } else {
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
  att[0].initialLayout = color_final_layout;
  att[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  att[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  if (dfn.vkCreateRenderPass(device, &rpci, nullptr, &s.render_pass_load) !=
      VK_SUCCESS) {
    s.render_pass_load = VK_NULL_HANDLE;
    return false;
  }
  }  // end legacy (no depth-resolve) pass creation

  // Shared framebuffer. The depth-resolve pass declares a 3rd attachment (the
  // single-sample depth resolve target), so the framebuffer must match it.
  const bool has_depth_resolve = (s.depth_resolve_view != VK_NULL_HANDLE);
  VkImageView fb_attachments[3] = {s.color_view, s.depth_view,
                                   s.depth_resolve_view};
  VkFramebufferCreateInfo fbci = {};
  fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbci.renderPass = s.render_pass_clear;
  fbci.attachmentCount = has_depth_resolve ? 3u : 2u;
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
  if (s.resolve_view != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, s.resolve_view, nullptr);
    s.resolve_view = VK_NULL_HANDLE;
  }
  if (s.depth_resolve_view != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, s.depth_resolve_view, nullptr);
    s.depth_resolve_view = VK_NULL_HANDLE;
  }
  if (s.depth_resolve_image != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, s.depth_resolve_image, nullptr);
    s.depth_resolve_image = VK_NULL_HANDLE;
  }
  if (s.depth_resolve_memory != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, s.depth_resolve_memory, nullptr);
    s.depth_resolve_memory = VK_NULL_HANDLE;
  }
  if (s.resolve_image != VK_NULL_HANDLE) {
    dfn.vkDestroyImage(device, s.resolve_image, nullptr);
    s.resolve_image = VK_NULL_HANDLE;
  }
  if (s.resolve_memory != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, s.resolve_memory, nullptr);
    s.resolve_memory = VK_NULL_HANDLE;
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
