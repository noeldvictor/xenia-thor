/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Blue Dragon native D3D9->Vulkan HLE renderer (DXVK-for-360) — Brick 1.
 *
 * The FULL-HLE path (user mandate 2026-07-05): a SEPARATE native Vulkan renderer
 * that captures BD's D3D9 draws at seam 0x82489F40 and renders the whole frame
 * into ONE persistent full-surface host RT in a FEW held-open passes with native
 * pipelines — BYPASSING xenia's PM4/EDRAM/95-pass LLE back-end (which the partial
 * decoupled-native-HLE kept, hence it was perf-flat). Structure/correctness is
 * developed on desktop `--gpu=vulkan`; the 30fps comes from SUPER-OPTIMIZING for
 * the Thor's Turnip/Adreno TBDR (minimize passes = GMEM flushes, keep the RT
 * GMEM-resident, Turnip extensions). See docs/research/20260705-native-vulkan-
 * renderer-plan.md. Gated by gpu_bd_native_renderer (default off); LLE stays the
 * correctness fallback.
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_BD_NATIVE_RENDERER_H_
#define XENIA_GPU_VULKAN_BD_NATIVE_RENDERER_H_

#include <cstdint>

#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;
class DeferredCommandBuffer;

// Brick 1: owns a persistent native full-surface color+depth RT + one render
// pass. Bricks 2-4 add native pipelines (VkBuffer vertex-input, Xenos->SPIR-V,
// hardware ROP blend), the captured-draw submit loop, depth-prepass + early-Z,
// and composite folding. Kept deliberately separate from RenderTargetCache so it
// does NOT go through the EDRAM tile/transfer/resolve machinery.
class BdNativeRenderer {
 public:
  explicit BdNativeRenderer(VulkanCommandProcessor& command_processor)
      : command_processor_(command_processor) {}
  ~BdNativeRenderer() { Shutdown(); }

  // Allocate the persistent color+depth image + render pass + framebuffer, sized
  // to BD's full field surface (default 720x1280; the real dims come from the
  // captured surface state). Returns false on failure (caller falls back to LLE).
  bool Initialize(uint32_t width, uint32_t height);
  void Shutdown();

  // Recreate the color image + view + render pass + framebuffer for `format` if
  // it differs from the current one - so xenia's field pipelines (built for the
  // field's color format) are render-pass-COMPATIBLE with the native pass (else
  // "device lost"). Call BEFORE redirecting a field draw (pass not yet begun, so
  // the images aren't in use). Returns false on failure.
  bool EnsureColorFormat(VkFormat format);

  // Brick 2a: begin the ONE held-open native render pass on `command_buffer`
  // (clears color+depth), then EndRenderPass. Bricks 2b-3 record the captured
  // 0x82489F40 draws (native pipelines + vertex-input) BETWEEN begin and end.
  // Leaves color_image_ in SHADER_READ_ONLY_OPTIMAL (finalLayout) for present.
  // Uses xenia's DeferredCommandBuffer (recorded on the CP thread, replayed on
  // the worker) - NOT a raw VkCommandBuffer - so it slots into the frame flow.
  void RenderFrame(DeferredCommandBuffer& command_buffer);

  bool initialized() const { return render_pass_ != VK_NULL_HANDLE; }
  VkImage color_image() const { return color_image_; }
  // Sampled color view (SHADER_READ_ONLY after RenderFrame) - substitute as the
  // swap source to present the native RT directly.
  VkImageView color_view() const { return color_view_; }
  // The native render pass + framebuffer - redirect BD's field draws into these
  // to render the real geometry natively in ONE pass (Brick 2b, reuses xenia's
  // shaders/pipelines; requires format-compatibility with the field pipelines).
  VkFramebuffer framebuffer() const { return framebuffer_; }
  VkRenderPass render_pass() const { return render_pass_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }

 private:
  bool CreateRenderPass();
  bool CreateImages();
  bool CreateFramebuffer();

  VulkanCommandProcessor& command_processor_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;

  // Native (NON-EDRAM) attachments — the whole point: no tile/transfer machinery.
  VkImage color_image_ = VK_NULL_HANDLE;
  VkDeviceMemory color_memory_ = VK_NULL_HANDLE;
  VkImageView color_view_ = VK_NULL_HANDLE;
  VkImage depth_image_ = VK_NULL_HANDLE;
  VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
  VkImageView depth_view_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

  // Color format is dynamic (matched to the field's format for pipeline compat);
  // depth is fixed.
  VkFormat color_format_ = VK_FORMAT_R8G8B8A8_UNORM;
  static constexpr VkFormat kDepthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_BD_NATIVE_RENDERER_H_
