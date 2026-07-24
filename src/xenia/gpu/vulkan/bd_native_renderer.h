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
#include <unordered_map>

#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;
class DeferredCommandBuffer;

// THE real-HLE unit (EDRAM deletion): a single native render-to-texture surface
// — its OWN persistent color(+depth) VkImage, keyed by the guest RESOLVE-
// DESTINATION address (the stable D3D9 resource identity: the main-memory
// texture the 360 surface resolves to and is LATER SAMPLED as). Because each
// logical surface owns one image that is BOTH rendered into AND sampled, there
// are NO EDRAM ownership transfers (the 35-pass / ~110ms wall). This replaces
// xenia's EDRAM-base RenderTargetKey addressing with DXVK/Cemu-style
// resource-identity addressing. Single-sample first (shadow/reflection RTs);
// MSAA aux resolve is a follow-up.
struct NativeSurface {
  uint32_t key = 0;  // resolve-dest guest address = resource identity
  uint32_t width = 0;
  uint32_t height = 0;
  VkFormat color_format = VK_FORMAT_UNDEFINED;
  VkFormat depth_format = VK_FORMAT_UNDEFINED;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
  VkImage color_image = VK_NULL_HANDLE;
  VkDeviceMemory color_memory = VK_NULL_HANDLE;
  VkImageView color_view = VK_NULL_HANDLE;  // SAMPLED + COLOR_ATTACHMENT
  VkImage depth_image = VK_NULL_HANDLE;
  VkDeviceMemory depth_memory = VK_NULL_HANDLE;
  VkImageView depth_view = VK_NULL_HANDLE;
  // MSAA surfaces (samples>1): a single-sample resolve target for the color, so
  // the surface is sampleable (an MSAA image sampled directly bands). Null for
  // single-sample surfaces. The resolve is populated by a separate step (the
  // render pass carries no in-pass resolve, matching the Adreno-compat path).
  VkImage resolve_image = VK_NULL_HANDLE;
  VkDeviceMemory resolve_memory = VK_NULL_HANDLE;
  VkImageView resolve_view = VK_NULL_HANDLE;
  // MSAA DEPTH resolve target (gpu_bd_native_depth_resolve). Unlike the color
  // resolve above - which is a separate vkCmdResolveImage step - this one is an
  // IN-PASS resolve ATTACHMENT (VkSubpassDescriptionDepthStencilResolve), so the
  // multisampled depth is resolved to single-sample as part of the pass's existing
  // GMEM tile store: no extra render pass, no extra tile round-trip. That
  // distinction is the whole point on a TBDR - a separate depth-resolve pass would
  // re-add the very tile-store cost the native renderer exists to delete.
  // vkCmdResolveImage cannot serve this at all (it is color-only).
  VkImage depth_resolve_image = VK_NULL_HANDLE;
  VkDeviceMemory depth_resolve_memory = VK_NULL_HANDLE;
  VkImageView depth_resolve_view = VK_NULL_HANDLE;
  VkRenderPass render_pass_clear = VK_NULL_HANDLE;  // first draw into this surface
  VkRenderPass render_pass_load = VK_NULL_HANDLE;   // accumulate re-begins
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  // Tracks the color image's current layout so binds/attaches barrier correctly.
  VkImageLayout color_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool rendered_this_frame = false;  // false => next attach uses the CLEAR pass
  // Set only for the default-off frontbuffer-sized redirect. Unlike the smaller
  // aux surfaces, these require current-frame production + stable consumer proof
  // before their EDRAM publication copy may be deleted.
  bool is_main_scene = false;
};

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
  // stretch_src_width: when >0, the field renders into the left stretch_src_width
  // px and is blitted STRETCHED to the full width on present (fill the screen);
  // 0 = present the rendered region as-is.
  bool Initialize(uint32_t width, uint32_t height, uint32_t stretch_src_width = 0);
  void Shutdown();

  // Recreate the color image + view + render pass + framebuffer for `format` if
  // it differs from the current one - so xenia's field pipelines (built for the
  // field's color format) are render-pass-COMPATIBLE with the native pass (else
  // "device lost"). Call BEFORE redirecting a field draw (pass not yet begun, so
  // the images aren't in use). Returns false on failure.
  bool EnsureColorFormat(VkFormat color_format, VkFormat depth_format,
                         VkSampleCountFlagBits samples);

  // Brick 2a: begin the ONE held-open native render pass on `command_buffer`
  // (clears color+depth), then EndRenderPass. Bricks 2b-3 record the captured
  // 0x82489F40 draws (native pipelines + vertex-input) BETWEEN begin and end.
  // Leaves color_image_ in SHADER_READ_ONLY_OPTIMAL (finalLayout) for present.
  // Uses xenia's DeferredCommandBuffer (recorded on the CP thread, replayed on
  // the worker) - NOT a raw VkCommandBuffer - so it slots into the frame flow.
  void RenderFrame(DeferredCommandBuffer& command_buffer);

  bool initialized() const { return render_pass_ != VK_NULL_HANDLE; }
  VkImage color_image() const { return color_image_; }
  // Sampled color view to present. When the field is MSAA (samples_ > 1) the
  // render pass resolves color into a SINGLE-SAMPLE resolve image - present THAT
  // (an MSAA image sampled directly bands). Single-sample: present color directly.
  VkImageView color_view() const {
    return (samples_ != VK_SAMPLE_COUNT_1_BIT && resolve_view_ != VK_NULL_HANDLE)
               ? resolve_view_
               : color_view_;
  }
  // The view to PRESENT: the stretched present image when stretch is active + ready,
  // else the rendered image directly (color_view()).
  VkImageView present_output_view() const {
    if (stretch_src_width_ > 0 && present_view_ != VK_NULL_HANDLE &&
        present_image_ready_) {
      return present_view_;
    }
    return color_view();
  }
  // The single-sample image holding the rendered field (resolve when MSAA, else
  // color) - the SOURCE for the stretch blit.
  VkImage rendered_image() const {
    return (samples_ != VK_SAMPLE_COUNT_1_BIT && resolve_image_ != VK_NULL_HANDLE)
               ? resolve_image_
               : color_image_;
  }
  // BD field-fill STRETCH: the bin-once renders the whole field into the left
  // src_width px of the native RT (the field's guest surface is narrower than the
  // 1280 display - BD upscales it in the resolve). Blit that region to the full
  // present_image_ width so the field fills the screen, then present present_image_.
  // No-op (returns false) if the present image isn't allocated (stretch disabled).
  bool StretchToPresent(DeferredCommandBuffer& command_buffer, uint32_t src_width,
                        uint32_t src_height);
  // ADRENO: the render pass has no in-pass resolve (for pipeline compat), so resolve
  // the MSAA color -> single-sample resolve_image_ SEPARATELY before present. No-op
  // on desktop (in-pass resolve) or single-sample. Call before present/StretchToPresent.
  bool ResolveMsaa(DeferredCommandBuffer& command_buffer);
  bool has_present_image() const { return present_image_ != VK_NULL_HANDLE; }
  // The native render pass + framebuffer - redirect BD's field draws into these
  // to render the real geometry natively in ONE pass (Brick 2b, reuses xenia's
  // shaders/pipelines; requires format-compatibility with the field pipelines).
  VkFramebuffer framebuffer() const { return framebuffer_; }
  // Two render passes (both compatible with framebuffer_): the CLEAR pass for the
  // FIRST redirected field draw of a frame, the LOAD pass (preserves content,
  // initial+final layout SHADER_READ) for every re-begin - so geometry ACCUMULATES
  // across mid-frame pass breaks instead of being wiped (the black-render fix).
  VkRenderPass render_pass_load() const { return render_pass_load_; }
  VkRenderPass render_pass() const { return render_pass_; }
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  // THE desktop-vs-Thor fork: true on Qualcomm Adreno / Mesa Turnip (the driver
  // that crashes on the loose render-pass compat desktop tolerates). Branch
  // Adreno-strict / super-optimized paths on this.
  bool IsAdreno() const;

  // ---- Native-surface registry (the real HLE / EDRAM-deletion substrate) ----
  // Get-or-create the persistent native surface for resource `key` (the guest
  // resolve-destination address), sized/typed to the surface. Reused across
  // frames; recreated in place if the format/size/samples change. Returns nullptr
  // on failure (caller falls back to the EDRAM path). MSAA (samples>1) is not yet
  // supported for aux surfaces and returns nullptr.
  NativeSurface* AcquireSurface(uint32_t key, uint32_t width, uint32_t height,
                                VkFormat color_format, VkFormat depth_format,
                                VkSampleCountFlagBits samples);
  // Get-or-create a DEPTH-ONLY native surface (no color image) for BD's depth
  // prepass / shadow passes - a sampled depth image + depth-only render pass +
  // framebuffer. Same registry/reuse rules as AcquireSurface; MSAA deferred.
  NativeSurface* AcquireDepthOnlySurface(uint32_t key, uint32_t width,
                                         uint32_t height, VkFormat depth_format,
                                         VkSampleCountFlagBits samples);
  // Resolve one reserved MSAA surface's multisampled color into its single-
  // sample resolve_image (leaving it SHADER_READ so the surface is sampleable),
  // mirroring the primary ResolveMsaa. Call after rendering into the surface
  // (its color is COLOR_ATTACHMENT). No-op for single-sample surfaces or those
  // without a resolve target. The redirect consumer uses this before sampling
  // an MSAA native surface.
  bool ResolveSurface(NativeSurface& surface,
                      DeferredCommandBuffer& command_buffer);
  // Look up a native surface by key (nullptr if none). Const view for binding.
  NativeSurface* FindSurface(uint32_t key);
  // The sampled color view of the native surface whose key == guest_address, or
  // VK_NULL_HANDLE if none exists — the texture-fetch redirect uses this to bind
  // the natively-rendered image instead of the EDRAM-resolved upload. Only
  // returns a view once the surface has actually been rendered this run (else the
  // image is undefined/garbage and must fall back to EDRAM).
  VkImageView LookupSampledSurface(uint32_t guest_address);
  // The single-sample DEPTH RESOLVE image of the native surface whose key ==
  // guest_address, or VK_NULL_HANDLE if that surface has no depth resolve (not
  // MSAA, or gpu_bd_native_depth_resolve off) or has not rendered yet. This is the
  // consumer-redirect half of the depth work: BD's depth ownership transfers are
  // 66.7% pure MSAA sample-count conversions (device census, n=8192), and a
  // SAMPLE_ZERO in-pass resolve produces exactly that content natively - so a
  // consumer that would have read the EDRAM-converted depth can read this image
  // instead, which is what makes the corresponding EDRAM transfer droppable.
  // Same "rendered this run" guard as LookupSampledSurface: before the surface has
  // been rendered the resolve target is undefined and MUST fall back to EDRAM.
  VkImage LookupDepthResolveImage(uint32_t guest_address);
  // Number of live native surfaces (diagnostics).
  size_t surface_count() const { return surfaces_.size(); }
  // Diagnostic: dump every live native surface's key (= resolve-dest guest
  // address) with its dims/format/samples and whether it currently has a
  // depth-resolve image with valid current-frame content. Exists to establish the
  // KEY CORRESPONDENCE that blocks the depth-consumer redirect: consumers are
  // identified by EDRAM keys (base_tiles/pitch/msaa) while native surfaces are
  // keyed by guest address, and nothing today maps between them. Logging both
  // sides at the same moment lets the mapping be derived from evidence instead of
  // guessed - a wrong depth redirect renders correct on desktop and COLLAPSES the
  // field on Turnip, so this must not be assumed.
  void LogSurfaceKeys(const char* context);
  // Clear per-frame render flags on all surfaces (call at frame start): the next
  // draw into each surface re-CLEARs it, matching BD re-priming its RTs per frame.
  void BeginSurfaceFrame();

 private:
  bool CreateRenderPass();
  bool CreateImages();
  bool CreateFramebuffer();
  // Allocate images/views/passes/framebuffer into `surface` per its fields.
  bool CreateSurfaceResources(NativeSurface& surface);
  void DestroySurfaceResources(NativeSurface& surface);

  // key(resolve-dest guest address) -> persistent native surface. Node stability
  // (unordered_map) so returned NativeSurface* stay valid across inserts.
  std::unordered_map<uint32_t, NativeSurface> surfaces_;

  VulkanCommandProcessor& command_processor_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;

  // Native (NON-EDRAM) attachments — the whole point: no tile/transfer machinery.
  VkImage color_image_ = VK_NULL_HANDLE;
  VkDeviceMemory color_memory_ = VK_NULL_HANDLE;
  VkImageView color_view_ = VK_NULL_HANDLE;
  // Single-sample resolve target (only when samples_ > 1) - the MSAA color
  // resolves into this at pass end; it is what gets presented.
  VkImage resolve_image_ = VK_NULL_HANDLE;
  VkDeviceMemory resolve_memory_ = VK_NULL_HANDLE;
  VkImageView resolve_view_ = VK_NULL_HANDLE;
  // Full-display present target (only when stretch is enabled): the rendered
  // field (in the left src_width px) is blitted STRETCHED into this at the full
  // display width, and THIS is presented. Kept SHADER_READ between frames.
  VkImage present_image_ = VK_NULL_HANDLE;
  VkDeviceMemory present_memory_ = VK_NULL_HANDLE;
  VkImageView present_view_ = VK_NULL_HANDLE;
  uint32_t present_width_ = 0;
  uint32_t stretch_src_width_ = 0;  // >0 = stretch [0..this] -> full width
  bool present_image_ready_ = false;  // has valid SHADER_READ content to present
  VkImage depth_image_ = VK_NULL_HANDLE;
  VkDeviceMemory depth_memory_ = VK_NULL_HANDLE;
  VkImageView depth_view_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;       // CLEAR (first draw/frame)
  VkRenderPass render_pass_load_ = VK_NULL_HANDLE;  // LOAD (accumulate re-begins)
  VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

  // Color format is dynamic (matched to the field's format for pipeline compat);
  // depth is fixed.
  VkFormat color_format_ = VK_FORMAT_R8G8B8A8_UNORM;
  VkFormat depth_format_ = VK_FORMAT_D24_UNORM_S8_UINT;
  VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_BD_NATIVE_RENDERER_H_
