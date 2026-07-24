/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_render_target_cache.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "third_party/glslang/SPIRV/GLSL.std.450.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/spirv_builder.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/texture_cache.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_util.h"

DEFINE_bool(
    vulkan_dump_rt_full_barrier, false,
    "Widen the EDRAM dump's source image barrier to a full color/depth-attachment-"
    "write + transfer-write -> compute-read flush. Diagnostic/fix for Turnip (Mesa) "
    "reading a just-rendered render target as zero in the resolve dump; default off "
    "(a strict superset, so the proprietary Qualcomm path is unaffected).",
    "GPU");

DEFINE_bool(
    vulkan_force_float_color_unorm, false,
    "Diagnostic: map the guest k_2_10_10_10_FLOAT color render target to the "
    "non-float host format A8B8G8R8_UNORM_PACK32 instead of R16G16B16A16_SFLOAT. "
    "Color-incorrect (loses the 7e3 extended range); isolates whether the float "
    "color attachment is why color never renders on Turnip. Default off.",
    "GPU");

DEFINE_bool(
    gpu_fp10_color_as_unorm10, false,
    "Efficiency (RT-bandwidth recovery): map the guest k_2_10_10_10_FLOAT (7e3) "
    "color render target to the 32-bpp host format A2B10G10R10_UNORM_PACK32 "
    "instead of the 64-bpp R16G16B16A16_SFLOAT default. Halves color render "
    "target bandwidth/footprint on the dominant Blue Dragon color format while "
    "keeping a full 10 bits per channel (strictly better than the 8-bit "
    "vulkan_force_float_color_unorm diagnostic, same 32-bit format class so the "
    "integer-aliased ownership-transfer view stays legal). Clamps the 7e3 [0, ~31) "
    "extended range to [0, 1] -> correct for SDR scenes, loses HDR highlights "
    "(bloom/tonemap intermediates); gate off for HDR-heavy titles. Default off.",
    "GPU");

DEFINE_bool(
    gpu_2101010_color_as_unorm10, false,
    "Quality: map the guest k_2_10_10_10 (non-float, 10-bit UNORM) color render "
    "target to the matching 32-bpp host format A2B10G10R10_UNORM_PACK32 instead of "
    "the 8-bit A8B8G8R8_UNORM_PACK32 default. The guest format genuinely has 10 "
    "bits per color channel; the 8-bit host fallback silently discards 2 bits per "
    "channel (banding). This keeps the full 10 bits at identical 32-bpp footprint "
    "(no bandwidth cost), same 32-bit format class so the integer-aliased "
    "ownership-transfer view stays legal. Bit-exact-or-better; pending device A/B. "
    "Default off.",
    "GPU");

DEFINE_bool(
    gpu_vulkan_rt_keep_ubwc, false,
    "Efficiency (RT-bandwidth recovery): keep Adreno UBWC framebuffer compression "
    "alive on color render targets that need MUTABLE_FORMAT (for the integer-aliased "
    "ownership-transfer view) by attaching a VkImageFormatListCreateInfo listing the "
    "exact 2 view formats (base + transfer alias). Pre-750 Adreno (the 740) disables "
    "UBWC on a MUTABLE_FORMAT image unless a format list is provided; Turnip honors "
    "the list. Recovers part of BD's RT-bandwidth slice (the efficiency-gap analysis "
    "put ~10-18ms of the 126ms heavy frame on format/UBWC byte inflation - the clean "
    "recoverable part, vs the a740-irreducible LRZ-defeat overdraw which is frame-gen's "
    "job). The color RT has exactly these 2 views so the list is complete + correct. "
    "Default off; A/B on a gpu_freeze_at_guest_ms field (on vs off, or vs "
    "gpu_vulkan_driver_debug=noubwc) to size the win, then default-on per-title.",
    "GPU");

DEFINE_uint32(
    gpu_fdm_foliage, 0,
    "FDM (VK_EXT_fragment_density_map) overdraw lever: when nonzero, attach a "
    "uniform fragment density map to GUEST-GEOMETRY host render passes so Turnip "
    "renders bins at 1/value resolution (a7xx GRAS_BIN_FOVEAT - distinct HW from "
    "VRS coarse-shading, gate-confirmed 2026-06-22), cutting the COUNT of "
    "rasterized/depth-tested/shaded fragments on BD's per-covered-fragment "
    "overdraw floor. value = fragment-area edge: 2 = half-res per axis (~4x fewer "
    "fragments), 4 = quarter-res (~16x). EDRAM-transfer passes (which share the "
    "render-pass path) are excluded so copies stay pixel-exact. Density map is "
    "R16G16_SFLOAT (Turnip advertises the FDM format feature only for float "
    "formats). Stacks with VRS (which coarse-shades the surviving fragments). "
    "Default 0 (off). KNOWN BROKEN on xenia's EDRAM-in-GMEM multi-pass renderer "
    "(device-tested 2026-06-22): FDM applies but CORRUPTS output (per-bin foveation "
    "offsets conflict with the GMEM tile config + cross-pass depth/color reuse) and "
    "shows NO gpu_frame_us reduction (foveated-GMEM->full-RT resolve cancels the "
    "fragment saving). Left as a gated experiment; do NOT enable expecting a win.",
    "GPU");

DEFINE_uint32(
    gpu_clamp_rt_framebuffer_height, 0,
    "BD tile-I/O cut: clamp the host render-target FRAMEBUFFER height to this many "
    "pixels (0 = off = full tile-rounded height). Host RTs are tile-rounded to huge "
    "heights (e.g. 4096 / 8192) so overlapping EDRAM ranges alias correctly, but at "
    "720p only ~720 rows are ever rendered; the TBDR storeOp/loadOp cover the "
    "framebuffer height, so the unused rows are stored+loaded every pass (the bulk of "
    "BD's ~79ms tile I/O for the tall RTs). LOSSLESS only when no EDRAM aliasing "
    "transfer reads the clamped rows (BD: aliasing transfers measured = 0). Set e.g. "
    "768 for 720p. Default 0 (off).",
    "GPU");

DEFINE_uint32(
    gpu_clamp_rt_image_height, 0,
    "BD tile-I/O cut (THE REAL KNOB - gpu_clamp_rt_framebuffer_height was inert "
    "because on Turnip the per-pass GMEM resolve / storeOp covers the full "
    "ATTACHMENT IMAGE extent, NOT the renderArea/framebuffer). Host RT VkImages are "
    "allocated tile-rounded to huge heights (4096 / 8192) for EDRAM aliasing "
    "reserve, but only ~720 rows are ever rendered at 720p, so every render pass "
    "resolves an 8192-tall tile grid (the resolution-invariant ~74ms tile-resolve "
    "floor). Clamp the IMAGE allocation height to this many pixels so the store "
    "target itself shrinks to the used rows. Set EQUAL TO OR ABOVE the rendered "
    "height (e.g. 768 for 720p) AND together with gpu_clamp_rt_framebuffer_height "
    "(framebuffer must be <= image). LOSSLESS only when no draw renders beyond the "
    "clamp AND no EDRAM aliasing transfer reads the clamped rows (BD: aliasing "
    "transfers measured = 0; max rendered height 720 < 768). Default 0 (off).",
    "GPU");

DEFINE_bool(
    gpu_rt_as_texture, false,
    "EDRAM-recompiler RT-as-texture: when a pixel-texture fetch samples a still-"
    "resident render target that was resolved this frame (non-converting format, "
    "1-sample, no resolution scale), bind the RT image view directly instead of "
    "reloading the resolved texture from shared memory - skipping the "
    "resolve->RAM->reload round-trip. Default off.",
    "GPU");

DEFINE_bool(
    gpu_vulkan_compute_postprocess_probe, false,
    "EDRAM compute-post-process FOUNDATION probe (brick 1 of the compute fusion "
    "core): on the host-RT path, dispatch an IDENTITY compute shader over the "
    "EDRAM SSBO at each resolve (reads then writes back the SAME dwords, so EDRAM "
    "contents are byte-unchanged and rendering must stay pixel-identical). "
    "Validates that a mid-frame compute dispatch + barriers over the EDRAM buffer "
    "works and stays synchronized against live host-RT rendering on Turnip - the "
    "unproven foundation the composite-compute fusion is built on. Default off.",
    "GPU");

DEFINE_bool(
    vulkan_trace_dump_depth_image, false,
    "Diagnostic: like vulkan_trace_dump_rt_image but for the DEPTH render target "
    "image - settles whether geometry rasterized (depth has varying geometry Z) "
    "vs only the clear (no fragments). Default off.",
    "GPU");

DEFINE_bool(
    vulkan_trace_dump_rt_image, false,
    "Diagnostic: before the EDRAM dump, copy the first 1xMSAA color render "
    "target image straight to a host buffer (vkCmdCopyImageToBuffer, independent "
    "of the dump compute shader) and log a sparse checksum, to tell whether the "
    "RT image actually holds the rendered data (render/store bug) vs the dump "
    "shader reading it as zero (e.g. on Turnip). Very slow; default off.",
    "GPU");

DEFINE_string(
    render_target_path_vulkan, "",
    "Render target emulation path to use on Vulkan.\n"
    "Use: [any, fbo, fsi]\n"
    " fbo:\n"
    "  Host framebuffers and fixed-function blending and depth / stencil "
    "testing, copying between render targets when needed.\n"
    "  Lower accuracy (limited pixel format support).\n"
    "  Performance limited primarily by render target layout changes requiring "
    "copying, but generally higher.\n"
    " fsi:\n"
    "  Manual pixel packing, blending and depth / stencil testing, with free "
    "render target layout changes.\n"
    "  Requires a GPU supporting fragment shader interlock.\n"
    "  Highest accuracy (all pixel formats handled in software).\n"
    "  Performance limited primarily by overdraw.\n"
    " Any other value:\n"
    "  Choose what is considered the most optimal for the system (currently "
    "always FB because the FSI path is much slower now).",
    "GPU");

// Blue Dragon native-draw HLE step 3 (defined in command_processor.cc): drop
// BD's emulated per-tile base-0 EDRAM->RAM resolve copy (the ~120ms field GPU
// fence). 1 = base-0 color copies only; 2 = all color copies (upper bound).
DECLARE_uint32(gpu_bd_hle_drop_resolve);
DECLARE_bool(gpu_bd_native_renderer);
DECLARE_int32(gpu_bd_native_color_lifetime_hle);
DECLARE_bool(gpu_bd_native_field_convert);
DECLARE_bool(gpu_bd_native_keep_scissor);
DECLARE_bool(gpu_bd_field_decouple);
DECLARE_bool(gpu_bd_native_depth_convert);
DECLARE_bool(gpu_bd_native_depth_handoff);
DECLARE_bool(gpu_bd_framegraph_depth);
DECLARE_bool(gpu_bd_framegraph_depth_dump);
DECLARE_bool(gpu_bd_framegraph_depth_shadow);
DECLARE_bool(gpu_bd_patha_depth_snapshot);
DECLARE_bool(gpu_bd_depth_xfer_census);
DECLARE_bool(gpu_bd_native_drop_depth_downscale);
DECLARE_bool(gpu_bd_native_drop_resolves);
DECLARE_bool(gpu_bd_native_drop_transfers);
DECLARE_bool(gpu_bd_native_drop_all_color_xfer);
DECLARE_bool(gpu_bd_native_drop_all_xfer);

namespace xe {
namespace gpu {
namespace vulkan {

namespace {

std::atomic<int32_t> vulkan_resolve_trace_count{0};
std::atomic<int32_t> vulkan_edram_checksum_trace_count{0};
std::atomic<int32_t> vulkan_dump_state_trace_count{0};
std::atomic<uint64_t> bd_framegraph_rung3_scheduled{0};
std::atomic<uint64_t> bd_framegraph_rung3_fused{0};
std::atomic<uint64_t> bd_framegraph_rung3_relocated_fallback{0};
uint32_t bd_framegraph_rung3_swaps = 0;

bool ShouldTraceVulkanResolve() {
  if (!cvars::vulkan_trace_resolve) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_resolve_budget;
  return budget < 0 || vulkan_resolve_trace_count.fetch_add(1) < budget;
}

bool ShouldTraceVulkanEdramChecksum() {
  if (!cvars::vulkan_trace_edram_checksum) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_edram_checksum_budget;
  return budget < 0 ||
         vulkan_edram_checksum_trace_count.fetch_add(1) < budget;
}

bool ShouldTraceVulkanDumpState() {
  if (!cvars::vulkan_trace_copy_state) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_copy_state_budget;
  return budget < 0 || vulkan_dump_state_trace_count.fetch_add(1) < budget;
}

const char* ResolveCopyShaderName(draw_util::ResolveCopyShaderIndex shader) {
  switch (shader) {
    case draw_util::ResolveCopyShaderIndex::kFast32bpp1x2xMSAA:
      return "Fast32bpp1x2xMSAA";
    case draw_util::ResolveCopyShaderIndex::kFast32bpp4xMSAA:
      return "Fast32bpp4xMSAA";
    case draw_util::ResolveCopyShaderIndex::kFast64bpp1x2xMSAA:
      return "Fast64bpp1x2xMSAA";
    case draw_util::ResolveCopyShaderIndex::kFast64bpp4xMSAA:
      return "Fast64bpp4xMSAA";
    case draw_util::ResolveCopyShaderIndex::kFull8bpp:
      return "Full8bpp";
    case draw_util::ResolveCopyShaderIndex::kFull16bpp:
      return "Full16bpp";
    case draw_util::ResolveCopyShaderIndex::kFull32bpp:
      return "Full32bpp";
    case draw_util::ResolveCopyShaderIndex::kFull64bpp:
      return "Full64bpp";
    case draw_util::ResolveCopyShaderIndex::kFull128bpp:
      return "Full128bpp";
    case draw_util::ResolveCopyShaderIndex::kUnknown:
    default:
      return "Unknown";
  }
}

}  // namespace

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/edram_identity_probe_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/host_depth_store_1xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/host_depth_store_2xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/host_depth_store_4xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/passthrough_position_xy_vs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_clear_32bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_clear_32bpp_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_clear_64bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_clear_64bpp_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_fast_32bpp_1x2xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_fast_32bpp_1x2xmsaa_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_fast_32bpp_4xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_fast_32bpp_4xmsaa_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_fast_64bpp_1x2xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_fast_64bpp_1x2xmsaa_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_fast_64bpp_4xmsaa_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_fast_64bpp_4xmsaa_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_128bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_128bpp_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_16bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_16bpp_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_32bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_32bpp_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_64bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_64bpp_scaled_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_8bpp_cs.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_8bpp_scaled_cs.h"
}  // namespace shaders

const VulkanRenderTargetCache::ResolveCopyShaderCode
    VulkanRenderTargetCache::kResolveCopyShaders[size_t(
        draw_util::ResolveCopyShaderIndex::kCount)] = {
        {shaders::resolve_fast_32bpp_1x2xmsaa_cs,
         sizeof(shaders::resolve_fast_32bpp_1x2xmsaa_cs),
         shaders::resolve_fast_32bpp_1x2xmsaa_scaled_cs,
         sizeof(shaders::resolve_fast_32bpp_1x2xmsaa_scaled_cs)},
        {shaders::resolve_fast_32bpp_4xmsaa_cs,
         sizeof(shaders::resolve_fast_32bpp_4xmsaa_cs),
         shaders::resolve_fast_32bpp_4xmsaa_scaled_cs,
         sizeof(shaders::resolve_fast_32bpp_4xmsaa_scaled_cs)},
        {shaders::resolve_fast_64bpp_1x2xmsaa_cs,
         sizeof(shaders::resolve_fast_64bpp_1x2xmsaa_cs),
         shaders::resolve_fast_64bpp_1x2xmsaa_scaled_cs,
         sizeof(shaders::resolve_fast_64bpp_1x2xmsaa_scaled_cs)},
        {shaders::resolve_fast_64bpp_4xmsaa_cs,
         sizeof(shaders::resolve_fast_64bpp_4xmsaa_cs),
         shaders::resolve_fast_64bpp_4xmsaa_scaled_cs,
         sizeof(shaders::resolve_fast_64bpp_4xmsaa_scaled_cs)},
        {shaders::resolve_full_8bpp_cs, sizeof(shaders::resolve_full_8bpp_cs),
         shaders::resolve_full_8bpp_scaled_cs,
         sizeof(shaders::resolve_full_8bpp_scaled_cs)},
        {shaders::resolve_full_16bpp_cs, sizeof(shaders::resolve_full_16bpp_cs),
         shaders::resolve_full_16bpp_scaled_cs,
         sizeof(shaders::resolve_full_16bpp_scaled_cs)},
        {shaders::resolve_full_32bpp_cs, sizeof(shaders::resolve_full_32bpp_cs),
         shaders::resolve_full_32bpp_scaled_cs,
         sizeof(shaders::resolve_full_32bpp_scaled_cs)},
        {shaders::resolve_full_64bpp_cs, sizeof(shaders::resolve_full_64bpp_cs),
         shaders::resolve_full_64bpp_scaled_cs,
         sizeof(shaders::resolve_full_64bpp_scaled_cs)},
        {shaders::resolve_full_128bpp_cs,
         sizeof(shaders::resolve_full_128bpp_cs),
         shaders::resolve_full_128bpp_scaled_cs,
         sizeof(shaders::resolve_full_128bpp_scaled_cs)},
};

const VulkanRenderTargetCache::TransferPipelineLayoutInfo
    VulkanRenderTargetCache::kTransferPipelineLayoutInfos[size_t(
        TransferPipelineLayoutIndex::kCount)] = {
        // kColor
        {kTransferUsedDescriptorSetColorTextureBit,
         kTransferUsedPushConstantDwordAddressBit},
        // kDepth
        {kTransferUsedDescriptorSetDepthStencilTexturesBit,
         kTransferUsedPushConstantDwordAddressBit},
        // kColorToStencilBit
        {kTransferUsedDescriptorSetColorTextureBit,
         kTransferUsedPushConstantDwordAddressBit |
             kTransferUsedPushConstantDwordStencilMaskBit},
        // kDepthToStencilBit
        {kTransferUsedDescriptorSetDepthStencilTexturesBit,
         kTransferUsedPushConstantDwordAddressBit |
             kTransferUsedPushConstantDwordStencilMaskBit},
        // kColorAndHostDepthTexture
        {kTransferUsedDescriptorSetHostDepthStencilTexturesBit |
             kTransferUsedDescriptorSetColorTextureBit,
         kTransferUsedPushConstantDwordHostDepthAddressBit |
             kTransferUsedPushConstantDwordAddressBit},
        // kColorAndHostDepthBuffer
        {kTransferUsedDescriptorSetHostDepthBufferBit |
             kTransferUsedDescriptorSetColorTextureBit,
         kTransferUsedPushConstantDwordHostDepthAddressBit |
             kTransferUsedPushConstantDwordAddressBit},
        // kDepthAndHostDepthTexture
        {kTransferUsedDescriptorSetHostDepthStencilTexturesBit |
             kTransferUsedDescriptorSetDepthStencilTexturesBit,
         kTransferUsedPushConstantDwordHostDepthAddressBit |
             kTransferUsedPushConstantDwordAddressBit},
        // kDepthAndHostDepthBuffer
        {kTransferUsedDescriptorSetHostDepthBufferBit |
             kTransferUsedDescriptorSetDepthStencilTexturesBit,
         kTransferUsedPushConstantDwordHostDepthAddressBit |
             kTransferUsedPushConstantDwordAddressBit},
};

const VulkanRenderTargetCache::TransferModeInfo
    VulkanRenderTargetCache::kTransferModes[size_t(TransferMode::kCount)] = {
        // kColorToDepth
        {TransferOutput::kDepth, TransferPipelineLayoutIndex::kColor},
        // kColorToColor
        {TransferOutput::kColor, TransferPipelineLayoutIndex::kColor},
        // kDepthToDepth
        {TransferOutput::kDepth, TransferPipelineLayoutIndex::kDepth},
        // kDepthToColor
        {TransferOutput::kColor, TransferPipelineLayoutIndex::kDepth},
        // kColorToStencilBit
        {TransferOutput::kStencilBit,
         TransferPipelineLayoutIndex::kColorToStencilBit},
        // kDepthToStencilBit
        {TransferOutput::kStencilBit,
         TransferPipelineLayoutIndex::kDepthToStencilBit},
        // kColorAndHostDepthToDepth
        {TransferOutput::kDepth,
         TransferPipelineLayoutIndex::kColorAndHostDepthTexture},
        // kDepthAndHostDepthToDepth
        {TransferOutput::kDepth,
         TransferPipelineLayoutIndex::kDepthAndHostDepthTexture},
        // kColorAndHostDepthCopyToDepth
        {TransferOutput::kDepth,
         TransferPipelineLayoutIndex::kColorAndHostDepthBuffer},
        // kDepthAndHostDepthCopyToDepth
        {TransferOutput::kDepth,
         TransferPipelineLayoutIndex::kDepthAndHostDepthBuffer},
};

VulkanRenderTargetCache::VulkanRenderTargetCache(
    const RegisterFile& register_file, const Memory& memory,
    TraceWriter& trace_writer, uint32_t draw_resolution_scale_x,
    uint32_t draw_resolution_scale_y, VulkanCommandProcessor& command_processor)
    : RenderTargetCache(register_file, memory, &trace_writer,
                        draw_resolution_scale_x, draw_resolution_scale_y),
      command_processor_(command_processor),
      trace_writer_(trace_writer) {}

VulkanRenderTargetCache::~VulkanRenderTargetCache() { Shutdown(true); }

bool VulkanRenderTargetCache::Initialize(uint32_t shared_memory_binding_count) {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanInstance::Functions& ifn =
      vulkan_device->vulkan_instance()->functions();
  const VkPhysicalDevice physical_device = vulkan_device->physical_device();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  const ui::vulkan::VulkanDevice::Properties& device_properties =
      vulkan_device->properties();

  bool fsi_requested = cvars::render_target_path_vulkan == "fsi";
  // THE EDRAM SOLVE (gpu_vulkan_edram_atomic): force the interlock BUFFER path on
  // Turnip with NO FSI - the depth+color RMW is ordered by atomics instead of the
  // fragment-shader-interlock, so the interlock feature requirement below is
  // bypassed. The buffer path has NO render-to-texture passes (one EDRAM SSBO),
  // collapsing BD's 42 tile-I/O passes by construction. Non-functional until the
  // atomic ROP SPIR-V lands (forcing it earlier emits FSI SPIR-V the driver
  // rejects); default off.
  bool atomic_no_fsi = cvars::gpu_vulkan_edram_atomic;
  if (fsi_requested || atomic_no_fsi) {
    path_ = Path::kPixelShaderInterlock;
  } else {
    path_ = Path::kHostRenderTargets;
  }
  edram_atomic_no_fsi_ = atomic_no_fsi;
  // Fragment shader interlock is a feature implemented by pretty advanced GPUs,
  // closer to Direct3D 11 / OpenGL ES 3.2 level mainly, not Direct3D 10 /
  // OpenGL ES 3.1. Thus, it's fine to demand a wide range of other optional
  // features for the fragment shader interlock backend to work.
  if (path_ == Path::kPixelShaderInterlock && !atomic_no_fsi) {
    // Interlocking between fragments with common sample coverage is enough, but
    // interlocking more is acceptable too (fragmentShaderShadingRateInterlock
    // would be okay too, but it's unlikely that an implementation would
    // advertise only it and not any other ones, as it's a very specific feature
    // interacting with another optional feature that is variable shading rate,
    // so there's no need to overcomplicate the checks and the shader execution
    // mode setting).
    // Sample-rate shading is required by certain SPIR-V revisions to access the
    // sample mask fragment shader input.
    // Stanard sample locations are needed for calculating the depth at the
    // samples.
    // It's unlikely that a device exposing fragment shader interlock won't have
    // a large enough storage buffer range and a sufficient SSBO slot count for
    // all the shared memory buffers and the EDRAM buffer - an in a conflict
    // between, for instance, the ability to vfetch and memexport in fragment
    // shaders, and the usage of fragment shader interlock, prefer the former
    // for simplicity.
    bool fsi_features_supported =
        (device_properties.fragmentShaderSampleInterlock ||
         device_properties.fragmentShaderPixelInterlock) &&
        device_properties.fragmentStoresAndAtomics &&
        device_properties.sampleRateShading &&
        device_properties.standardSampleLocations &&
        shared_memory_binding_count <
            device_properties.maxPerStageDescriptorStorageBuffers;
    if (!fsi_features_supported) {
      XELOGW(
          "VulkanRenderTargetCache: falling back from requested FSI render "
          "target path: sample_interlock={} pixel_interlock={} "
          "fragment_stores_atomics={} sample_rate_shading={} "
          "standard_sample_locations={} shared_memory_bindings={} "
          "max_storage_buffers={}",
          device_properties.fragmentShaderSampleInterlock,
          device_properties.fragmentShaderPixelInterlock,
          device_properties.fragmentStoresAndAtomics,
          device_properties.sampleRateShading,
          device_properties.standardSampleLocations, shared_memory_binding_count,
          device_properties.maxPerStageDescriptorStorageBuffers);
      path_ = Path::kHostRenderTargets;
    }
  }
  XELOGI(
      "VulkanRenderTargetCache: render_target_path_vulkan='{}' selected={}",
      cvars::render_target_path_vulkan,
      path_ == Path::kPixelShaderInterlock ? "fsi" : "fbo");

  // Track #6 (gpu_vulkan_edram_roaa): EDRAM-ROAA sub-mode of the host
  // render-target path - eliminates the EDRAM ownership-transfer copies
  // (device-measured ~9ms / ~22% of the BTTF GPU frame) by reading prior RT
  // contents via rasterization-order input attachments instead of copying.
  // Requires the host RT path + VK_EXT_rasterization_order_attachment_access +
  // the color attachment-access feature. Gates only the transfer / render-pass /
  // pipeline / pixel-shader sites; default off, byte-identical when off.
  edram_roaa_ =
      cvars::gpu_vulkan_edram_roaa && path_ == Path::kHostRenderTargets &&
      vulkan_device->extensions()
          .ext_EXT_rasterization_order_attachment_access &&
      vulkan_device->properties().rasterizationOrderColorAttachmentAccess;
  XELOGI("VulkanRenderTargetCache: edram_roaa={}", edram_roaa_);

  // THE EDRAM SOLVE, hybrid form (gpu_vulkan_hybrid_postprocess): keep the host-RT
  // path for the overdraw-heavy main scene (GMEM ROP) and reroute only the
  // 1x-coverage post-process composites through the EDRAM buffer/SSBO path. Only
  // meaningful on the host-RT path (it augments it); ignored under the forced
  // full-buffer atomic path. The rerouting itself is built incrementally - this
  // just latches the intent + logs it as the entry point.
  hybrid_postprocess_ =
      cvars::gpu_vulkan_hybrid_postprocess && path_ == Path::kHostRenderTargets;
  XELOGI("VulkanRenderTargetCache: hybrid_postprocess={}", hybrid_postprocess_);

  // Format support.
  constexpr VkFormatFeatureFlags kUsedDepthFormatFeatures =
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
  VkFormatProperties depth_unorm24_properties;
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_D24_UNORM_S8_UINT, &depth_unorm24_properties);
  depth_unorm24_vulkan_format_supported_ =
      (depth_unorm24_properties.optimalTilingFeatures &
       kUsedDepthFormatFeatures) == kUsedDepthFormatFeatures;

  // BD-30 native depth conversion: does the host depth format support a native
  // BLIT (src+dst)? On Turnip/Adreno depth blit is commonly supported and lets the
  // mixed-resolution depth-downscale conversion be a native vkCmdBlitImage instead
  // of the EDRAM tile-reinterpreting transfer (the ~30ms/frame between-pass GAP).
  // Both host depth formats are checked; the conversion path also re-checks per RT.
  constexpr VkFormatFeatureFlags kBlitSrcDst =
      VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
  VkFormatProperties depth_d24_blit_props, depth_d32_blit_props;
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_D24_UNORM_S8_UINT, &depth_d24_blit_props);
  ifn.vkGetPhysicalDeviceFormatProperties(
      physical_device, VK_FORMAT_D32_SFLOAT_S8_UINT, &depth_d32_blit_props);
  depth_blit_supported_ =
      ((depth_d24_blit_props.optimalTilingFeatures & kBlitSrcDst) == kBlitSrcDst) ||
      ((depth_d32_blit_props.optimalTilingFeatures & kBlitSrcDst) == kBlitSrcDst);
  XELOGI("BD native depth-convert: depth_blit_supported={}", depth_blit_supported_);

  // 2x MSAA support.
  // TODO(Triang3l): Handle sampledImageIntegerSampleCounts 4 not supported in
  // transfers.
  if (cvars::native_2x_msaa) {
    // Multisampled integer sampled images are optional in Vulkan and in Xenia.
    msaa_2x_attachments_supported_ =
        (device_properties.framebufferColorSampleCounts &
         device_properties.framebufferDepthSampleCounts &
         device_properties.framebufferStencilSampleCounts &
         device_properties.sampledImageColorSampleCounts &
         device_properties.sampledImageDepthSampleCounts &
         device_properties.sampledImageStencilSampleCounts &
         VK_SAMPLE_COUNT_2_BIT) &&
        (device_properties.sampledImageIntegerSampleCounts &
         (VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT)) !=
            VK_SAMPLE_COUNT_4_BIT;
    msaa_2x_no_attachments_supported_ =
        (device_properties.framebufferNoAttachmentsSampleCounts &
         VK_SAMPLE_COUNT_2_BIT) != 0;
  } else {
    msaa_2x_attachments_supported_ = false;
    msaa_2x_no_attachments_supported_ = false;
  }

  // Descriptor set layouts.
  VkDescriptorSetLayoutBinding descriptor_set_layout_bindings[2];
  descriptor_set_layout_bindings[0].binding = 0;
  descriptor_set_layout_bindings[0].descriptorType =
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  descriptor_set_layout_bindings[0].descriptorCount = 1;
  descriptor_set_layout_bindings[0].stageFlags =
      VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
  descriptor_set_layout_bindings[0].pImmutableSamplers = nullptr;
  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
  descriptor_set_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_create_info.pNext = nullptr;
  descriptor_set_layout_create_info.flags = 0;
  descriptor_set_layout_create_info.bindingCount = 1;
  descriptor_set_layout_create_info.pBindings = descriptor_set_layout_bindings;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_storage_buffer_) != VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the descriptor set layout "
        "with one storage buffer");
    Shutdown();
    return false;
  }
  descriptor_set_layout_bindings[0].descriptorType =
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_sampled_image_) != VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the descriptor set layout "
        "with one sampled image");
    Shutdown();
    return false;
  }
  descriptor_set_layout_bindings[1].binding = 1;
  descriptor_set_layout_bindings[1].descriptorType =
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  descriptor_set_layout_bindings[1].descriptorCount = 1;
  descriptor_set_layout_bindings[1].stageFlags =
      descriptor_set_layout_bindings[0].stageFlags;
  descriptor_set_layout_bindings[1].pImmutableSamplers = nullptr;
  descriptor_set_layout_create_info.bindingCount = 2;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_sampled_image_x2_) != VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the descriptor set layout "
        "with two sampled images");
    Shutdown();
    return false;
  }

  // Descriptor set pools.
  // The pool sizes were chosen without a specific reason.
  VkDescriptorPoolSize descriptor_set_layout_size;
  descriptor_set_layout_size.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  descriptor_set_layout_size.descriptorCount = 1;
  descriptor_set_pool_sampled_image_ =
      std::make_unique<ui::vulkan::SingleLayoutDescriptorSetPool>(
          vulkan_device, 256, 1, &descriptor_set_layout_size,
          descriptor_set_layout_sampled_image_);
  descriptor_set_layout_size.descriptorCount = 2;
  descriptor_set_pool_sampled_image_x2_ =
      std::make_unique<ui::vulkan::SingleLayoutDescriptorSetPool>(
          vulkan_device, 256, 1, &descriptor_set_layout_size,
          descriptor_set_layout_sampled_image_x2_);

  // EDRAM contents reinterpretation buffer.
  // 90 MB with 9x resolution scaling - within the minimum
  // maxStorageBufferRange.
  if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
          vulkan_device,
          VkDeviceSize(xenos::kEdramSizeBytes *
                       (draw_resolution_scale_x() * draw_resolution_scale_y())),
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, edram_buffer_,
          edram_buffer_memory_)) {
    XELOGE("VulkanRenderTargetCache: Failed to create the EDRAM buffer");
    Shutdown();
    return false;
  }
  if (GetPath() == Path::kPixelShaderInterlock) {
    // The first operation will likely be drawing.
    edram_buffer_usage_ = EdramBufferUsage::kFragmentReadWrite;
  } else {
    // The first operation will likely be depth self-comparison.
    edram_buffer_usage_ = EdramBufferUsage::kFragmentRead;
  }
  edram_buffer_modification_status_ =
      EdramBufferModificationStatus::kUnmodified;
  VkDescriptorPoolSize edram_storage_buffer_descriptor_pool_size;
  edram_storage_buffer_descriptor_pool_size.type =
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  edram_storage_buffer_descriptor_pool_size.descriptorCount = 1;
  VkDescriptorPoolCreateInfo edram_storage_buffer_descriptor_pool_create_info;
  edram_storage_buffer_descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  edram_storage_buffer_descriptor_pool_create_info.pNext = nullptr;
  edram_storage_buffer_descriptor_pool_create_info.flags = 0;
  edram_storage_buffer_descriptor_pool_create_info.maxSets = 1;
  edram_storage_buffer_descriptor_pool_create_info.poolSizeCount = 1;
  edram_storage_buffer_descriptor_pool_create_info.pPoolSizes =
      &edram_storage_buffer_descriptor_pool_size;
  if (dfn.vkCreateDescriptorPool(
          device, &edram_storage_buffer_descriptor_pool_create_info, nullptr,
          &edram_storage_buffer_descriptor_pool_) != VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the EDRAM buffer storage "
        "buffer descriptor pool");
    Shutdown();
    return false;
  }
  VkDescriptorSetAllocateInfo edram_storage_buffer_descriptor_set_allocate_info;
  edram_storage_buffer_descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  edram_storage_buffer_descriptor_set_allocate_info.pNext = nullptr;
  edram_storage_buffer_descriptor_set_allocate_info.descriptorPool =
      edram_storage_buffer_descriptor_pool_;
  edram_storage_buffer_descriptor_set_allocate_info.descriptorSetCount = 1;
  edram_storage_buffer_descriptor_set_allocate_info.pSetLayouts =
      &descriptor_set_layout_storage_buffer_;
  if (dfn.vkAllocateDescriptorSets(
          device, &edram_storage_buffer_descriptor_set_allocate_info,
          &edram_storage_buffer_descriptor_set_) != VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to allocate the EDRAM buffer storage "
        "buffer descriptor set");
    Shutdown();
    return false;
  }
  VkDescriptorBufferInfo edram_storage_buffer_descriptor_buffer_info;
  edram_storage_buffer_descriptor_buffer_info.buffer = edram_buffer_;
  edram_storage_buffer_descriptor_buffer_info.offset = 0;
  edram_storage_buffer_descriptor_buffer_info.range = VK_WHOLE_SIZE;
  VkWriteDescriptorSet edram_storage_buffer_descriptor_write;
  edram_storage_buffer_descriptor_write.sType =
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  edram_storage_buffer_descriptor_write.pNext = nullptr;
  edram_storage_buffer_descriptor_write.dstSet =
      edram_storage_buffer_descriptor_set_;
  edram_storage_buffer_descriptor_write.dstBinding = 0;
  edram_storage_buffer_descriptor_write.dstArrayElement = 0;
  edram_storage_buffer_descriptor_write.descriptorCount = 1;
  edram_storage_buffer_descriptor_write.descriptorType =
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  edram_storage_buffer_descriptor_write.pImageInfo = nullptr;
  edram_storage_buffer_descriptor_write.pBufferInfo =
      &edram_storage_buffer_descriptor_buffer_info;
  edram_storage_buffer_descriptor_write.pTexelBufferView = nullptr;
  dfn.vkUpdateDescriptorSets(device, 1, &edram_storage_buffer_descriptor_write,
                             0, nullptr);

  // Compute-post-process foundation probe pipeline. Created on ALL paths (cheap,
  // idle unless gpu_vulkan_compute_postprocess_probe is on) so the host-RT path
  // can validate a mid-frame identity compute dispatch over the EDRAM buffer.
  // Push constants = 8 bytes {offset_dwords, count_dwords}; descriptor set 0 =
  // the EDRAM storage buffer layout (matches edram_storage_buffer_descriptor_set_).
  {
    VkPushConstantRange probe_push_constant_range;
    probe_push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    probe_push_constant_range.offset = 0;
    probe_push_constant_range.size = sizeof(uint32_t) * 2;
    VkPipelineLayoutCreateInfo probe_pipeline_layout_create_info;
    probe_pipeline_layout_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    probe_pipeline_layout_create_info.pNext = nullptr;
    probe_pipeline_layout_create_info.flags = 0;
    probe_pipeline_layout_create_info.setLayoutCount = 1;
    probe_pipeline_layout_create_info.pSetLayouts =
        &descriptor_set_layout_storage_buffer_;
    probe_pipeline_layout_create_info.pushConstantRangeCount = 1;
    probe_pipeline_layout_create_info.pPushConstantRanges =
        &probe_push_constant_range;
    if (dfn.vkCreatePipelineLayout(
            device, &probe_pipeline_layout_create_info, nullptr,
            &compute_postprocess_probe_pipeline_layout_) != VK_SUCCESS) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the compute-post-process "
          "probe pipeline layout");
      Shutdown();
      return false;
    }
    compute_postprocess_probe_pipeline_ =
        ui::vulkan::util::CreateComputePipeline(
            vulkan_device, compute_postprocess_probe_pipeline_layout_,
            shaders::edram_identity_probe_cs,
            sizeof(shaders::edram_identity_probe_cs));
    if (compute_postprocess_probe_pipeline_ == VK_NULL_HANDLE) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the compute-post-process "
          "probe pipeline");
      Shutdown();
      return false;
    }
  }

  bool draw_resolution_scaled = IsDrawResolutionScaled();

  // Resolve copy pipeline layout.
  VkDescriptorSetLayout
      resolve_copy_descriptor_set_layouts[kResolveCopyDescriptorSetCount] = {};
  resolve_copy_descriptor_set_layouts[kResolveCopyDescriptorSetEdram] =
      descriptor_set_layout_storage_buffer_;
  resolve_copy_descriptor_set_layouts[kResolveCopyDescriptorSetDest] =
      command_processor_.GetSingleTransientDescriptorLayout(
          VulkanCommandProcessor::SingleTransientDescriptorLayout ::
              kStorageBufferCompute);
  VkPushConstantRange resolve_copy_push_constant_range;
  resolve_copy_push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  resolve_copy_push_constant_range.offset = 0;
  // Potentially binding all of the shared memory at 1x resolution, but only
  // portions with scaled resolution.
  resolve_copy_push_constant_range.size =
      draw_resolution_scaled
          ? sizeof(draw_util::ResolveCopyShaderConstants::DestRelative)
          : sizeof(draw_util::ResolveCopyShaderConstants);
  VkPipelineLayoutCreateInfo resolve_copy_pipeline_layout_create_info;
  resolve_copy_pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  resolve_copy_pipeline_layout_create_info.pNext = nullptr;
  resolve_copy_pipeline_layout_create_info.flags = 0;
  resolve_copy_pipeline_layout_create_info.setLayoutCount =
      kResolveCopyDescriptorSetCount;
  resolve_copy_pipeline_layout_create_info.pSetLayouts =
      resolve_copy_descriptor_set_layouts;
  resolve_copy_pipeline_layout_create_info.pushConstantRangeCount = 1;
  resolve_copy_pipeline_layout_create_info.pPushConstantRanges =
      &resolve_copy_push_constant_range;
  if (dfn.vkCreatePipelineLayout(
          device, &resolve_copy_pipeline_layout_create_info, nullptr,
          &resolve_copy_pipeline_layout_) != VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the resolve copy pipeline "
        "layout");
    Shutdown();
    return false;
  }

  // Resolve copy pipelines.
  for (size_t i = 0; i < size_t(draw_util::ResolveCopyShaderIndex::kCount);
       ++i) {
    const draw_util::ResolveCopyShaderInfo& resolve_copy_shader_info =
        draw_util::resolve_copy_shader_info[i];
    const ResolveCopyShaderCode& resolve_copy_shader_code =
        kResolveCopyShaders[i];
    // Somewhat verification whether resolve_copy_shaders_ is up to date.
    assert_true(resolve_copy_shader_code.unscaled &&
                resolve_copy_shader_code.unscaled_size_bytes &&
                resolve_copy_shader_code.scaled &&
                resolve_copy_shader_code.scaled_size_bytes);
    VkPipeline resolve_copy_pipeline = ui::vulkan::util::CreateComputePipeline(
        vulkan_device, resolve_copy_pipeline_layout_,
        draw_resolution_scaled ? resolve_copy_shader_code.scaled
                               : resolve_copy_shader_code.unscaled,
        draw_resolution_scaled ? resolve_copy_shader_code.scaled_size_bytes
                               : resolve_copy_shader_code.unscaled_size_bytes);
    if (resolve_copy_pipeline == VK_NULL_HANDLE) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the resolve copy "
          "pipeline {}",
          resolve_copy_shader_info.debug_name);
      Shutdown();
      return false;
    }
    vulkan_device->SetObjectName(VK_OBJECT_TYPE_PIPELINE, resolve_copy_pipeline,
                                 resolve_copy_shader_info.debug_name);
    resolve_copy_pipelines_[i] = resolve_copy_pipeline;
  }

  // TODO(Triang3l): All paths (FSI).

  if (path_ == Path::kHostRenderTargets) {
    // Host render targets.

    // TODO(Triang3l): When color space conversion is implemented in the
    // ownership transfer and resolve dump shaders, allow
    // `gamma_render_target_as_unorm16` if VK_FORMAT_R16G16B16A16_UNORM supports
    // the SAMPLED_IMAGE | COLOR_ATTACHMENT | COLOR_ATTACHMENT_BLEND features.
    gamma_render_target_as_unorm16_ = false;

    depth_float24_round_ = cvars::depth_float24_round;

    // Host depth storing pipeline layout.
    VkDescriptorSetLayout host_depth_store_descriptor_set_layouts[] = {
        // Destination EDRAM storage buffer.
        descriptor_set_layout_storage_buffer_,
        // Source depth / stencil texture (only depth is used).
        descriptor_set_layout_sampled_image_x2_,
    };
    VkPushConstantRange host_depth_store_push_constant_range;
    host_depth_store_push_constant_range.stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;
    host_depth_store_push_constant_range.offset = 0;
    host_depth_store_push_constant_range.size = sizeof(HostDepthStoreConstants);
    VkPipelineLayoutCreateInfo host_depth_store_pipeline_layout_create_info;
    host_depth_store_pipeline_layout_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    host_depth_store_pipeline_layout_create_info.pNext = nullptr;
    host_depth_store_pipeline_layout_create_info.flags = 0;
    host_depth_store_pipeline_layout_create_info.setLayoutCount =
        uint32_t(xe::countof(host_depth_store_descriptor_set_layouts));
    host_depth_store_pipeline_layout_create_info.pSetLayouts =
        host_depth_store_descriptor_set_layouts;
    host_depth_store_pipeline_layout_create_info.pushConstantRangeCount = 1;
    host_depth_store_pipeline_layout_create_info.pPushConstantRanges =
        &host_depth_store_push_constant_range;
    if (dfn.vkCreatePipelineLayout(
            device, &host_depth_store_pipeline_layout_create_info, nullptr,
            &host_depth_store_pipeline_layout_) != VK_SUCCESS) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the host depth storing "
          "pipeline layout");
      Shutdown();
      return false;
    }
    const std::pair<const uint32_t*, size_t> host_depth_store_shaders[] = {
        {shaders::host_depth_store_1xmsaa_cs,
         sizeof(shaders::host_depth_store_1xmsaa_cs)},
        {shaders::host_depth_store_2xmsaa_cs,
         sizeof(shaders::host_depth_store_2xmsaa_cs)},
        {shaders::host_depth_store_4xmsaa_cs,
         sizeof(shaders::host_depth_store_4xmsaa_cs)},
    };
    for (size_t i = 0; i < xe::countof(host_depth_store_shaders); ++i) {
      const std::pair<const uint32_t*, size_t> host_depth_store_shader =
          host_depth_store_shaders[i];
      VkPipeline host_depth_store_pipeline =
          ui::vulkan::util::CreateComputePipeline(
              vulkan_device, host_depth_store_pipeline_layout_,
              host_depth_store_shader.first, host_depth_store_shader.second);
      if (host_depth_store_pipeline == VK_NULL_HANDLE) {
        XELOGE(
            "VulkanRenderTargetCache: Failed to create the {}-sample host "
            "depth storing pipeline",
            uint32_t(1) << i);
        Shutdown();
        return false;
      }
      host_depth_store_pipelines_[i] = host_depth_store_pipeline;
    }

    // Transfer and clear vertex buffer, for quads of up to tile granularity.
    transfer_vertex_buffer_pool_ =
        std::make_unique<ui::vulkan::VulkanUploadBufferPool>(
            vulkan_device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            std::max(ui::vulkan::VulkanUploadBufferPool::kDefaultPageSize,
                     sizeof(float) * 2 * 6 *
                         Transfer::kMaxCutoutBorderRectangles *
                         xenos::kEdramTileCount));

    // Transfer vertex shader.
    transfer_passthrough_vertex_shader_ = ui::vulkan::util::CreateShaderModule(
        vulkan_device, shaders::passthrough_position_xy_vs,
        sizeof(shaders::passthrough_position_xy_vs));
    if (transfer_passthrough_vertex_shader_ == VK_NULL_HANDLE) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the render target "
          "ownership transfer vertex shader");
      Shutdown();
      return false;
    }

    // Transfer pipeline layouts.
    VkDescriptorSetLayout transfer_pipeline_layout_descriptor_set_layouts
        [kTransferUsedDescriptorSetCount];
    VkPushConstantRange transfer_pipeline_layout_push_constant_range;
    transfer_pipeline_layout_push_constant_range.stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;
    transfer_pipeline_layout_push_constant_range.offset = 0;
    VkPipelineLayoutCreateInfo transfer_pipeline_layout_create_info;
    transfer_pipeline_layout_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    transfer_pipeline_layout_create_info.pNext = nullptr;
    transfer_pipeline_layout_create_info.flags = 0;
    transfer_pipeline_layout_create_info.pSetLayouts =
        transfer_pipeline_layout_descriptor_set_layouts;
    transfer_pipeline_layout_create_info.pPushConstantRanges =
        &transfer_pipeline_layout_push_constant_range;
    for (size_t i = 0; i < size_t(TransferPipelineLayoutIndex::kCount); ++i) {
      const TransferPipelineLayoutInfo& transfer_pipeline_layout_info =
          kTransferPipelineLayoutInfos[i];
      transfer_pipeline_layout_create_info.setLayoutCount = 0;
      uint32_t transfer_pipeline_layout_descriptor_sets_remaining =
          transfer_pipeline_layout_info.used_descriptor_sets;
      uint32_t transfer_pipeline_layout_descriptor_set_index;
      while (xe::bit_scan_forward(
          transfer_pipeline_layout_descriptor_sets_remaining,
          &transfer_pipeline_layout_descriptor_set_index)) {
        transfer_pipeline_layout_descriptor_sets_remaining &=
            ~(uint32_t(1) << transfer_pipeline_layout_descriptor_set_index);
        VkDescriptorSetLayout transfer_pipeline_layout_descriptor_set_layout =
            VK_NULL_HANDLE;
        switch (TransferUsedDescriptorSet(
            transfer_pipeline_layout_descriptor_set_index)) {
          case kTransferUsedDescriptorSetHostDepthBuffer:
            transfer_pipeline_layout_descriptor_set_layout =
                descriptor_set_layout_storage_buffer_;
            break;
          case kTransferUsedDescriptorSetHostDepthStencilTextures:
          case kTransferUsedDescriptorSetDepthStencilTextures:
            transfer_pipeline_layout_descriptor_set_layout =
                descriptor_set_layout_sampled_image_x2_;
            break;
          case kTransferUsedDescriptorSetColorTexture:
            transfer_pipeline_layout_descriptor_set_layout =
                descriptor_set_layout_sampled_image_;
            break;
          default:
            assert_unhandled_case(TransferUsedDescriptorSet(
                transfer_pipeline_layout_descriptor_set_index));
        }
        transfer_pipeline_layout_descriptor_set_layouts
            [transfer_pipeline_layout_create_info.setLayoutCount++] =
                transfer_pipeline_layout_descriptor_set_layout;
      }
      transfer_pipeline_layout_push_constant_range.size = uint32_t(
          sizeof(uint32_t) *
          xe::bit_count(
              transfer_pipeline_layout_info.used_push_constant_dwords));
      transfer_pipeline_layout_create_info.pushConstantRangeCount =
          transfer_pipeline_layout_info.used_push_constant_dwords ? 1 : 0;
      if (dfn.vkCreatePipelineLayout(
              device, &transfer_pipeline_layout_create_info, nullptr,
              &transfer_pipeline_layouts_[i]) != VK_SUCCESS) {
        XELOGE(
            "VulkanRenderTargetCache: Failed to create the render target "
            "ownership transfer pipeline layout {}",
            i);
        Shutdown();
        return false;
      }
    }

    // Dump pipeline layouts.
    VkDescriptorSetLayout
        dump_pipeline_layout_descriptor_set_layouts[kDumpDescriptorSetCount];
    dump_pipeline_layout_descriptor_set_layouts[kDumpDescriptorSetEdram] =
        descriptor_set_layout_storage_buffer_;
    dump_pipeline_layout_descriptor_set_layouts[kDumpDescriptorSetSource] =
        descriptor_set_layout_sampled_image_;
    VkPushConstantRange dump_pipeline_layout_push_constant_range;
    dump_pipeline_layout_push_constant_range.stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;
    dump_pipeline_layout_push_constant_range.offset = 0;
    dump_pipeline_layout_push_constant_range.size =
        sizeof(uint32_t) * kDumpPushConstantCount;
    VkPipelineLayoutCreateInfo dump_pipeline_layout_create_info;
    dump_pipeline_layout_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    dump_pipeline_layout_create_info.pNext = nullptr;
    dump_pipeline_layout_create_info.flags = 0;
    dump_pipeline_layout_create_info.setLayoutCount =
        uint32_t(xe::countof(dump_pipeline_layout_descriptor_set_layouts));
    dump_pipeline_layout_create_info.pSetLayouts =
        dump_pipeline_layout_descriptor_set_layouts;
    dump_pipeline_layout_create_info.pushConstantRangeCount = 1;
    dump_pipeline_layout_create_info.pPushConstantRanges =
        &dump_pipeline_layout_push_constant_range;
    if (dfn.vkCreatePipelineLayout(device, &dump_pipeline_layout_create_info,
                                   nullptr, &dump_pipeline_layout_color_) !=
        VK_SUCCESS) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the color render target "
          "dumping pipeline layout");
      Shutdown();
      return false;
    }
    dump_pipeline_layout_descriptor_set_layouts[kDumpDescriptorSetSource] =
        descriptor_set_layout_sampled_image_x2_;
    if (dfn.vkCreatePipelineLayout(device, &dump_pipeline_layout_create_info,
                                   nullptr, &dump_pipeline_layout_depth_) !=
        VK_SUCCESS) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the depth render target "
          "dumping pipeline layout");
      Shutdown();
      return false;
    }
  }
  // THE EDRAM SOLVE, hybrid form: also build the FSI render pass + framebuffer +
  // EDRAM-buffer resolve/clear pipelines when hybrid_postprocess is on, so the
  // 1x-coverage post-process composites can render PASS-LESS into the EDRAM buffer
  // while the main scene uses the host-RT infra above. Un-chained from the host-RT
  // branch (a separate if, not else-if) so BOTH run under hybrid; the FSI
  // resources are additive (FSI-specific handles, shared layouts already built).
  if (path_ == Path::kPixelShaderInterlock || hybrid_postprocess_) {
    // Pixel (fragment) shader interlock.

    // Piecewise linear gamma is 8-bit with programmable blending.
    gamma_render_target_as_unorm16_ = false;

    // Always true float24 depth rounded to the nearest even.
    depth_float24_round_ = true;

    // The pipeline layout and the pipelines for clearing the EDRAM buffer in
    // resolves.
    VkPushConstantRange resolve_fsi_clear_push_constant_range;
    resolve_fsi_clear_push_constant_range.stageFlags =
        VK_SHADER_STAGE_COMPUTE_BIT;
    resolve_fsi_clear_push_constant_range.offset = 0;
    resolve_fsi_clear_push_constant_range.size =
        sizeof(draw_util::ResolveClearShaderConstants);
    VkPipelineLayoutCreateInfo resolve_fsi_clear_pipeline_layout_create_info;
    resolve_fsi_clear_pipeline_layout_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    resolve_fsi_clear_pipeline_layout_create_info.pNext = nullptr;
    resolve_fsi_clear_pipeline_layout_create_info.flags = 0;
    resolve_fsi_clear_pipeline_layout_create_info.setLayoutCount = 1;
    resolve_fsi_clear_pipeline_layout_create_info.pSetLayouts =
        &descriptor_set_layout_storage_buffer_;
    resolve_fsi_clear_pipeline_layout_create_info.pushConstantRangeCount = 1;
    resolve_fsi_clear_pipeline_layout_create_info.pPushConstantRanges =
        &resolve_fsi_clear_push_constant_range;
    if (dfn.vkCreatePipelineLayout(
            device, &resolve_fsi_clear_pipeline_layout_create_info, nullptr,
            &resolve_fsi_clear_pipeline_layout_) != VK_SUCCESS) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the resolve EDRAM buffer "
          "clear pipeline layout");
      Shutdown();
      return false;
    }
    resolve_fsi_clear_32bpp_pipeline_ = ui::vulkan::util::CreateComputePipeline(
        vulkan_device, resolve_fsi_clear_pipeline_layout_,
        draw_resolution_scaled ? shaders::resolve_clear_32bpp_scaled_cs
                               : shaders::resolve_clear_32bpp_cs,
        draw_resolution_scaled ? sizeof(shaders::resolve_clear_32bpp_scaled_cs)
                               : sizeof(shaders::resolve_clear_32bpp_cs));
    if (resolve_fsi_clear_32bpp_pipeline_ == VK_NULL_HANDLE) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the 32bpp resolve EDRAM "
          "buffer clear pipeline");
      Shutdown();
      return false;
    }
    resolve_fsi_clear_64bpp_pipeline_ = ui::vulkan::util::CreateComputePipeline(
        vulkan_device, resolve_fsi_clear_pipeline_layout_,
        draw_resolution_scaled ? shaders::resolve_clear_64bpp_scaled_cs
                               : shaders::resolve_clear_64bpp_cs,
        draw_resolution_scaled ? sizeof(shaders::resolve_clear_64bpp_scaled_cs)
                               : sizeof(shaders::resolve_clear_64bpp_cs));
    if (resolve_fsi_clear_32bpp_pipeline_ == VK_NULL_HANDLE) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the 64bpp resolve EDRAM "
          "buffer clear pipeline");
      Shutdown();
      return false;
    }

    // Common render pass.
    VkSubpassDescription fsi_subpass = {};
    fsi_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    // Fragment shader interlock provides synchronization and ordering within a
    // subpass, create an external by-region dependency to maintain interlocking
    // between passes. Framebuffer-global dependencies will be made with
    // explicit barriers when the addressing of the EDRAM buffer relatively to
    // the fragment coordinates is changed.
    VkSubpassDependency fsi_subpass_dependencies[2];
    fsi_subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    fsi_subpass_dependencies[0].dstSubpass = 0;
    fsi_subpass_dependencies[0].srcStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    fsi_subpass_dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    fsi_subpass_dependencies[0].srcAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    fsi_subpass_dependencies[0].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    fsi_subpass_dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    fsi_subpass_dependencies[1] = fsi_subpass_dependencies[0];
    std::swap(fsi_subpass_dependencies[1].srcSubpass,
              fsi_subpass_dependencies[1].dstSubpass);
    VkRenderPassCreateInfo fsi_render_pass_create_info;
    fsi_render_pass_create_info.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    fsi_render_pass_create_info.pNext = nullptr;
    fsi_render_pass_create_info.flags = 0;
    fsi_render_pass_create_info.attachmentCount = 0;
    fsi_render_pass_create_info.pAttachments = nullptr;
    fsi_render_pass_create_info.subpassCount = 1;
    fsi_render_pass_create_info.pSubpasses = &fsi_subpass;
    fsi_render_pass_create_info.dependencyCount =
        uint32_t(xe::countof(fsi_subpass_dependencies));
    fsi_render_pass_create_info.pDependencies = fsi_subpass_dependencies;
    if (dfn.vkCreateRenderPass(device, &fsi_render_pass_create_info, nullptr,
                               &fsi_render_pass_) != VK_SUCCESS) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the fragment shader "
          "interlock render backend render pass");
      Shutdown();
      return false;
    }

    // Common framebuffer.
    VkFramebufferCreateInfo fsi_framebuffer_create_info;
    fsi_framebuffer_create_info.sType =
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fsi_framebuffer_create_info.pNext = nullptr;
    fsi_framebuffer_create_info.flags = 0;
    fsi_framebuffer_create_info.renderPass = fsi_render_pass_;
    fsi_framebuffer_create_info.attachmentCount = 0;
    fsi_framebuffer_create_info.pAttachments = nullptr;
    fsi_framebuffer_create_info.width = std::min(
        xenos::kTexture2DCubeMaxWidthHeight * draw_resolution_scale_x(),
        device_properties.maxFramebufferWidth);
    fsi_framebuffer_create_info.height = std::min(
        xenos::kTexture2DCubeMaxWidthHeight * draw_resolution_scale_y(),
        device_properties.maxFramebufferHeight);
    fsi_framebuffer_create_info.layers = 1;
    if (dfn.vkCreateFramebuffer(device, &fsi_framebuffer_create_info, nullptr,
                                &fsi_framebuffer_.framebuffer) != VK_SUCCESS) {
      XELOGE(
          "VulkanRenderTargetCache: Failed to create the fragment shader "
          "interlock render backend framebuffer");
      Shutdown();
      return false;
    }
    fsi_framebuffer_.host_extent.width = fsi_framebuffer_create_info.width;
    fsi_framebuffer_.host_extent.height = fsi_framebuffer_create_info.height;
  } else if (path_ != Path::kHostRenderTargets) {
    // The host-RT branch (path_ == kHostRenderTargets) is handled above at its
    // own `if` and is a valid terminal path even when the FSI/hybrid branch does
    // not run (hybrid_postprocess_ == false). Only a genuinely unhandled path
    // (neither host-RT nor PSI) reaches here. Regression guard: when the FSI
    // block was un-chained from the host-RT `if` into a standalone `if`, this
    // trailing `else` was left bound to the FSI branch and rejected the DEFAULT
    // host-RT + hybrid-off path via assert_unhandled_case (silent init failure).
    assert_unhandled_case(path_);
    Shutdown();
    return false;
  }

  // Reset the last update structures, to keep the defaults consistent between
  // paths regardless of whether the update for the path actually modifies them.
  last_update_render_pass_key_ = RenderPassKey();
  last_update_render_pass_ = VK_NULL_HANDLE;
  last_update_framebuffer_pitch_tiles_at_32bpp_ = 0;
  std::memset(last_update_framebuffer_attachments_, 0,
              sizeof(last_update_framebuffer_attachments_));
  last_update_framebuffer_ = VK_NULL_HANDLE;

  InitializeCommon();
  return true;
}

void VulkanRenderTargetCache::Shutdown(bool from_destructor) {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // Destroy all render targets before the descriptor set pool is destroyed -
  // may happen if shutting down the VulkanRenderTargetCache by destroying it,
  // so ShutdownCommon is called by the RenderTargetCache destructor, when it's
  // already too late.
  DestroyAllRenderTargets(true);

  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                         compute_postprocess_probe_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyPipelineLayout, device,
      compute_postprocess_probe_pipeline_layout_);

  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                         resolve_fsi_clear_64bpp_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                         resolve_fsi_clear_32bpp_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         resolve_fsi_clear_pipeline_layout_);

  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device,
                                         fsi_framebuffer_.framebuffer);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                                         fsi_render_pass_);

  for (const auto& dump_pipeline_pair : dump_pipelines_) {
    // May be null to prevent recreation attempts.
    if (dump_pipeline_pair.second != VK_NULL_HANDLE) {
      dfn.vkDestroyPipeline(device, dump_pipeline_pair.second, nullptr);
    }
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         dump_pipeline_layout_depth_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         dump_pipeline_layout_color_);

  for (const auto& transfer_pipeline_array_pair : transfer_pipelines_) {
    for (VkPipeline transfer_pipeline : transfer_pipeline_array_pair.second) {
      // May be null to prevent recreation attempts.
      if (transfer_pipeline != VK_NULL_HANDLE) {
        dfn.vkDestroyPipeline(device, transfer_pipeline, nullptr);
      }
    }
  }
  transfer_pipelines_.clear();
  for (const auto& transfer_shader_pair : transfer_shaders_) {
    if (transfer_shader_pair.second != VK_NULL_HANDLE) {
      dfn.vkDestroyShaderModule(device, transfer_shader_pair.second, nullptr);
    }
  }
  transfer_shaders_.clear();
  for (size_t i = 0; i < size_t(TransferPipelineLayoutIndex::kCount); ++i) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                           transfer_pipeline_layouts_[i]);
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyShaderModule, device,
                                         transfer_passthrough_vertex_shader_);
  transfer_vertex_buffer_pool_.reset();

  for (size_t i = 0; i < xe::countof(host_depth_store_pipelines_); ++i) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                           host_depth_store_pipelines_[i]);
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         host_depth_store_pipeline_layout_);

  last_update_framebuffer_ = VK_NULL_HANDLE;
  for (const auto& framebuffer_pair : framebuffers_) {
    const Framebuffer& fb = framebuffer_pair.second;
    dfn.vkDestroyFramebuffer(device, fb.framebuffer, nullptr);
    // FDM density map (null when gpu_fdm_foliage is off).
    if (fb.fdm_image != VK_NULL_HANDLE) {
      dfn.vkDestroyImageView(device, fb.fdm_view, nullptr);
      dfn.vkDestroyImage(device, fb.fdm_image, nullptr);
      dfn.vkFreeMemory(device, fb.fdm_memory, nullptr);
    }
    // LEVEL 4 native color producer (null unless gpu_bd_native_color_lifetime_hle>=4).
    if (fb.bd_native_color_image != VK_NULL_HANDLE) {
      dfn.vkDestroyFramebuffer(device, fb.bd_native_color_framebuffer, nullptr);
      dfn.vkDestroyImageView(device, fb.bd_native_color_view, nullptr);
      for (const auto& sv : fb.bd_native_color_swizzled_views_) {
        dfn.vkDestroyImageView(device, sv.second, nullptr);
      }
      dfn.vkDestroyImage(device, fb.bd_native_color_image, nullptr);
      dfn.vkFreeMemory(device, fb.bd_native_color_memory, nullptr);
      // DIRECT-NATIVE on-tile resolve target (null unless keep_scissor + MSAA).
      if (fb.bd_native_color_resolve_image != VK_NULL_HANDLE) {
        dfn.vkDestroyImageView(device, fb.bd_native_color_resolve_view, nullptr);
        dfn.vkDestroyImage(device, fb.bd_native_color_resolve_image, nullptr);
        dfn.vkFreeMemory(device, fb.bd_native_color_resolve_memory, nullptr);
      }
    }
    // PATH A STAGE 1 native depth snapshot (null unless gpu_bd_patha_depth_snapshot).
    if (fb.bd_native_depth_image != VK_NULL_HANDLE) {
      dfn.vkDestroyFramebuffer(device, fb.bd_native_depth_framebuffer, nullptr);
      dfn.vkDestroyImageView(device, fb.bd_native_depth_view, nullptr);
      dfn.vkDestroyImage(device, fb.bd_native_depth_image, nullptr);
      dfn.vkFreeMemory(device, fb.bd_native_depth_memory, nullptr);
    }
  }
  framebuffers_.clear();
  for (auto& rt : bd_native_resolved_) {
    for (auto& sv : rt.second.swizzled_views) {
      dfn.vkDestroyImageView(device, sv.second, nullptr);
    }
    if (rt.second.identity_view != VK_NULL_HANDLE) {
      dfn.vkDestroyImageView(device, rt.second.identity_view, nullptr);
    }
    if (rt.second.image != VK_NULL_HANDLE) {
      dfn.vkDestroyImage(device, rt.second.image, nullptr);
      dfn.vkFreeMemory(device, rt.second.memory, nullptr);
    }
  }
  bd_native_resolved_.clear();

  last_update_render_pass_ = VK_NULL_HANDLE;
  for (const auto& render_pass_pair : render_passes_) {
    if (render_pass_pair.second != VK_NULL_HANDLE) {
      dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
    }
  }
  render_passes_.clear();
  for (const auto& render_pass_pair : load_dont_care_render_passes_) {
    if (render_pass_pair.second != VK_NULL_HANDLE) {
      dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
    }
  }
  load_dont_care_render_passes_.clear();
  for (const auto& render_pass_pair : feedback_render_passes_) {
    if (render_pass_pair.second != VK_NULL_HANDLE) {
      dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
    }
  }
  feedback_render_passes_.clear();
  for (const auto& render_pass_pair : bd_custom_resolve_render_passes_) {
    if (render_pass_pair.second != VK_NULL_HANDLE) {
      dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
    }
  }
  bd_custom_resolve_render_passes_.clear();
  for (const FeedbackFramebuffer& fb : feedback_framebuffers_) {
    if (fb.framebuffer != VK_NULL_HANDLE) {
      dfn.vkDestroyFramebuffer(device, fb.framebuffer, nullptr);
    }
  }
  feedback_framebuffers_.clear();

  for (VkPipeline& resolve_copy_pipeline : resolve_copy_pipelines_) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                           resolve_copy_pipeline);
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         resolve_copy_pipeline_layout_);

  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device,
                                         edram_storage_buffer_descriptor_pool_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                         edram_buffer_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                         edram_buffer_memory_);

  descriptor_set_pool_sampled_image_x2_.reset();
  descriptor_set_pool_sampled_image_.reset();

  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      descriptor_set_layout_sampled_image_x2_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout,
                                         device,
                                         descriptor_set_layout_sampled_image_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout,
                                         device,
                                         descriptor_set_layout_storage_buffer_);

  if (!from_destructor) {
    ShutdownCommon();
  }
}

void VulkanRenderTargetCache::ClearCache() {
  // Never destroy render targets retained by a deferred framegraph edge. Replay
  // while the source/destination and their descriptor sets are still live.
  if (!bd_framegraph_deferred_depth_transfers_.empty()) {
    FallbackBdFramegraphDepthTransfer("cache clear before consumer");
  }
  // Shadow entries retain identity pointers only for observation. Expire them
  // before the cache destroys those render targets so a later pass-enter can
  // never inspect a stale pointer.
  for (const BdFramegraphShadowDepthTransfer& entry :
       bd_framegraph_shadow_depth_transfers_) {
    if (!entry.matched) {
      ++bd_framegraph_shadow_unmatched_;
      XELOGI(
          "BD SHADOW: expiry unmatched frame={} scheduled_frame={} entry={} "
          "first_use_nonconsumer={} reason=cache-clear",
          command_processor_.GetCurrentFrame(), entry.schedule_frame, entry.id,
          entry.first_dest_use_seen ? 1 : 0);
    }
  }
  bd_framegraph_shadow_depth_transfers_.clear();
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // Framebuffer objects must be destroyed because they reference views of
  // attachment images, which may be removed by the common ClearCache.
  last_update_framebuffer_ = VK_NULL_HANDLE;
  for (const auto& framebuffer_pair : framebuffers_) {
    const Framebuffer& fb = framebuffer_pair.second;
    dfn.vkDestroyFramebuffer(device, fb.framebuffer, nullptr);
    // FDM density map (null when gpu_fdm_foliage is off).
    if (fb.fdm_image != VK_NULL_HANDLE) {
      dfn.vkDestroyImageView(device, fb.fdm_view, nullptr);
      dfn.vkDestroyImage(device, fb.fdm_image, nullptr);
      dfn.vkFreeMemory(device, fb.fdm_memory, nullptr);
    }
    // LEVEL 4 native color producer (null unless gpu_bd_native_color_lifetime_hle>=4).
    if (fb.bd_native_color_image != VK_NULL_HANDLE) {
      dfn.vkDestroyFramebuffer(device, fb.bd_native_color_framebuffer, nullptr);
      dfn.vkDestroyImageView(device, fb.bd_native_color_view, nullptr);
      for (const auto& sv : fb.bd_native_color_swizzled_views_) {
        dfn.vkDestroyImageView(device, sv.second, nullptr);
      }
      dfn.vkDestroyImage(device, fb.bd_native_color_image, nullptr);
      dfn.vkFreeMemory(device, fb.bd_native_color_memory, nullptr);
      // DIRECT-NATIVE on-tile resolve target (null unless keep_scissor + MSAA).
      if (fb.bd_native_color_resolve_image != VK_NULL_HANDLE) {
        dfn.vkDestroyImageView(device, fb.bd_native_color_resolve_view, nullptr);
        dfn.vkDestroyImage(device, fb.bd_native_color_resolve_image, nullptr);
        dfn.vkFreeMemory(device, fb.bd_native_color_resolve_memory, nullptr);
      }
    }
    // PATH A STAGE 1 native depth snapshot (null unless gpu_bd_patha_depth_snapshot).
    if (fb.bd_native_depth_image != VK_NULL_HANDLE) {
      dfn.vkDestroyFramebuffer(device, fb.bd_native_depth_framebuffer, nullptr);
      dfn.vkDestroyImageView(device, fb.bd_native_depth_view, nullptr);
      dfn.vkDestroyImage(device, fb.bd_native_depth_image, nullptr);
      dfn.vkFreeMemory(device, fb.bd_native_depth_memory, nullptr);
    }
  }
  framebuffers_.clear();
  for (auto& rt : bd_native_resolved_) {
    for (auto& sv : rt.second.swizzled_views) {
      dfn.vkDestroyImageView(device, sv.second, nullptr);
    }
    if (rt.second.identity_view != VK_NULL_HANDLE) {
      dfn.vkDestroyImageView(device, rt.second.identity_view, nullptr);
    }
    if (rt.second.image != VK_NULL_HANDLE) {
      dfn.vkDestroyImage(device, rt.second.image, nullptr);
      dfn.vkFreeMemory(device, rt.second.memory, nullptr);
    }
  }
  bd_native_resolved_.clear();

  last_update_render_pass_ = VK_NULL_HANDLE;
  for (const auto& render_pass_pair : render_passes_) {
    dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
  }
  render_passes_.clear();
  for (const auto& render_pass_pair : load_dont_care_render_passes_) {
    if (render_pass_pair.second != VK_NULL_HANDLE) {
      dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
    }
  }
  load_dont_care_render_passes_.clear();
  for (const auto& render_pass_pair : feedback_render_passes_) {
    if (render_pass_pair.second != VK_NULL_HANDLE) {
      dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
    }
  }
  feedback_render_passes_.clear();
  for (const auto& render_pass_pair : bd_custom_resolve_render_passes_) {
    if (render_pass_pair.second != VK_NULL_HANDLE) {
      dfn.vkDestroyRenderPass(device, render_pass_pair.second, nullptr);
    }
  }
  bd_custom_resolve_render_passes_.clear();
  for (const FeedbackFramebuffer& fb : feedback_framebuffers_) {
    if (fb.framebuffer != VK_NULL_HANDLE) {
      dfn.vkDestroyFramebuffer(device, fb.framebuffer, nullptr);
    }
  }
  feedback_framebuffers_.clear();

  RenderTargetCache::ClearCache();
}

void VulkanRenderTargetCache::CompletedSubmissionUpdated() {
  if (transfer_vertex_buffer_pool_) {
    transfer_vertex_buffer_pool_->Reclaim(
        command_processor_.GetCompletedSubmission());
  }
}

void VulkanRenderTargetCache::EndSubmission() {
  if (!bd_framegraph_deferred_depth_transfers_.empty()) {
    FallbackBdFramegraphDepthTransfer("submission ended before consumer");
  }
  if (transfer_vertex_buffer_pool_) {
    transfer_vertex_buffer_pool_->FlushWrites();
  }
}

void VulkanRenderTargetCache::RunComputePostProcessProbe() {
  if (!cvars::gpu_vulkan_compute_postprocess_probe ||
      compute_postprocess_probe_pipeline_ == VK_NULL_HANDLE) {
    return;
  }
  // Compute-post-process FOUNDATION probe (brick 1). Dispatch an IDENTITY
  // compute shader over the EDRAM buffer: it reads then writes back the SAME
  // dwords, so EDRAM contents are byte-unchanged and no rendered result can
  // change. The point is only to prove that a mid-frame compute dispatch +
  // barrier over the EDRAM SSBO records, submits, and stays synchronized
  // against live host-RT rendering on Turnip - the unproven foundation the
  // composite-compute fusion is built on. Must be called outside a render pass.
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();
  // Transition the EDRAM buffer to compute write and flush the pending barrier
  // (UseEdramBuffer queues it; SubmitBarriers records it before the dispatch).
  // The next real EDRAM user transitions out of kComputeWrite, providing the
  // barrier that makes the identity writes visible. Deliberately does NOT call
  // MarkEdramBufferModified - the contents are identical, so the buffer<->RT
  // ownership model must keep saying "unmodified".
  UseEdramBuffer(EdramBufferUsage::kComputeWrite);
  command_buffer.CmdVkBindDescriptorSets(
      VK_PIPELINE_BIND_POINT_COMPUTE,
      compute_postprocess_probe_pipeline_layout_, 0, 1,
      &edram_storage_buffer_descriptor_set_, 0, nullptr);
  command_processor_.BindExternalComputePipeline(
      compute_postprocess_probe_pipeline_);
  // 8x8 workgroup; the shader early-returns for indices >= count. One group =>
  // 64 identity dwords from EDRAM dword offset 0. Bounded and harmless.
  uint32_t probe_push_constants[2] = {0u /* offset_dwords */,
                                      64u /* count_dwords */};
  command_buffer.CmdVkPushConstants(
      compute_postprocess_probe_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
      0, sizeof(probe_push_constants), probe_push_constants);
  command_processor_.SubmitBarriers(true);
  command_buffer.CmdVkDispatch(1, 1, 1);
  // Prove engagement (that the cvar reached C++ and the dispatch was actually
  // recorded, not silently early-returned) - log the first dispatch and then a
  // heartbeat, so the device log can confirm the probe ran without spamming.
  ++compute_postprocess_probe_dispatch_count_;
  if (compute_postprocess_probe_dispatch_count_ == 1 ||
      (compute_postprocess_probe_dispatch_count_ % 600) == 0) {
    XELOGI(
        "compute-post-process probe: recorded identity EDRAM dispatch #{} "
        "(host-RT path, offset_dwords=0 count_dwords=64)",
        compute_postprocess_probe_dispatch_count_);
  }
}

bool VulkanRenderTargetCache::Resolve(const Memory& memory,
                                      VulkanSharedMemory& shared_memory,
                                      VulkanTextureCache& texture_cache,
                                      uint32_t& written_address_out,
                                      uint32_t& written_length_out) {
  written_address_out = 0;
  written_length_out = 0;

  bool draw_resolution_scaled = IsDrawResolutionScaled();

  draw_util::ResolveInfo resolve_info;
  if (!draw_util::GetResolveInfo(
          register_file(), memory, trace_writer_, draw_resolution_scale_x(),
          draw_resolution_scale_y(), IsFixedRG16TruncatedToMinus1To1(),
          IsFixedRGBA16TruncatedToMinus1To1(), resolve_info)) {
    return false;
  }

  // Nothing to copy/clear.
  if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {
    return true;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();

  // Blue Dragon native-draw HLE step 3 (gpu_bd_hle_drop_resolve): BD's field
  // frame spends ~120ms in ONE GPU fence = the emulated per-tile base-0
  // EDRAM->RAM resolve copy. When the decoupled full-surface RT is the source of
  // truth (gpu_bd_hle_present_decoupled), that copy is dead weight - skip its GPU
  // work (DumpRenderTargets + the per-pixel copy dispatch). Only the COLOR copy
  // is dropped; depth resolves and clears still run. 1 = base-0 only (BD's
  // field); 2 = ALL color copies (upper-bound perf probe). On its own (no
  // decoupled present) it measures the resolve's raw GPU cost against baseline.
  bool bd_drop_this_resolve = false;
  if (cvars::gpu_bd_hle_drop_resolve && resolve_info.copy_dest_extent_length &&
      !resolve_info.IsCopyingDepth()) {
    if (cvars::gpu_bd_hle_drop_resolve >= 2 ||
        resolve_info.color_edram_info.base_tiles == 0) {
      bd_drop_this_resolve = true;
      static uint32_t s_bd_drop_log = 0;
      if (s_bd_drop_log < 8) {
        ++s_bd_drop_log;
        XELOGI(
            "BD HLE drop-resolve: SKIPPED color resolve copy src_base_tiles={} "
            "dest_base={:08X} coord={}x{} mode={}",
            resolve_info.color_edram_info.base_tiles,
            resolve_info.copy_dest_base,
            resolve_info.coordinate_info.width_div_8
                << xenos::kResolveAlignmentPixelsLog2,
            resolve_info.height_div_8 << xenos::kResolveAlignmentPixelsLog2,
            uint32_t(cvars::gpu_bd_hle_drop_resolve));
      }
    }
  }
  // REAL-HLE EDRAM deletion (gpu_bd_native_aux_rt): SURGICALLY drop a color resolve
  // whose dest guest address is now backed by a LIVE native surface (the field
  // samples that native image via Brick B, so the EDRAM->RAM resolve is dead weight).
  // Unlike the blunt drop-all-base-0 above (which black-screened with NO native
  // replacement), this fires ONLY when native content actually serves the dest =
  // safe. This is what makes the ~110ms EDRAM work redundant.
  if (!bd_drop_this_resolve && cvars::gpu_bd_native_drop_resolves &&
      resolve_info.copy_dest_extent_length && !resolve_info.IsCopyingDepth() &&
      command_processor_.BdNativeSurfaceServes(resolve_info.copy_dest_base)) {
    bd_drop_this_resolve = true;
    command_processor_.AddBdNativeResolveDropped();
    static uint32_t s_bd_native_drop_log = 0;
    if (s_bd_native_drop_log < 8) {
      ++s_bd_native_drop_log;
      XELOGI(
          "BD REAL-HLE: dropped EDRAM resolve dest={:08X} (native surface serves "
          "it) — EDRAM work deleted for this surface",
          resolve_info.copy_dest_base);
    }
  }

  // Copying.
  bool copied = false;
  if (resolve_info.copy_dest_extent_length && !bd_drop_this_resolve) {
    // EDRAM-recompiler first brick: record the resolve->sample dependency EDGE -
    // the dest range PLUS the source RT identity - so a later texture fetch that
    // lands in this dest range can be routed to the resident source RT directly
    // (RT-as-texture) or have this resolve deferred (lazy), instead of the
    // EDRAM->RAM->reload round-trip the increment-1 detector only measured.
    const bool resolve_is_depth = resolve_info.IsCopyingDepth();
    const draw_util::ResolveEdramInfo& src_edram =
        resolve_is_depth ? resolve_info.depth_edram_info
                         : resolve_info.color_edram_info;
    VulkanCommandProcessor::ResolveEdge resolve_edge;
    resolve_edge.dest_start = resolve_info.copy_dest_extent_start;
    resolve_edge.dest_length = resolve_info.copy_dest_extent_length;
    resolve_edge.dest_base = resolve_info.copy_dest_base;
    resolve_edge.src_edram_base_tiles = src_edram.base_tiles;
    resolve_edge.src_pitch_tiles = src_edram.pitch_tiles;
    resolve_edge.src_format = src_edram.format;
    resolve_edge.src_msaa = uint8_t(src_edram.msaa_samples);
    resolve_edge.src_is_depth = resolve_is_depth;
    resolve_edge.dest_texture_format =
        uint8_t(resolve_info.copy_dest_info.copy_dest_format);
    // Pack the FULL source-RT identity (same RenderTargetKey the render draw's
    // color RT carries) so the native render-redirect can key by it — BD renders
    // every RT at EDRAM base 0, so base alone aliases; pitch+format+msaa
    // disambiguate. BD RTs are 32bpp, so pitch_tiles == pitch_tiles_at_32bpp.
    RenderTargetKey src_rt_key;
    src_rt_key.base_tiles = src_edram.base_tiles;
    src_rt_key.pitch_tiles_at_32bpp = src_edram.pitch_tiles;
    src_rt_key.msaa_samples = src_edram.msaa_samples;
    src_rt_key.is_depth = resolve_is_depth ? 1 : 0;
    src_rt_key.resource_format = src_edram.format;
    resolve_edge.src_rt_key = src_rt_key.key;
    command_processor_.AddResolveCopyStats(resolve_edge);
    // THE EDRAM SOLVE, hybrid form: while the post-process phase is active, EDRAM is
    // buffer-authoritative (BeginHybridPostprocessPhase dumped the main scene +
    // cleared ownership), so SKIP the host-RT dump - the resolve-copy reads
    // edram_buffer_ (the composite's FSI output + the bridged main scene). This
    // renders BD CORRECTLY (device-proven). The per-range no-gate variant GARBLED
    // BD - composites read the EVOLVING main scene which interleaved main-scene
    // draws re-own host-side, so it must stay bridged in edram_buffer_. Global gate.
    if (GetPath() == Path::kHostRenderTargets &&
        !hybrid_postprocess_phase_active_) {
      // Dump the current contents of the render targets owning the affected
      // range to edram_buffer_.
      // TODO(Triang3l): Direct host render target -> shared memory resolve
      // shaders for non-converting cases.
      uint32_t dump_base;
      uint32_t dump_row_length_used;
      uint32_t dump_rows;
      uint32_t dump_pitch;
      resolve_info.GetCopyEdramTileSpan(dump_base, dump_row_length_used,
                                        dump_rows, dump_pitch);
      DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch);
      if (ShouldTraceVulkanEdramChecksum() &&
          !ReadbackEdramBufferRange(dump_base, dump_row_length_used, dump_rows,
                                    dump_pitch, resolve_info)) {
        return false;
      }
    }

    draw_util::ResolveCopyShaderConstants copy_shader_constants;
    uint32_t copy_group_count_x, copy_group_count_y;
    draw_util::ResolveCopyShaderIndex copy_shader = resolve_info.GetCopyShader(
        draw_resolution_scale_x(), draw_resolution_scale_y(),
        copy_shader_constants, copy_group_count_x, copy_group_count_y);
    assert_true(copy_group_count_x && copy_group_count_y);
    if (ShouldTraceVulkanResolve()) {
      XELOGI(
          "GPU resolve trace: vulkan copy dest_base={:08X} "
          "extent_start={:08X} extent_length={:08X} raw_rb_dest={:08X} "
          "coord={}x{} scaled={} shader={} groups={}x{} dest_format={} "
          "dest_pitch={} dest_height={} clear_color={} clear_depth={}",
          resolve_info.copy_dest_base, resolve_info.copy_dest_extent_start,
          resolve_info.copy_dest_extent_length,
          register_file()[XE_GPU_REG_RB_COPY_DEST_BASE],
          resolve_info.coordinate_info.width_div_8
              << xenos::kResolveAlignmentPixelsLog2,
          resolve_info.height_div_8 << xenos::kResolveAlignmentPixelsLog2,
          draw_resolution_scaled, ResolveCopyShaderName(copy_shader),
          copy_group_count_x, copy_group_count_y,
          uint32_t(resolve_info.copy_dest_info.copy_dest_format),
          uint32_t(register_file().Get<reg::RB_COPY_DEST_PITCH>()
                       .copy_dest_pitch),
          uint32_t(register_file().Get<reg::RB_COPY_DEST_PITCH>()
                       .copy_dest_height),
          resolve_info.IsClearingColor(), resolve_info.IsClearingDepth());
    }
    if (copy_shader != draw_util::ResolveCopyShaderIndex::kUnknown) {
      const draw_util::ResolveCopyShaderInfo& copy_shader_info =
          draw_util::resolve_copy_shader_info[size_t(copy_shader)];

      // Make sure there is memory to write to.
      bool copy_dest_committed;
      // TODO(Triang3l): Resolution-scaled buffer committing.
      copy_dest_committed =
          shared_memory.RequestRange(resolve_info.copy_dest_extent_start,
                                     resolve_info.copy_dest_extent_length);
      if (!copy_dest_committed) {
        XELOGE(
            "VulkanRenderTargetCache: Failed to obtain the resolve destination "
            "memory region");
      } else {
        // TODO(Triang3l): Switching between descriptors if exceeding
        // maxStorageBufferRange.
        // TODO(Triang3l): Use a single 512 MB shared memory binding if
        // possible.
        VkDescriptorSet descriptor_set_dest =
            command_processor_.AllocateSingleTransientDescriptor(
                VulkanCommandProcessor::SingleTransientDescriptorLayout ::
                    kStorageBufferCompute);
        if (descriptor_set_dest != VK_NULL_HANDLE) {
          // Write the destination descriptor.
          // TODO(Triang3l): Scaled resolve buffer binding.
          VkDescriptorBufferInfo write_descriptor_set_dest_buffer_info;
          write_descriptor_set_dest_buffer_info.buffer = shared_memory.buffer();
          write_descriptor_set_dest_buffer_info.offset =
              resolve_info.copy_dest_base;
          write_descriptor_set_dest_buffer_info.range =
              resolve_info.copy_dest_extent_start -
              resolve_info.copy_dest_base +
              resolve_info.copy_dest_extent_length;
          VkWriteDescriptorSet write_descriptor_set_dest;
          write_descriptor_set_dest.sType =
              VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
          write_descriptor_set_dest.pNext = nullptr;
          write_descriptor_set_dest.dstSet = descriptor_set_dest;
          write_descriptor_set_dest.dstBinding = 0;
          write_descriptor_set_dest.dstArrayElement = 0;
          write_descriptor_set_dest.descriptorCount = 1;
          write_descriptor_set_dest.descriptorType =
              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
          write_descriptor_set_dest.pImageInfo = nullptr;
          write_descriptor_set_dest.pBufferInfo =
              &write_descriptor_set_dest_buffer_info;
          write_descriptor_set_dest.pTexelBufferView = nullptr;
          dfn.vkUpdateDescriptorSets(device, 1, &write_descriptor_set_dest, 0,
                                     nullptr);

          // Submit the resolve.
          // TODO(Triang3l): Transition the scaled resolve buffer.
          shared_memory.Use(VulkanSharedMemory::Usage::kComputeWrite,
                            std::pair<uint32_t, uint32_t>(
                                resolve_info.copy_dest_extent_start,
                                resolve_info.copy_dest_extent_length));
          UseEdramBuffer(EdramBufferUsage::kComputeRead);
          command_processor_.BindExternalComputePipeline(
              resolve_copy_pipelines_[size_t(copy_shader)]);
          VkDescriptorSet descriptor_sets[kResolveCopyDescriptorSetCount] = {};
          descriptor_sets[kResolveCopyDescriptorSetEdram] =
              edram_storage_buffer_descriptor_set_;
          descriptor_sets[kResolveCopyDescriptorSetDest] = descriptor_set_dest;
          command_buffer.CmdVkBindDescriptorSets(
              VK_PIPELINE_BIND_POINT_COMPUTE, resolve_copy_pipeline_layout_, 0,
              uint32_t(xe::countof(descriptor_sets)), descriptor_sets, 0,
              nullptr);
          if (draw_resolution_scaled) {
            command_buffer.CmdVkPushConstants(
                resolve_copy_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                sizeof(copy_shader_constants.dest_relative),
                &copy_shader_constants.dest_relative);
          } else {
            // TODO(Triang3l): Proper dest_base in case of one 512 MB shared
            // memory binding, or multiple shared memory bindings in case of
            // splitting due to maxStorageBufferRange overflow.
            copy_shader_constants.dest_base -=
                uint32_t(write_descriptor_set_dest_buffer_info.offset);
            command_buffer.CmdVkPushConstants(
                resolve_copy_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                sizeof(copy_shader_constants), &copy_shader_constants);
          }
          command_processor_.SubmitBarriers(true);
          command_processor_.RecordResolveTimingBracket(
              true, VulkanCommandProcessor::GpuPassKind::kResolveCopyDispatch);
          command_buffer.CmdVkDispatch(copy_group_count_x, copy_group_count_y,
                                       1);
          command_processor_.RecordResolveTimingBracket(
              false, VulkanCommandProcessor::GpuPassKind::kResolveCopyDispatch);

          // Invalidate textures and mark the range as scaled if needed.
          texture_cache.MarkRangeAsResolved(
              resolve_info.copy_dest_extent_start,
              resolve_info.copy_dest_extent_length);
          written_address_out = resolve_info.copy_dest_extent_start;
          written_length_out = resolve_info.copy_dest_extent_length;
          copied = true;
        }
      }
    }
  } else {
    copied = true;
  }

  // Clearing.
  bool cleared = false;
  bool clear_depth = resolve_info.IsClearingDepth();
  bool clear_color = resolve_info.IsClearingColor();
  if (clear_depth || clear_color) {
    switch (GetPath()) {
      case Path::kHostRenderTargets: {
        Transfer::Rectangle clear_rectangle;
        RenderTarget* clear_render_targets[2];
        // If PrepareHostRenderTargetsResolveClear returns false, may be just an
        // empty region (success) or an error - don't care.
        if (PrepareHostRenderTargetsResolveClear(
                resolve_info, clear_rectangle, clear_render_targets[0],
                clear_transfers_[0], clear_render_targets[1],
                clear_transfers_[1])) {
          uint64_t clear_values[2];
          clear_values[0] = resolve_info.rb_depth_clear;
          clear_values[1] = resolve_info.rb_color_clear |
                            (uint64_t(resolve_info.rb_color_clear_lo) << 32);
          PerformTransfersAndResolveClears(2, clear_render_targets,
                                           clear_transfers_, clear_values,
                                           &clear_rectangle);
        }
        cleared = true;
      } break;
      case Path::kPixelShaderInterlock: {
        UseEdramBuffer(EdramBufferUsage::kComputeWrite);
        // Should be safe to only commit once (if was accessed as unordered or
        // with fragment shader interlock previously - if there was nothing to
        // copy, only to clear, for some reason, for instance), overlap of the
        // depth and the color ranges is highly unlikely.
        CommitEdramBufferShaderWrites();
        command_buffer.CmdVkBindDescriptorSets(
            VK_PIPELINE_BIND_POINT_COMPUTE, resolve_fsi_clear_pipeline_layout_,
            0, 1, &edram_storage_buffer_descriptor_set_, 0, nullptr);
        std::pair<uint32_t, uint32_t> clear_group_count =
            resolve_info.GetClearShaderGroupCount(draw_resolution_scale_x(),
                                                  draw_resolution_scale_y());
        assert_true(clear_group_count.first && clear_group_count.second);
        if (clear_depth) {
          command_processor_.BindExternalComputePipeline(
              resolve_fsi_clear_32bpp_pipeline_);
          draw_util::ResolveClearShaderConstants depth_clear_constants;
          resolve_info.GetDepthClearShaderConstants(depth_clear_constants);
          command_buffer.CmdVkPushConstants(
              resolve_fsi_clear_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
              0, sizeof(depth_clear_constants), &depth_clear_constants);
          command_processor_.SubmitBarriers(true);
          command_processor_.RecordResolveTimingBracket(
              true, VulkanCommandProcessor::GpuPassKind::kResolveClearDispatch);
          command_buffer.CmdVkDispatch(clear_group_count.first,
                                       clear_group_count.second, 1);
          command_processor_.RecordResolveTimingBracket(
              false, VulkanCommandProcessor::GpuPassKind::kResolveClearDispatch);
        }
        if (clear_color) {
          command_processor_.BindExternalComputePipeline(
              resolve_info.color_edram_info.format_is_64bpp
                  ? resolve_fsi_clear_64bpp_pipeline_
                  : resolve_fsi_clear_32bpp_pipeline_);
          draw_util::ResolveClearShaderConstants color_clear_constants;
          resolve_info.GetColorClearShaderConstants(color_clear_constants);
          if (clear_depth) {
            // Non-RT-specific constants have already been set.
            command_buffer.CmdVkPushConstants(
                resolve_fsi_clear_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                uint32_t(offsetof(draw_util::ResolveClearShaderConstants,
                                  rt_specific)),
                sizeof(color_clear_constants.rt_specific),
                &color_clear_constants.rt_specific);
          } else {
            command_buffer.CmdVkPushConstants(
                resolve_fsi_clear_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
                0, sizeof(color_clear_constants), &color_clear_constants);
          }
          command_processor_.SubmitBarriers(true);
          command_processor_.RecordResolveTimingBracket(
              true, VulkanCommandProcessor::GpuPassKind::kResolveClearDispatch);
          command_buffer.CmdVkDispatch(clear_group_count.first,
                                       clear_group_count.second, 1);
          command_processor_.RecordResolveTimingBracket(
              false, VulkanCommandProcessor::GpuPassKind::kResolveClearDispatch);
        }
        MarkEdramBufferModified();
        cleared = true;
      } break;
      default:
        assert_unhandled_case(GetPath());
    }
  } else {
    cleared = true;
  }

  // Compute-post-process foundation probe: exercise a mid-frame identity compute
  // dispatch over the EDRAM buffer on the host-RT path (no-op unless the cvar is
  // on). Resolve() runs outside a render pass, the required precondition. The
  // FSI path already drives real EDRAM compute, so restrict the probe to the
  // host-RT path we're validating.
  if (GetPath() == Path::kHostRenderTargets) {
    RunComputePostProcessProbe();
  }

  return copied && cleared;
}

bool VulkanRenderTargetCache::Update(
    bool is_rasterization_done, reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask, const Shader& vertex_shader) {
  if (!RenderTargetCache::Update(is_rasterization_done,
                                 normalized_depth_control,
                                 normalized_color_mask, vertex_shader)) {
    return false;
  }

  auto rb_surface_info = register_file().Get<reg::RB_SURFACE_INFO>();

  RenderPassKey render_pass_key;
  // Needed even with the fragment shader interlock render backend for passing
  // the sample count to the pipeline cache.
  // BD-30 foliage ROP lever: clamp the MSAA consistently with the RT-cache key and
  // the resolve path (gpu_force_max_msaa_samples) so the render pass + pipeline
  // sample count match the (lowered) host RT image.
  render_pass_key.msaa_samples =
      draw_util::ClampForcedMsaaSamples(rb_surface_info.msaa_samples);

  switch (GetPath()) {
    case Path::kHostRenderTargets: {
      // THE EDRAM SOLVE, hybrid form: a main-scene draw resets the post-process
      // phase, so the NEXT composite re-bridges (DumpRenderTargets) - this captures
      // the main-scene updates that interleave with BD's composites (without the
      // re-bridge the composites read the stale phase-start main scene = garble,
      // device-proven). Costs a re-bridge per interleaved cluster (brk_img_sr ~21
      // not the ~16 ideal) but renders CORRECTLY. Per-frame IssueSwap reset backs
      // this up at frame boundaries. Reducing the re-bridge cost (targeted dump
      // instead of full-EDRAM) is the next optimization - NOT removing this reset.
      hybrid_postprocess_phase_active_ = false;
      RenderTarget* const* depth_and_color_render_targets =
          last_update_accumulated_render_targets();

      // Building the guest render pass key is pure - safe to do before the
      // transfers (needed up-front for the in-pass transfer mode).
      if (depth_and_color_render_targets[0]) {
        render_pass_key.depth_and_color_used |= 1 << 0;
        render_pass_key.depth_format =
            depth_and_color_render_targets[0]->key().GetDepthFormat();
      }
      if (depth_and_color_render_targets[1]) {
        render_pass_key.depth_and_color_used |= 1 << 1;
        render_pass_key.color_0_view_format =
            depth_and_color_render_targets[1]->key().GetColorFormat();
      }
      if (depth_and_color_render_targets[2]) {
        render_pass_key.depth_and_color_used |= 1 << 2;
        render_pass_key.color_1_view_format =
            depth_and_color_render_targets[2]->key().GetColorFormat();
      }
      if (depth_and_color_render_targets[3]) {
        render_pass_key.depth_and_color_used |= 1 << 3;
        render_pass_key.color_2_view_format =
            depth_and_color_render_targets[3]->key().GetColorFormat();
      }
      if (depth_and_color_render_targets[4]) {
        render_pass_key.depth_and_color_used |= 1 << 4;
        render_pass_key.color_3_view_format =
            depth_and_color_render_targets[4]->key().GetColorFormat();
      }

      // FDM: mark this GUEST-GEOMETRY pass for a fragment density map (cuts the
      // raster/depth/shade fragment count over the viewport). Set ONLY here, not
      // on the EDRAM-transfer render_pass_key, so transfer copies stay pixel-exact;
      // propagates via last_update_render_pass_key_ to the pipeline so the pipeline
      // render pass == the draw render pass (compatible).
      render_pass_key.use_fdm =
          cvars::gpu_fdm_foliage != 0 &&
          command_processor_.GetVulkanDevice()
              ->extensions()
              .ext_EXT_fragment_density_map;

      uint32_t pitch_tiles_at_32bpp =
          ((rb_surface_info.surface_pitch
            << uint32_t(draw_util::ClampForcedMsaaSamples(
                            rb_surface_info.msaa_samples) >=
                        xenos::MsaaSamples::k4X)) +
           (xenos::kEdramTileWidthSamples - 1)) /
          xenos::kEdramTileWidthSamples;

      // Resolves the guest render pass + framebuffer (cached when unchanged
      // since the last update). Pure cache lookups/creation - records no
      // commands, so its ordering relative to the transfers only matters for
      // making the objects available to the in-pass transfer mode.
      const Framebuffer* framebuffer = nullptr;
      VkRenderPass render_pass = VK_NULL_HANDLE;
      auto lookup_guest_pass_objects = [&]() -> bool {
        framebuffer = last_update_framebuffer_;
        render_pass = last_update_render_pass_key_ == render_pass_key
                          ? last_update_render_pass_
                          : VK_NULL_HANDLE;
        if (render_pass == VK_NULL_HANDLE) {
          render_pass = GetHostRenderTargetsRenderPass(render_pass_key);
          if (render_pass == VK_NULL_HANDLE) {
            return false;
          }
          // Framebuffer for a different render pass needed now.
          framebuffer = nullptr;
        }
        if (framebuffer) {
          if (last_update_framebuffer_pitch_tiles_at_32bpp_ !=
                  pitch_tiles_at_32bpp ||
              std::memcmp(last_update_framebuffer_attachments_,
                          depth_and_color_render_targets,
                          sizeof(last_update_framebuffer_attachments_))) {
            framebuffer = nullptr;
          }
        }
        if (!framebuffer) {
          // LEVEL 4 color-only native HLE: augment the producer framebuffer with
          // a private native color image + alternate framebuffer (idempotent,
          // cached on the entry). Returns the plain base framebuffer for non-
          // producer shapes, so the command processor transparently falls back.
          if (cvars::gpu_bd_native_color_lifetime_hle >= 4) {
            framebuffer = GetBdNativeColorProducerFramebuffer(
                render_pass_key, pitch_tiles_at_32bpp,
                depth_and_color_render_targets);
          } else if (cvars::gpu_bd_patha_depth_snapshot) {
            // PATH A STAGE 1: augment the framebuffer with a native 4x depth
            // snapshot for the ONE HDR-effect depth consumer (returns the plain
            // base framebuffer, with bd_native_depth_framebuffer set only for the
            // target pass, so the command processor transparently falls back).
            framebuffer = GetBdNativeDepthConsumerFramebuffer(
                render_pass_key, pitch_tiles_at_32bpp,
                depth_and_color_render_targets);
          } else {
            framebuffer = GetHostRenderTargetsFramebuffer(
                render_pass_key, pitch_tiles_at_32bpp,
                depth_and_color_render_targets);
          }
          if (!framebuffer) {
            return false;
          }
        }
        return true;
      };

      if (cvars::gpu_vulkan_inpass_edram_transfers > 0 &&
          !render_pass_key.use_fdm) {
        // In-pass transfer mode: resolve the guest pass objects first and hand
        // them to the transfer executor so eligible destinations can be
        // transferred inside the guest pass (left open for the guest draw).
        // Forced OFF when FDM is on (use_fdm): in-pass transfers would render
        // inside the FDM guest pass and get density-downscaled, corrupting the
        // pixel-exact copies - the separate-pass path uses non-FDM transfer passes.
        if (!lookup_guest_pass_objects()) {
          return false;
        }
        PerformTransfersAndResolveClears(
            1 + xenos::kMaxColorRenderTargets, depth_and_color_render_targets,
            last_update_transfers(), nullptr, nullptr, &render_pass_key,
            render_pass, framebuffer);
      } else {
        PerformTransfersAndResolveClears(1 + xenos::kMaxColorRenderTargets,
                                         depth_and_color_render_targets,
                                         last_update_transfers());
        if (!lookup_guest_pass_objects()) {
          return false;
        }
      }

      // Successful update - write the new configuration.
      last_update_render_pass_key_ = render_pass_key;
      last_update_render_pass_ = render_pass;
      last_update_framebuffer_pitch_tiles_at_32bpp_ = pitch_tiles_at_32bpp;
      std::memcpy(last_update_framebuffer_attachments_,
                  depth_and_color_render_targets,
                  sizeof(last_update_framebuffer_attachments_));
      last_update_framebuffer_ = framebuffer;

      // Transition the used render targets.
      for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
        RenderTarget* rt = depth_and_color_render_targets[i];
        if (!rt) {
          continue;
        }
        auto& vulkan_rt = *static_cast<VulkanRenderTarget*>(rt);
        VkPipelineStageFlags rt_dst_stage_mask;
        VkAccessFlags rt_dst_access_mask;
        VkImageLayout rt_new_layout;
        VulkanRenderTarget::GetDrawUsage(i == 0, &rt_dst_stage_mask,
                                         &rt_dst_access_mask, &rt_new_layout);
        command_processor_.PushImageMemoryBarrier(
            vulkan_rt.image(),
            ui::vulkan::util::InitializeSubresourceRange(
                i ? VK_IMAGE_ASPECT_COLOR_BIT
                  : (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)),
            vulkan_rt.current_stage_mask(), rt_dst_stage_mask,
            vulkan_rt.current_access_mask(), rt_dst_access_mask,
            vulkan_rt.current_layout(), rt_new_layout);
        vulkan_rt.SetUsage(rt_dst_stage_mask, rt_dst_access_mask,
                           rt_new_layout);
      }
    } break;

    case Path::kPixelShaderInterlock: {
      // For FSI, only the barrier is needed - already scheduled if required.
      // But the buffer will be used for FSI drawing now.
      UseEdramBuffer(EdramBufferUsage::kFragmentReadWrite);
      // Commit preceding unordered (but not FSI) writes like clears as they
      // aren't synchronized with FSI accesses.
      CommitEdramBufferShaderWrites(
          EdramBufferModificationStatus::kViaUnordered);
      // TODO(Triang3l): Check if this draw call modifies color or depth /
      // stencil, at least coarsely, to prevent useless barriers.
      MarkEdramBufferModified(
          EdramBufferModificationStatus::kViaFragmentShaderInterlock);
      last_update_render_pass_key_ = render_pass_key;
      last_update_render_pass_ = fsi_render_pass_;
      last_update_framebuffer_ = &fsi_framebuffer_;
    } break;

    default:
      assert_unhandled_case(GetPath());
      return false;
  }

  return true;
}

bool VulkanRenderTargetCache::UpdateForHybridPostprocessComposite() {
  // THE EDRAM SOLVE, hybrid form: RT update for a post-process composite - the FSI
  // (edram_buffer_/SSBO) representation, mirroring the kPixelShaderInterlock branch
  // of Update(), while path_ stays host-RT. The composite renders PASS-LESS into
  // edram_buffer_ via the 0-attachment FSI render pass (no host-RT transfer/pass),
  // so it does not break the tiler. Ownership is already buffer-authoritative
  // (BeginHybridPostprocessPhase), so producer resolves read edram_buffer_.
  auto rb_surface_info = register_file().Get<reg::RB_SURFACE_INFO>();
  RenderPassKey render_pass_key;
  render_pass_key.msaa_samples =
      draw_util::ClampForcedMsaaSamples(rb_surface_info.msaa_samples);
  UseEdramBuffer(EdramBufferUsage::kFragmentReadWrite);
  CommitEdramBufferShaderWrites(EdramBufferModificationStatus::kViaUnordered);
  MarkEdramBufferModified(
      EdramBufferModificationStatus::kViaFragmentShaderInterlock);
  last_update_render_pass_key_ = render_pass_key;
  last_update_render_pass_ = fsi_render_pass_;
  last_update_framebuffer_ = &fsi_framebuffer_;
  return true;
}

void VulkanRenderTargetCache::BeginHybridPostprocessPhase() {
  // THE EDRAM SOLVE, hybrid form: the first post-process composite of the frame.
  // Bridge the main-scene host RTs INTO edram_buffer_ (a full-EDRAM dump - the one
  // unavoidable host->buffer transfer), then hand EDRAM ownership to the buffer so
  // every subsequent composite resolve reads edram_buffer_ (not the host images)
  // and the composites chain coherently with NO render-to-texture pass-breaks.
  // The command processor ends the open host render pass before calling this.
  if (hybrid_postprocess_phase_active_) {
    return;
  }
  DumpRenderTargets(0, xenos::kEdramTileCount, 1, xenos::kEdramTileCount);
  ClearOwnershipForEdramBufferAuthoritative();
  hybrid_postprocess_phase_active_ = true;
}

VkRenderPass VulkanRenderTargetCache::GetHostRenderTargetsRenderPass(
    RenderPassKey key, uint32_t load_dont_care_mask, bool depth_store_op_none,
    bool bd_color_resolve) {
  assert_true(GetPath() == Path::kHostRenderTargets);

  // Only attachments that are actually bound can be marked.
  load_dont_care_mask &= key.depth_and_color_used;
  // Depth-store-NONE only applies when a depth/stencil attachment is bound.
  depth_store_op_none =
      depth_store_op_none && (key.depth_and_color_used & 0b1) != 0;
  // BD direct-native color resolve: only meaningful with MSAA + a color RT (the
  // MSAA color resolves ON-TILE to a trailing 1x attachment; depth unchanged).
  bd_color_resolve = bd_color_resolve &&
                     key.msaa_samples != xenos::MsaaSamples::k1X &&
                     (key.depth_and_color_used >> 1) != 0;
  // Variant key: [.. key ..][color_resolve:1][depth_store_op_none:1][load:5].
  bool is_variant =
      load_dont_care_mask != 0 || depth_store_op_none || bd_color_resolve;
  uint64_t load_dont_care_key = (uint64_t(key.key) << 7) |
                                (uint64_t(bd_color_resolve) << 6) |
                                (uint64_t(depth_store_op_none) << 5) |
                                uint64_t(load_dont_care_mask);
  if (is_variant) {
    auto variant_it = load_dont_care_render_passes_.find(load_dont_care_key);
    if (variant_it != load_dont_care_render_passes_.end()) {
      return variant_it->second;
    }
  } else {
    auto it = render_passes_.find(key);
    if (it != render_passes_.end()) {
      return it->second;
    }
  }

  VkSampleCountFlagBits samples;
  switch (key.msaa_samples) {
    case xenos::MsaaSamples::k1X:
      samples = VK_SAMPLE_COUNT_1_BIT;
      break;
    case xenos::MsaaSamples::k2X:
      samples = IsMsaa2xSupported(key.depth_and_color_used != 0)
                    ? VK_SAMPLE_COUNT_2_BIT
                    : VK_SAMPLE_COUNT_4_BIT;
      break;
    case xenos::MsaaSamples::k4X:
      samples = VK_SAMPLE_COUNT_4_BIT;
      break;
    default:
      return VK_NULL_HANDLE;
  }

  // +kMaxColorRenderTargets trailing slots for optional BD color-resolve targets,
  // +1 for an optional FDM (fragment density map) attachment.
  VkAttachmentDescription
      attachments[1 + xenos::kMaxColorRenderTargets +
                  xenos::kMaxColorRenderTargets + 1];
  if (key.depth_and_color_used & 0b1) {
    VkAttachmentDescription& attachment = attachments[0];
    attachment.flags = 0;
    attachment.format = GetDepthVulkanFormat(key.depth_format);
    attachment.samples = samples;
    // gpu_edram_passes_dont_care = the raw diagnostic (all passes, load AND
    // store - black-screens titles that need the contents); the safe per-pass
    // variant elides only the LOAD of attachments in load_dont_care_mask,
    // proven fully overwritten by the pass's first draw.
    const bool dont_care = cvars::gpu_edram_passes_dont_care;
    const bool load_dont_care = dont_care || (load_dont_care_mask & 0b1);
    // LRZ spike: force the depth loadOp to CLEAR so Turnip keeps Adreno LRZ valid
    // (it disables LRZ when depth enters via LOAD_OP_LOAD). The matching depth
    // clearValue is supplied at pass-begin in vulkan_command_processor.cc.
    attachment.loadOp = cvars::gpu_lrz_spike_depth_clear
                            ? VK_ATTACHMENT_LOAD_OP_CLEAR
                        : load_dont_care ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                         : VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = dont_care ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                   : VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = load_dont_care ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                              : VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.stencilStoreOp = dont_care ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                          : VK_ATTACHMENT_STORE_OP_STORE;
    // gpu_vulkan_skip_unused_depth_store: this variant is selected by the command
    // processor only for guest passes whose draws provably never test or write
    // depth/stencil (it breaks the pass before any depth-using draw), so the
    // attachment is never accessed. Skip the oversized depth tile load AND store:
    // DONT_CARE the load (no draw reads it) and STORE_OP_NONE the store so the
    // depth EDRAM memory is PRESERVED (STORE_OP_DONT_CARE would undefine it and
    // corrupt render targets that alias those tiles). Takes precedence over the
    // load_dont_care / lrz-spike-clear paths above (mutually exclusive by caller).
    // STORE_OP_NONE is Vulkan 1.3 core; the variant accessor gates on the device
    // API version, so this enum is only reached when it is valid.
    if (depth_store_op_none) {
      attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      attachment.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
      attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_NONE;
    }
    attachment.initialLayout = VulkanRenderTarget::kDepthDrawLayout;
    attachment.finalLayout = VulkanRenderTarget::kDepthDrawLayout;
  }
  VkAttachmentReference color_attachments[xenos::kMaxColorRenderTargets];
  xenos::ColorRenderTargetFormat color_formats[] = {
      key.color_0_view_format,
      key.color_1_view_format,
      key.color_2_view_format,
      key.color_3_view_format,
  };
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    VkAttachmentReference& color_attachment = color_attachments[i];
    color_attachment.layout = VulkanRenderTarget::kColorDrawLayout;
    uint32_t attachment_bit = uint32_t(1) << (1 + i);
    if (!(key.depth_and_color_used & attachment_bit)) {
      color_attachment.attachment = VK_ATTACHMENT_UNUSED;
      continue;
    }
    uint32_t attachment_index =
        xe::bit_count(key.depth_and_color_used & (attachment_bit - 1));
    color_attachment.attachment = attachment_index;
    VkAttachmentDescription& attachment = attachments[attachment_index];
    attachment.flags = 0;
    xenos::ColorRenderTargetFormat color_format = color_formats[i];
    attachment.format =
        key.color_rts_use_transfer_formats
            ? GetColorOwnershipTransferVulkanFormat(color_format)
            : GetColorVulkanFormat(color_format);
    attachment.samples = samples;
    attachment.loadOp = (cvars::gpu_edram_passes_dont_care ||
                         (load_dont_care_mask & attachment_bit))
                            ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                            : VK_ATTACHMENT_LOAD_OP_LOAD;
    // BD color-resolve: the MSAA color need not be stored (only the resolved 1x
    // is kept) - DONT_CARE lets the tiler skip the MSAA store.
    attachment.storeOp = (cvars::gpu_edram_passes_dont_care || bd_color_resolve)
                             ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                             : VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VulkanRenderTarget::kColorDrawLayout;
    attachment.finalLayout = VulkanRenderTarget::kColorDrawLayout;
  }

  // BD direct-native color resolve: build the pResolveAttachments array (one entry
  // per color slot, UNUSED except the used colors which resolve to a trailing 1x
  // attachment). The trailing resolve attachment(s) are appended below, keyed by a
  // running index after the depth+color attachments. One resolve image per used
  // color; the framebuffer supplies them at these trailing indices.
  VkAttachmentReference bd_resolve_refs[xenos::kMaxColorRenderTargets];
  uint32_t bd_resolve_first_index = xe::bit_count(key.depth_and_color_used);
  uint32_t bd_resolve_count = 0;
  if (bd_color_resolve) {
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      if (color_attachments[i].attachment == VK_ATTACHMENT_UNUSED) {
        bd_resolve_refs[i].attachment = VK_ATTACHMENT_UNUSED;
        bd_resolve_refs[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
      } else {
        bd_resolve_refs[i].attachment = bd_resolve_first_index + bd_resolve_count;
        bd_resolve_refs[i].layout = VulkanRenderTarget::kColorDrawLayout;
        ++bd_resolve_count;
      }
    }
  }

  VkAttachmentReference depth_stencil_attachment;
  depth_stencil_attachment.attachment =
      (key.depth_and_color_used & 0b1) ? 0 : VK_ATTACHMENT_UNUSED;
  depth_stencil_attachment.layout = VulkanRenderTarget::kDepthDrawLayout;

  VkSubpassDescription subpass;
  subpass.flags = 0;
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.inputAttachmentCount = 0;
  subpass.pInputAttachments = nullptr;
  subpass.colorAttachmentCount =
      32 - xe::lzcnt(uint32_t(key.depth_and_color_used >> 1));
  subpass.pColorAttachments = color_attachments;
  subpass.pResolveAttachments = bd_color_resolve ? bd_resolve_refs : nullptr;
  subpass.pDepthStencilAttachment =
      (key.depth_and_color_used & 0b1) ? &depth_stencil_attachment : nullptr;
  subpass.preserveAttachmentCount = 0;
  subpass.pPreserveAttachments = nullptr;

  VkPipelineStageFlags dependency_stage_mask = 0;
  VkAccessFlags dependency_access_mask = 0;
  if (key.depth_and_color_used & 0b1) {
    dependency_stage_mask |= VulkanRenderTarget::kDepthDrawStageMask;
    dependency_access_mask |= VulkanRenderTarget::kDepthDrawAccessMask;
  }
  if (key.depth_and_color_used >> 1) {
    dependency_stage_mask |= VulkanRenderTarget::kColorDrawStageMask;
    dependency_access_mask |= VulkanRenderTarget::kColorDrawAccessMask;
  }
  VkSubpassDependency subpass_dependencies[3];
  subpass_dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  subpass_dependencies[0].dstSubpass = 0;
  subpass_dependencies[0].srcStageMask = dependency_stage_mask;
  subpass_dependencies[0].dstStageMask = dependency_stage_mask;
  subpass_dependencies[0].srcAccessMask = dependency_access_mask;
  subpass_dependencies[0].dstAccessMask = dependency_access_mask;
  subpass_dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  subpass_dependencies[1].srcSubpass = 0;
  subpass_dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  subpass_dependencies[1].srcStageMask = dependency_stage_mask;
  subpass_dependencies[1].dstStageMask = dependency_stage_mask;
  subpass_dependencies[1].srcAccessMask = dependency_access_mask;
  subpass_dependencies[1].dstAccessMask = dependency_access_mask;
  subpass_dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  // Rung 3 records a depth transfer as the first draw of the consumer subpass.
  // This framebuffer-local self-dependency permits its in-pass barrier from
  // depth/stencil writes to the following guest depth tests.
  subpass_dependencies[2].srcSubpass = 0;
  subpass_dependencies[2].dstSubpass = 0;
  subpass_dependencies[2].srcStageMask =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  subpass_dependencies[2].dstStageMask =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  subpass_dependencies[2].srcAccessMask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  subpass_dependencies[2].dstAccessMask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  subpass_dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  if (cvars::gpu_vulkan_weak_external_subpass_deps) {
    // DIAGNOSTIC (knowingly unsafe in theory): turn both EXTERNAL dependencies
    // into no-ops so the driver may overlap this pass's binning with prior
    // passes' deferred render. Every BTTF frame fully drains (~12.7ms, ~30% of
    // the GPU frame) right before its main scene pass and the drain survives
    // transfer elimination AND draw merging - these EXTERNAL deps are the
    // remaining candidate serializer. If the drain vanishes and the frame
    // stays pixel-correct, build the precise version (emit targeted barriers
    // only on actual same-RT reuse between passes).
    subpass_dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    subpass_dependencies[0].srcAccessMask = 0;
    subpass_dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpass_dependencies[1].dstAccessMask = 0;
  }

  VkRenderPassCreateInfo render_pass_create_info;
  render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_create_info.pNext = nullptr;
  render_pass_create_info.flags = 0;
  render_pass_create_info.attachmentCount =
      xe::bit_count(key.depth_and_color_used);
  render_pass_create_info.pAttachments = attachments;
  render_pass_create_info.subpassCount = 1;
  render_pass_create_info.pSubpasses = &subpass;
  render_pass_create_info.dependencyCount =
      (key.depth_and_color_used & 0b1)
          ? uint32_t(xe::countof(subpass_dependencies))
          : (key.depth_and_color_used ? 2u : 0u);
  render_pass_create_info.pDependencies = subpass_dependencies;

  // BD color-resolve: append the trailing 1x resolve attachment(s) at
  // bd_resolve_first_index onward (before the FDM append, which follows). The
  // framebuffer supplies matching 1x image views at these same indices. Format
  // MUST equal the source color attachment's format (Vulkan resolve rule).
  if (bd_color_resolve) {
    uint32_t ri = 0;
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      if (color_attachments[i].attachment == VK_ATTACHMENT_UNUSED) {
        continue;
      }
      VkAttachmentDescription& ra = attachments[bd_resolve_first_index + ri];
      ra.flags = 0;
      ra.format = key.color_rts_use_transfer_formats
                      ? GetColorOwnershipTransferVulkanFormat(color_formats[i])
                      : GetColorVulkanFormat(color_formats[i]);
      ra.samples = VK_SAMPLE_COUNT_1_BIT;
      ra.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      ra.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      ra.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      ra.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      ra.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      ra.finalLayout = VulkanRenderTarget::kColorDrawLayout;
      ++ri;
    }
    render_pass_create_info.attachmentCount += bd_resolve_count;
  }

  // FDM: append the fragment density map attachment + chain its create-info, only
  // on guest-geometry passes (key.use_fdm). The attachment is referenced solely
  // via pNext (never a subpass color/depth/input ref); the matching density image
  // view is supplied by the framebuffer at this same trailing index. Must outlive
  // the vkCreateRenderPass call below (same scope).
  VkRenderPassFragmentDensityMapCreateInfoEXT fdm_create_info;
  if (key.use_fdm) {
    uint32_t fdm_attachment_index = render_pass_create_info.attachmentCount;
    VkAttachmentDescription& fdm_attachment = attachments[fdm_attachment_index];
    fdm_attachment.flags = 0;
    fdm_attachment.format = VK_FORMAT_R16G16_SFLOAT;
    fdm_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    fdm_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    fdm_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    fdm_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    fdm_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    fdm_attachment.initialLayout =
        VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
    fdm_attachment.finalLayout =
        VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
    fdm_create_info.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_FRAGMENT_DENSITY_MAP_CREATE_INFO_EXT;
    fdm_create_info.pNext = render_pass_create_info.pNext;
    fdm_create_info.fragmentDensityMapAttachment.attachment =
        fdm_attachment_index;
    fdm_create_info.fragmentDensityMapAttachment.layout =
        VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT;
    render_pass_create_info.pNext = &fdm_create_info;
    ++render_pass_create_info.attachmentCount;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkRenderPass render_pass;
  if (dfn.vkCreateRenderPass(device, &render_pass_create_info, nullptr,
                             &render_pass) != VK_SUCCESS) {
    XELOGE("VulkanRenderTargetCache: Failed to create a render pass");
    if (is_variant) {
      load_dont_care_render_passes_.emplace(load_dont_care_key,
                                            VK_NULL_HANDLE);
    } else {
      render_passes_.emplace(key, VK_NULL_HANDLE);
    }
    return VK_NULL_HANDLE;
  }
  if (is_variant) {
    load_dont_care_render_passes_.emplace(load_dont_care_key, render_pass);
  } else {
    render_passes_.emplace(key, render_pass);
  }
  return render_pass;
}

VkRenderPass VulkanRenderTargetCache::GetFeedbackRenderPass(
    xenos::ColorRenderTargetFormat producer_format,
    xenos::ColorRenderTargetFormat consumer_format,
    xenos::MsaaSamples msaa_samples, bool in_place) {
  // BD input-attachment merge (Inc2): the 2-subpass feedback render pass that
  // lets a same-pixel render-to-texture composite read its producer RT as a
  // Vulkan INPUT ATTACHMENT (GMEM-resident) instead of the store->DRAM->sample
  // round-trip (the ~79ms EDRAM tile-I/O lever). Subpass 0 renders the producer
  // (attachment 0); subpass 1 reads it as input + renders the consumer.
  // in_place=true: producer == consumer RT (BD's composites read+write the same
  // image) -> ONE attachment, subpass1 input=att0 + color=att0 (GENERAL), with a
  // 1->1 self-dependency = programmable blending / framebuffer fetch.
  uint32_t key = (uint32_t(producer_format) & 0xF) |
                 ((uint32_t(consumer_format) & 0xF) << 4) |
                 ((uint32_t(msaa_samples) & 0x3) << 8) |
                 (in_place ? (uint32_t(1) << 10) : 0);
  auto it = feedback_render_passes_.find(key);
  if (it != feedback_render_passes_.end()) {
    return it->second;
  }

  VkSampleCountFlagBits samples =
      VkSampleCountFlagBits(uint32_t(1) << uint32_t(msaa_samples));

  VkAttachmentDescription attachments[2] = {};
  // Attachment 0: producer RT - rendered in subpass 0, read as input in subpass 1.
  attachments[0].format = GetColorVulkanFormat(producer_format);
  attachments[0].samples = samples;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VulkanRenderTarget::kColorDrawLayout;
  attachments[0].finalLayout = VulkanRenderTarget::kColorDrawLayout;
  // Attachment 1: consumer RT - rendered in subpass 1.
  attachments[1].format = GetColorVulkanFormat(consumer_format);
  attachments[1].samples = samples;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VulkanRenderTarget::kColorDrawLayout;
  attachments[1].finalLayout = VulkanRenderTarget::kColorDrawLayout;

  VkAttachmentReference producer_color_ref;
  producer_color_ref.attachment = 0;
  producer_color_ref.layout = VulkanRenderTarget::kColorDrawLayout;
  VkAttachmentReference producer_input_ref;
  producer_input_ref.attachment = 0;
  // In-place: att0 is BOTH input and color in subpass 1 -> GENERAL (one layout
  // valid for read+write). The 2-RT case reads att0 read-only.
  producer_input_ref.layout = in_place
                                  ? VK_IMAGE_LAYOUT_GENERAL
                                  : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference consumer_color_ref;
  consumer_color_ref.attachment = in_place ? 0 : 1;
  consumer_color_ref.layout =
      in_place ? VK_IMAGE_LAYOUT_GENERAL : VulkanRenderTarget::kColorDrawLayout;

  VkSubpassDescription subpasses[2] = {};
  subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpasses[0].colorAttachmentCount = 1;
  subpasses[0].pColorAttachments = &producer_color_ref;
  subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpasses[1].inputAttachmentCount = 1;
  subpasses[1].pInputAttachments = &producer_input_ref;
  subpasses[1].colorAttachmentCount = 1;
  subpasses[1].pColorAttachments = &consumer_color_ref;

  // External deps mirror the normal pass; 0->1 carries the producer's color
  // write to the consumer's same-pixel input-attachment read (BY_REGION).
  VkSubpassDependency dependencies[4];
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VulkanRenderTarget::kColorDrawStageMask;
  dependencies[0].dstStageMask = VulkanRenderTarget::kColorDrawStageMask;
  dependencies[0].srcAccessMask = VulkanRenderTarget::kColorDrawAccessMask;
  dependencies[0].dstAccessMask = VulkanRenderTarget::kColorDrawAccessMask;
  dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = 1;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
  dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  uint32_t dependency_count = 2;
  if (in_place) {
    // Feedback self-dependency: subpass 1 reads att0 as input while writing it
    // as color. BY_REGION + a once-per-pixel composite = well-defined without
    // rasterization-order-attachment-access (absent on Turnip).
    dependencies[dependency_count].srcSubpass = 1;
    dependencies[dependency_count].dstSubpass = 1;
    dependencies[dependency_count].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[dependency_count].dstStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[dependency_count].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[dependency_count].dstAccessMask =
        VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    dependencies[dependency_count].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    ++dependency_count;
  }
  dependencies[dependency_count].srcSubpass = 1;
  dependencies[dependency_count].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[dependency_count].srcStageMask =
      VulkanRenderTarget::kColorDrawStageMask;
  dependencies[dependency_count].dstStageMask =
      VulkanRenderTarget::kColorDrawStageMask;
  dependencies[dependency_count].srcAccessMask =
      VulkanRenderTarget::kColorDrawAccessMask;
  dependencies[dependency_count].dstAccessMask =
      VulkanRenderTarget::kColorDrawAccessMask;
  dependencies[dependency_count].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  ++dependency_count;

  VkRenderPassCreateInfo render_pass_create_info = {};
  render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_create_info.attachmentCount = in_place ? 1 : 2;
  render_pass_create_info.pAttachments = attachments;
  render_pass_create_info.subpassCount = 2;
  render_pass_create_info.pSubpasses = subpasses;
  render_pass_create_info.dependencyCount = dependency_count;
  render_pass_create_info.pDependencies = dependencies;

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkRenderPass render_pass;
  if (dfn.vkCreateRenderPass(device, &render_pass_create_info, nullptr,
                             &render_pass) != VK_SUCCESS) {
    XELOGE("VulkanRenderTargetCache: Failed to create a feedback render pass");
    feedback_render_passes_.emplace(key, VK_NULL_HANDLE);
    return VK_NULL_HANDLE;
  }
  feedback_render_passes_.emplace(key, render_pass);
  return render_pass;
}

VkRenderPass VulkanRenderTargetCache::GetBdNativeCustomResolveRenderPass(
    xenos::ColorRenderTargetFormat producer_format, VkFormat resolve_format,
    xenos::MsaaSamples msaa_samples, VkFormat depth_format) {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  if (!vulkan_device->extensions().ext_EXT_custom_resolve ||
      !vulkan_device->properties().customResolve) {
    return VK_NULL_HANDLE;
  }
  const bool has_depth = depth_format != VK_FORMAT_UNDEFINED;
  uint32_t key = (uint32_t(producer_format) & 0xF) |
                 ((uint32_t(resolve_format) & 0xFF) << 4) |
                 ((uint32_t(msaa_samples) & 0x3) << 12) |
                 (has_depth ? (uint32_t(1) << 14) : 0);
  auto it = bd_custom_resolve_render_passes_.find(key);
  if (it != bd_custom_resolve_render_passes_.end()) {
    return it->second;
  }

  // MUST match the producer image's sample count exactly (same 2x->4x fallback as
  // CreateRenderTarget / the producer framebuffer), or the framebuffer is invalid.
  VkSampleCountFlagBits samples;
  if (msaa_samples == xenos::MsaaSamples::k2X && !msaa_2x_attachments_supported_) {
    samples = VK_SAMPLE_COUNT_4_BIT;
  } else {
    samples = VkSampleCountFlagBits(uint32_t(1) << uint32_t(msaa_samples));
  }

  // Attachment 0: MSAA producer color (subpass 0 color, subpass 1 input). Its
  // MSAA content need not be stored - only the resolved 1x is kept (DONT_CARE).
  // Attachment 1: 1x A2B10 resolve output (subpass 1 color). LOAD DONT_CARE,
  // STORE, finalLayout SHADER_READ so the composite samples it.
  // Attachment 2 (optional): MSAA depth (subpass 0 depth-stencil).
  VkAttachmentDescription attachments[3] = {};
  attachments[0].format = GetColorVulkanFormat(producer_format);
  attachments[0].samples = samples;
  // DECOUPLED single contiguous pass: the field's captured draws FULLY cover the
  // producer in ONE pass (no interleave), so the MSAA needs neither LOAD (no prior
  // content) nor STORE (only the resolved att1 is kept) -> DONT_CARE + UNDEFINED.
  // This also removes the WRITE-AFTER-WRITE store/input-read sync hazard that the
  // LOAD/STORE-persistence (interleaving) variant introduced.
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VulkanRenderTarget::kColorDrawLayout;
  attachments[1].format = resolve_format;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  // The composite samples the resolve output -> leave it SHADER_READ (matches the
  // sampler descriptor layout; the 1->EXTERNAL dep orders the write before it).
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  attachments[2].format =
      has_depth ? depth_format : VK_FORMAT_D24_UNORM_S8_UINT;
  attachments[2].samples = samples;
  // Depth: LOAD the LLE depth (the in-order depth-only prepass primed it) so the
  // replayed field draws depth-test correctly; DONT_CARE store (the field depth
  // isn't reused after the single pass). Already in kDepthDrawLayout (RTC Update).
  attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[2].initialLayout = VulkanRenderTarget::kDepthDrawLayout;
  attachments[2].finalLayout = VulkanRenderTarget::kDepthDrawLayout;

  VkAttachmentReference producer_color_ref;
  producer_color_ref.attachment = 0;
  producer_color_ref.layout = VulkanRenderTarget::kColorDrawLayout;
  VkAttachmentReference producer_depth_ref;
  producer_depth_ref.attachment = 2;
  producer_depth_ref.layout = VulkanRenderTarget::kDepthDrawLayout;
  VkAttachmentReference resolve_input_ref;
  resolve_input_ref.attachment = 0;
  resolve_input_ref.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference resolve_color_ref;
  resolve_color_ref.attachment = 1;
  resolve_color_ref.layout = VulkanRenderTarget::kColorDrawLayout;

  VkSubpassDescription subpasses[2] = {};
  // Subpass 0: BD's field producer (guest draws), MSAA color + optional depth.
  subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpasses[0].colorAttachmentCount = 1;
  subpasses[0].pColorAttachments = &producer_color_ref;
  subpasses[0].pDepthStencilAttachment = has_depth ? &producer_depth_ref : nullptr;
  // Subpass 1: the shader custom resolve. flags CUSTOM_RESOLVE_BIT; reads the MSAA
  // producer as an input attachment; writes the 1x A2B10 resolve output.
  subpasses[1].flags = VK_SUBPASS_DESCRIPTION_CUSTOM_RESOLVE_BIT_EXT;
  subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpasses[1].inputAttachmentCount = 1;
  subpasses[1].pInputAttachments = &resolve_input_ref;
  subpasses[1].colorAttachmentCount = 1;
  subpasses[1].pColorAttachments = &resolve_color_ref;

  // 0->1 carries the producer's MSAA color write to the custom-resolve subpass's
  // input-attachment read. MUST be framebuffer-local (BY_REGION) or Turnip
  // disables GMEM rendering (5.6-sol).
  VkSubpassDependency dependencies[3];
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VulkanRenderTarget::kColorDrawStageMask;
  dependencies[0].dstStageMask = VulkanRenderTarget::kColorDrawStageMask;
  dependencies[0].srcAccessMask = VulkanRenderTarget::kColorDrawAccessMask;
  dependencies[0].dstAccessMask = VulkanRenderTarget::kColorDrawAccessMask;
  dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = 1;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
  dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  dependencies[2].srcSubpass = 1;
  dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  // NOT framebuffer-local: the composite samples the resolve output with UVs
  // (possibly scaled/filtered), not same-pixel -> a global dependency (5.6-sol).
  dependencies[2].dependencyFlags = 0;

  VkRenderPassCreateInfo render_pass_create_info = {};
  render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_create_info.attachmentCount = has_depth ? 3 : 2;
  render_pass_create_info.pAttachments = attachments;
  render_pass_create_info.subpassCount = 2;
  render_pass_create_info.pSubpasses = subpasses;
  render_pass_create_info.dependencyCount = 3;
  render_pass_create_info.pDependencies = dependencies;

  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkRenderPass render_pass;
  if (dfn.vkCreateRenderPass(device, &render_pass_create_info, nullptr,
                             &render_pass) != VK_SUCCESS) {
    XELOGE("BD custom-resolve: failed to create the 2-subpass render pass");
    bd_custom_resolve_render_passes_.emplace(key, VK_NULL_HANDLE);
    return VK_NULL_HANDLE;
  }
  XELOGI(
      "BD custom-resolve: created 2-subpass render pass (producer fmt={} resolve "
      "fmt={} msaa={} depth={})",
      uint32_t(producer_format), uint32_t(resolve_format),
      uint32_t(msaa_samples), has_depth ? 1 : 0);
  bd_custom_resolve_render_passes_.emplace(key, render_pass);
  return render_pass;
}

VkFramebuffer VulkanRenderTargetCache::GetFeedbackFramebuffer(
    VkImageView producer_view, VkImageView consumer_view, VkExtent2D extent,
    VkRenderPass feedback_render_pass, bool in_place) {
  // BD input-attachment merge: a 2-attachment framebuffer for the feedback
  // render pass - attachment 0 = producer (subpass 0 color / subpass 1 input),
  // attachment 1 = consumer (subpass 1 color). in_place => ONE attachment (the
  // shared view; producer_view == consumer_view). Cached by the (producer,
  // consumer) view pair; the views are stable while their RTs are cached, so a
  // steady scene reuses one framebuffer (no per-frame leak). Destroyed in ClearCache.
  for (const FeedbackFramebuffer& fb : feedback_framebuffers_) {
    if (fb.producer_view == producer_view && fb.consumer_view == consumer_view) {
      return fb.framebuffer;
    }
  }
  VkImageView attachments[2] = {producer_view, consumer_view};
  VkFramebufferCreateInfo create_info;
  create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  create_info.pNext = nullptr;
  create_info.flags = 0;
  create_info.renderPass = feedback_render_pass;
  create_info.attachmentCount = in_place ? 1 : 2;
  create_info.pAttachments = attachments;
  create_info.width = extent.width;
  create_info.height = extent.height;
  create_info.layers = 1;
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  VkFramebuffer framebuffer;
  if (dfn.vkCreateFramebuffer(vulkan_device->device(), &create_info, nullptr,
                              &framebuffer) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  feedback_framebuffers_.push_back({producer_view, consumer_view, framebuffer});
  return framebuffer;
}

VkRenderPass VulkanRenderTargetCache::GetLoadDontCareVariantForLastUpdate(
    VkRenderPass original, uint32_t load_dont_care_mask) {
  if (GetPath() != Path::kHostRenderTargets ||
      original != last_update_render_pass_ ||
      original == VK_NULL_HANDLE) {
    return original;
  }
  RenderPassKey key = last_update_render_pass_key_;
  load_dont_care_mask &= key.depth_and_color_used;
  if (!load_dont_care_mask) {
    return original;
  }
  VkRenderPass variant =
      GetHostRenderTargetsRenderPass(key, load_dont_care_mask);
  return variant != VK_NULL_HANDLE ? variant : original;
}

VkRenderPass VulkanRenderTargetCache::GetDepthStoreNoneVariantForLastUpdate(
    VkRenderPass original) {
  if (GetPath() != Path::kHostRenderTargets ||
      original != last_update_render_pass_ || original == VK_NULL_HANDLE) {
    return original;
  }
  RenderPassKey key = last_update_render_pass_key_;
  // Only meaningful when a depth/stencil attachment is actually bound.
  if (!(key.depth_and_color_used & 0b1)) {
    return original;
  }
  // VK_ATTACHMENT_STORE_OP_NONE is Vulkan 1.3 core (and has no feature bit); fall
  // back to the normal pass on older devices rather than emitting an invalid enum.
  if (command_processor_.GetVulkanDevice()->properties().apiVersion <
      VK_MAKE_API_VERSION(0, 1, 3, 0)) {
    return original;
  }
  VkRenderPass variant =
      GetHostRenderTargetsRenderPass(key, 0, /*depth_store_op_none=*/true);
  return variant != VK_NULL_HANDLE ? variant : original;
}

VkFormat VulkanRenderTargetCache::GetDepthVulkanFormat(
    xenos::DepthRenderTargetFormat format) const {
  if (format == xenos::DepthRenderTargetFormat::kD24S8 &&
      depth_unorm24_vulkan_format_supported()) {
    return VK_FORMAT_D24_UNORM_S8_UINT;
  }
  return VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkImageView
VulkanRenderTargetCache::GetResolveSourceRenderTargetViewForSampling(
    uint32_t src_edram_base_tiles, uint32_t src_pitch_tiles,
    uint32_t src_format, uint8_t src_msaa, bool src_is_depth,
    VkFormat expected_texture_host_format) {
  // EDRAM-recompiler RT-as-texture, increment 1 (gpu_rt_as_texture, default-off):
  // if a pixel-texture fetch samples data a resolve wrote to shared memory this
  // frame and the SOURCE render target is still resident, 1-sample, not
  // resolution-scaled and host-format-identical, bind that render target's image
  // view directly instead of reloading the resolved copy from shared memory.
  if (!cvars::gpu_rt_as_texture) {
    return VK_NULL_HANDLE;
  }
  // Resolution scaling gives the host RT a different footprint than the reloaded
  // guest texture - not a 1:1 substitute.
  if (IsDrawResolutionScaled()) {
    return VK_NULL_HANDLE;
  }
  // 1-sample color only in increment 1.
  if (src_msaa != uint8_t(xenos::MsaaSamples::k1X) || src_is_depth) {
    return VK_NULL_HANDLE;
  }
  // Resident, identity-matched, not-a-current-attachment lookup (private state -
  // done on the base class).
  RenderTarget* render_target_base = ResolveSourceResidentRenderTarget(
      src_edram_base_tiles, src_pitch_tiles, src_format, uint32_t(src_msaa),
      src_is_depth);
  if (!render_target_base) {
    return VK_NULL_HANDLE;
  }
  auto& render_target = *static_cast<VulkanRenderTarget*>(render_target_base);
  // Non-converting only: the resident RT's host color format must exactly match
  // the unsigned host format the texture cache would have produced for the fetch.
  if (GetColorVulkanFormat(render_target.key().GetColorFormat()) !=
      expected_texture_host_format) {
    return VK_NULL_HANDLE;
  }
  // Make the render target readable by the guest shaders as a sampled image.
  // Usually it was already left SHADER_READ_ONLY_OPTIMAL by the resolve's
  // DumpRenderTargets, so this is at most a stage/access-only barrier (no layout
  // change); skip_if_equal drops it entirely if it is a complete no-op. Mirrors
  // the dump barrier (current_*_mask() as src).
  command_processor_.PushImageMemoryBarrier(
      render_target.image(),
      ui::vulkan::util::InitializeSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
      render_target.current_stage_mask(),
      command_processor_.guest_shader_pipeline_stages_,
      render_target.current_access_mask(), VK_ACCESS_SHADER_READ_BIT,
      render_target.current_layout(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  render_target.SetUsage(command_processor_.guest_shader_pipeline_stages_,
                         VK_ACCESS_SHADER_READ_BIT,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  return render_target.view_color_transfer();
}

void VulkanRenderTargetCache::LatchBoundColorRTForDecoupledCapture() {
  // Blue Dragon native-draw HLE decoupled present: latch color[0] of the last
  // Update (index [1]; [0] is depth). When a native draw redirected RB_COLOR_INFO
  // to a non-aliasing EDRAM base, this is the dedicated full-surface host RT. All
  // covered field draws share the base (same RT), so latch once per frame.
  if (bd_decoupled_capture_rt_ || GetPath() != Path::kHostRenderTargets) {
    return;
  }
  RenderTarget* color0 = last_update_accumulated_render_targets()[1];
  if (color0) {
    bd_decoupled_capture_rt_ = static_cast<VulkanRenderTarget*>(color0);
  }
}

VkImageView VulkanRenderTargetCache::GetDecoupledPresentView() const {
  if (!bd_decoupled_capture_rt_) {
    return VK_NULL_HANDLE;
  }
  return bd_decoupled_capture_rt_->view_color_transfer();
}

void VulkanRenderTargetCache::TransitionDecoupledRTToShaderRead() {
  if (!bd_decoupled_capture_rt_) {
    return;
  }
  VulkanRenderTarget& rt = *bd_decoupled_capture_rt_;
  // Mirror the resolve dump / RT-as-texture barrier (current_*_mask() as src);
  // skip_if_equal drops it entirely if it is already shader-read.
  command_processor_.PushImageMemoryBarrier(
      rt.image(),
      ui::vulkan::util::InitializeSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT),
      rt.current_stage_mask(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      rt.current_access_mask(), VK_ACCESS_SHADER_READ_BIT, rt.current_layout(),
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  rt.SetUsage(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

VkFormat VulkanRenderTargetCache::GetColorVulkanFormat(
    xenos::ColorRenderTargetFormat format) const {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return gamma_render_target_as_unorm16_ ? VK_FORMAT_R16G16B16A16_UNORM
                                             : VK_FORMAT_R8G8B8A8_UNORM;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      // The guest format is 10-bit-per-channel UNORM; the historical 8-bit host
      // fallback discards 2 bits/channel (banding) for no bandwidth benefit (both
      // are 32-bpp). Keep the full 10 bits when enabled (same 32-bit format class).
      return cvars::gpu_2101010_color_as_unorm10
                 ? VK_FORMAT_A2B10G10R10_UNORM_PACK32
                 : VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      // Diagnostic: fall back to a non-float host format to isolate whether the
      // float color attachment is why color never renders on Turnip.
      if (cvars::vulkan_force_float_color_unorm) {
        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
      }
      // Efficiency: the 7e3 float buffer fits a 32-bpp 10-bit-per-channel UNORM
      // (half the 64-bpp float16 bandwidth) for SDR content. Same 32-bit format
      // class as the integer transfer alias, so the mutable view stays legal.
      // Clamps the >1.0 extended range -> gated off for HDR.
      if (cvars::gpu_fp10_color_as_unorm10) {
        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
      }
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case xenos::ColorRenderTargetFormat::k_16_16:
      // TODO(Triang3l): Fallback to float16 (disregarding clearing correctness
      // likely) - possibly on render target gathering, treating them entirely
      // as float16.
      return VK_FORMAT_R16G16_SNORM;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      // TODO(Triang3l): Fallback to float16 (disregarding clearing correctness
      // likely) - possibly on render target gathering, treating them entirely
      // as float16.
      return VK_FORMAT_R16G16B16A16_SNORM;
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return VK_FORMAT_R16G16_SFLOAT;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return VK_FORMAT_R32_SFLOAT;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return VK_FORMAT_R32G32_SFLOAT;
    default:
      assert_unhandled_case(format);
      return VK_FORMAT_UNDEFINED;
  }
}

VkFormat VulkanRenderTargetCache::GetColorOwnershipTransferVulkanFormat(
    xenos::ColorRenderTargetFormat format, bool* is_integer_out) const {
  if (is_integer_out) {
    *is_integer_out = true;
  }
  // Floating-point numbers have NaNs that need to be propagated without
  // modifications to the bit representation, and SNORM has two representations
  // of -1.
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_16_16:
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return VK_FORMAT_R16G16_UINT;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return VK_FORMAT_R16G16B16A16_UINT;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return VK_FORMAT_R32_UINT;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return VK_FORMAT_R32G32_UINT;
    default:
      if (is_integer_out) {
        *is_integer_out = false;
      }
      return GetColorVulkanFormat(format);
  }
}

VulkanRenderTargetCache::VulkanRenderTarget::~VulkanRenderTarget() {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      render_target_cache_.command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  ui::vulkan::SingleLayoutDescriptorSetPool& descriptor_set_pool =
      key().is_depth
          ? *render_target_cache_.descriptor_set_pool_sampled_image_x2_
          : *render_target_cache_.descriptor_set_pool_sampled_image_;
  descriptor_set_pool.Free(descriptor_set_index_transfer_source_);
  if (view_color_transfer_separate_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_color_transfer_separate_, nullptr);
  }
  if (view_stencil_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_stencil_, nullptr);
  }
  if (view_depth_stencil_ != VK_NULL_HANDLE) {
    dfn.vkDestroyImageView(device, view_depth_stencil_, nullptr);
  }
  dfn.vkDestroyImageView(device, view_depth_color_, nullptr);
  dfn.vkDestroyImage(device, image_, nullptr);
  dfn.vkFreeMemory(device, memory_, nullptr);
}

bool VulkanRenderTargetCache::IsGammaFormatHostStorageSeparate() const {
  return gamma_render_target_as_unorm16_;
}

uint32_t VulkanRenderTargetCache::GetMaxRenderTargetWidth() const {
  const ui::vulkan::VulkanDevice::Properties& device_properties =
      command_processor_.GetVulkanDevice()->properties();
  return std::min(device_properties.maxFramebufferWidth,
                  device_properties.maxImageDimension2D);
}

uint32_t VulkanRenderTargetCache::GetMaxRenderTargetHeight() const {
  const ui::vulkan::VulkanDevice::Properties& device_properties =
      command_processor_.GetVulkanDevice()->properties();
  return std::min(device_properties.maxFramebufferHeight,
                  device_properties.maxImageDimension2D);
}

RenderTargetCache::RenderTarget* VulkanRenderTargetCache::CreateRenderTarget(
    RenderTargetKey key) {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // Create the image.

  VkImageCreateInfo image_create_info;
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = nullptr;
  image_create_info.flags = 0;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.extent.width = key.GetWidth() * draw_resolution_scale_x();
  image_create_info.extent.height =
      GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples) *
      draw_resolution_scale_y();
  // BD tile-I/O cut (THE REAL KNOB): Turnip's per-pass GMEM resolve / storeOp
  // covers the full ATTACHMENT IMAGE extent, not the renderArea - so clamping only
  // the framebuffer (host_extent, in GetHostRenderTargetsFramebuffer) was inert and
  // every resolve walked the 8192-tall tile-rounded image. Shrink the IMAGE
  // allocation to the used rows. Safe only when no draw renders past the clamp and
  // no EDRAM aliasing reads the clamped rows (BD: max render height 720 < 768,
  // aliasing transfers = 0). Default-0 (off, lossless). See the cvar comment.
  if (cvars::gpu_clamp_rt_image_height &&
      image_create_info.extent.height > cvars::gpu_clamp_rt_image_height) {
    image_create_info.extent.height = cvars::gpu_clamp_rt_image_height;
  }
  // gpu_resolution_downscale_pct (increment 1): shrink the host RT image so the
  // expensive main scene renders fewer fragments. Draws' viewport/scissor are
  // scaled by the same factor (vulkan_command_processor); composites sampling
  // the RT with normalized UVs upscale it transparently. Keep >=1.
  // gpu_diag_raster_ab keeps the RT FULL-size (only the per-draw viewport
  // alternates) so the fill test isn't confounded by a shrunk RT clipping the
  // full-viewport phase.
  if (cvars::gpu_resolution_downscale_pct > 0 &&
      cvars::gpu_resolution_downscale_pct < 100 && !cvars::gpu_diag_raster_ab) {
    uint32_t pct = uint32_t(cvars::gpu_resolution_downscale_pct);
    image_create_info.extent.width =
        std::max(1u, image_create_info.extent.width * pct / 100u);
    image_create_info.extent.height =
        std::max(1u, image_create_info.extent.height * pct / 100u);
  }
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  if (key.msaa_samples == xenos::MsaaSamples::k2X &&
      !msaa_2x_attachments_supported_) {
    image_create_info.samples = VK_SAMPLE_COUNT_4_BIT;
  } else {
    image_create_info.samples =
        VkSampleCountFlagBits(uint32_t(1) << uint32_t(key.msaa_samples));
  }
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  // The RT-image readback diagnostics vkCmdCopyImageToBuffer from this image,
  // which REQUIRES TRANSFER_SRC usage; without it the copy is invalid and the
  // driver may return zeros (making the readback lie). Only add it when a
  // readback diagnostic is enabled so the normal path stays unchanged.
  if (cvars::vulkan_trace_dump_rt_image || cvars::vulkan_trace_dump_depth_image) {
    image_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  // LEVEL 4 mirror round-trip prerequisite (gpu_bd_native_color_lifetime_hle >= 4,
  // 5.6-sol step 1 + #1 risk): the seed (LLE color -> native) + mirror (native ->
  // LLE color) vkCmdCopyImage need the LLE COLOR image to be both TRANSFER_SRC and
  // TRANSFER_DST. Gated + color-only so the normal path + depth are untouched.
  if (cvars::gpu_bd_native_color_lifetime_hle >= 4 && !key.is_depth) {
    image_create_info.usage |=
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkFormat transfer_format;
  VkFormat rt_view_formats[2] = {};
  VkImageFormatListCreateInfo rt_format_list_info{
      VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
  if (key.is_depth) {
    image_create_info.format = GetDepthVulkanFormat(key.GetDepthFormat());
    transfer_format = image_create_info.format;
    image_create_info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  } else {
    xenos::ColorRenderTargetFormat color_format = key.GetColorFormat();
    image_create_info.format = GetColorVulkanFormat(color_format);
    transfer_format = GetColorOwnershipTransferVulkanFormat(color_format);
    if (image_create_info.format != transfer_format) {
      image_create_info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
      // RT-bandwidth recovery: MUTABLE_FORMAT disables UBWC on pre-750 Adreno
      // unless we declare the exact view formats. The color RT is viewed only as
      // its base format (color view) and the transfer alias (ownership-transfer
      // view), so this 2-entry list is complete and keeps UBWC alive. cvar-gated.
      if (cvars::gpu_vulkan_rt_keep_ubwc &&
          vulkan_device->extensions().ext_1_2_KHR_image_format_list) {
        rt_view_formats[0] = image_create_info.format;
        rt_view_formats[1] = transfer_format;
        rt_format_list_info.viewFormatCount = 2;
        rt_format_list_info.pViewFormats = rt_view_formats;
        rt_format_list_info.pNext = image_create_info.pNext;
        image_create_info.pNext = &rt_format_list_info;
      }
    }
    image_create_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if (image_create_info.format == VK_FORMAT_UNDEFINED) {
    XELOGE("VulkanRenderTargetCache: Unknown {} render target format {}",
           key.is_depth ? "depth" : "color", key.resource_format);
    return nullptr;
  }
  VkImage image;
  VkDeviceMemory memory;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device, image_create_info,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, image, memory)) {
    XELOGE(
        "VulkanRenderTarget: Failed to create a {}x{} {}xMSAA {} render target "
        "image",
        image_create_info.extent.width, image_create_info.extent.height,
        uint32_t(1) << uint32_t(key.msaa_samples), key.GetFormatName());
    return nullptr;
  }

  // Create the image views.

  VkImageViewCreateInfo view_create_info;
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.pNext = nullptr;
  view_create_info.flags = 0;
  view_create_info.image = image;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create_info.format = image_create_info.format;
  view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.subresourceRange =
      ui::vulkan::util::InitializeSubresourceRange(
          key.is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
  VkImageView view_depth_color;
  if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                            &view_depth_color) != VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTarget: Failed to create a {} view for a {}x{} {}xMSAA {} "
        "render target",
        key.is_depth ? "depth" : "color", image_create_info.extent.width,
        image_create_info.extent.height,
        uint32_t(1) << uint32_t(key.msaa_samples), key.GetFormatName());
    dfn.vkDestroyImage(device, image, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return nullptr;
  }
  VkImageView view_depth_stencil = VK_NULL_HANDLE;
  VkImageView view_stencil = VK_NULL_HANDLE;
  VkImageView view_color_transfer_separate = VK_NULL_HANDLE;
  if (key.is_depth) {
    view_create_info.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                              &view_depth_stencil) != VK_SUCCESS) {
      XELOGE(
          "VulkanRenderTarget: Failed to create a depth / stencil view for a "
          "{}x{} {}xMSAA {} render target",
          image_create_info.extent.width, image_create_info.extent.height,
          uint32_t(1) << uint32_t(key.msaa_samples),
          xenos::GetDepthRenderTargetFormatName(key.GetDepthFormat()));
      dfn.vkDestroyImageView(device, view_depth_color, nullptr);
      dfn.vkDestroyImage(device, image, nullptr);
      dfn.vkFreeMemory(device, memory, nullptr);
      return nullptr;
    }
    view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                              &view_stencil) != VK_SUCCESS) {
      XELOGE(
          "VulkanRenderTarget: Failed to create a stencil view for a {}x{} "
          "{}xMSAA render target",
          image_create_info.extent.width, image_create_info.extent.height,
          uint32_t(1) << uint32_t(key.msaa_samples),
          xenos::GetDepthRenderTargetFormatName(key.GetDepthFormat()));
      dfn.vkDestroyImageView(device, view_depth_stencil, nullptr);
      dfn.vkDestroyImageView(device, view_depth_color, nullptr);
      dfn.vkDestroyImage(device, image, nullptr);
      dfn.vkFreeMemory(device, memory, nullptr);
      return nullptr;
    }
  } else {
    if (transfer_format != image_create_info.format) {
      view_create_info.format = transfer_format;
      if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                                &view_color_transfer_separate) != VK_SUCCESS) {
        XELOGE(
            "VulkanRenderTarget: Failed to create a transfer view for a {}x{} "
            "{}xMSAA {} render target",
            image_create_info.extent.width, image_create_info.extent.height,
            uint32_t(1) << uint32_t(key.msaa_samples), key.GetFormatName());
        dfn.vkDestroyImageView(device, view_depth_color, nullptr);
        dfn.vkDestroyImage(device, image, nullptr);
        dfn.vkFreeMemory(device, memory, nullptr);
        return nullptr;
      }
    }
  }

  ui::vulkan::SingleLayoutDescriptorSetPool& descriptor_set_pool =
      key.is_depth ? *descriptor_set_pool_sampled_image_x2_
                   : *descriptor_set_pool_sampled_image_;
  size_t descriptor_set_index_transfer_source = descriptor_set_pool.Allocate();
  if (descriptor_set_index_transfer_source == SIZE_MAX) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to allocate sampled image descriptors "
        "for a {} render target",
        key.is_depth ? "depth/stencil" : "color");
    if (view_color_transfer_separate != VK_NULL_HANDLE) {
      dfn.vkDestroyImageView(device, view_color_transfer_separate, nullptr);
    }
    dfn.vkDestroyImageView(device, view_depth_color, nullptr);
    dfn.vkDestroyImage(device, image, nullptr);
    dfn.vkFreeMemory(device, memory, nullptr);
    return nullptr;
  }
  VkDescriptorSet descriptor_set_transfer_source =
      descriptor_set_pool.Get(descriptor_set_index_transfer_source);
  VkWriteDescriptorSet descriptor_set_write[2];
  VkDescriptorImageInfo descriptor_set_write_depth_color;
  descriptor_set_write_depth_color.sampler = VK_NULL_HANDLE;
  descriptor_set_write_depth_color.imageView =
      view_color_transfer_separate != VK_NULL_HANDLE
          ? view_color_transfer_separate
          : view_depth_color;
  descriptor_set_write_depth_color.imageLayout =
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  descriptor_set_write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_set_write[0].pNext = nullptr;
  descriptor_set_write[0].dstSet = descriptor_set_transfer_source;
  descriptor_set_write[0].dstBinding = 0;
  descriptor_set_write[0].dstArrayElement = 0;
  descriptor_set_write[0].descriptorCount = 1;
  descriptor_set_write[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  descriptor_set_write[0].pImageInfo = &descriptor_set_write_depth_color;
  descriptor_set_write[0].pBufferInfo = nullptr;
  descriptor_set_write[0].pTexelBufferView = nullptr;
  VkDescriptorImageInfo descriptor_set_write_stencil;
  if (key.is_depth) {
    descriptor_set_write_stencil.sampler = VK_NULL_HANDLE;
    descriptor_set_write_stencil.imageView = view_stencil;
    descriptor_set_write_stencil.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptor_set_write[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_set_write[1].pNext = nullptr;
    descriptor_set_write[1].dstSet = descriptor_set_transfer_source;
    descriptor_set_write[1].dstBinding = 1;
    descriptor_set_write[1].dstArrayElement = 0;
    descriptor_set_write[1].descriptorCount = 1;
    descriptor_set_write[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptor_set_write[1].pImageInfo = &descriptor_set_write_stencil;
    descriptor_set_write[1].pBufferInfo = nullptr;
    descriptor_set_write[1].pTexelBufferView = nullptr;
  }
  dfn.vkUpdateDescriptorSets(device, key.is_depth ? 2 : 1, descriptor_set_write,
                             0, nullptr);

  return new VulkanRenderTarget(key, *this, image, memory, view_depth_color,
                                view_depth_stencil, view_stencil,
                                view_color_transfer_separate,
                                descriptor_set_index_transfer_source);
}

bool VulkanRenderTargetCache::IsHostDepthEncodingDifferent(
    xenos::DepthRenderTargetFormat format) const {
  // TODO(Triang3l): Conversion directly in shaders.
  switch (format) {
    case xenos::DepthRenderTargetFormat::kD24S8:
      return !depth_unorm24_vulkan_format_supported();
    case xenos::DepthRenderTargetFormat::kD24FS8:
      return true;
  }
  return false;
}

void VulkanRenderTargetCache::RequestPixelShaderInterlockBarrier() {
  if (edram_buffer_usage_ == EdramBufferUsage::kFragmentReadWrite) {
    CommitEdramBufferShaderWrites();
  }
}

void VulkanRenderTargetCache::GetEdramBufferUsageMasks(
    EdramBufferUsage usage, VkPipelineStageFlags& stage_mask_out,
    VkAccessFlags& access_mask_out) {
  switch (usage) {
    case EdramBufferUsage::kFragmentRead:
      stage_mask_out = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      access_mask_out = VK_ACCESS_SHADER_READ_BIT;
      break;
    case EdramBufferUsage::kFragmentReadWrite:
      stage_mask_out = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      access_mask_out = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      break;
    case EdramBufferUsage::kComputeRead:
      stage_mask_out = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      access_mask_out = VK_ACCESS_SHADER_READ_BIT;
      break;
    case EdramBufferUsage::kComputeWrite:
      stage_mask_out = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      access_mask_out = VK_ACCESS_SHADER_WRITE_BIT;
      break;
    case EdramBufferUsage::kTransferRead:
      stage_mask_out = VK_PIPELINE_STAGE_TRANSFER_BIT;
      access_mask_out = VK_ACCESS_TRANSFER_READ_BIT;
      break;
    case EdramBufferUsage::kTransferWrite:
      stage_mask_out = VK_PIPELINE_STAGE_TRANSFER_BIT;
      access_mask_out = VK_ACCESS_TRANSFER_WRITE_BIT;
      break;
    default:
      assert_unhandled_case(usage);
  }
}

void VulkanRenderTargetCache::UseEdramBuffer(EdramBufferUsage new_usage) {
  if (edram_buffer_usage_ == new_usage) {
    return;
  }
  VkPipelineStageFlags src_stage_mask, dst_stage_mask;
  VkAccessFlags src_access_mask, dst_access_mask;
  GetEdramBufferUsageMasks(edram_buffer_usage_, src_stage_mask,
                           src_access_mask);
  GetEdramBufferUsageMasks(new_usage, dst_stage_mask, dst_access_mask);
  if (command_processor_.PushBufferMemoryBarrier(
          edram_buffer_, 0, VK_WHOLE_SIZE, src_stage_mask, dst_stage_mask,
          src_access_mask, dst_access_mask)) {
    // Resetting edram_buffer_modification_status_ only if the barrier has been
    // truly inserted.
    edram_buffer_modification_status_ =
        EdramBufferModificationStatus::kUnmodified;
  }
  edram_buffer_usage_ = new_usage;
}

void VulkanRenderTargetCache::MarkEdramBufferModified(
    EdramBufferModificationStatus modification_status) {
  assert_true(modification_status !=
              EdramBufferModificationStatus::kUnmodified);
  switch (edram_buffer_usage_) {
    case EdramBufferUsage::kFragmentReadWrite:
      // max because being modified via unordered access requires stricter
      // synchronization than via fragment shader interlocks.
      edram_buffer_modification_status_ =
          std::max(edram_buffer_modification_status_, modification_status);
      break;
    case EdramBufferUsage::kComputeWrite:
      assert_true(modification_status ==
                  EdramBufferModificationStatus::kViaUnordered);
      modification_status = EdramBufferModificationStatus::kViaUnordered;
      break;
    default:
      assert_always(
          "While changing the usage of the EDRAM buffer before marking it as "
          "modified is handled safely (but will cause spurious marking as "
          "modified after the changes have been implicitly committed by the "
          "usage switch), normally that shouldn't be done and is an "
          "indication of architectural mistakes. Alternatively, this may "
          "indicate that the usage switch has been forgotten before writing, "
          "which is a clearly invalid situation.");
  }
}

void VulkanRenderTargetCache::CommitEdramBufferShaderWrites(
    EdramBufferModificationStatus commit_status) {
  assert_true(commit_status != EdramBufferModificationStatus::kUnmodified);
  if (edram_buffer_modification_status_ < commit_status) {
    return;
  }
  VkPipelineStageFlags stage_mask;
  VkAccessFlags access_mask;
  GetEdramBufferUsageMasks(edram_buffer_usage_, stage_mask, access_mask);
  assert_not_zero(access_mask & VK_ACCESS_SHADER_WRITE_BIT);
  // DIAGNOSTIC (gpu_edram_atomic_barrier_bytes): scope the barrier to a smaller
  // byte range to isolate whether the ~450ms buffer-path cost is the 90MB cache
  // flush (a small size collapses gap_guest => scoping is the fix) or the pipeline
  // serialization (unchanged => the barrier is fundamental to no-FSI ordering).
  VkDeviceSize barrier_size = VK_WHOLE_SIZE;
  if (cvars::gpu_edram_atomic_barrier_bytes != 0) {
    barrier_size = VkDeviceSize(cvars::gpu_edram_atomic_barrier_bytes);
  }
  command_processor_.PushBufferMemoryBarrier(
      edram_buffer_, 0, barrier_size, stage_mask, stage_mask, access_mask,
      access_mask, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, false);
  edram_buffer_modification_status_ =
      EdramBufferModificationStatus::kUnmodified;
  PixelShaderInterlockFullEdramBarrierPlaced();
}

const VulkanRenderTargetCache::Framebuffer*
VulkanRenderTargetCache::GetHostRenderTargetsFramebuffer(
    RenderPassKey render_pass_key, uint32_t pitch_tiles_at_32bpp,
    const RenderTarget* const* depth_and_color_render_targets) {
  FramebufferKey key;
  key.render_pass_key = render_pass_key;
  key.pitch_tiles_at_32bpp = pitch_tiles_at_32bpp;
  if (render_pass_key.depth_and_color_used & (1 << 0)) {
    key.depth_base_tiles = depth_and_color_render_targets[0]->key().base_tiles;
  }
  if (render_pass_key.depth_and_color_used & (1 << 1)) {
    key.color_0_base_tiles =
        depth_and_color_render_targets[1]->key().base_tiles;
  }
  if (render_pass_key.depth_and_color_used & (1 << 2)) {
    key.color_1_base_tiles =
        depth_and_color_render_targets[2]->key().base_tiles;
  }
  if (render_pass_key.depth_and_color_used & (1 << 3)) {
    key.color_2_base_tiles =
        depth_and_color_render_targets[3]->key().base_tiles;
  }
  if (render_pass_key.depth_and_color_used & (1 << 4)) {
    key.color_3_base_tiles =
        depth_and_color_render_targets[4]->key().base_tiles;
  }
  auto it = framebuffers_.find(key);
  if (it != framebuffers_.end()) {
    return &it->second;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  const ui::vulkan::VulkanDevice::Properties& device_properties =
      vulkan_device->properties();

  VkRenderPass render_pass = GetHostRenderTargetsRenderPass(render_pass_key);
  if (render_pass == VK_NULL_HANDLE) {
    return nullptr;
  }

  // +1 trailing slot for an optional FDM density-map attachment.
  VkImageView attachments[1 + xenos::kMaxColorRenderTargets + 1];
  uint32_t attachment_count = 0;
  uint32_t depth_and_color_rts_remaining = render_pass_key.depth_and_color_used;
  uint32_t rt_index;
  // BD input-attachment merge: remember the first color RT view so a feedback
  // merge can assemble a 2-RT framebuffer from the producer + consumer
  // framebuffers' color attachments.
  VkImageView main_color_view = VK_NULL_HANDLE;
  VkImageView depth_view = VK_NULL_HANDLE;
  while (xe::bit_scan_forward(depth_and_color_rts_remaining, &rt_index)) {
    depth_and_color_rts_remaining &= ~(uint32_t(1) << rt_index);
    const auto& vulkan_rt = *static_cast<const VulkanRenderTarget*>(
        depth_and_color_render_targets[rt_index]);
    VkImageView attachment;
    if (rt_index) {
      attachment = render_pass_key.color_rts_use_transfer_formats
                       ? vulkan_rt.view_color_transfer()
                       : vulkan_rt.view_depth_color();
      if (main_color_view == VK_NULL_HANDLE) {
        main_color_view = attachment;
      }
    } else {
      attachment = vulkan_rt.view_depth_stencil();
      depth_view = attachment;
    }
    attachments[attachment_count++] = attachment;
  }

  VkFramebufferCreateInfo framebuffer_create_info;
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.pNext = nullptr;
  framebuffer_create_info.flags = 0;
  framebuffer_create_info.renderPass = render_pass;
  framebuffer_create_info.pAttachments = attachments;
  VkExtent2D host_extent;
  if (pitch_tiles_at_32bpp) {
    host_extent.width = RenderTargetKey::GetWidth(pitch_tiles_at_32bpp,
                                                  render_pass_key.msaa_samples);
    host_extent.height = GetRenderTargetHeight(pitch_tiles_at_32bpp,
                                               render_pass_key.msaa_samples);
  } else {
    assert_zero(render_pass_key.depth_and_color_used);
    // Still needed for occlusion queries.
    host_extent.width = xenos::kTexture2DCubeMaxWidthHeight;
    host_extent.height = xenos::kTexture2DCubeMaxWidthHeight;
  }
  // Limiting to the device limit for the case of no attachments, for which
  // there's no limit imposed by the sizes of the attachments that have been
  // created successfully.
  host_extent.width = std::min(host_extent.width * draw_resolution_scale_x(),
                               device_properties.maxFramebufferWidth);
  host_extent.height = std::min(host_extent.height * draw_resolution_scale_y(),
                                device_properties.maxFramebufferHeight);
  // BD tile-I/O cut: the host RT is tile-rounded to a huge height (e.g. 4096 /
  // 8192) for EDRAM aliasing, but at 720p only ~720 rows are ever rendered. On a
  // TBDR the storeOp/loadOp cover the FRAMEBUFFER height, so the unused rows are
  // stored+loaded wastefully (the bulk of the ~79ms tile I/O for the tall RTs).
  // Clamp the framebuffer height when no EDRAM aliasing reads the clamped rows
  // (BD: aliasing transfers measured = 0). Gated default-0 (off, lossless).
  if (cvars::gpu_clamp_rt_framebuffer_height &&
      host_extent.height > cvars::gpu_clamp_rt_framebuffer_height) {
    host_extent.height = cvars::gpu_clamp_rt_framebuffer_height;
  }
  // The framebuffer height must not exceed the (possibly clamped) attachment IMAGE
  // height (gpu_clamp_rt_image_height) - a framebuffer larger than its attachment is
  // invalid. So the image clamp transitively clamps the framebuffer too, making
  // gpu_clamp_rt_image_height self-sufficient (no need to also set the framebuffer one).
  if (cvars::gpu_clamp_rt_image_height &&
      host_extent.height > cvars::gpu_clamp_rt_image_height) {
    host_extent.height = cvars::gpu_clamp_rt_image_height;
  }
  // gpu_resolution_downscale_pct: match the framebuffer to the downscaled
  // attachment images (created shrunk above by the same factor). Skipped for
  // gpu_diag_raster_ab (RT stays full; only the viewport alternates).
  if (cvars::gpu_resolution_downscale_pct > 0 &&
      cvars::gpu_resolution_downscale_pct < 100 && !cvars::gpu_diag_raster_ab) {
    uint32_t pct = uint32_t(cvars::gpu_resolution_downscale_pct);
    host_extent.width = std::max(1u, host_extent.width * pct / 100u);
    host_extent.height = std::max(1u, host_extent.height * pct / 100u);
  }
  framebuffer_create_info.width = host_extent.width;
  framebuffer_create_info.height = host_extent.height;
  framebuffer_create_info.layers = 1;

  // FDM: create + uniform-fill the per-framebuffer density map and append its view
  // at the trailing index (matching the render pass's FDM attachment). The render
  // pass declared the FDM attachment, so the framebuffer MUST supply it; on
  // failure, fail the framebuffer (graceful - the guest pass is skipped, no crash).
  VkImage fdm_image = VK_NULL_HANDLE;
  VkDeviceMemory fdm_memory = VK_NULL_HANDLE;
  VkImageView fdm_view = VK_NULL_HANDLE;
  if (render_pass_key.use_fdm) {
    if (!CreateFragmentDensityMap(host_extent, fdm_image, fdm_memory,
                                  fdm_view)) {
      return nullptr;
    }
    attachments[attachment_count++] = fdm_view;
  }
  framebuffer_create_info.attachmentCount = attachment_count;

  VkFramebuffer framebuffer;
  if (dfn.vkCreateFramebuffer(device, &framebuffer_create_info, nullptr,
                              &framebuffer) != VK_SUCCESS) {
    if (fdm_image != VK_NULL_HANDLE) {
      dfn.vkDestroyImageView(device, fdm_view, nullptr);
      dfn.vkDestroyImage(device, fdm_image, nullptr);
      dfn.vkFreeMemory(device, fdm_memory, nullptr);
    }
    return nullptr;
  }
  // Creates at a persistent location - safe to use pointers.
  Framebuffer& framebuffer_entry =
      framebuffers_
          .emplace(std::piecewise_construct, std::forward_as_tuple(key),
                   std::forward_as_tuple(framebuffer, host_extent))
          .first->second;
  framebuffer_entry.color_view = main_color_view;
  framebuffer_entry.depth_view = depth_view;
  framebuffer_entry.fdm_image = fdm_image;
  framebuffer_entry.fdm_memory = fdm_memory;
  framebuffer_entry.fdm_view = fdm_view;
  // Fill the density map now that the framebuffer (which owns the image) exists -
  // recording the clear+barriers only on the success path avoids a command
  // referencing an image freed by the failure path above.
  if (fdm_image != VK_NULL_HANDLE) {
    FillFragmentDensityMap(fdm_image);
  }
  return &framebuffer_entry;
}

const VulkanRenderTargetCache::Framebuffer*
VulkanRenderTargetCache::GetBdNativeColorProducerFramebuffer(
    RenderPassKey render_pass_key, uint32_t pitch_tiles_at_32bpp,
    const RenderTarget* const* depth_and_color_render_targets) {
  // Base LLE framebuffer (created/looked up exactly as the normal path). The
  // native variant is cached ON this entry, so we reuse its host_extent + key.
  const Framebuffer* base = GetHostRenderTargetsFramebuffer(
      render_pass_key, pitch_tiles_at_32bpp, depth_and_color_render_targets);
  if (!base) {
    return nullptr;
  }
  // Already built for this framebuffer -> reuse.
  if (base->bd_native_color_framebuffer != VK_NULL_HANDLE) {
    return base;
  }
  // Producer constraints: transfer-format color attachments reinterpret the
  // image format (ownership-transfer passes) -> the native single-format image
  // + copy would not match; bail to plain LLE. Require a SINGLE color RT; depth
  // is OPTIONAL (the pure-LLE field producer has depth 473; the final COMPOSITE
  // producer is color-only).
  if (render_pass_key.color_rts_use_transfer_formats) {
    return base;
  }
  uint32_t used = render_pass_key.depth_and_color_used;
  bool has_depth = (used & (uint32_t(1) << 0)) != 0;
  uint32_t color_mask = used & ~(uint32_t(1) << 0);
  uint32_t color_index;
  if (!xe::bit_scan_forward(color_mask, &color_index)) {
    return base;  // No color attachment.
  }
  if (color_mask & ~(uint32_t(1) << color_index)) {
    return base;  // More than one color RT -> not the producer shape.
  }

  // Cover the WHOLE color chain the EDRAM transfers alias: composite (1280),
  // field (pitch ~672-720) AND bloom pyramid (320/160/80). Each gets its OWN
  // native image (no EDRAM aliasing), so once consumers are redirected the
  // ownership transfers between them are redundant (droppable). >=64 covers
  // field+bloom; depth-only shadow passes have no color bit so they still bail
  // above (single-color required). Below 64 = tiny/degenerate, skip.
  if (base->host_extent.width < 64u) {
    return base;
  }

  const VulkanRenderTarget* depth_rt =
      has_depth ? static_cast<const VulkanRenderTarget*>(
                      depth_and_color_render_targets[0])
                : nullptr;
  const auto& color_rt = *static_cast<const VulkanRenderTarget*>(
      depth_and_color_render_targets[color_index]);
  VkFormat color_format = GetColorVulkanFormat(color_rt.key().GetColorFormat());
  if (color_format == VK_FORMAT_UNDEFINED) {
    return base;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // Sample count must MATCH the LLE color image exactly (the seed/mirror
  // vkCmdCopyImage requires identical sample counts) - replicate CreateRenderTarget's
  // 2x->4x fallback.
  VkSampleCountFlagBits samples;
  if (render_pass_key.msaa_samples == xenos::MsaaSamples::k2X &&
      !msaa_2x_attachments_supported_) {
    samples = VK_SAMPLE_COUNT_4_BIT;
  } else {
    samples = VkSampleCountFlagBits(uint32_t(1)
                                    << uint32_t(render_pass_key.msaa_samples));
  }

  // VK_EXT_custom_resolve (Turnip): when the producer is MSAA + direct-native, the
  // field renders float16 into subpass 0 and a shader custom-resolve subpass reads
  // it as an input attachment and writes 1x A2B10 - on-tile, no off-chip MSAA spill.
  // The producer image then also needs INPUT_ATTACHMENT usage.
  // The 2-subpass CR producer is used ONLY by the interleaved direct-native path
  // (keep_scissor). The DECOUPLE path deliberately uses the PLAIN producer on all
  // platforms (incl. Turnip where the ext exists): the CR on-tile convert measured
  // net-NEGATIVE (it ADDS a convert subpass + forces MSAA materialization on TBDR),
  // whereas the plain path renders the field contiguously + mirrors native->LLE and
  // just COLLAPSES the pass count (98->72 begins/frame on the field) with no added
  // conversion. So field_decouple must NOT select custom-resolve.
  const bool use_custom_resolve =
      cvars::gpu_bd_native_keep_scissor &&
      samples != VK_SAMPLE_COUNT_1_BIT &&
      vulkan_device->extensions().ext_EXT_custom_resolve &&
      vulkan_device->properties().customResolve;

  // DIRECT-NATIVE path (gpu_bd_native_keep_scissor, 5.6-sol): create the producer
  // at LOGICAL dims (from the resource graph via SelectNativeBinding) instead of
  // the tile-rounded host_extent, so the composite's [0,1] UVs sample correct
  // content directly (no copy-on-resolve, which is perf-dead on Turnip). The field
  // renders into [0,logical] via its per-group scissors (kept, not overridden).
  // Falls back to host_extent when no logical dims are learned yet (first frame).
  uint32_t prod_width = base->host_extent.width;
  uint32_t prod_height = base->host_extent.height;
  if (cvars::gpu_bd_native_keep_scissor || cvars::gpu_bd_field_decouple) {
    VulkanCommandProcessor::NativeBindingPlan plan =
        command_processor_.SelectNativeBinding(color_rt.key().key);
    if (plan.use_native && plan.key.logical_width && plan.key.logical_height) {
      prod_width = std::min(plan.key.logical_width * draw_resolution_scale_x(),
                            base->host_extent.width);
      prod_height = std::min(plan.key.logical_height * draw_resolution_scale_y(),
                             base->host_extent.height);
    }
  }
  VkImageCreateInfo image_create_info;
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = nullptr;
  image_create_info.flags = 0;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.format = color_format;
  image_create_info.extent.width = prod_width;
  image_create_info.extent.height = prod_height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = samples;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            // LEVEL 5 (generation bridge): consumers (composite
                            // sampler / present) bind this image directly, so it
                            // must be sampleable.
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            // custom-resolve reads the MSAA producer as an input
                            // attachment in subpass 1.
                            (use_custom_resolve
                                 ? VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
                                 : 0);
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage native_image = VK_NULL_HANDLE;
  VkDeviceMemory native_memory = VK_NULL_HANDLE;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device, image_create_info,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, native_image,
          native_memory)) {
    XELOGE("BD L4: failed to create native color producer image");
    return base;
  }

  VkImageViewCreateInfo view_create_info;
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.pNext = nullptr;
  view_create_info.flags = 0;
  view_create_info.image = native_image;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create_info.format = color_format;
  view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_create_info.subresourceRange.baseMipLevel = 0;
  view_create_info.subresourceRange.levelCount = 1;
  view_create_info.subresourceRange.baseArrayLayer = 0;
  view_create_info.subresourceRange.layerCount = 1;
  VkImageView native_view = VK_NULL_HANDLE;
  if (dfn.vkCreateImageView(device, &view_create_info, nullptr, &native_view) !=
      VK_SUCCESS) {
    XELOGE("BD L4: failed to create native color producer image view");
    dfn.vkDestroyImage(device, native_image, nullptr);
    dfn.vkFreeMemory(device, native_memory, nullptr);
    return base;
  }

  // VK_EXT_custom_resolve path (Turnip): build the 2-subpass producer framebuffer.
  // att0 = the MSAA float16 producer (subpass0 color / subpass1 input); att1 = a
  // 1x A2B10 output (subpass1 custom-resolve color, sampled by the composite);
  // att2 = the MSAA depth. Only for BD's 2_10_10_10(_FLOAT) field.
  xenos::ColorRenderTargetFormat guest_cf = color_rt.key().GetColorFormat();
  bool cr_fmt_ok =
      guest_cf == xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT ||
      guest_cf ==
          xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16 ||
      guest_cf == xenos::ColorRenderTargetFormat::k_2_10_10_10 ||
      guest_cf ==
          xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10;
  if (use_custom_resolve && cr_fmt_ok) {
    const VkFormat kResolveFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    const VkFormat depth_vk_format =
        has_depth ? GetDepthVulkanFormat(depth_rt->key().GetDepthFormat())
                  : VK_FORMAT_UNDEFINED;
    VkRenderPass cr_render_pass = GetBdNativeCustomResolveRenderPass(
        guest_cf, kResolveFormat, render_pass_key.msaa_samples, depth_vk_format);
    // 1x A2B10 resolve output: subpass-1 color + sampled by the composite.
    VkImage cr_resolve_image = VK_NULL_HANDLE;
    VkDeviceMemory cr_resolve_memory = VK_NULL_HANDLE;
    VkImageView cr_resolve_view = VK_NULL_HANDLE;
    if (cr_render_pass != VK_NULL_HANDLE) {
      VkImageCreateInfo ci = image_create_info;
      ci.format = kResolveFormat;
      ci.samples = VK_SAMPLE_COUNT_1_BIT;
      ci.usage =
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      if (ui::vulkan::util::CreateDedicatedAllocationImage(
              vulkan_device, ci, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
              cr_resolve_image, cr_resolve_memory)) {
        VkImageViewCreateInfo vi = view_create_info;
        vi.image = cr_resolve_image;
        vi.format = kResolveFormat;
        if (dfn.vkCreateImageView(device, &vi, nullptr, &cr_resolve_view) !=
            VK_SUCCESS) {
          dfn.vkDestroyImage(device, cr_resolve_image, nullptr);
          dfn.vkFreeMemory(device, cr_resolve_memory, nullptr);
          cr_resolve_image = VK_NULL_HANDLE;
        }
      }
    }
    if (cr_render_pass != VK_NULL_HANDLE && cr_resolve_view != VK_NULL_HANDLE) {
      // Attachment order MUST match GetBdNativeCustomResolveRenderPass:
      // [producer color(0), A2B10 resolve(1), depth(2)].
      VkImageView cr_attachments[3];
      uint32_t cr_count = 0;
      cr_attachments[cr_count++] = native_view;
      cr_attachments[cr_count++] = cr_resolve_view;
      if (has_depth) {
        cr_attachments[cr_count++] = depth_rt->view_depth_stencil();
      }
      VkFramebufferCreateInfo fci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fci.renderPass = cr_render_pass;
      fci.attachmentCount = cr_count;
      fci.pAttachments = cr_attachments;
      fci.width = prod_width;
      fci.height = prod_height;
      fci.layers = 1;
      VkFramebuffer cr_framebuffer = VK_NULL_HANDLE;
      if (dfn.vkCreateFramebuffer(device, &fci, nullptr, &cr_framebuffer) ==
          VK_SUCCESS) {
        Framebuffer& entry = const_cast<Framebuffer&>(*base);
        entry.bd_native_color_image = native_image;
        entry.bd_native_color_memory = native_memory;
        entry.bd_native_color_view = native_view;
        entry.bd_native_color_framebuffer = cr_framebuffer;
        entry.bd_native_color_lle_image = color_rt.image();
        entry.bd_native_color_lle_rt = const_cast<RenderTarget*>(
            depth_and_color_render_targets[color_index]);
        // The composite samples the A2B10 resolve output -> the sampled format is
        // A2B10 (matches fetch fmt 64), NOT the float16 producer.
        entry.bd_native_color_format = kResolveFormat;
        entry.bd_native_color_samples = samples;
        entry.bd_native_color_resolve_image = cr_resolve_image;
        entry.bd_native_color_resolve_memory = cr_resolve_memory;
        entry.bd_native_color_resolve_view = cr_resolve_view;
        entry.bd_native_color_custom_resolve_rp = cr_render_pass;
        entry.bd_native_color_custom_resolve_samples = uint32_t(samples);
        entry.bd_native_color_extent = {prod_width, prod_height};
        XELOGI(
            "BD custom-resolve: producer framebuffer {}x{} (guest fmt={} msaa={} "
            "depth={}) -> A2B10 on-tile resolve",
            prod_width, prod_height, uint32_t(guest_cf),
            uint32_t(samples), has_depth ? 1 : 0);
        return base;
      }
      dfn.vkDestroyFramebuffer(device, cr_framebuffer, nullptr);
    }
    // Fell through (creation failed) -> clean up + drop to the non-CR path below.
    if (cr_resolve_view != VK_NULL_HANDLE) {
      dfn.vkDestroyImageView(device, cr_resolve_view, nullptr);
      dfn.vkDestroyImage(device, cr_resolve_image, nullptr);
      dfn.vkFreeMemory(device, cr_resolve_memory, nullptr);
    }
    XELOGW("BD custom-resolve: producer FB setup failed; using non-CR path");
  }

  // DIRECT-NATIVE on-tile resolve: when the producer is MSAA and we render
  // directly into logical-size RTs (keep_scissor), allocate a 1x resolve image
  // the render pass resolves the color INTO (pResolveAttachments). The composite
  // samples this 1x instead of the dropped EDRAM transfer. The resolve happens at
  // tile-store time on TBDR = no extra pass, no GMEM spill.
  bool build_resolve =
      cvars::gpu_bd_native_keep_scissor && samples != VK_SAMPLE_COUNT_1_BIT;
  VkImage native_resolve_image = VK_NULL_HANDLE;
  VkDeviceMemory native_resolve_memory = VK_NULL_HANDLE;
  VkImageView native_resolve_view = VK_NULL_HANDLE;
  if (build_resolve) {
    VkImageCreateInfo resolve_create_info = image_create_info;
    resolve_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    resolve_create_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!ui::vulkan::util::CreateDedicatedAllocationImage(
            vulkan_device, resolve_create_info,
            ui::vulkan::util::MemoryPurpose::kDeviceLocal, native_resolve_image,
            native_resolve_memory)) {
      XELOGE("BD resolve: failed to create 1x resolve image (falling back)");
      build_resolve = false;
    } else {
      VkImageViewCreateInfo resolve_view_info = view_create_info;
      resolve_view_info.image = native_resolve_image;
      if (dfn.vkCreateImageView(device, &resolve_view_info, nullptr,
                                &native_resolve_view) != VK_SUCCESS) {
        XELOGE("BD resolve: failed to create resolve view (falling back)");
        dfn.vkDestroyImage(device, native_resolve_image, nullptr);
        dfn.vkFreeMemory(device, native_resolve_memory, nullptr);
        native_resolve_image = VK_NULL_HANDLE;
        native_resolve_memory = VK_NULL_HANDLE;
        build_resolve = false;
      }
    }
  }

  // Build the alternate framebuffer: same attachment ORDER as
  // GetHostRenderTargetsFramebuffer (depth first at bit 0 if present, then the
  // single color), but the color attachment view is the native image's view.
  // With build_resolve, the render pass carries a trailing 1x resolve attachment
  // (matching GetHostRenderTargetsRenderPass's bd_color_resolve layout).
  VkRenderPass render_pass =
      GetHostRenderTargetsRenderPass(render_pass_key, 0, false, build_resolve);
  VkImageView attachments[3];
  uint32_t attachment_count = 0;
  if (has_depth) {
    attachments[attachment_count++] = depth_rt->view_depth_stencil();
  }
  attachments[attachment_count++] = native_view;
  if (build_resolve) {
    // Trailing resolve attachment index (bit_count(depth_and_color_used)),
    // matching bd_resolve_first_index in the render pass.
    attachments[attachment_count++] = native_resolve_view;
  }
  VkFramebufferCreateInfo framebuffer_create_info;
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.pNext = nullptr;
  framebuffer_create_info.flags = 0;
  framebuffer_create_info.renderPass = render_pass;
  framebuffer_create_info.attachmentCount = attachment_count;
  framebuffer_create_info.pAttachments = attachments;
  // DIRECT-NATIVE: logical-size framebuffer (matches the logical-size producer
  // image). The depth attachment may be larger (Vulkan permits attachment >
  // framebuffer); the native color is exactly prod_width/height.
  framebuffer_create_info.width = prod_width;
  framebuffer_create_info.height = prod_height;
  framebuffer_create_info.layers = 1;
  VkFramebuffer native_framebuffer = VK_NULL_HANDLE;
  if (render_pass == VK_NULL_HANDLE ||
      dfn.vkCreateFramebuffer(device, &framebuffer_create_info, nullptr,
                              &native_framebuffer) != VK_SUCCESS) {
    XELOGE("BD L4: failed to create native color producer framebuffer");
    if (native_resolve_view != VK_NULL_HANDLE) {
      dfn.vkDestroyImageView(device, native_resolve_view, nullptr);
      dfn.vkDestroyImage(device, native_resolve_image, nullptr);
      dfn.vkFreeMemory(device, native_resolve_memory, nullptr);
    }
    dfn.vkDestroyImageView(device, native_view, nullptr);
    dfn.vkDestroyImage(device, native_image, nullptr);
    dfn.vkFreeMemory(device, native_memory, nullptr);
    return base;
  }

  // Cache on the base entry (const_cast: framebuffers_ owns a mutable entry; the
  // getter returns const for read-only key lookups, but this augments lifetime-
  // managed members). Safe: emplace guarantees a persistent address.
  Framebuffer& entry = const_cast<Framebuffer&>(*base);
  entry.bd_native_color_image = native_image;
  entry.bd_native_color_memory = native_memory;
  entry.bd_native_color_view = native_view;
  entry.bd_native_color_framebuffer = native_framebuffer;
  // Stage 0: the plain single-subpass render pass this native framebuffer is
  // compatible with (the decouple replay begins THIS when there is no CR pass).
  entry.bd_native_color_plain_rp = render_pass;
  entry.bd_native_color_lle_image = color_rt.image();
  entry.bd_native_color_lle_rt =
      const_cast<RenderTarget*>(depth_and_color_render_targets[color_index]);
  entry.bd_native_color_format = color_format;
  entry.bd_native_color_samples = samples;
  // Logical producer extent (the plain replay's renderArea uses it, matching the
  // logical-size framebuffer built above).
  entry.bd_native_color_extent = {prod_width, prod_height};
  entry.bd_native_color_resolve_image = native_resolve_image;
  entry.bd_native_color_resolve_memory = native_resolve_memory;
  entry.bd_native_color_resolve_view = native_resolve_view;
  XELOGI("BD L4: native color producer framebuffer created {}x{} fmt={} depth={}",
         base->host_extent.width, base->host_extent.height,
         uint32_t(color_rt.key().GetColorFormat()), has_depth ? 1 : 0);
  return base;
}

VkImageView VulkanRenderTargetCache::GetBdNativeColorSwizzledView(
    const Framebuffer* fb, uint32_t host_swizzle) {
  if (!fb || fb->bd_native_color_image == VK_NULL_HANDLE ||
      fb->bd_native_color_format == VK_FORMAT_UNDEFINED) {
    return VK_NULL_HANDLE;
  }
  Framebuffer& mfb = const_cast<Framebuffer&>(*fb);
  auto it = mfb.bd_native_color_swizzled_views_.find(host_swizzle);
  if (it != mfb.bd_native_color_swizzled_views_.end()) {
    return it->second;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  // Map the packed guest texture swizzle to per-channel VkComponentSwizzle (same
  // logic as VulkanTexture::GetComponentSwizzle).
  auto component = [](uint32_t swz, uint32_t i) -> VkComponentSwizzle {
    xenos::XE_GPU_TEXTURE_SWIZZLE c =
        xenos::XE_GPU_TEXTURE_SWIZZLE((swz >> (3 * i)) & 0b111);
    if (c == xenos::XE_GPU_TEXTURE_SWIZZLE(i)) {
      return VK_COMPONENT_SWIZZLE_IDENTITY;
    }
    switch (c) {
      case xenos::XE_GPU_TEXTURE_SWIZZLE_R:
        return VK_COMPONENT_SWIZZLE_R;
      case xenos::XE_GPU_TEXTURE_SWIZZLE_G:
        return VK_COMPONENT_SWIZZLE_G;
      case xenos::XE_GPU_TEXTURE_SWIZZLE_B:
        return VK_COMPONENT_SWIZZLE_B;
      case xenos::XE_GPU_TEXTURE_SWIZZLE_A:
        return VK_COMPONENT_SWIZZLE_A;
      case xenos::XE_GPU_TEXTURE_SWIZZLE_0:
        return VK_COMPONENT_SWIZZLE_ZERO;
      case xenos::XE_GPU_TEXTURE_SWIZZLE_1:
        return VK_COMPONENT_SWIZZLE_ONE;
      default:
        return VK_COMPONENT_SWIZZLE_IDENTITY;
    }
  };
  // DIRECT-NATIVE MSAA: the composite samples the 1x RESOLVE image (a normal
  // sampler can't fetch an MSAA image), not the multisampled producer. The
  // resolve carries identical content (pResolveAttachments) in the same format.
  VkImage sample_image = fb->bd_native_color_resolve_image != VK_NULL_HANDLE
                             ? fb->bd_native_color_resolve_image
                             : fb->bd_native_color_image;
  VkImageViewCreateInfo view_create_info = {};
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.image = sample_image;
  // Guest 2D textures translate to OpTypeImage Arrayed=1 -> the sampled view must
  // be a compatible ARRAY view (VUID-07752), even with a single layer (5.6-sol).
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  view_create_info.format = fb->bd_native_color_format;
  view_create_info.components.r = component(host_swizzle, 0);
  view_create_info.components.g = component(host_swizzle, 1);
  view_create_info.components.b = component(host_swizzle, 2);
  view_create_info.components.a = component(host_swizzle, 3);
  view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_create_info.subresourceRange.levelCount = 1;
  view_create_info.subresourceRange.layerCount = 1;
  VkImageView view = VK_NULL_HANDLE;
  if (dfn.vkCreateImageView(device, &view_create_info, nullptr, &view) !=
      VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  mfb.bd_native_color_swizzled_views_.emplace(host_swizzle, view);
  return view;
}

static VkComponentSwizzle BdSwizzleComponent(uint32_t swz, uint32_t i) {
  xenos::XE_GPU_TEXTURE_SWIZZLE c =
      xenos::XE_GPU_TEXTURE_SWIZZLE((swz >> (3 * i)) & 0b111);
  if (c == xenos::XE_GPU_TEXTURE_SWIZZLE(i)) {
    return VK_COMPONENT_SWIZZLE_IDENTITY;
  }
  switch (c) {
    case xenos::XE_GPU_TEXTURE_SWIZZLE_R:
      return VK_COMPONENT_SWIZZLE_R;
    case xenos::XE_GPU_TEXTURE_SWIZZLE_G:
      return VK_COMPONENT_SWIZZLE_G;
    case xenos::XE_GPU_TEXTURE_SWIZZLE_B:
      return VK_COMPONENT_SWIZZLE_B;
    case xenos::XE_GPU_TEXTURE_SWIZZLE_A:
      return VK_COMPONENT_SWIZZLE_A;
    case xenos::XE_GPU_TEXTURE_SWIZZLE_0:
      return VK_COMPONENT_SWIZZLE_ZERO;
    case xenos::XE_GPU_TEXTURE_SWIZZLE_1:
      return VK_COMPONENT_SWIZZLE_ONE;
    default:
      return VK_COMPONENT_SWIZZLE_IDENTITY;
  }
}

const VulkanRenderTargetCache::NativeResolvedTexture*
VulkanRenderTargetCache::PublishBdNativeResolved(
    const Framebuffer* fb, uint32_t dest_base, uint32_t logical_width,
    uint32_t logical_height, VkFormat target_format, float exp_bias_factor,
    uint32_t swap, uint32_t epoch) {
  if (!fb || fb->bd_native_color_image == VK_NULL_HANDLE ||
      fb->bd_native_color_format == VK_FORMAT_UNDEFINED || !dest_base ||
      !logical_width || !logical_height) {
    return nullptr;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  uint32_t w =
      std::min(logical_width * draw_resolution_scale_x(), fb->host_extent.width);
  uint32_t h = std::min(logical_height * draw_resolution_scale_y(),
                        fb->host_extent.height);
  // 5.6-sol path-A blit slice: convert the HDR-float16 producer into the fetch's
  // A2B10 host format via a format-converting vkCmdBlitImage, so the identity-
  // format sampler gate passes and the composite samples native field/bloom.
  // Only a SINGLE-SAMPLED producer whose target differs (MSAA needs a resolve-
  // then-blit two-step, deferred to the next increment; matching/undefined keeps
  // the legacy same-format copy). Approximate: blit does a linear format convert
  // (clamp to [0,1]); exp_bias/swap are NOT applied (gated to 0 by the caller).
  bool convert_wanted = target_format != VK_FORMAT_UNDEFINED &&
                        target_format != fb->bd_native_color_format;
  bool source_1x = fb->bd_native_color_samples == VK_SAMPLE_COUNT_1_BIT;
  bool exp_bias_is_identity = exp_bias_factor == 1.0f;
  // 1x + no exp_bias -> a format-converting vkCmdBlitImage (the bloom slice); the
  // blit can't scale (exp_bias) and doesn't swap (swap baked into the view).
  bool do_convert_blit = convert_wanted && source_1x && exp_bias_is_identity;
  // MSAA or a non-identity exp_bias (the field, exp_bias=-2) -> the fragment
  // convert pass (average + exp_bias + swap, all in the shader). Gated: this path
  // is PROVEN perf-dead on Turnip (1.8fps - MSAA spill + added pass); set
  // gpu_bd_native_field_convert=false to isolate the bloom-only deletion.
  bool do_convert_shader =
      convert_wanted && !do_convert_blit &&
      (!source_1x || !exp_bias_is_identity) &&
      cvars::gpu_bd_native_field_convert;
  bool do_convert = do_convert_blit || do_convert_shader;
  VkFormat t_format = do_convert ? target_format : fb->bd_native_color_format;
  uint32_t source_sample_count =
      uint32_t(fb->bd_native_color_samples) == 0
          ? 1
          : uint32_t(fb->bd_native_color_samples);
  {
    // Diagnostic (throttled): prove the blit-convert actually fires (vs silently
    // falling back to LLE). Logs the dest, producer format, target, samples, and
    // the do_convert decision.
    static std::atomic<uint32_t> s_l9c{0};
    if (s_l9c.fetch_add(1) < 40) {
      XELOGI("L9 CONVERT? dest={:08X} prodfmt={} target={} samples={} convert={}",
             dest_base, uint32_t(fb->bd_native_color_format),
             uint32_t(target_format), uint32_t(fb->bd_native_color_samples),
             do_convert ? 1 : 0);
    }
  }
  NativeResolvedTexture& t = bd_native_resolved_[dest_base];
  // Create the snapshot ONCE and never destroy it mid-frame - a previous frame's
  // in-flight submission may still be sampling/presenting it, and destroying an
  // in-flight image is a non-deterministic device-lost.
  // CONFLICT SKIP (the smear fix): if a snapshot already exists for this dest but
  // THIS producer has a different geometry/format, DON'T copy - a second producer
  // aliasing the same guest dest, or a SCALING resolve (e.g. a 1280-wide P
  // resolving into a 360-wide texture) - copying the wrong region (the left 360
  // cols of the 1280 P) is exactly the smear. Keep the existing correct snapshot;
  // the conflicting consumer falls back to LLE. (Identity 1:1 resolves only, per
  // 5.6-sol's first slice; scaling/format-convert resolves need the Resolve hook.)
  if (t.image != VK_NULL_HANDLE &&
      (t.format != t_format || t.width != w || t.height != h)) {
    return &t;
  }
  if (t.image == VK_NULL_HANDLE) {
    t = NativeResolvedTexture{};
    VkImageCreateInfo ici = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = t_format;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    // The shader convert path renders into T (COLOR_ATTACHMENT); the blit/copy
    // path writes it via TRANSFER_DST. Always SAMPLED (the composite reads it).
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                (do_convert_shader ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                                   : VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!ui::vulkan::util::CreateDedicatedAllocationImage(
            vulkan_device, ici, ui::vulkan::util::MemoryPurpose::kDeviceLocal,
            t.image, t.memory)) {
      bd_native_resolved_.erase(dest_base);
      return nullptr;
    }
    VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = t_format;
    // Bake copy_dest_swap's R<->B exchange into the view ONLY for the BLIT path
    // (the blit did not apply swap). The SHADER path bakes swap itself, so its
    // view (used both as the render attachment AND sampled) stays identity. The
    // same-format copy path also keeps identity.
    vci.components.r = do_convert_blit ? VK_COMPONENT_SWIZZLE_B
                                       : VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.components.b = do_convert_blit ? VK_COMPONENT_SWIZZLE_R
                                       : VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    if (dfn.vkCreateImageView(device, &vci, nullptr, &t.identity_view) !=
        VK_SUCCESS) {
      dfn.vkDestroyImage(device, t.image, nullptr);
      dfn.vkFreeMemory(device, t.memory, nullptr);
      bd_native_resolved_.erase(dest_base);
      return nullptr;
    }
    t.format = t_format;
    t.convert_rb_swap = do_convert_blit;  // shader path bakes swap itself
    t.width = w;
    t.height = h;
    t.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    t.stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    t.access = 0;
  }
  // SHADER convert path (MSAA / exp_bias): render the convert pass into T,
  // sampling the producer, applying average + exp_bias + swap. Skips the
  // transfer-copy path below. On failure, the snapshot keeps its prior content
  // (or is unfilled on first creation - fail-closed via the sampler epoch check).
  if (do_convert_shader) {
    if (ConvertBdNativeMsaaToResolved(fb, t, w, h, exp_bias_factor, swap,
                                      source_sample_count)) {
      t.publish_epoch = epoch;
    }
    return &t;
  }
  // Copy the producer's valid [0,logical] rect into the frozen snapshot.
  VkImageSubresourceRange range = {};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.levelCount = 1;
  range.layerCount = 1;
  Framebuffer& mfb = const_cast<Framebuffer&>(*fb);
  // CONSERVATIVE: wait for ALL prior work on the shared producer image P (it is
  // written by the field draws + read by the mirror, and may be re-begun right
  // after this resolve) before reading it, and make T's write broadly visible.
  command_processor_.PushImageMemoryBarrier(
      fb->bd_native_color_image, range, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
      VK_ACCESS_TRANSFER_READ_BIT, mfb.bd_native_color_layout,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  command_processor_.PushImageMemoryBarrier(
      t.image, range, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT, t.layout,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  {
    static std::atomic<uint32_t> s_l7{0};
    if (s_l7.fetch_add(1) < 40) {
      XELOGI("L7 RESOLVE dest={:08X} fmt={} samples={} P={}x{} -> T={}x{}",
             dest_base, uint32_t(fb->bd_native_color_format),
             uint32_t(fb->bd_native_color_samples), fb->host_extent.width,
             fb->host_extent.height, w, h);
    }
  }
  // ORDERING FIX (5.6-sol path-A review): PushImageMemoryBarrier only QUEUES;
  // SubmitBarriers is what records the vkCmdPipelineBarrier into the deferred
  // command buffer. The pre-copy transitions (P->TRANSFER_SRC, T->TRANSFER_DST)
  // MUST be recorded BEFORE the copy/resolve reads P and writes T - otherwise the
  // single SubmitBarriers below emitted them AFTER the copy, so the copy ran
  // against the wrong layouts (tolerated on desktop, undefined on strict Turnip).
  // Flush the two pre-copy barriers here; the post-copy SHADER_READ barrier is
  // pushed + submitted after the copy as before.
  command_processor_.SubmitBarriers(true);
  if (do_convert) {
    // FORMAT-CONVERTING BLIT (5.6-sol path-A slice): float16 HDR producer ->
    // A2B10 T. vkCmdBlitImage does a linear texel format convert (read float,
    // clamp, write UNORM). Single-sampled only (guaranteed by do_convert). Same
    // extent, NEAREST (no scaling here - T is logical-size). Approximate: exp_bias
    // and swap are not applied (caller gates them to 0).
    VkImageBlit breg = {};
    breg.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    breg.srcSubresource.layerCount = 1;
    breg.srcOffsets[1] = {int32_t(w), int32_t(h), 1};
    breg.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    breg.dstSubresource.layerCount = 1;
    breg.dstOffsets[1] = {int32_t(w), int32_t(h), 1};
    command_processor_.deferred_command_buffer().CmdVkBlitImage(
        fb->bd_native_color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &breg, VK_FILTER_NEAREST);
  } else if (fb->bd_native_color_samples != VK_SAMPLE_COUNT_1_BIT) {
    // MSAA producer (e.g. 2x foliage) -> single-sample snapshot: RESOLVE, not
    // copy (vkCmdCopyImage between different sample counts is invalid = device
    // lost). The snapshot is what the composite samples (a resolved texture).
    VkImageResolve rreg = {};
    rreg.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rreg.srcSubresource.layerCount = 1;
    rreg.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rreg.dstSubresource.layerCount = 1;
    rreg.extent = {w, h, 1};
    command_processor_.deferred_command_buffer().CmdVkResolveImage(
        fb->bd_native_color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rreg);
  } else {
    VkImageCopy creg = {};
    creg.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    creg.srcSubresource.layerCount = 1;
    creg.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    creg.dstSubresource.layerCount = 1;
    creg.extent = {w, h, 1};
    command_processor_.deferred_command_buffer().CmdVkCopyImage(
        fb->bd_native_color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &creg);
  }
  command_processor_.PushImageMemoryBarrier(
      t.image, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  command_processor_.SubmitBarriers(true);
  t.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  t.stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  t.access = VK_ACCESS_SHADER_READ_BIT;
  t.publish_epoch = epoch;
  // The producer native was just read (TRANSFER_SRC) - update its tracking so
  // the next seed/mirror barriers from the real state.
  mfb.bd_native_color_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  mfb.bd_native_color_access = VK_ACCESS_TRANSFER_READ_BIT;
  mfb.bd_native_color_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  return &t;
}

VkImageView VulkanRenderTargetCache::GetBdNativeResolvedSwizzledView(
    const NativeResolvedTexture* t, uint32_t host_swizzle) {
  if (!t || t->image == VK_NULL_HANDLE) {
    return VK_NULL_HANDLE;
  }
  if (host_swizzle == uint32_t(xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA)) {
    return t->identity_view;
  }
  NativeResolvedTexture& mt = const_cast<NativeResolvedTexture&>(*t);
  auto it = mt.swizzled_views.find(host_swizzle);
  if (it != mt.swizzled_views.end()) {
    return it->second;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = t->image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = t->format;
  VkComponentSwizzle comp[4] = {BdSwizzleComponent(host_swizzle, 0),
                                BdSwizzleComponent(host_swizzle, 1),
                                BdSwizzleComponent(host_swizzle, 2),
                                BdSwizzleComponent(host_swizzle, 3)};
  if (t->convert_rb_swap) {
    // Compose copy_dest_swap's R<->B exchange into the fetch swizzle (5.6-sol:
    // "replace every R selected by S with B, and vice versa"). Resolve IDENTITY
    // to the explicit channel for its position first so the swap applies.
    static const VkComponentSwizzle kIdentityChannel[4] = {
        VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A};
    for (uint32_t i = 0; i < 4; ++i) {
      VkComponentSwizzle c = comp[i] == VK_COMPONENT_SWIZZLE_IDENTITY
                                 ? kIdentityChannel[i]
                                 : comp[i];
      if (c == VK_COMPONENT_SWIZZLE_R) {
        c = VK_COMPONENT_SWIZZLE_B;
      } else if (c == VK_COMPONENT_SWIZZLE_B) {
        c = VK_COMPONENT_SWIZZLE_R;
      }
      comp[i] = c;
    }
  }
  vci.components.r = comp[0];
  vci.components.g = comp[1];
  vci.components.b = comp[2];
  vci.components.a = comp[3];
  vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  vci.subresourceRange.levelCount = 1;
  vci.subresourceRange.layerCount = 1;
  VkImageView view = VK_NULL_HANDLE;
  if (dfn.vkCreateImageView(device, &vci, nullptr, &view) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  mt.swizzled_views.emplace(host_swizzle, view);
  return view;
}

VkShaderModule VulkanRenderTargetCache::GetBdNativeConvertShader(
    uint32_t source_sample_count) {
  auto it = bd_convert_shaders_.find(source_sample_count);
  if (it != bd_convert_shaders_.end()) {
    return it->second;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  bool source_msaa = source_sample_count > 1;

  SpirvBuilder builder(spv::Spv_1_0,
                       (SpirvShaderTranslator::kSpirvMagicToolId << 16) | 1,
                       nullptr);
  builder.addCapability(spv::CapabilityShader);
  builder.setMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
  builder.setSource(spv::SourceLanguageUnknown, 0);

  spv::Id type_void = builder.makeVoidType();
  spv::Id type_bool = builder.makeBoolType();
  spv::Id type_int = builder.makeIntType(32);
  spv::Id type_int2 = builder.makeVectorType(type_int, 2);
  spv::Id type_uint = builder.makeUintType(32);
  spv::Id type_float = builder.makeFloatType(32);
  spv::Id type_float2 = builder.makeVectorType(type_float, 2);
  spv::Id type_float4 = builder.makeVectorType(type_float, 4);

  // Sampled float image (set 0, binding 0). MS or 2D depending on the source.
  spv::Id type_image = builder.makeImageType(
      type_float, spv::Dim2D, false, false, source_msaa, 1,
      spv::ImageFormatUnknown);
  spv::Id source_image = builder.createVariable(
      spv::NoPrecision, spv::StorageClassUniformConstant, type_image,
      "xe_bd_convert_source");
  builder.addDecoration(source_image, spv::DecorationDescriptorSet, 0);
  builder.addDecoration(source_image, spv::DecorationBinding, 0);

  // Push constants: { float exp_bias_factor; uint swap; uint sample_count; }.
  std::vector<spv::Id> pc_members;
  pc_members.push_back(type_float);
  pc_members.push_back(type_uint);
  pc_members.push_back(type_uint);
  spv::Id type_pc = builder.makeStructType(pc_members, "xe_bd_convert_pc");
  builder.addMemberDecoration(type_pc, 0, spv::DecorationOffset, 0);
  builder.addMemberDecoration(type_pc, 1, spv::DecorationOffset, 4);
  builder.addMemberDecoration(type_pc, 2, spv::DecorationOffset, 8);
  builder.addDecoration(type_pc, spv::DecorationBlock);
  spv::Id push_constants =
      builder.createVariable(spv::NoPrecision, spv::StorageClassPushConstant,
                             type_pc, "xe_bd_convert_push_constants");

  // gl_FragCoord input.
  spv::Id input_fragment_coord = builder.createVariable(
      spv::NoPrecision, spv::StorageClassInput, type_float4, "gl_FragCoord");
  builder.addDecoration(input_fragment_coord, spv::DecorationBuiltIn,
                        spv::BuiltInFragCoord);

  // Output color (location 0).
  spv::Id output_color = builder.createVariable(
      spv::NoPrecision, spv::StorageClassOutput, type_float4, "xe_frag_color");
  builder.addDecoration(output_color, spv::DecorationLocation, 0);

  std::vector<spv::Id> main_interface;
  main_interface.push_back(input_fragment_coord);
  main_interface.push_back(output_color);

  std::vector<spv::Id> main_param_types;
  std::vector<std::vector<spv::Decoration>> main_precisions;
  spv::Block* main_entry;
  spv::Function* main_function =
      builder.makeFunctionEntry(spv::NoPrecision, type_void, "main",
                                main_param_types, main_precisions, &main_entry);

  // ivec2 p = ivec2(gl_FragCoord.xy);
  std::vector<unsigned int> xy_swizzle;
  xy_swizzle.push_back(0);
  xy_swizzle.push_back(1);
  spv::Id frag_xy = builder.createRvalueSwizzle(
      spv::NoPrecision, type_float2,
      builder.createLoad(input_fragment_coord, spv::NoPrecision), xy_swizzle);
  spv::Id coord_int = builder.createUnaryOp(spv::OpConvertFToS, type_int2,
                                            frag_xy);

  // Fetch + average the selected samples.
  spv::Id image_value = builder.createLoad(source_image, spv::NoPrecision);
  spv::Builder::TextureParameters tp = {};
  tp.sampler = image_value;
  tp.coords = coord_int;
  spv::Id color;
  if (source_msaa) {
    color = spv::NoResult;
    for (uint32_t s = 0; s < source_sample_count; ++s) {
      tp.sample = builder.makeIntConstant(int32_t(s));
      spv::Id sample_color = builder.createTextureCall(
          spv::NoPrecision, type_float4, false, true, false, false, false, tp,
          spv::ImageOperandsMaskNone);
      color = color == spv::NoResult
                  ? sample_color
                  : builder.createBinOp(spv::OpFAdd, type_float4, color,
                                        sample_color);
    }
    spv::Id inv_count =
        builder.makeFloatConstant(1.0f / float(source_sample_count));
    color = builder.createBinOp(
        spv::OpVectorTimesScalar, type_float4, color, inv_count);
  } else {
    tp.sample = spv::NoResult;
    color = builder.createTextureCall(spv::NoPrecision, type_float4, false, true,
                                      false, false, false, tp,
                                      spv::ImageOperandsMaskNone);
  }

  // color *= exp_bias_factor (push constant member 0).
  std::vector<spv::Id> pc_index0;
  pc_index0.push_back(builder.makeIntConstant(0));
  spv::Id exp_bias = builder.createLoad(
      builder.createAccessChain(spv::StorageClassPushConstant, push_constants,
                                pc_index0),
      spv::NoPrecision);
  color = builder.createBinOp(spv::OpVectorTimesScalar, type_float4, color,
                              exp_bias);

  // if (swap != 0) color = color.bgra;
  std::vector<spv::Id> pc_index1;
  pc_index1.push_back(builder.makeIntConstant(1));
  spv::Id swap = builder.createLoad(
      builder.createAccessChain(spv::StorageClassPushConstant, push_constants,
                                pc_index1),
      spv::NoPrecision);
  spv::Id swap_bool = builder.createBinOp(spv::OpINotEqual, type_bool, swap,
                                          builder.makeUintConstant(0));
  std::vector<unsigned int> bgra;
  bgra.push_back(2);
  bgra.push_back(1);
  bgra.push_back(0);
  bgra.push_back(3);
  spv::Id color_bgra =
      builder.createRvalueSwizzle(spv::NoPrecision, type_float4, color, bgra);
  color = builder.createTriOp(spv::OpSelect, type_float4, swap_bool, color_bgra,
                              color);

  builder.createStore(color, output_color);

  builder.leaveFunction();
  builder.addExecutionMode(main_function, spv::ExecutionModeOriginUpperLeft);
  spv::Instruction* entry_point = builder.addEntryPoint(
      spv::ExecutionModelFragment, main_function, "main");
  for (spv::Id interface_id : main_interface) {
    entry_point->addIdOperand(interface_id);
  }

  std::vector<unsigned int> shader_code;
  builder.dump(shader_code);
  VkShaderModule shader_module = ui::vulkan::util::CreateShaderModule(
      vulkan_device, reinterpret_cast<const uint32_t*>(shader_code.data()),
      sizeof(uint32_t) * shader_code.size());
  bd_convert_shaders_.emplace(source_sample_count, shader_module);
  return shader_module;
}

VkShaderModule VulkanRenderTargetCache::GetBdNativeCustomResolveShader(
    uint32_t source_sample_count) {
  auto it = bd_custom_resolve_shaders_.find(source_sample_count);
  if (it != bd_custom_resolve_shaders_.end()) {
    return it->second;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  bool source_msaa = source_sample_count > 1;

  SpirvBuilder builder(spv::Spv_1_0,
                       (SpirvShaderTranslator::kSpirvMagicToolId << 16) | 1,
                       nullptr);
  builder.addCapability(spv::CapabilityShader);
  // Reading an MSAA input attachment requires the InputAttachment capability.
  builder.addCapability(spv::CapabilityInputAttachment);
  builder.setMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
  builder.setSource(spv::SourceLanguageUnknown, 0);

  spv::Id type_void = builder.makeVoidType();
  spv::Id type_bool = builder.makeBoolType();
  spv::Id type_int = builder.makeIntType(32);
  spv::Id type_int2 = builder.makeVectorType(type_int, 2);
  spv::Id type_uint = builder.makeUintType(32);
  spv::Id type_float = builder.makeFloatType(32);
  spv::Id type_float4 = builder.makeVectorType(type_float, 4);

  // Input attachment (set 0, binding 0, input_attachment_index 0). Dim=SubpassData,
  // MS iff the producer is multisampled - read per sample with subpassLoad.
  spv::Id type_image = builder.makeImageType(
      type_float, spv::DimSubpassData, false, false, source_msaa, 2,
      spv::ImageFormatUnknown);
  spv::Id source_image = builder.createVariable(
      spv::NoPrecision, spv::StorageClassUniformConstant, type_image,
      "xe_bd_custom_resolve_source");
  builder.addDecoration(source_image, spv::DecorationDescriptorSet, 0);
  builder.addDecoration(source_image, spv::DecorationBinding, 0);
  builder.addDecoration(source_image, spv::DecorationInputAttachmentIndex, 0);

  // Push constants: { float exp_bias_factor; uint swap; uint sample_count; }.
  std::vector<spv::Id> pc_members;
  pc_members.push_back(type_float);
  pc_members.push_back(type_uint);
  pc_members.push_back(type_uint);
  spv::Id type_pc =
      builder.makeStructType(pc_members, "xe_bd_custom_resolve_pc");
  builder.addMemberDecoration(type_pc, 0, spv::DecorationOffset, 0);
  builder.addMemberDecoration(type_pc, 1, spv::DecorationOffset, 4);
  builder.addMemberDecoration(type_pc, 2, spv::DecorationOffset, 8);
  builder.addDecoration(type_pc, spv::DecorationBlock);
  spv::Id push_constants = builder.createVariable(
      spv::NoPrecision, spv::StorageClassPushConstant, type_pc,
      "xe_bd_custom_resolve_push_constants");

  // Output color (location 0) - the ROP packs float4 -> the A2B10 attachment.
  spv::Id output_color = builder.createVariable(
      spv::NoPrecision, spv::StorageClassOutput, type_float4, "xe_frag_color");
  builder.addDecoration(output_color, spv::DecorationLocation, 0);

  std::vector<spv::Id> main_interface;
  main_interface.push_back(source_image);
  main_interface.push_back(output_color);

  std::vector<spv::Id> main_param_types;
  std::vector<std::vector<spv::Decoration>> main_precisions;
  spv::Block* main_entry;
  spv::Function* main_function =
      builder.makeFunctionEntry(spv::NoPrecision, type_void, "main",
                                main_param_types, main_precisions, &main_entry);

  // subpassLoad uses the fragment's own location; the SPIR-V coordinate is the
  // canonical constant ivec2(0, 0).
  spv::Id const_i0 = builder.makeIntConstant(0);
  std::vector<spv::Id> coord_components;
  coord_components.push_back(const_i0);
  coord_components.push_back(const_i0);
  spv::Id subpass_coord =
      builder.makeCompositeConstant(type_int2, coord_components);

  spv::Id image_value = builder.createLoad(source_image, spv::NoPrecision);

  // Read + average the samples (Xenos k0123 resolve = average all; matches the
  // EDRAM copy for BD's field). 1x = a single subpassLoad.
  spv::Id color = spv::NoResult;
  for (uint32_t s = 0; s < source_sample_count; ++s) {
    std::unique_ptr<spv::Instruction> read_op =
        std::make_unique<spv::Instruction>(builder.getUniqueId(), type_float4,
                                           spv::OpImageRead);
    read_op->addIdOperand(image_value);
    read_op->addIdOperand(subpass_coord);
    if (source_msaa) {
      // ImageOperands = Sample, followed by the sample index id.
      read_op->addImmediateOperand(spv::ImageOperandsSampleMask);
      read_op->addIdOperand(builder.makeIntConstant(int32_t(s)));
    }
    spv::Id sample_color = read_op->getResultId();
    builder.getBuildPoint()->addInstruction(std::move(read_op));
    color = color == spv::NoResult
                ? sample_color
                : builder.createBinOp(spv::OpFAdd, type_float4, color,
                                      sample_color);
  }
  if (source_sample_count > 1) {
    spv::Id inv_count =
        builder.makeFloatConstant(1.0f / float(source_sample_count));
    color = builder.createBinOp(spv::OpVectorTimesScalar, type_float4, color,
                                inv_count);
  }

  // color *= exp_bias_factor (push constant member 0).
  std::vector<spv::Id> pc_index0;
  pc_index0.push_back(builder.makeIntConstant(0));
  spv::Id exp_bias = builder.createLoad(
      builder.createAccessChain(spv::StorageClassPushConstant, push_constants,
                                pc_index0),
      spv::NoPrecision);
  color = builder.createBinOp(spv::OpVectorTimesScalar, type_float4, color,
                              exp_bias);

  // if (swap != 0) color = color.bgra;
  std::vector<spv::Id> pc_index1;
  pc_index1.push_back(builder.makeIntConstant(1));
  spv::Id swap = builder.createLoad(
      builder.createAccessChain(spv::StorageClassPushConstant, push_constants,
                                pc_index1),
      spv::NoPrecision);
  spv::Id swap_bool = builder.createBinOp(spv::OpINotEqual, type_bool, swap,
                                          builder.makeUintConstant(0));
  std::vector<unsigned int> bgra;
  bgra.push_back(2);
  bgra.push_back(1);
  bgra.push_back(0);
  bgra.push_back(3);
  spv::Id color_bgra =
      builder.createRvalueSwizzle(spv::NoPrecision, type_float4, color, bgra);
  color = builder.createTriOp(spv::OpSelect, type_float4, swap_bool, color_bgra,
                              color);

  builder.createStore(color, output_color);

  builder.leaveFunction();
  builder.addExecutionMode(main_function, spv::ExecutionModeOriginUpperLeft);
  spv::Instruction* entry_point = builder.addEntryPoint(
      spv::ExecutionModelFragment, main_function, "main");
  for (spv::Id interface_id : main_interface) {
    entry_point->addIdOperand(interface_id);
  }

  std::vector<unsigned int> shader_code;
  builder.dump(shader_code);
  VkShaderModule shader_module = ui::vulkan::util::CreateShaderModule(
      vulkan_device, reinterpret_cast<const uint32_t*>(shader_code.data()),
      sizeof(uint32_t) * shader_code.size());
  bd_custom_resolve_shaders_.emplace(source_sample_count, shader_module);
  return shader_module;
}

bool VulkanRenderTargetCache::GetBdNativeConvertPipeline(
    VkFormat dest_format, uint32_t source_sample_count,
    VkPipeline& pipeline_out, VkRenderPass& render_pass_out,
    VkPipelineLayout& layout_out) {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // Pipeline layout (once): set 0 = one sampled image, + the convert push range.
  if (bd_convert_pipeline_layout_ == VK_NULL_HANDLE) {
    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(BdConvertPushConstants);
    VkPipelineLayoutCreateInfo plci = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descriptor_set_layout_sampled_image_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &push_range;
    if (dfn.vkCreatePipelineLayout(device, &plci, nullptr,
                                   &bd_convert_pipeline_layout_) != VK_SUCCESS) {
      return false;
    }
  }
  layout_out = bd_convert_pipeline_layout_;

  // Render pass (per dest format): one color attachment, leaves T SHADER_READ.
  auto rp_it = bd_convert_render_passes_.find(uint32_t(dest_format));
  VkRenderPass render_pass;
  if (rp_it != bd_convert_render_passes_.end()) {
    render_pass = rp_it->second;
  } else {
    VkAttachmentDescription attachment = {};
    attachment.format = dest_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    // Dependency so the color write is visible to the later composite's sample
    // (the render pass finalLayout transition alone doesn't order the memory).
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &attachment;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dependency;
    if (dfn.vkCreateRenderPass(device, &rpci, nullptr, &render_pass) !=
        VK_SUCCESS) {
      return false;
    }
    bd_convert_render_passes_.emplace(uint32_t(dest_format), render_pass);
  }
  render_pass_out = render_pass;

  // Pipeline (per dest format + source sample count).
  uint64_t pipe_key = (uint64_t(dest_format) << 8) | source_sample_count;
  auto pipe_it = bd_convert_pipelines_.find(pipe_key);
  if (pipe_it != bd_convert_pipelines_.end()) {
    pipeline_out = pipe_it->second;
    return pipeline_out != VK_NULL_HANDLE;
  }
  VkShaderModule fs = GetBdNativeConvertShader(source_sample_count);
  if (fs == VK_NULL_HANDLE ||
      transfer_passthrough_vertex_shader_ == VK_NULL_HANDLE) {
    bd_convert_pipelines_.emplace(pipe_key, VK_NULL_HANDLE);
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = transfer_passthrough_vertex_shader_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";

  VkVertexInputBindingDescription vertex_binding = {};
  vertex_binding.binding = 0;
  vertex_binding.stride = sizeof(float) * 2;
  vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription vertex_attribute = {};
  vertex_attribute.location = 0;
  vertex_attribute.binding = 0;
  vertex_attribute.format = VK_FORMAT_R32G32_SFLOAT;
  vertex_attribute.offset = 0;
  VkPipelineVertexInputStateCreateInfo vertex_input = {
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &vertex_binding;
  vertex_input.vertexAttributeDescriptionCount = 1;
  vertex_input.pVertexAttributeDescriptions = &vertex_attribute;

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state = {
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterization = {
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization.cullMode = VK_CULL_MODE_NONE;
  rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample = {
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend_attachment = {};
  blend_attachment.blendEnable = VK_FALSE;
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo color_blend = {
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pci = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pci.stageCount = 2;
  pci.pStages = stages;
  pci.pVertexInputState = &vertex_input;
  pci.pInputAssemblyState = &input_assembly;
  pci.pViewportState = &viewport_state;
  pci.pRasterizationState = &rasterization;
  pci.pMultisampleState = &multisample;
  pci.pColorBlendState = &color_blend;
  pci.pDynamicState = &dynamic_state;
  pci.layout = bd_convert_pipeline_layout_;
  pci.renderPass = render_pass;
  pci.subpass = 0;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (dfn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                    &pipeline) != VK_SUCCESS) {
    bd_convert_pipelines_.emplace(pipe_key, VK_NULL_HANDLE);
    return false;
  }
  bd_convert_pipelines_.emplace(pipe_key, pipeline);
  pipeline_out = pipeline;
  return true;
}

bool VulkanRenderTargetCache::GetBdNativeCustomResolvePipeline(
    VkRenderPass custom_resolve_render_pass, VkFormat resolve_format,
    uint32_t source_sample_count, VkPipeline& pipeline_out,
    VkPipelineLayout& layout_out) {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  if (custom_resolve_render_pass == VK_NULL_HANDLE) {
    return false;
  }

  // Descriptor set layout (once): set 0 binding 0 = one INPUT_ATTACHMENT.
  if (bd_custom_resolve_ia_set_layout_ == VK_NULL_HANDLE) {
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslci = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings = &binding;
    if (dfn.vkCreateDescriptorSetLayout(device, &dslci, nullptr,
                                        &bd_custom_resolve_ia_set_layout_) !=
        VK_SUCCESS) {
      return false;
    }
  }
  // Pipeline layout (once): the input-attachment set + the convert push range.
  if (bd_custom_resolve_pipeline_layout_ == VK_NULL_HANDLE) {
    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(BdConvertPushConstants);
    VkPipelineLayoutCreateInfo plci = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &bd_custom_resolve_ia_set_layout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &push_range;
    if (dfn.vkCreatePipelineLayout(device, &plci, nullptr,
                                   &bd_custom_resolve_pipeline_layout_) !=
        VK_SUCCESS) {
      return false;
    }
  }
  layout_out = bd_custom_resolve_pipeline_layout_;

  uint64_t pipe_key = (uint64_t(reinterpret_cast<uintptr_t>(
                          custom_resolve_render_pass))) ^
                      (uint64_t(source_sample_count) << 1);
  auto pipe_it = bd_custom_resolve_pipelines_.find(pipe_key);
  if (pipe_it != bd_custom_resolve_pipelines_.end()) {
    pipeline_out = pipe_it->second;
    return pipeline_out != VK_NULL_HANDLE;
  }
  VkShaderModule fs = GetBdNativeCustomResolveShader(source_sample_count);
  if (fs == VK_NULL_HANDLE ||
      transfer_passthrough_vertex_shader_ == VK_NULL_HANDLE) {
    bd_custom_resolve_pipelines_.emplace(pipe_key, VK_NULL_HANDLE);
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = transfer_passthrough_vertex_shader_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs;
  stages[1].pName = "main";

  VkVertexInputBindingDescription vertex_binding = {};
  vertex_binding.binding = 0;
  vertex_binding.stride = sizeof(float) * 2;
  vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription vertex_attribute = {};
  vertex_attribute.location = 0;
  vertex_attribute.binding = 0;
  vertex_attribute.format = VK_FORMAT_R32G32_SFLOAT;
  vertex_attribute.offset = 0;
  VkPipelineVertexInputStateCreateInfo vertex_input = {
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &vertex_binding;
  vertex_input.vertexAttributeDescriptionCount = 1;
  vertex_input.pVertexAttributeDescriptions = &vertex_attribute;

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewport_state = {
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterization = {
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization.cullMode = VK_CULL_MODE_NONE;
  rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization.lineWidth = 1.0f;

  // Subpass 1's color output is the 1x resolve attachment.
  VkPipelineMultisampleStateCreateInfo multisample = {
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend_attachment = {};
  blend_attachment.blendEnable = VK_FALSE;
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo color_blend = {
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend_attachment;

  VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state = {
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  // Chain VkCustomResolveCreateInfoEXT (fragment-output stage) - marks this
  // pipeline as a custom-resolve pipeline writing the given resolve format.
  VkCustomResolveCreateInfoEXT custom_resolve_info = {};
  custom_resolve_info.sType = VK_STRUCTURE_TYPE_CUSTOM_RESOLVE_CREATE_INFO_EXT;
  custom_resolve_info.customResolve = VK_TRUE;
  custom_resolve_info.colorAttachmentCount = 1;
  custom_resolve_info.pColorAttachmentFormats = &resolve_format;
  custom_resolve_info.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
  custom_resolve_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

  VkGraphicsPipelineCreateInfo pci = {
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pci.pNext = &custom_resolve_info;
  pci.stageCount = 2;
  pci.pStages = stages;
  pci.pVertexInputState = &vertex_input;
  pci.pInputAssemblyState = &input_assembly;
  pci.pViewportState = &viewport_state;
  pci.pRasterizationState = &rasterization;
  pci.pMultisampleState = &multisample;
  pci.pColorBlendState = &color_blend;
  pci.pDynamicState = &dynamic_state;
  pci.layout = bd_custom_resolve_pipeline_layout_;
  pci.renderPass = custom_resolve_render_pass;
  pci.subpass = 1;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (dfn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                    &pipeline) != VK_SUCCESS) {
    XELOGE("BD custom-resolve: failed to create the subpass-1 convert pipeline");
    bd_custom_resolve_pipelines_.emplace(pipe_key, VK_NULL_HANDLE);
    return false;
  }
  bd_custom_resolve_pipelines_.emplace(pipe_key, pipeline);
  pipeline_out = pipeline;
  return true;
}

bool VulkanRenderTargetCache::ConvertBdNativeMsaaToResolved(
    const Framebuffer* fb, NativeResolvedTexture& t, uint32_t w, uint32_t h,
    float exp_bias_factor, uint32_t swap, uint32_t sample_count) {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();

  VkPipeline pipeline = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  if (!GetBdNativeConvertPipeline(t.format, sample_count, pipeline, render_pass,
                                  layout)) {
    return false;
  }

  // Descriptor pool (once).
  if (bd_convert_descriptor_pool_ == VK_NULL_HANDLE) {
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64};
    VkDescriptorPoolCreateInfo dpci = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 64;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &pool_size;
    if (dfn.vkCreateDescriptorPool(device, &dpci, nullptr,
                                   &bd_convert_descriptor_pool_) != VK_SUCCESS) {
      return false;
    }
  }
  // Descriptor set (create-once per T), binding the producer's MSAA sampled view.
  if (t.convert_descriptor_set == VK_NULL_HANDLE) {
    VkDescriptorSetAllocateInfo dsai = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = bd_convert_descriptor_pool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &descriptor_set_layout_sampled_image_;
    if (dfn.vkAllocateDescriptorSets(device, &dsai,
                                     &t.convert_descriptor_set) != VK_SUCCESS) {
      t.convert_descriptor_set = VK_NULL_HANDLE;
      return false;
    }
    VkDescriptorImageInfo image_info = {};
    image_info.sampler = VK_NULL_HANDLE;
    image_info.imageView = fb->bd_native_color_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = t.convert_descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image_info;
    dfn.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  // Framebuffer (create-once) wrapping T as the A2B10 color attachment.
  if (t.convert_framebuffer == VK_NULL_HANDLE) {
    VkFramebufferCreateInfo fbci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbci.renderPass = render_pass;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &t.identity_view;
    fbci.width = w;
    fbci.height = h;
    fbci.layers = 1;
    if (dfn.vkCreateFramebuffer(device, &fbci, nullptr,
                                &t.convert_framebuffer) != VK_SUCCESS) {
      t.convert_framebuffer = VK_NULL_HANDLE;
      return false;
    }
  }

  // Fullscreen quad (two triangles covering NDC [-1,1]).
  uint64_t submission = command_processor_.GetCurrentSubmission();
  VkBuffer vertex_buffer = VK_NULL_HANDLE;
  VkDeviceSize vertex_offset = 0;
  float* verts = reinterpret_cast<float*>(transfer_vertex_buffer_pool_->Request(
      submission, sizeof(float) * 2 * 6, sizeof(float), vertex_buffer,
      vertex_offset));
  if (!verts) {
    return false;
  }
  const float quad[12] = {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f,  -1.0f,
                          1.0f,  -1.0f, -1.0f, 1.0f, 1.0f,  1.0f};
  std::memcpy(verts, quad, sizeof(quad));

  // Transition the producer P -> SHADER_READ so the convert FS can sample it.
  Framebuffer& mfb = const_cast<Framebuffer&>(*fb);
  VkImageSubresourceRange range = {};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.levelCount = 1;
  range.layerCount = 1;
  command_processor_.PushImageMemoryBarrier(
      fb->bd_native_color_image, range, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
      VK_ACCESS_SHADER_READ_BIT, mfb.bd_native_color_layout,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  command_processor_.SubmitBarriers(true);

  // The convert draw.
  VkRenderPassBeginInfo rpbi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rpbi.renderPass = render_pass;
  rpbi.framebuffer = t.convert_framebuffer;
  rpbi.renderArea.offset = {0, 0};
  rpbi.renderArea.extent = {w, h};
  rpbi.clearValueCount = 0;
  command_buffer.CmdVkBeginRenderPass(&rpbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport = {0.0f, 0.0f, float(w), float(h), 0.0f, 1.0f};
  command_buffer.CmdVkSetViewport(0, 1, &viewport);
  VkRect2D scissor = {{0, 0}, {w, h}};
  command_buffer.CmdVkSetScissor(0, 1, &scissor);
  command_buffer.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                         0, 1, &t.convert_descriptor_set, 0,
                                         nullptr);
  command_buffer.CmdVkBindVertexBuffers(0, 1, &vertex_buffer, &vertex_offset);
  BdConvertPushConstants pc = {exp_bias_factor, swap, sample_count, 0};
  command_buffer.CmdVkPushConstants(layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                    sizeof(pc), &pc);
  command_buffer.CmdVkDraw(6, 1, 0, 0);
  command_buffer.CmdVkEndRenderPass();

  // The render pass finalLayout left T in SHADER_READ; P was sampled.
  t.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  t.stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  t.access = VK_ACCESS_SHADER_READ_BIT;
  mfb.bd_native_color_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  mfb.bd_native_color_access = VK_ACCESS_SHADER_READ_BIT;
  mfb.bd_native_color_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  return true;
}

bool VulkanRenderTargetCache::RecordBdCustomResolveConvert(
    const Framebuffer* fb, uint32_t w, uint32_t h, float exp_bias_factor,
    uint32_t swap) {
  if (!fb || fb->bd_native_color_custom_resolve_rp == VK_NULL_HANDLE ||
      fb->bd_native_color_view == VK_NULL_HANDLE) {
    return false;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();

  uint32_t sample_count = fb->bd_native_color_custom_resolve_samples;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  if (!GetBdNativeCustomResolvePipeline(
          fb->bd_native_color_custom_resolve_rp,
          VK_FORMAT_A2B10G10R10_UNORM_PACK32, sample_count, pipeline, layout)) {
    return false;
  }

  // Descriptor pool (once) + input-attachment descriptor set (once per fb).
  if (bd_custom_resolve_descriptor_pool_ == VK_NULL_HANDLE) {
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 64};
    VkDescriptorPoolCreateInfo dpci = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 64;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &pool_size;
    if (dfn.vkCreateDescriptorPool(device, &dpci, nullptr,
                                   &bd_custom_resolve_descriptor_pool_) !=
        VK_SUCCESS) {
      return false;
    }
  }
  Framebuffer& mfb = const_cast<Framebuffer&>(*fb);
  if (mfb.bd_native_color_cr_descriptor_set == VK_NULL_HANDLE) {
    VkDescriptorSetAllocateInfo dsai = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = bd_custom_resolve_descriptor_pool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &bd_custom_resolve_ia_set_layout_;
    if (dfn.vkAllocateDescriptorSets(
            device, &dsai, &mfb.bd_native_color_cr_descriptor_set) !=
        VK_SUCCESS) {
      mfb.bd_native_color_cr_descriptor_set = VK_NULL_HANDLE;
      return false;
    }
    VkDescriptorImageInfo image_info = {};
    image_info.sampler = VK_NULL_HANDLE;
    image_info.imageView = fb->bd_native_color_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = mfb.bd_native_color_cr_descriptor_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    write.pImageInfo = &image_info;
    dfn.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }

  // Fullscreen quad (NDC [-1,1], 2 triangles).
  uint64_t submission = command_processor_.GetCurrentSubmission();
  VkBuffer vertex_buffer = VK_NULL_HANDLE;
  VkDeviceSize vertex_offset = 0;
  float* verts = reinterpret_cast<float*>(transfer_vertex_buffer_pool_->Request(
      submission, sizeof(float) * 2 * 6, sizeof(float), vertex_buffer,
      vertex_offset));
  if (!verts) {
    return false;
  }
  const float quad[12] = {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f,
                          1.0f,  -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
  std::memcpy(verts, quad, sizeof(quad));

  // The pass is already open in subpass 0 (the field draws). Advance to the
  // custom-resolve subpass 1 and draw the fullscreen convert.
  command_buffer.CmdVkNextSubpass(VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport = {0.0f, 0.0f, float(w), float(h), 0.0f, 1.0f};
  command_buffer.CmdVkSetViewport(0, 1, &viewport);
  VkRect2D scissor = {{0, 0}, {w, h}};
  command_buffer.CmdVkSetScissor(0, 1, &scissor);
  command_buffer.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  command_buffer.CmdVkBindDescriptorSets(
      VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
      &mfb.bd_native_color_cr_descriptor_set, 0, nullptr);
  command_buffer.CmdVkBindVertexBuffers(0, 1, &vertex_buffer, &vertex_offset);
  BdConvertPushConstants pc = {exp_bias_factor, swap, sample_count, 0};
  command_buffer.CmdVkPushConstants(layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                    sizeof(pc), &pc);
  command_buffer.CmdVkDraw(6, 1, 0, 0);
  {
    static std::atomic<uint32_t> s_cr{0};
    if (s_cr.fetch_add(1) < 20) {
      XELOGI("BD custom-resolve: convert draw {}x{} samples={} (subpass1)", w, h,
             sample_count);
    }
  }
  // The resolve output (bd_native_color_resolve_image = A2B10) is now the sampled
  // image; the render pass finalLayout leaves it SHADER_READ for the composite.
  return true;
}

bool VulkanRenderTargetCache::SeedBdNativeColorProducer(const Framebuffer* fb) {
  if (!fb || fb->bd_native_color_image == VK_NULL_HANDLE ||
      !fb->bd_native_color_lle_rt) {
    return false;
  }
  VulkanRenderTarget& lle_rt =
      *static_cast<VulkanRenderTarget*>(fb->bd_native_color_lle_rt);
  Framebuffer& mfb = const_cast<Framebuffer&>(*fb);
  VkImageSubresourceRange range = {};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.levelCount = 1;
  range.layerCount = 1;
  // LLE color -> TRANSFER_SRC (tracked via SetUsage so downstream sees it). Left
  // in TRANSFER_SRC after the pass (5.6-sol: no fictional COLOR write-back; the
  // next real consumer transitions from the actual last usage).
  command_processor_.PushImageMemoryBarrier(
      lle_rt.image(), range, lle_rt.current_stage_mask(),
      VK_PIPELINE_STAGE_TRANSFER_BIT, lle_rt.current_access_mask(),
      VK_ACCESS_TRANSFER_READ_BIT, lle_rt.current_layout(),
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  lle_rt.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  // Native color -> TRANSFER_DST. srcStage/srcAccess = the native image's REAL
  // prior state (the previous mirror's outstanding TRANSFER_READ, or TOP/0 on
  // first use) so the new transfer WRITE waits for that read = the WAR fix.
  // oldLayout=UNDEFINED still discards the (about-to-be-overwritten) contents.
  command_processor_.PushImageMemoryBarrier(
      fb->bd_native_color_image, range, fb->bd_native_color_stage,
      VK_PIPELINE_STAGE_TRANSFER_BIT, fb->bd_native_color_access,
      VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  command_processor_.SubmitBarriers(true);
  VkImageCopy creg = {};
  creg.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  creg.srcSubresource.layerCount = 1;
  creg.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  creg.dstSubresource.layerCount = 1;
  creg.extent = {fb->host_extent.width, fb->host_extent.height, 1};
  command_processor_.deferred_command_buffer().CmdVkCopyImage(
      lle_rt.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      fb->bd_native_color_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &creg);
  // Native -> COLOR_ATTACHMENT_OPTIMAL (the render pass's initial layout).
  command_processor_.PushImageMemoryBarrier(
      fb->bd_native_color_image, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  command_processor_.SubmitBarriers(true);
  // The native image will next be written by the render pass as a color
  // attachment; the mirror records its post-pass state.
  mfb.bd_native_color_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  mfb.bd_native_color_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  mfb.bd_native_color_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  return true;
}

void VulkanRenderTargetCache::MirrorBdNativeColorProducer(
    const Framebuffer* fb) {
  if (!fb || fb->bd_native_color_image == VK_NULL_HANDLE ||
      !fb->bd_native_color_lle_rt) {
    return;
  }
  VulkanRenderTarget& lle_rt =
      *static_cast<VulkanRenderTarget*>(fb->bd_native_color_lle_rt);
  Framebuffer& mfb = const_cast<Framebuffer&>(*fb);
  VkImageSubresourceRange range = {};
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.levelCount = 1;
  range.layerCount = 1;
  // Native (ends the pass in its tracked state = COLOR_ATTACHMENT_OPTIMAL) ->
  // TRANSFER_SRC.
  command_processor_.PushImageMemoryBarrier(
      fb->bd_native_color_image, range, fb->bd_native_color_stage,
      VK_PIPELINE_STAGE_TRANSFER_BIT, fb->bd_native_color_access,
      VK_ACCESS_TRANSFER_READ_BIT, fb->bd_native_color_layout,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  // LLE color -> TRANSFER_DST (tracked). Left in TRANSFER_DST after the copy;
  // the next real consumer transitions from there (no fictional COLOR write-back).
  command_processor_.PushImageMemoryBarrier(
      lle_rt.image(), range, lle_rt.current_stage_mask(),
      VK_PIPELINE_STAGE_TRANSFER_BIT, lle_rt.current_access_mask(),
      VK_ACCESS_TRANSFER_WRITE_BIT, lle_rt.current_layout(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  lle_rt.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  command_processor_.SubmitBarriers(true);
  VkImageCopy creg = {};
  creg.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  creg.srcSubresource.layerCount = 1;
  creg.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  creg.dstSubresource.layerCount = 1;
  creg.extent = {fb->host_extent.width, fb->host_extent.height, 1};
  command_processor_.deferred_command_buffer().CmdVkCopyImage(
      fb->bd_native_color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      lle_rt.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &creg);
  command_processor_.SubmitBarriers(true);
  if (cvars::gpu_bd_native_color_lifetime_hle >= 5) {
    // LEVEL 5 generation bridge: also make the native image sampleable so the
    // composite sampler + present can read it directly (the CONSUME half). The
    // mirror above still keeps the LLE surface correct (un-dropped fallback), so
    // this stays correctness-neutral until transfers are actually dropped.
    command_processor_.PushImageMemoryBarrier(
        fb->bd_native_color_image, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    command_processor_.SubmitBarriers(true);
    mfb.bd_native_color_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    mfb.bd_native_color_access = VK_ACCESS_SHADER_READ_BIT;
    mfb.bd_native_color_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  } else {
    // Record the native image's outstanding TRANSFER_READ so the NEXT seed
    // barriers its write after this read = the WAR fix.
    mfb.bd_native_color_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    mfb.bd_native_color_access = VK_ACCESS_TRANSFER_READ_BIT;
    mfb.bd_native_color_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  }
}

const VulkanRenderTargetCache::Framebuffer*
VulkanRenderTargetCache::GetBdNativeDepthConsumerFramebuffer(
    RenderPassKey render_pass_key, uint32_t pitch_tiles_at_32bpp,
    const RenderTarget* const* depth_and_color_render_targets) {
  const Framebuffer* base = GetHostRenderTargetsFramebuffer(
      render_pass_key, pitch_tiles_at_32bpp, depth_and_color_render_targets);
  if (!base) {
    return nullptr;
  }
  if (base->bd_native_depth_framebuffer != VK_NULL_HANDLE) {
    return base;  // already built for this framebuffer
  }
  if (render_pass_key.color_rts_use_transfer_formats) {
    return base;
  }
  uint32_t used = render_pass_key.depth_and_color_used;
  bool has_depth = (used & (uint32_t(1) << 0)) != 0;
  uint32_t color_mask = used & ~(uint32_t(1) << 0);
  uint32_t color_index;
  if (!has_depth || !xe::bit_scan_forward(color_mask, &color_index)) {
    return base;  // need depth + at least one color RT
  }
  if (color_mask & ~(uint32_t(1) << color_index)) {
    return base;  // single color RT only (the effect-pass shape)
  }
  const VulkanRenderTarget* depth_rt = static_cast<const VulkanRenderTarget*>(
      depth_and_color_render_targets[0]);
  const VulkanRenderTarget* color_rt = static_cast<const VulkanRenderTarget*>(
      depth_and_color_render_targets[color_index]);
  RenderTargetKey dkey = depth_rt->key();
  RenderTargetKey ckey = color_rt->key();
  // The ONE target consumer (Path A Stage 0 census): depth base810 pitch9 4x +
  // color base0 pitch9 2:10:10:10-float (resource_format 3) 4x.
  if (!dkey.is_depth || dkey.base_tiles != 810 || dkey.GetPitchTiles() != 9 ||
      dkey.msaa_samples != xenos::MsaaSamples::k4X || ckey.base_tiles != 0 ||
      ckey.GetPitchTiles() != 9 ||
      ckey.msaa_samples != xenos::MsaaSamples::k4X ||
      ckey.resource_format != 3u) {
    return base;
  }

  // KEY-CORRESPONDENCE DIAGNOSTIC (gpu_bd_depth_xfer_census). We have reached the
  // ONE Path-A consumer, identified purely by EDRAM keys. The native surfaces that
  // could serve its depth are keyed by RESOLVE-DEST GUEST ADDRESS, and no mapping
  // between the two key spaces exists today - which is exactly what blocks seeding
  // this consumer from the in-pass depth resolve. Logging BOTH sides at the same
  // instant is how that mapping gets derived from evidence: this consumer's EDRAM
  // depth/color keys here, and every live native surface's guest-address key +
  // dims/format/samples + depth-resolve readiness from LogSurfaceKeys.
  // Deliberately evidence-first: a wrong depth redirect renders CORRECT on desktop
  // and COLLAPSES the field on Turnip, so the mapping must not be guessed.
  if (cvars::gpu_bd_depth_xfer_census) {
    XELOGI(
        "BD DEPTH CONSUMER MATCH: depth(base={} pitchT={} msaa={}) "
        "color(base={} pitchT={} msaa={} fmt={}) host_extent={}x{}",
        dkey.base_tiles, dkey.GetPitchTiles(), uint32_t(dkey.msaa_samples),
        ckey.base_tiles, ckey.GetPitchTiles(), uint32_t(ckey.msaa_samples),
        uint32_t(ckey.resource_format), base->host_extent.width,
        base->host_extent.height);
    command_processor_.LogBdNativeSurfaceKeys("depth-consumer");
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  VkFormat depth_format = GetDepthVulkanFormat(dkey.GetDepthFormat());
  if (depth_format == VK_FORMAT_UNDEFINED) {
    return base;
  }

  VkImageCreateInfo image_create_info = {};
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.format = depth_format;
  image_create_info.extent.width = base->host_extent.width;
  image_create_info.extent.height = base->host_extent.height;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = VK_SAMPLE_COUNT_4_BIT;  // k4X consumer
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage native_image = VK_NULL_HANDLE;
  VkDeviceMemory native_memory = VK_NULL_HANDLE;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device, image_create_info,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, native_image,
          native_memory)) {
    return base;
  }
  VkImageViewCreateInfo view_create_info = {};
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.image = native_image;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create_info.format = depth_format;
  view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  const VkImageAspectFlags depth_aspect =
      VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  view_create_info.subresourceRange =
      ui::vulkan::util::InitializeSubresourceRange(depth_aspect);
  VkImageView native_view = VK_NULL_HANDLE;
  if (dfn.vkCreateImageView(device, &view_create_info, nullptr, &native_view) !=
      VK_SUCCESS) {
    dfn.vkDestroyImage(device, native_image, nullptr);
    dfn.vkFreeMemory(device, native_memory, nullptr);
    return base;
  }
  // Clone the framebuffer: att0 depth = the native snapshot, att1 color = the
  // consumer's real LLE color (same attachment order as GetHostRenderTargets-
  // Framebuffer: depth at bit 0, then the single color).
  VkRenderPass render_pass = GetHostRenderTargetsRenderPass(render_pass_key);
  VkImageView attachments[2];
  attachments[0] = native_view;
  attachments[1] = color_rt->view_depth_color();
  VkFramebufferCreateInfo framebuffer_create_info = {};
  framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_create_info.renderPass = render_pass;
  framebuffer_create_info.attachmentCount = 2;
  framebuffer_create_info.pAttachments = attachments;
  framebuffer_create_info.width = base->host_extent.width;
  framebuffer_create_info.height = base->host_extent.height;
  framebuffer_create_info.layers = 1;
  VkFramebuffer native_framebuffer = VK_NULL_HANDLE;
  if (render_pass == VK_NULL_HANDLE ||
      dfn.vkCreateFramebuffer(device, &framebuffer_create_info, nullptr,
                              &native_framebuffer) != VK_SUCCESS) {
    dfn.vkDestroyImageView(device, native_view, nullptr);
    dfn.vkDestroyImage(device, native_image, nullptr);
    dfn.vkFreeMemory(device, native_memory, nullptr);
    return base;
  }
  Framebuffer& entry = const_cast<Framebuffer&>(*base);
  entry.bd_native_depth_image = native_image;
  entry.bd_native_depth_memory = native_memory;
  entry.bd_native_depth_view = native_view;
  entry.bd_native_depth_framebuffer = native_framebuffer;
  entry.bd_native_depth_lle_image = depth_rt->image();
  entry.bd_native_depth_lle_rt =
      const_cast<RenderTarget*>(depth_and_color_render_targets[0]);
  entry.bd_native_depth_aspect = depth_aspect;
  XELOGI(
      "BD PATHA DEPTH: consumer framebuffer built (depth base=810 pitchT=9 4x, "
      "color base=0 pitchT=9 fmt=3 4x) extent={}x{}",
      base->host_extent.width, base->host_extent.height);
  return base;
}

bool VulkanRenderTargetCache::SeedBdNativeDepthConsumer(const Framebuffer* fb) {
  if (!fb || fb->bd_native_depth_image == VK_NULL_HANDLE ||
      !fb->bd_native_depth_lle_rt) {
    return false;
  }
  VulkanRenderTarget& lle_rt =
      *static_cast<VulkanRenderTarget*>(fb->bd_native_depth_lle_rt);
  Framebuffer& mfb = const_cast<Framebuffer&>(*fb);
  VkImageSubresourceRange range = {};
  range.aspectMask = fb->bd_native_depth_aspect;
  range.levelCount = 1;
  range.layerCount = 1;
  // LLE depth -> TRANSFER_SRC (tracked). Left there after; the next real consumer
  // transitions from the actual last usage.
  command_processor_.PushImageMemoryBarrier(
      lle_rt.image(), range, lle_rt.current_stage_mask(),
      VK_PIPELINE_STAGE_TRANSFER_BIT, lle_rt.current_access_mask(),
      VK_ACCESS_TRANSFER_READ_BIT, lle_rt.current_layout(),
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  lle_rt.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  // Native depth -> TRANSFER_DST (srcStage/access = its real prior state = the
  // previous mirror's outstanding TRANSFER_READ, or TOP/0 first use = WAR fix).
  command_processor_.PushImageMemoryBarrier(
      fb->bd_native_depth_image, range, fb->bd_native_depth_stage,
      VK_PIPELINE_STAGE_TRANSFER_BIT, fb->bd_native_depth_access,
      VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  command_processor_.SubmitBarriers(true);
  VkImageCopy dreg = {};
  dreg.srcSubresource.aspectMask = fb->bd_native_depth_aspect;
  dreg.srcSubresource.layerCount = 1;
  dreg.dstSubresource.aspectMask = fb->bd_native_depth_aspect;
  dreg.dstSubresource.layerCount = 1;
  dreg.extent = {fb->host_extent.width, fb->host_extent.height, 1};
  command_processor_.deferred_command_buffer().CmdVkCopyImage(
      lle_rt.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      fb->bd_native_depth_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &dreg);
  // Native -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL (the render pass initial layout).
  command_processor_.PushImageMemoryBarrier(
      fb->bd_native_depth_image, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  command_processor_.SubmitBarriers(true);
  mfb.bd_native_depth_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  mfb.bd_native_depth_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  mfb.bd_native_depth_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  return true;
}

void VulkanRenderTargetCache::MirrorBdNativeDepthConsumer(
    const Framebuffer* fb) {
  if (!fb || fb->bd_native_depth_image == VK_NULL_HANDLE ||
      !fb->bd_native_depth_lle_rt) {
    return;
  }
  VulkanRenderTarget& lle_rt =
      *static_cast<VulkanRenderTarget*>(fb->bd_native_depth_lle_rt);
  Framebuffer& mfb = const_cast<Framebuffer&>(*fb);
  VkImageSubresourceRange range = {};
  range.aspectMask = fb->bd_native_depth_aspect;
  range.levelCount = 1;
  range.layerCount = 1;
  // Native (ends the pass as depth attachment) -> TRANSFER_SRC.
  command_processor_.PushImageMemoryBarrier(
      fb->bd_native_depth_image, range, fb->bd_native_depth_stage,
      VK_PIPELINE_STAGE_TRANSFER_BIT, fb->bd_native_depth_access,
      VK_ACCESS_TRANSFER_READ_BIT, fb->bd_native_depth_layout,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  // LLE depth -> TRANSFER_DST (tracked).
  command_processor_.PushImageMemoryBarrier(
      lle_rt.image(), range, lle_rt.current_stage_mask(),
      VK_PIPELINE_STAGE_TRANSFER_BIT, lle_rt.current_access_mask(),
      VK_ACCESS_TRANSFER_WRITE_BIT, lle_rt.current_layout(),
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  lle_rt.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  command_processor_.SubmitBarriers(true);
  VkImageCopy dreg = {};
  dreg.srcSubresource.aspectMask = fb->bd_native_depth_aspect;
  dreg.srcSubresource.layerCount = 1;
  dreg.dstSubresource.aspectMask = fb->bd_native_depth_aspect;
  dreg.dstSubresource.layerCount = 1;
  dreg.extent = {fb->host_extent.width, fb->host_extent.height, 1};
  command_processor_.deferred_command_buffer().CmdVkCopyImage(
      fb->bd_native_depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      lle_rt.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &dreg);
  command_processor_.SubmitBarriers(true);
  // Record the native image's outstanding TRANSFER_READ so the NEXT seed
  // barriers its write after this read (WAR fix).
  mfb.bd_native_depth_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  mfb.bd_native_depth_access = VK_ACCESS_TRANSFER_READ_BIT;
  mfb.bd_native_depth_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

bool VulkanRenderTargetCache::CreateFragmentDensityMap(
    VkExtent2D framebuffer_extent, VkImage& image_out,
    VkDeviceMemory& memory_out, VkImageView& view_out) {
  // Establish a clean failure contract regardless of the helper's behavior.
  image_out = VK_NULL_HANDLE;
  memory_out = VK_NULL_HANDLE;
  view_out = VK_NULL_HANDLE;

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // 1 density texel per HW-max-texel region -> a tiny image whose dims always
  // land in the per-framebuffer VUID window [ceil(fb/maxTexel), ceil(fb/minTexel)]
  // (sizing at ceil(fb/maxTexel) = the lower bound). The texel size is queried at
  // device init, not assumed - skip FDM if it didn't populate (defensive).
  // R16G16_SFLOAT because Turnip advertises the FDM format feature only for float
  // formats (not the spec-typical R8G8_UNORM).
  const VkExtent2D max_texel =
      vulkan_device->extensions().fragment_density_map_max_texel_size;
  if (!max_texel.width || !max_texel.height) {
    XELOGE("VulkanRenderTargetCache: FDM max texel size unavailable; skipping");
    return false;
  }
  VkImageCreateInfo image_create_info;
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.pNext = nullptr;
  image_create_info.flags = 0;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.format = VK_FORMAT_R16G16_SFLOAT;
  image_create_info.extent.width = std::max(
      uint32_t(1),
      (framebuffer_extent.width + max_texel.width - 1) / max_texel.width);
  image_create_info.extent.height = std::max(
      uint32_t(1),
      (framebuffer_extent.height + max_texel.height - 1) / max_texel.height);
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_create_info.usage = VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.pQueueFamilyIndices = nullptr;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!ui::vulkan::util::CreateDedicatedAllocationImage(
          vulkan_device, image_create_info,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, image_out,
          memory_out)) {
    XELOGE("VulkanRenderTargetCache: Failed to create the FDM density image");
    return false;
  }

  VkImageViewCreateInfo view_create_info;
  view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_create_info.pNext = nullptr;
  view_create_info.flags = 0;
  view_create_info.image = image_out;
  view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_create_info.format = VK_FORMAT_R16G16_SFLOAT;
  view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_create_info.subresourceRange.baseMipLevel = 0;
  view_create_info.subresourceRange.levelCount = 1;
  view_create_info.subresourceRange.baseArrayLayer = 0;
  view_create_info.subresourceRange.layerCount = 1;
  if (dfn.vkCreateImageView(device, &view_create_info, nullptr, &view_out) !=
      VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the FDM density image view");
    dfn.vkDestroyImage(device, image_out, nullptr);
    dfn.vkFreeMemory(device, memory_out, nullptr);
    image_out = VK_NULL_HANDLE;
    memory_out = VK_NULL_HANDLE;
    return false;
  }

  // Image + view only - NO commands are recorded here, so the caller can safely
  // destroy them synchronously if vkCreateFramebuffer then fails (the fill, which
  // references the image in the command stream, runs only AFTER the framebuffer
  // succeeds, via FillFragmentDensityMap).
  return true;
}

void VulkanRenderTargetCache::FillFragmentDensityMap(VkImage image) {
  // Uniform-fill once via a clear (density = 1/value -> fragment area = value),
  // then leave it in FRAGMENT_DENSITY_MAP_OPTIMAL for the guest passes to read.
  // Recorded into the open draw stream; one-time (the framebuffer + its filled
  // density image are cached). Independent of the EDRAM transfers (a different
  // image). Called only after vkCreateFramebuffer succeeds so a failed framebuffer
  // never leaves a recorded command referencing a freed image.
  VkImageSubresourceRange range;
  range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  range.baseMipLevel = 0;
  range.levelCount = 1;
  range.baseArrayLayer = 0;
  range.layerCount = 1;
  command_processor_.PushImageMemoryBarrier(
      image, range, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  command_processor_.SubmitBarriers(true);
  float density = 1.0f / float(cvars::gpu_fdm_foliage);
  VkClearColorValue clear_color;
  clear_color.float32[0] = density;
  clear_color.float32[1] = density;
  clear_color.float32[2] = 0.0f;
  clear_color.float32[3] = 1.0f;
  command_processor_.deferred_command_buffer().CmdVkClearColorImage(
      image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);
  command_processor_.PushImageMemoryBarrier(
      image, range, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT,
      VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_FRAGMENT_DENSITY_MAP_READ_BIT_EXT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_FRAGMENT_DENSITY_MAP_OPTIMAL_EXT);
  command_processor_.SubmitBarriers(true);
}

VkShaderModule VulkanRenderTargetCache::GetTransferShader(
    TransferShaderKey key) {
  auto shader_it = transfer_shaders_.find(key);
  if (shader_it != transfer_shaders_.end()) {
    return shader_it->second;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Properties& device_properties =
      vulkan_device->properties();

  std::vector<spv::Id> id_vector_temp;
  std::vector<unsigned int> uint_vector_temp;

  SpirvBuilder builder(spv::Spv_1_0,
                       (SpirvShaderTranslator::kSpirvMagicToolId << 16) | 1,
                       nullptr);
  spv::Id ext_inst_glsl_std_450 = builder.import("GLSL.std.450");
  builder.addCapability(spv::CapabilityShader);
  builder.setMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
  builder.setSource(spv::SourceLanguageUnknown, 0);

  spv::Id type_void = builder.makeVoidType();
  spv::Id type_bool = builder.makeBoolType();
  spv::Id type_int = builder.makeIntType(32);
  spv::Id type_int2 = builder.makeVectorType(type_int, 2);
  spv::Id type_uint = builder.makeUintType(32);
  spv::Id type_uint2 = builder.makeVectorType(type_uint, 2);
  spv::Id type_uint4 = builder.makeVectorType(type_uint, 4);
  spv::Id type_float = builder.makeFloatType(32);
  spv::Id type_float2 = builder.makeVectorType(type_float, 2);
  spv::Id type_float4 = builder.makeVectorType(type_float, 4);

  const TransferModeInfo& mode = kTransferModes[size_t(key.mode)];
  const TransferPipelineLayoutInfo& pipeline_layout_info =
      kTransferPipelineLayoutInfos[size_t(mode.pipeline_layout)];

  // If not dest_is_color, it's depth, or stencil bit - 40-sample columns are
  // swapped as opposed to color source.
  bool dest_is_color = (mode.output == TransferOutput::kColor);
  xenos::ColorRenderTargetFormat dest_color_format =
      xenos::ColorRenderTargetFormat(key.dest_resource_format);
  xenos::DepthRenderTargetFormat dest_depth_format =
      xenos::DepthRenderTargetFormat(key.dest_resource_format);
  bool dest_is_64bpp =
      dest_is_color && xenos::IsColorRenderTargetFormat64bpp(dest_color_format);

  xenos::ColorRenderTargetFormat source_color_format =
      xenos::ColorRenderTargetFormat(key.source_resource_format);
  xenos::DepthRenderTargetFormat source_depth_format =
      xenos::DepthRenderTargetFormat(key.source_resource_format);
  // If not source_is_color, it's depth / stencil - 40-sample columns are
  // swapped as opposed to color destination.
  bool source_is_color = (pipeline_layout_info.used_descriptor_sets &
                          kTransferUsedDescriptorSetColorTextureBit) != 0;
  bool source_is_64bpp;
  uint32_t source_color_format_component_count;
  uint32_t source_color_texture_component_mask;
  bool source_color_is_uint;
  spv::Id source_color_component_type;
  if (source_is_color) {
    assert_zero(pipeline_layout_info.used_descriptor_sets &
                kTransferUsedDescriptorSetDepthStencilTexturesBit);
    source_is_64bpp =
        xenos::IsColorRenderTargetFormat64bpp(source_color_format);
    source_color_format_component_count =
        xenos::GetColorRenderTargetFormatComponentCount(source_color_format);
    if (mode.output == TransferOutput::kStencilBit) {
      if (source_is_64bpp && !dest_is_64bpp) {
        // Need one component, but choosing from the two 32bpp halves of the
        // 64bpp sample.
        source_color_texture_component_mask =
            0b1 | (0b1 << (source_color_format_component_count >> 1));
      } else {
        // Red is at least 8 bits per component in all formats.
        source_color_texture_component_mask = 0b1;
      }
    } else {
      source_color_texture_component_mask =
          (uint32_t(1) << source_color_format_component_count) - 1;
    }
    GetColorOwnershipTransferVulkanFormat(source_color_format,
                                          &source_color_is_uint);
    source_color_component_type = source_color_is_uint ? type_uint : type_float;
  } else {
    source_is_64bpp = false;
    source_color_format_component_count = 0;
    source_color_texture_component_mask = 0;
    source_color_is_uint = false;
    source_color_component_type = spv::NoType;
  }

  std::vector<spv::Id> main_interface;

  // Outputs.
  bool shader_uses_stencil_reference_output =
      mode.output == TransferOutput::kDepth &&
      vulkan_device->extensions().ext_EXT_shader_stencil_export;
  bool dest_color_is_uint = false;
  uint32_t dest_color_component_count = 0;
  spv::Id type_fragment_data_component = spv::NoResult;
  spv::Id type_fragment_data = spv::NoResult;
  spv::Id output_fragment_data = spv::NoResult;
  spv::Id output_fragment_depth = spv::NoResult;
  spv::Id output_fragment_stencil_ref = spv::NoResult;
  switch (mode.output) {
    case TransferOutput::kColor:
      GetColorOwnershipTransferVulkanFormat(dest_color_format,
                                            &dest_color_is_uint);
      dest_color_component_count =
          xenos::GetColorRenderTargetFormatComponentCount(dest_color_format);
      type_fragment_data_component =
          dest_color_is_uint ? type_uint : type_float;
      type_fragment_data =
          dest_color_component_count > 1
              ? builder.makeVectorType(type_fragment_data_component,
                                       dest_color_component_count)
              : type_fragment_data_component;
      output_fragment_data = builder.createVariable(
          spv::NoPrecision, spv::StorageClassOutput, type_fragment_data,
          "xe_transfer_fragment_data");
      builder.addDecoration(output_fragment_data, spv::DecorationLocation,
                            key.dest_color_rt_index);
      main_interface.push_back(output_fragment_data);
      break;
    case TransferOutput::kDepth:
      output_fragment_depth =
          builder.createVariable(spv::NoPrecision, spv::StorageClassOutput,
                                 type_float, "gl_FragDepth");
      builder.addDecoration(output_fragment_depth, spv::DecorationBuiltIn,
                            spv::BuiltInFragDepth);
      main_interface.push_back(output_fragment_depth);
      if (shader_uses_stencil_reference_output) {
        builder.addExtension("SPV_EXT_shader_stencil_export");
        builder.addCapability(spv::CapabilityStencilExportEXT);
        output_fragment_stencil_ref =
            builder.createVariable(spv::NoPrecision, spv::StorageClassOutput,
                                   type_int, "gl_FragStencilRefARB");
        builder.addDecoration(output_fragment_stencil_ref,
                              spv::DecorationBuiltIn,
                              spv::BuiltInFragStencilRefEXT);
        main_interface.push_back(output_fragment_stencil_ref);
      }
      break;
    default:
      break;
  }

  // Bindings.
  // Generating SPIR-V 1.0, no need to add bindings to the entry point's
  // interface until SPIR-V 1.4.
  // Color source.
  bool source_is_multisampled =
      key.source_msaa_samples != xenos::MsaaSamples::k1X;
  spv::Id source_color_texture = spv::NoResult;
  if (pipeline_layout_info.used_descriptor_sets &
      kTransferUsedDescriptorSetColorTextureBit) {
    source_color_texture = builder.createVariable(
        spv::NoPrecision, spv::StorageClassUniformConstant,
        builder.makeImageType(source_color_component_type, spv::Dim2D, false,
                              false, source_is_multisampled, 1,
                              spv::ImageFormatUnknown),
        "xe_transfer_color");
    builder.addDecoration(
        source_color_texture, spv::DecorationDescriptorSet,
        xe::bit_count(pipeline_layout_info.used_descriptor_sets &
                      (kTransferUsedDescriptorSetColorTextureBit - 1)));
    builder.addDecoration(source_color_texture, spv::DecorationBinding, 0);
  }
  // Depth / stencil source.
  spv::Id source_depth_texture = spv::NoResult;
  spv::Id source_stencil_texture = spv::NoResult;
  if (pipeline_layout_info.used_descriptor_sets &
      kTransferUsedDescriptorSetDepthStencilTexturesBit) {
    uint32_t source_depth_stencil_descriptor_set =
        xe::bit_count(pipeline_layout_info.used_descriptor_sets &
                      (kTransferUsedDescriptorSetDepthStencilTexturesBit - 1));
    // Using `depth == false` in makeImageType because comparisons are not
    // required, and other values of `depth` are causing issues in drivers.
    // https://github.com/microsoft/DirectXShaderCompiler/issues/1107
    if (mode.output != TransferOutput::kStencilBit) {
      source_depth_texture = builder.createVariable(
          spv::NoPrecision, spv::StorageClassUniformConstant,
          builder.makeImageType(type_float, spv::Dim2D, false, false,
                                source_is_multisampled, 1,
                                spv::ImageFormatUnknown),
          "xe_transfer_depth");
      builder.addDecoration(source_depth_texture, spv::DecorationDescriptorSet,
                            source_depth_stencil_descriptor_set);
      builder.addDecoration(source_depth_texture, spv::DecorationBinding, 0);
    }
    if (mode.output != TransferOutput::kDepth ||
        shader_uses_stencil_reference_output) {
      source_stencil_texture = builder.createVariable(
          spv::NoPrecision, spv::StorageClassUniformConstant,
          builder.makeImageType(type_uint, spv::Dim2D, false, false,
                                source_is_multisampled, 1,
                                spv::ImageFormatUnknown),
          "xe_transfer_stencil");
      builder.addDecoration(source_stencil_texture,
                            spv::DecorationDescriptorSet,
                            source_depth_stencil_descriptor_set);
      builder.addDecoration(source_stencil_texture, spv::DecorationBinding, 1);
    }
  }
  // Host depth source buffer.
  spv::Id host_depth_source_buffer = spv::NoResult;
  if (pipeline_layout_info.used_descriptor_sets &
      kTransferUsedDescriptorSetHostDepthBufferBit) {
    id_vector_temp.clear();
    id_vector_temp.push_back(builder.makeRuntimeArray(type_uint));
    // Storage buffers have std430 packing, no padding to 4-component vectors.
    builder.addDecoration(id_vector_temp.back(), spv::DecorationArrayStride,
                          sizeof(uint32_t));
    spv::Id type_host_depth_source_buffer =
        builder.makeStructType(id_vector_temp, "XeTransferHostDepthBuffer");
    builder.addMemberName(type_host_depth_source_buffer, 0, "host_depth");
    builder.addMemberDecoration(type_host_depth_source_buffer, 0,
                                spv::DecorationNonWritable);
    builder.addMemberDecoration(type_host_depth_source_buffer, 0,
                                spv::DecorationOffset, 0);
    // Block since SPIR-V 1.3, but since SPIR-V 1.0 is generated, it's
    // BufferBlock.
    builder.addDecoration(type_host_depth_source_buffer,
                          spv::DecorationBufferBlock);
    // StorageBuffer since SPIR-V 1.3, but since SPIR-V 1.0 is generated, it's
    // Uniform.
    host_depth_source_buffer = builder.createVariable(
        spv::NoPrecision, spv::StorageClassUniform,
        type_host_depth_source_buffer, "xe_transfer_host_depth_buffer");
    builder.addDecoration(
        host_depth_source_buffer, spv::DecorationDescriptorSet,
        xe::bit_count(pipeline_layout_info.used_descriptor_sets &
                      (kTransferUsedDescriptorSetHostDepthBufferBit - 1)));
    builder.addDecoration(host_depth_source_buffer, spv::DecorationBinding, 0);
  }
  // Host depth source texture (the depth / stencil descriptor set is reused,
  // but stencil is not needed).
  spv::Id host_depth_source_texture = spv::NoResult;
  if (pipeline_layout_info.used_descriptor_sets &
      kTransferUsedDescriptorSetHostDepthStencilTexturesBit) {
    host_depth_source_texture = builder.createVariable(
        spv::NoPrecision, spv::StorageClassUniformConstant,
        builder.makeImageType(
            type_float, spv::Dim2D, false, false,
            key.host_depth_source_msaa_samples != xenos::MsaaSamples::k1X, 1,
            spv::ImageFormatUnknown),
        "xe_transfer_host_depth");
    builder.addDecoration(
        host_depth_source_texture, spv::DecorationDescriptorSet,
        xe::bit_count(
            pipeline_layout_info.used_descriptor_sets &
            (kTransferUsedDescriptorSetHostDepthStencilTexturesBit - 1)));
    builder.addDecoration(host_depth_source_texture, spv::DecorationBinding, 0);
  }
  // Push constants.
  id_vector_temp.clear();
  uint32_t push_constants_member_host_depth_address = UINT32_MAX;
  if (pipeline_layout_info.used_push_constant_dwords &
      kTransferUsedPushConstantDwordHostDepthAddressBit) {
    push_constants_member_host_depth_address = uint32_t(id_vector_temp.size());
    id_vector_temp.push_back(type_uint);
  }
  uint32_t push_constants_member_address = UINT32_MAX;
  if (pipeline_layout_info.used_push_constant_dwords &
      kTransferUsedPushConstantDwordAddressBit) {
    push_constants_member_address = uint32_t(id_vector_temp.size());
    id_vector_temp.push_back(type_uint);
  }
  uint32_t push_constants_member_stencil_mask = UINT32_MAX;
  if (pipeline_layout_info.used_push_constant_dwords &
      kTransferUsedPushConstantDwordStencilMaskBit) {
    push_constants_member_stencil_mask = uint32_t(id_vector_temp.size());
    id_vector_temp.push_back(type_uint);
  }
  spv::Id push_constants = spv::NoResult;
  if (!id_vector_temp.empty()) {
    spv::Id type_push_constants =
        builder.makeStructType(id_vector_temp, "XeTransferPushConstants");
    if (pipeline_layout_info.used_push_constant_dwords &
        kTransferUsedPushConstantDwordHostDepthAddressBit) {
      assert_true(push_constants_member_host_depth_address != UINT32_MAX);
      builder.addMemberName(type_push_constants,
                            push_constants_member_host_depth_address,
                            "host_depth_address");
      builder.addMemberDecoration(
          type_push_constants, push_constants_member_host_depth_address,
          spv::DecorationOffset,
          sizeof(uint32_t) *
              xe::bit_count(
                  pipeline_layout_info.used_push_constant_dwords &
                  (kTransferUsedPushConstantDwordHostDepthAddressBit - 1)));
    }
    if (pipeline_layout_info.used_push_constant_dwords &
        kTransferUsedPushConstantDwordAddressBit) {
      assert_true(push_constants_member_address != UINT32_MAX);
      builder.addMemberName(type_push_constants, push_constants_member_address,
                            "address");
      builder.addMemberDecoration(
          type_push_constants, push_constants_member_address,
          spv::DecorationOffset,
          sizeof(uint32_t) *
              xe::bit_count(pipeline_layout_info.used_push_constant_dwords &
                            (kTransferUsedPushConstantDwordAddressBit - 1)));
    }
    if (pipeline_layout_info.used_push_constant_dwords &
        kTransferUsedPushConstantDwordStencilMaskBit) {
      assert_true(push_constants_member_stencil_mask != UINT32_MAX);
      builder.addMemberName(type_push_constants,
                            push_constants_member_stencil_mask, "stencil_mask");
      builder.addMemberDecoration(
          type_push_constants, push_constants_member_stencil_mask,
          spv::DecorationOffset,
          sizeof(uint32_t) *
              xe::bit_count(
                  pipeline_layout_info.used_push_constant_dwords &
                  (kTransferUsedPushConstantDwordStencilMaskBit - 1)));
    }
    builder.addDecoration(type_push_constants, spv::DecorationBlock);
    push_constants = builder.createVariable(
        spv::NoPrecision, spv::StorageClassPushConstant, type_push_constants,
        "xe_transfer_push_constants");
  }

  // Coordinate inputs.
  spv::Id input_fragment_coord = builder.createVariable(
      spv::NoPrecision, spv::StorageClassInput, type_float4, "gl_FragCoord");
  builder.addDecoration(input_fragment_coord, spv::DecorationBuiltIn,
                        spv::BuiltInFragCoord);
  main_interface.push_back(input_fragment_coord);
  spv::Id input_sample_id = spv::NoResult;
  spv::Id spec_const_sample_id = spv::NoResult;
  if (key.dest_msaa_samples != xenos::MsaaSamples::k1X) {
    if (device_properties.sampleRateShading) {
      // One draw for all samples.
      builder.addCapability(spv::CapabilitySampleRateShading);
      input_sample_id = builder.createVariable(
          spv::NoPrecision, spv::StorageClassInput, type_int, "gl_SampleID");
      builder.addDecoration(input_sample_id, spv::DecorationFlat);
      builder.addDecoration(input_sample_id, spv::DecorationBuiltIn,
                            spv::BuiltInSampleId);
      main_interface.push_back(input_sample_id);
    } else {
      // One sample per draw, with different sample masks.
      spec_const_sample_id = builder.makeUintConstant(0, true);
      builder.addName(spec_const_sample_id, "xe_transfer_sample_id");
      builder.addDecoration(spec_const_sample_id, spv::DecorationSpecId, 0);
    }
  }

  // Begin the main function.
  std::vector<spv::Id> main_param_types;
  std::vector<std::vector<spv::Decoration>> main_precisions;
  spv::Block* main_entry;
  spv::Function* main_function =
      builder.makeFunctionEntry(spv::NoPrecision, type_void, "main",
                                main_param_types, main_precisions, &main_entry);

  // Working with unsigned numbers for simplicity now, bitcasting to signed will
  // be done at texture fetch.

  uint32_t tile_width_samples =
      xenos::kEdramTileWidthSamples * draw_resolution_scale_x();
  uint32_t tile_height_samples =
      xenos::kEdramTileHeightSamples * draw_resolution_scale_y();

  // Split the destination pixel index into 32bpp tile and 32bpp-tile-relative
  // pixel index.
  // Note that division by non-power-of-two constants will include a 4-cycle
  // 32*32 multiplication on AMD, even though so many bits are not needed for
  // the pixel position - however, if an OpUnreachable path is inserted for the
  // case when the position has upper bits set, for some reason, the code for it
  // is not eliminated when compiling the shader for AMD via RenderDoc on
  // Windows, as of June 2022.
  uint_vector_temp.clear();
  uint_vector_temp.push_back(0);
  uint_vector_temp.push_back(1);
  spv::Id dest_pixel_coord = builder.createUnaryOp(
      spv::OpConvertFToU, type_uint2,
      builder.createRvalueSwizzle(
          spv::NoPrecision, type_float2,
          builder.createLoad(input_fragment_coord, spv::NoPrecision),
          uint_vector_temp));
  spv::Id dest_pixel_x =
      builder.createCompositeExtract(dest_pixel_coord, type_uint, 0);
  spv::Id const_dest_tile_width_pixels = builder.makeUintConstant(
      tile_width_samples >>
      (uint32_t(dest_is_64bpp) +
       uint32_t(key.dest_msaa_samples >= xenos::MsaaSamples::k4X)));
  spv::Id dest_tile_index_x = builder.createBinOp(
      spv::OpUDiv, type_uint, dest_pixel_x, const_dest_tile_width_pixels);
  spv::Id dest_tile_pixel_x = builder.createBinOp(
      spv::OpUMod, type_uint, dest_pixel_x, const_dest_tile_width_pixels);
  spv::Id dest_pixel_y =
      builder.createCompositeExtract(dest_pixel_coord, type_uint, 1);
  spv::Id const_dest_tile_height_pixels = builder.makeUintConstant(
      tile_height_samples >>
      uint32_t(key.dest_msaa_samples >= xenos::MsaaSamples::k2X));
  spv::Id dest_tile_index_y = builder.createBinOp(
      spv::OpUDiv, type_uint, dest_pixel_y, const_dest_tile_height_pixels);
  spv::Id dest_tile_pixel_y = builder.createBinOp(
      spv::OpUMod, type_uint, dest_pixel_y, const_dest_tile_height_pixels);

  assert_true(push_constants_member_address != UINT32_MAX);
  id_vector_temp.clear();
  id_vector_temp.push_back(
      builder.makeIntConstant(int32_t(push_constants_member_address)));
  spv::Id address_constant = builder.createLoad(
      builder.createAccessChain(spv::StorageClassPushConstant, push_constants,
                                id_vector_temp),
      spv::NoPrecision);

  // Calculate the 32bpp tile index from its X and Y parts.
  spv::Id dest_tile_index = builder.createBinOp(
      spv::OpIAdd, type_uint,
      builder.createBinOp(
          spv::OpIMul, type_uint,
          builder.createTriOp(
              spv::OpBitFieldUExtract, type_uint, address_constant,
              builder.makeUintConstant(0),
              builder.makeUintConstant(xenos::kEdramPitchTilesBits)),
          dest_tile_index_y),
      dest_tile_index_x);

  // Load the destination sample index.
  spv::Id dest_sample_id = spv::NoResult;
  if (key.dest_msaa_samples != xenos::MsaaSamples::k1X) {
    if (device_properties.sampleRateShading) {
      assert_true(input_sample_id != spv::NoResult);
      dest_sample_id = builder.createUnaryOp(
          spv::OpBitcast, type_uint,
          builder.createLoad(input_sample_id, spv::NoPrecision));
    } else {
      assert_true(spec_const_sample_id != spv::NoResult);
      // Already uint.
      dest_sample_id = spec_const_sample_id;
    }
  }

  // Transform the destination framebuffer pixel and sample coordinates into the
  // source texture pixel and sample coordinates.

  // First sample bit at 4x with Vulkan standard locations - horizontal sample.
  // Second sample bit at 4x with Vulkan standard locations - vertical sample.
  // At 2x:
  // - Native 2x: top is 1 in Vulkan, bottom is 0.
  // - 2x as 4x: top is 0, bottom is 3.

  spv::Id source_sample_id = dest_sample_id;
  spv::Id source_tile_pixel_x = dest_tile_pixel_x;
  spv::Id source_tile_pixel_y = dest_tile_pixel_y;
  spv::Id source_color_half = spv::NoResult;
  if (!source_is_64bpp && dest_is_64bpp) {
    // 32bpp -> 64bpp, need two samples of the source.
    if (key.source_msaa_samples >= xenos::MsaaSamples::k4X) {
      // 32bpp -> 64bpp, 4x ->.
      // Source has 32bpp halves in two adjacent samples.
      if (key.dest_msaa_samples >= xenos::MsaaSamples::k4X) {
        // 32bpp -> 64bpp, 4x -> 4x.
        // 1 destination horizontal sample = 2 source horizontal samples.
        // D p0,0 s0,0 = S p0,0 s0,0 | S p0,0 s1,0
        // D p0,0 s1,0 = S p1,0 s0,0 | S p1,0 s1,0
        // D p0,0 s0,1 = S p0,0 s0,1 | S p0,0 s1,1
        // D p0,0 s1,1 = S p1,0 s0,1 | S p1,0 s1,1
        // Thus destination horizontal sample -> source horizontal pixel,
        // vertical samples are 1:1.
        source_sample_id =
            builder.createBinOp(spv::OpBitwiseAnd, type_uint, dest_sample_id,
                                builder.makeUintConstant(1 << 1));
        source_tile_pixel_x = builder.createQuadOp(
            spv::OpBitFieldInsert, type_uint, dest_sample_id, dest_tile_pixel_x,
            builder.makeUintConstant(1), builder.makeUintConstant(31));
      } else if (key.dest_msaa_samples == xenos::MsaaSamples::k2X) {
        // 32bpp -> 64bpp, 4x -> 2x.
        // 1 destination horizontal pixel = 2 source horizontal samples.
        // D p0,0 s0 = S p0,0 s0,0 | S p0,0 s1,0
        // D p0,0 s1 = S p0,0 s0,1 | S p0,0 s1,1
        // D p1,0 s0 = S p1,0 s0,0 | S p1,0 s1,0
        // D p1,0 s1 = S p1,0 s0,1 | S p1,0 s1,1
        // Pixel index can be reused. Sample 1 (for native 2x) or 0 (for 2x as
        // 4x) should become samples 01, sample 0 or 3 should become samples 23.
        if (msaa_2x_attachments_supported_) {
          source_sample_id = builder.createBinOp(
              spv::OpShiftLeftLogical, type_uint,
              builder.createBinOp(spv::OpBitwiseXor, type_uint, dest_sample_id,
                                  builder.makeUintConstant(1)),
              builder.makeUintConstant(1));
        } else {
          source_sample_id =
              builder.createBinOp(spv::OpBitwiseAnd, type_uint, dest_sample_id,
                                  builder.makeUintConstant(1 << 1));
        }
      } else {
        // 32bpp -> 64bpp, 4x -> 1x.
        // 1 destination horizontal pixel = 2 source horizontal samples.
        // D p0,0 = S p0,0 s0,0 | S p0,0 s1,0
        // D p0,1 = S p0,0 s0,1 | S p0,0 s1,1
        // Horizontal pixel index can be reused. Vertical pixel 1 should
        // become sample 2.
        source_sample_id = builder.createQuadOp(
            spv::OpBitFieldInsert, type_uint, builder.makeUintConstant(0),
            dest_tile_pixel_y, builder.makeUintConstant(1),
            builder.makeUintConstant(1));
        source_tile_pixel_y =
            builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                dest_tile_pixel_y, builder.makeUintConstant(1));
      }
    } else {
      // 32bpp -> 64bpp, 1x/2x ->.
      // Source has 32bpp halves in two adjacent pixels.
      if (key.dest_msaa_samples >= xenos::MsaaSamples::k4X) {
        // 32bpp -> 64bpp, 1x/2x -> 4x.
        // The X part.
        // 1 destination horizontal sample = 2 source horizontal pixels.
        source_tile_pixel_x = builder.createQuadOp(
            spv::OpBitFieldInsert, type_uint,
            builder.createBinOp(spv::OpShiftLeftLogical, type_uint,
                                dest_tile_pixel_x, builder.makeUintConstant(2)),
            dest_sample_id, builder.makeUintConstant(1),
            builder.makeUintConstant(1));
        // Y is handled by common code.
      } else {
        // 32bpp -> 64bpp, 1x/2x -> 1x/2x.
        // The X part.
        // 1 destination horizontal pixel = 2 source horizontal pixels.
        source_tile_pixel_x =
            builder.createBinOp(spv::OpShiftLeftLogical, type_uint,
                                dest_tile_pixel_x, builder.makeUintConstant(1));
        // Y is handled by common code.
      }
    }
  } else if (source_is_64bpp && !dest_is_64bpp) {
    // 64bpp -> 32bpp, also the half to load.
    if (key.dest_msaa_samples >= xenos::MsaaSamples::k4X) {
      // 64bpp -> 32bpp, -> 4x.
      // The needed half is in the destination horizontal sample index.
      if (key.source_msaa_samples >= xenos::MsaaSamples::k4X) {
        // 64bpp -> 32bpp, 4x -> 4x.
        // D p0,0 s0,0 = S s0,0 low
        // D p0,0 s1,0 = S s0,0 high
        // D p1,0 s0,0 = S s1,0 low
        // D p1,0 s1,0 = S s1,0 high
        // Vertical pixel and sample (second bit) addressing is the same.
        // However, 1 horizontal destination pixel = 1 horizontal source sample.
        source_sample_id = builder.createQuadOp(
            spv::OpBitFieldInsert, type_uint, dest_sample_id, dest_tile_pixel_x,
            builder.makeUintConstant(0), builder.makeUintConstant(1));
        // 2 destination horizontal samples = 1 source horizontal sample, thus
        // 2 destination horizontal pixels = 1 source horizontal pixel.
        source_tile_pixel_x =
            builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                dest_tile_pixel_x, builder.makeUintConstant(1));
      } else {
        // 64bpp -> 32bpp, 1x/2x -> 4x.
        // 2 destination horizontal samples = 1 source horizontal pixel, thus
        // 1 destination horizontal pixel = 1 source horizontal pixel. Can reuse
        // horizontal pixel index.
        // Y is handled by common code.
      }
      // Half from the destination horizontal sample index.
      source_color_half =
          builder.createBinOp(spv::OpBitwiseAnd, type_uint, dest_sample_id,
                              builder.makeUintConstant(1));
    } else {
      // 64bpp -> 32bpp, -> 1x/2x.
      // The needed half is in the destination horizontal pixel index.
      if (key.source_msaa_samples >= xenos::MsaaSamples::k4X) {
        // 64bpp -> 32bpp, 4x -> 1x/2x.
        // (Destination horizontal pixel >> 1) & 1 = source horizontal sample
        // (first bit).
        source_sample_id = builder.createTriOp(
            spv::OpBitFieldUExtract, type_uint, dest_tile_pixel_x,
            builder.makeUintConstant(1), builder.makeUintConstant(1));
        if (key.dest_msaa_samples == xenos::MsaaSamples::k2X) {
          // 64bpp -> 32bpp, 4x -> 2x.
          // Destination vertical samples (1/0 in the first bit for native 2x or
          // 0/1 in the second bit for 2x as 4x) = source vertical samples
          // (second bit).
          if (msaa_2x_attachments_supported_) {
            source_sample_id = builder.createQuadOp(
                spv::OpBitFieldInsert, type_uint, source_sample_id,
                builder.createBinOp(spv::OpBitwiseXor, type_uint,
                                    dest_sample_id,
                                    builder.makeUintConstant(1)),
                builder.makeUintConstant(1), builder.makeUintConstant(1));
          } else {
            source_sample_id = builder.createQuadOp(
                spv::OpBitFieldInsert, type_uint, dest_sample_id,
                source_sample_id, builder.makeUintConstant(0),
                builder.makeUintConstant(1));
          }
        } else {
          // 64bpp -> 32bpp, 4x -> 1x.
          // 1 destination vertical pixel = 1 source vertical sample.
          source_sample_id = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint, source_sample_id,
              source_tile_pixel_y, builder.makeUintConstant(1),
              builder.makeUintConstant(1));
          source_tile_pixel_y = builder.createBinOp(
              spv::OpShiftRightLogical, type_uint, dest_tile_pixel_y,
              builder.makeUintConstant(1));
        }
        // 2 destination horizontal pixels = 1 source horizontal sample.
        // 4 destination horizontal pixels = 1 source horizontal pixel.
        source_tile_pixel_x =
            builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                dest_tile_pixel_x, builder.makeUintConstant(2));
      } else {
        // 64bpp -> 32bpp, 1x/2x -> 1x/2x.
        // The X part.
        // 2 destination horizontal pixels = 1 destination source pixel.
        source_tile_pixel_x =
            builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                dest_tile_pixel_x, builder.makeUintConstant(1));
        // Y is handled by common code.
      }
      // Half from the destination horizontal pixel index.
      source_color_half =
          builder.createBinOp(spv::OpBitwiseAnd, type_uint, dest_tile_pixel_x,
                              builder.makeUintConstant(1));
    }
    assert_true(source_color_half != spv::NoResult);
  } else {
    // Same bit count.
    if (key.source_msaa_samples != key.dest_msaa_samples) {
      if (key.source_msaa_samples >= xenos::MsaaSamples::k4X) {
        // Same BPP, 4x -> 1x/2x.
        if (key.dest_msaa_samples == xenos::MsaaSamples::k2X) {
          // Same BPP, 4x -> 2x.
          // Horizontal pixels to samples. Vertical sample (1/0 in the first bit
          // for native 2x or 0/1 in the second bit for 2x as 4x) to second
          // sample bit.
          if (msaa_2x_attachments_supported_) {
            source_sample_id = builder.createQuadOp(
                spv::OpBitFieldInsert, type_uint, dest_tile_pixel_x,
                builder.createBinOp(spv::OpBitwiseXor, type_uint,
                                    dest_sample_id,
                                    builder.makeUintConstant(1)),
                builder.makeUintConstant(1), builder.makeUintConstant(31));
          } else {
            source_sample_id = builder.createQuadOp(
                spv::OpBitFieldInsert, type_uint, dest_sample_id,
                dest_tile_pixel_x, builder.makeUintConstant(0),
                builder.makeUintConstant(1));
          }
          source_tile_pixel_x = builder.createBinOp(
              spv::OpShiftRightLogical, type_uint, dest_tile_pixel_x,
              builder.makeUintConstant(1));
        } else {
          // Same BPP, 4x -> 1x.
          // Pixels to samples.
          source_sample_id = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint,
              builder.createBinOp(spv::OpBitwiseAnd, type_uint,
                                  dest_tile_pixel_x,
                                  builder.makeUintConstant(1)),
              dest_tile_pixel_y, builder.makeUintConstant(1),
              builder.makeUintConstant(1));
          source_tile_pixel_x = builder.createBinOp(
              spv::OpShiftRightLogical, type_uint, dest_tile_pixel_x,
              builder.makeUintConstant(1));
          source_tile_pixel_y = builder.createBinOp(
              spv::OpShiftRightLogical, type_uint, dest_tile_pixel_y,
              builder.makeUintConstant(1));
        }
      } else {
        // Same BPP, 1x/2x -> 1x/2x/4x (as long as they're different).
        // Only the X part - Y is handled by common code.
        if (key.dest_msaa_samples >= xenos::MsaaSamples::k4X) {
          // Horizontal samples to pixels.
          source_tile_pixel_x = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint, dest_sample_id,
              dest_tile_pixel_x, builder.makeUintConstant(1),
              builder.makeUintConstant(31));
        }
      }
    }
  }
  // Common source Y and sample index for 1x/2x AA sources, independent of bits
  // per sample.
  if (key.source_msaa_samples < xenos::MsaaSamples::k4X &&
      key.source_msaa_samples != key.dest_msaa_samples) {
    if (key.dest_msaa_samples >= xenos::MsaaSamples::k4X) {
      // 1x/2x -> 4x.
      if (key.source_msaa_samples == xenos::MsaaSamples::k2X) {
        // 2x -> 4x.
        // Vertical samples (second bit) of 4x destination to vertical sample
        // (1, 0 for native 2x, or 0, 3 for 2x as 4x) of 2x source.
        source_sample_id =
            builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                dest_sample_id, builder.makeUintConstant(1));
        if (msaa_2x_attachments_supported_) {
          source_sample_id = builder.createBinOp(spv::OpBitwiseXor, type_uint,
                                                 source_sample_id,
                                                 builder.makeUintConstant(1));
        } else {
          source_sample_id = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint, source_sample_id,
              source_sample_id, builder.makeUintConstant(1),
              builder.makeUintConstant(1));
        }
      } else {
        // 1x -> 4x.
        // Vertical samples (second bit) to Y pixels.
        source_tile_pixel_y = builder.createQuadOp(
            spv::OpBitFieldInsert, type_uint,
            builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                dest_sample_id, builder.makeUintConstant(1)),
            dest_tile_pixel_y, builder.makeUintConstant(1),
            builder.makeUintConstant(31));
      }
    } else {
      // 1x/2x -> different 1x/2x.
      if (key.source_msaa_samples == xenos::MsaaSamples::k2X) {
        // 2x -> 1x.
        // Vertical pixels of 2x destination to vertical samples (1, 0 for
        // native 2x, or 0, 3 for 2x as 4x) of 1x source.
        source_sample_id =
            builder.createBinOp(spv::OpBitwiseAnd, type_uint, dest_tile_pixel_y,
                                builder.makeUintConstant(1));
        if (msaa_2x_attachments_supported_) {
          source_sample_id = builder.createBinOp(spv::OpBitwiseXor, type_uint,
                                                 source_sample_id,
                                                 builder.makeUintConstant(1));
        } else {
          source_sample_id = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint, source_sample_id,
              source_sample_id, builder.makeUintConstant(1),
              builder.makeUintConstant(1));
        }
        source_tile_pixel_y =
            builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                dest_tile_pixel_y, builder.makeUintConstant(1));
      } else {
        // 1x -> 2x.
        // Vertical samples (1/0 in the first bit for native 2x or 0/1 in the
        // second bit for 2x as 4x) of 2x destination to vertical pixels of 1x
        // source.
        if (msaa_2x_attachments_supported_) {
          source_tile_pixel_y = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint,
              builder.createBinOp(spv::OpBitwiseXor, type_uint, dest_sample_id,
                                  builder.makeUintConstant(1)),
              dest_tile_pixel_y, builder.makeUintConstant(1),
              builder.makeUintConstant(31));
        } else {
          source_tile_pixel_y = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint,
              builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                  dest_sample_id, builder.makeUintConstant(1)),
              dest_tile_pixel_y, builder.makeUintConstant(1),
              builder.makeUintConstant(31));
        }
      }
    }
  }

  uint32_t source_pixel_width_dwords_log2 =
      uint32_t(key.source_msaa_samples >= xenos::MsaaSamples::k4X) +
      uint32_t(source_is_64bpp);

  if (source_is_color != dest_is_color) {
    // Copying between color and depth / stencil - swap 40-32bpp-sample columns
    // in the pixel index within the source 32bpp tile.
    uint32_t source_32bpp_tile_half_pixels =
        tile_width_samples >> (1 + source_pixel_width_dwords_log2);
    source_tile_pixel_x = builder.createUnaryOp(
        spv::OpBitcast, type_uint,
        builder.createBinOp(
            spv::OpIAdd, type_int,
            builder.createUnaryOp(spv::OpBitcast, type_int,
                                  source_tile_pixel_x),
            builder.createTriOp(
                spv::OpSelect, type_int,
                builder.createBinOp(
                    spv::OpULessThan, builder.makeBoolType(),
                    source_tile_pixel_x,
                    builder.makeUintConstant(source_32bpp_tile_half_pixels)),
                builder.makeIntConstant(int32_t(source_32bpp_tile_half_pixels)),
                builder.makeIntConstant(
                    -int32_t(source_32bpp_tile_half_pixels)))));
  }

  // Transform the destination 32bpp tile index into the source. After the
  // addition, it may be negative - in which case, the transfer is done across
  // EDRAM addressing wrapping, and xenos::kEdramTileCount must be added to it,
  // but `& (xenos::kEdramTileCount - 1)` handles that regardless of the sign.
  spv::Id source_tile_index = builder.createBinOp(
      spv::OpBitwiseAnd, type_uint,
      builder.createUnaryOp(
          spv::OpBitcast, type_uint,
          builder.createBinOp(
              spv::OpIAdd, type_int,
              builder.createUnaryOp(spv::OpBitcast, type_int, dest_tile_index),
              builder.createTriOp(
                  spv::OpBitFieldSExtract, type_int,
                  builder.createUnaryOp(spv::OpBitcast, type_int,
                                        address_constant),
                  builder.makeUintConstant(xenos::kEdramPitchTilesBits * 2),
                  builder.makeUintConstant(xenos::kEdramBaseTilesBits + 1)))),
      builder.makeUintConstant(xenos::kEdramTileCount - 1));
  // Split the source 32bpp tile index into X and Y tile index within the source
  // image.
  spv::Id source_pitch_tiles = builder.createTriOp(
      spv::OpBitFieldUExtract, type_uint, address_constant,
      builder.makeUintConstant(xenos::kEdramPitchTilesBits),
      builder.makeUintConstant(xenos::kEdramPitchTilesBits));
  spv::Id source_tile_index_y = builder.createBinOp(
      spv::OpUDiv, type_uint, source_tile_index, source_pitch_tiles);
  spv::Id source_tile_index_x = builder.createBinOp(
      spv::OpUMod, type_uint, source_tile_index, source_pitch_tiles);
  // Finally calculate the source texture coordinates.
  spv::Id source_pixel_x_int = builder.createUnaryOp(
      spv::OpBitcast, type_int,
      builder.createBinOp(
          spv::OpIAdd, type_uint,
          builder.createBinOp(
              spv::OpIMul, type_uint,
              builder.makeUintConstant(tile_width_samples >>
                                       source_pixel_width_dwords_log2),
              source_tile_index_x),
          source_tile_pixel_x));
  spv::Id source_pixel_y_int = builder.createUnaryOp(
      spv::OpBitcast, type_int,
      builder.createBinOp(
          spv::OpIAdd, type_uint,
          builder.createBinOp(
              spv::OpIMul, type_uint,
              builder.makeUintConstant(
                  tile_height_samples >>
                  uint32_t(key.source_msaa_samples >= xenos::MsaaSamples::k2X)),
              source_tile_index_y),
          source_tile_pixel_y));

  // Load the source.

  spv::Builder::TextureParameters source_texture_parameters = {};
  id_vector_temp.clear();
  id_vector_temp.push_back(source_pixel_x_int);
  id_vector_temp.push_back(source_pixel_y_int);
  spv::Id source_coordinates[2] = {
      builder.createCompositeConstruct(type_int2, id_vector_temp),
  };
  spv::Id source_sample_ids_int[2] = {};
  if (key.source_msaa_samples != xenos::MsaaSamples::k1X) {
    source_sample_ids_int[0] =
        builder.createUnaryOp(spv::OpBitcast, type_int, source_sample_id);
  } else {
    source_texture_parameters.lod = builder.makeIntConstant(0);
  }
  // Go to the next sample or pixel along X if need to load two dwords.
  bool source_load_is_two_32bpp_samples = !source_is_64bpp && dest_is_64bpp;
  if (source_load_is_two_32bpp_samples) {
    if (key.source_msaa_samples >= xenos::MsaaSamples::k4X) {
      source_coordinates[1] = source_coordinates[0];
      source_sample_ids_int[1] = builder.createBinOp(
          spv::OpBitwiseOr, type_int, source_sample_ids_int[0],
          builder.makeIntConstant(1));
    } else {
      id_vector_temp.clear();
      id_vector_temp.push_back(builder.createBinOp(spv::OpBitwiseOr, type_int,
                                                   source_pixel_x_int,
                                                   builder.makeIntConstant(1)));
      id_vector_temp.push_back(source_pixel_y_int);
      source_coordinates[1] =
          builder.createCompositeConstruct(type_int2, id_vector_temp);
      source_sample_ids_int[1] = source_sample_ids_int[0];
    }
  }
  spv::Id source_color[2][4] = {};
  if (source_color_texture != spv::NoResult) {
    source_texture_parameters.sampler =
        builder.createLoad(source_color_texture, spv::NoPrecision);
    assert_true(source_color_component_type != spv::NoType);
    spv::Id source_color_vec4_type =
        builder.makeVectorType(source_color_component_type, 4);
    for (uint32_t i = 0; i <= uint32_t(source_load_is_two_32bpp_samples); ++i) {
      source_texture_parameters.coords = source_coordinates[i];
      source_texture_parameters.sample = source_sample_ids_int[i];
      spv::Id source_color_vec4 = builder.createTextureCall(
          spv::NoPrecision, source_color_vec4_type, false, true, false, false,
          false, source_texture_parameters, spv::ImageOperandsMaskNone);
      uint32_t source_color_components_remaining =
          source_color_texture_component_mask;
      uint32_t source_color_component_index;
      while (xe::bit_scan_forward(source_color_components_remaining,
                                  &source_color_component_index)) {
        source_color_components_remaining &=
            ~(uint32_t(1) << source_color_component_index);
        source_color[i][source_color_component_index] =
            builder.createCompositeExtract(source_color_vec4,
                                           source_color_component_type,
                                           source_color_component_index);
      }
    }
  }
  spv::Id source_depth_float[2] = {};
  if (source_depth_texture != spv::NoResult) {
    source_texture_parameters.sampler =
        builder.createLoad(source_depth_texture, spv::NoPrecision);
    for (uint32_t i = 0; i <= uint32_t(source_load_is_two_32bpp_samples); ++i) {
      source_texture_parameters.coords = source_coordinates[i];
      source_texture_parameters.sample = source_sample_ids_int[i];
      source_depth_float[i] = builder.createCompositeExtract(
          builder.createTextureCall(
              spv::NoPrecision, type_float4, false, true, false, false, false,
              source_texture_parameters, spv::ImageOperandsMaskNone),
          type_float, 0);
    }
  }
  spv::Id source_stencil[2] = {};
  if (source_stencil_texture != spv::NoResult) {
    source_texture_parameters.sampler =
        builder.createLoad(source_stencil_texture, spv::NoPrecision);
    for (uint32_t i = 0; i <= uint32_t(source_load_is_two_32bpp_samples); ++i) {
      source_texture_parameters.coords = source_coordinates[i];
      source_texture_parameters.sample = source_sample_ids_int[i];
      source_stencil[i] = builder.createCompositeExtract(
          builder.createTextureCall(
              spv::NoPrecision, type_uint4, false, true, false, false, false,
              source_texture_parameters, spv::ImageOperandsMaskNone),
          type_uint, 0);
    }
  }

  // Pick the needed 32bpp half of the 64bpp color.
  if (source_is_64bpp && !dest_is_64bpp) {
    uint32_t source_color_half_component_count =
        source_color_format_component_count >> 1;
    assert_true(source_color_half != spv::NoResult);
    spv::Id source_color_is_second_half =
        builder.createBinOp(spv::OpINotEqual, type_bool, source_color_half,
                            builder.makeUintConstant(0));
    if (mode.output == TransferOutput::kStencilBit) {
      source_color[0][0] = builder.createTriOp(
          spv::OpSelect, source_color_component_type,
          source_color_is_second_half,
          source_color[0][source_color_half_component_count],
          source_color[0][0]);
    } else {
      for (uint32_t i = 0; i < source_color_half_component_count; ++i) {
        source_color[0][i] = builder.createTriOp(
            spv::OpSelect, source_color_component_type,
            source_color_is_second_half,
            source_color[0][source_color_half_component_count + i],
            source_color[0][i]);
      }
    }
  }

  if (output_fragment_stencil_ref != spv::NoResult &&
      source_stencil[0] != spv::NoResult) {
    // For the depth -> depth case, write the stencil directly to the output.
    assert_true(mode.output == TransferOutput::kDepth);
    builder.createStore(
        builder.createUnaryOp(spv::OpBitcast, type_int, source_stencil[0]),
        output_fragment_stencil_ref);
  }

  if (dest_is_64bpp) {
    // Construct the 64bpp color from two 32-bit samples or one 64-bit sample.
    // If `packed` (two uints) are created, use the generic path involving
    // unpacking.
    // Otherwise, the fragment data output must be written to directly by the
    // reached control flow path.
    spv::Id packed[2] = {};
    if (source_is_color) {
      switch (source_color_format) {
        case xenos::ColorRenderTargetFormat::k_8_8_8_8:
        case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA: {
          spv::Id unorm_round_offset = builder.makeFloatConstant(0.5f);
          spv::Id unorm_scale = builder.makeFloatConstant(255.0f);
          spv::Id component_width = builder.makeUintConstant(8);
          for (uint32_t i = 0; i < 2; ++i) {
            packed[i] = builder.createUnaryOp(
                spv::OpConvertFToU, type_uint,
                builder.createBinOp(
                    spv::OpFAdd, type_float,
                    builder.createBinOp(spv::OpFMul, type_float,
                                        source_color[i][0], unorm_scale),
                    unorm_round_offset));
            for (uint32_t j = 1; j < 4; ++j) {
              packed[i] = builder.createQuadOp(
                  spv::OpBitFieldInsert, type_uint, packed[i],
                  builder.createUnaryOp(
                      spv::OpConvertFToU, type_uint,
                      builder.createBinOp(
                          spv::OpFAdd, type_float,
                          builder.createBinOp(spv::OpFMul, type_float,
                                              source_color[i][j], unorm_scale),
                          unorm_round_offset)),
                  builder.makeUintConstant(8 * j), component_width);
            }
          }
        } break;
        case xenos::ColorRenderTargetFormat::k_2_10_10_10:
        case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10: {
          spv::Id unorm_round_offset = builder.makeFloatConstant(0.5f);
          spv::Id unorm_scale_rgb = builder.makeFloatConstant(1023.0f);
          spv::Id width_rgb = builder.makeUintConstant(10);
          spv::Id unorm_scale_a = builder.makeFloatConstant(3.0f);
          spv::Id width_a = builder.makeUintConstant(2);
          for (uint32_t i = 0; i < 2; ++i) {
            packed[i] = builder.createUnaryOp(
                spv::OpConvertFToU, type_uint,
                builder.createBinOp(
                    spv::OpFAdd, type_float,
                    builder.createBinOp(spv::OpFMul, type_float,
                                        source_color[i][0], unorm_scale_rgb),
                    unorm_round_offset));
            for (uint32_t j = 1; j < 4; ++j) {
              packed[i] = builder.createQuadOp(
                  spv::OpBitFieldInsert, type_uint, packed[i],
                  builder.createUnaryOp(
                      spv::OpConvertFToU, type_uint,
                      builder.createBinOp(
                          spv::OpFAdd, type_float,
                          builder.createBinOp(
                              spv::OpFMul, type_float, source_color[i][j],
                              j == 3 ? unorm_scale_a : unorm_scale_rgb),
                          unorm_round_offset)),
                  builder.makeUintConstant(10 * j),
                  j == 3 ? width_a : width_rgb);
            }
          }
        } break;
        case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
        case xenos::ColorRenderTargetFormat::
            k_2_10_10_10_FLOAT_AS_16_16_16_16: {
          spv::Id width_rgb = builder.makeUintConstant(10);
          spv::Id float_0 = builder.makeFloatConstant(0.0f);
          spv::Id float_1 = builder.makeFloatConstant(1.0f);
          spv::Id unorm_round_offset = builder.makeFloatConstant(0.5f);
          spv::Id unorm_scale_a = builder.makeFloatConstant(3.0f);
          spv::Id offset_a = builder.makeUintConstant(30);
          spv::Id width_a = builder.makeUintConstant(2);
          for (uint32_t i = 0; i < 2; ++i) {
            // Float16 has a wider range for both color and alpha, also NaNs -
            // clamp and convert.
            packed[i] = SpirvShaderTranslator::UnclampedFloat32To7e3(
                builder, source_color[i][0], ext_inst_glsl_std_450);
            for (uint32_t j = 1; j < 3; ++j) {
              packed[i] = builder.createQuadOp(
                  spv::OpBitFieldInsert, type_uint, packed[i],
                  SpirvShaderTranslator::UnclampedFloat32To7e3(
                      builder, source_color[i][j], ext_inst_glsl_std_450),
                  builder.makeUintConstant(10 * j), width_rgb);
            }
            // Saturate and convert the alpha.
            spv::Id alpha_saturated = builder.createTriBuiltinCall(
                type_float, ext_inst_glsl_std_450, GLSLstd450NClamp,
                source_color[i][3], float_0, float_1);
            packed[i] = builder.createQuadOp(
                spv::OpBitFieldInsert, type_uint, packed[i],
                builder.createUnaryOp(
                    spv::OpConvertFToU, type_uint,
                    builder.createBinOp(
                        spv::OpFAdd, type_float,
                        builder.createBinOp(spv::OpFMul, type_float,
                                            alpha_saturated, unorm_scale_a),
                        unorm_round_offset)),
                offset_a, width_a);
          }
        } break;
        // All 64bpp formats, and all 16 bits per component formats, are
        // represented as integers in ownership transfer for safe handling of
        // NaN encodings and -32768 / -32767.
        // TODO(Triang3l): Handle the case when that's not true (no multisampled
        // sampled images, no 16-bit UNORM, no cross-packing 32bpp aliasing on a
        // portability subset device or a 64bpp format where that wouldn't help
        // anyway).
        case xenos::ColorRenderTargetFormat::k_16_16:
        case xenos::ColorRenderTargetFormat::k_16_16_FLOAT: {
          if (dest_color_format ==
              xenos::ColorRenderTargetFormat::k_32_32_FLOAT) {
            spv::Id component_offset_width = builder.makeUintConstant(16);
            spv::Id color_16_in_32[2];
            for (uint32_t i = 0; i < 2; ++i) {
              color_16_in_32[i] = builder.createQuadOp(
                  spv::OpBitFieldInsert, type_uint, source_color[i][0],
                  source_color[i][1], component_offset_width,
                  component_offset_width);
            }
            id_vector_temp.clear();
            id_vector_temp.push_back(color_16_in_32[0]);
            id_vector_temp.push_back(color_16_in_32[1]);
            builder.createStore(builder.createCompositeConstruct(
                                    type_fragment_data, id_vector_temp),
                                output_fragment_data);
          } else {
            id_vector_temp.clear();
            for (uint32_t i = 0; i < 4; ++i) {
              id_vector_temp.push_back(source_color[i >> 1][i & 1]);
            }
            builder.createStore(builder.createCompositeConstruct(
                                    type_fragment_data, id_vector_temp),
                                output_fragment_data);
          }
        } break;
        case xenos::ColorRenderTargetFormat::k_16_16_16_16:
        case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT: {
          if (dest_color_format ==
              xenos::ColorRenderTargetFormat::k_32_32_FLOAT) {
            spv::Id component_offset_width = builder.makeUintConstant(16);
            spv::Id color_16_in_32[2];
            for (uint32_t i = 0; i < 2; ++i) {
              color_16_in_32[i] = builder.createQuadOp(
                  spv::OpBitFieldInsert, type_uint, source_color[0][i << 1],
                  source_color[0][(i << 1) + 1], component_offset_width,
                  component_offset_width);
            }
            id_vector_temp.clear();
            id_vector_temp.push_back(color_16_in_32[0]);
            id_vector_temp.push_back(color_16_in_32[1]);
            builder.createStore(builder.createCompositeConstruct(
                                    type_fragment_data, id_vector_temp),
                                output_fragment_data);
          } else {
            id_vector_temp.clear();
            for (uint32_t i = 0; i < 4; ++i) {
              id_vector_temp.push_back(source_color[0][i]);
            }
            builder.createStore(builder.createCompositeConstruct(
                                    type_fragment_data, id_vector_temp),
                                output_fragment_data);
          }
        } break;
        // Float32 is transferred as uint32 to preserve NaN encodings. However,
        // multisampled sampled image support is optional in Vulkan.
        case xenos::ColorRenderTargetFormat::k_32_FLOAT: {
          for (uint32_t i = 0; i < 2; ++i) {
            packed[i] = source_color[i][0];
            if (!source_color_is_uint) {
              packed[i] =
                  builder.createUnaryOp(spv::OpBitcast, type_uint, packed[i]);
            }
          }
        } break;
        case xenos::ColorRenderTargetFormat::k_32_32_FLOAT: {
          for (uint32_t i = 0; i < 2; ++i) {
            packed[i] = source_color[0][i];
            if (!source_color_is_uint) {
              packed[i] =
                  builder.createUnaryOp(spv::OpBitcast, type_uint, packed[i]);
            }
          }
        } break;
      }
    } else {
      assert_true(source_depth_texture != spv::NoResult);
      assert_true(source_stencil_texture != spv::NoResult);
      spv::Id depth_offset = builder.makeUintConstant(8);
      spv::Id depth_width = builder.makeUintConstant(24);
      for (uint32_t i = 0; i < 2; ++i) {
        spv::Id depth24 = spv::NoResult;
        switch (source_depth_format) {
          case xenos::DepthRenderTargetFormat::kD24S8: {
            // Round to the nearest even integer. This seems to be the
            // correct conversion, adding +0.5 and rounding towards zero results
            // in red instead of black in the 4D5307E6 clear shader.
            depth24 = builder.createUnaryOp(
                spv::OpConvertFToU, type_uint,
                builder.createUnaryBuiltinCall(
                    type_float, ext_inst_glsl_std_450, GLSLstd450RoundEven,
                    builder.createBinOp(
                        spv::OpFMul, type_float, source_depth_float[i],
                        builder.makeFloatConstant(float(0xFFFFFF)))));
          } break;
          case xenos::DepthRenderTargetFormat::kD24FS8: {
            depth24 = SpirvShaderTranslator::PreClampedDepthTo20e4(
                builder, source_depth_float[i], depth_float24_round(), true,
                ext_inst_glsl_std_450);
          } break;
        }
        // Merge depth and stencil.
        packed[i] = builder.createQuadOp(spv::OpBitFieldInsert, type_uint,
                                         source_stencil[i], depth24,
                                         depth_offset, depth_width);
      }
    }
    // Common path unless there was a specialized one - unpack two packed 32-bit
    // parts.
    if (packed[0] != spv::NoResult) {
      assert_true(packed[1] != spv::NoResult);
      if (dest_color_format == xenos::ColorRenderTargetFormat::k_32_32_FLOAT) {
        id_vector_temp.clear();
        id_vector_temp.push_back(packed[0]);
        id_vector_temp.push_back(packed[1]);
        // Multisampled sampled images are optional in Vulkan, and image views
        // of different formats can't be created separately for sampled image
        // and color attachment usages, so no multisampled integer sampled image
        // support implies no multisampled integer framebuffer attachment
        // support in Xenia.
        if (!dest_color_is_uint) {
          for (spv::Id& float32 : id_vector_temp) {
            float32 =
                builder.createUnaryOp(spv::OpBitcast, type_float, float32);
          }
        }
        builder.createStore(builder.createCompositeConstruct(type_fragment_data,
                                                             id_vector_temp),
                            output_fragment_data);
      } else {
        spv::Id const_uint_0 = builder.makeUintConstant(0);
        spv::Id const_uint_16 = builder.makeUintConstant(16);
        id_vector_temp.clear();
        for (uint32_t i = 0; i < 4; ++i) {
          id_vector_temp.push_back(builder.createTriOp(
              spv::OpBitFieldUExtract, type_uint, packed[i >> 1],
              (i & 1) ? const_uint_16 : const_uint_0, const_uint_16));
        }
        // TODO(Triang3l): Handle the case when that's not true (no multisampled
        // sampled images, no 16-bit UNORM, no cross-packing 32bpp aliasing on a
        // portability subset device or a 64bpp format where that wouldn't help
        // anyway).
        builder.createStore(builder.createCompositeConstruct(type_fragment_data,
                                                             id_vector_temp),
                            output_fragment_data);
      }
    }
  } else {
    // If `packed` is created, use the generic path involving unpacking.
    // - For a color destination, the packed 32bpp color.
    // - For a depth / stencil destination, stencil in 0:7, depth in 8:31
    //   normally, or depth in 0:23 and zeros in 24:31 with packed_only_depth.
    // - For a stencil bit, stencil in 0:7.
    // Otherwise, the fragment data or fragment depth / stencil output must be
    // written to directly by the reached control flow path.
    spv::Id packed = spv::NoResult;
    bool packed_only_depth = false;
    if (source_is_color) {
      switch (source_color_format) {
        case xenos::ColorRenderTargetFormat::k_8_8_8_8:
        case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA: {
          if (dest_is_color &&
              (dest_color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8 ||
               dest_color_format ==
                   xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA)) {
            // Same format - passthrough.
            id_vector_temp.clear();
            for (uint32_t i = 0; i < 4; ++i) {
              id_vector_temp.push_back(source_color[0][i]);
            }
            builder.createStore(builder.createCompositeConstruct(
                                    type_fragment_data, id_vector_temp),
                                output_fragment_data);
          } else {
            spv::Id unorm_round_offset = builder.makeFloatConstant(0.5f);
            spv::Id unorm_scale = builder.makeFloatConstant(255.0f);
            uint32_t packed_component_offset = 0;
            if (mode.output == TransferOutput::kDepth) {
              // When need only depth, not stencil, skip the red component, and
              // put the depth from GBA directly in the lower bits.
              packed_component_offset = 1;
              packed_only_depth = true;
              if (output_fragment_stencil_ref != spv::NoResult) {
                builder.createStore(
                    builder.createUnaryOp(
                        spv::OpBitcast, type_int,
                        builder.createUnaryOp(
                            spv::OpConvertFToU, type_uint,
                            builder.createBinOp(
                                spv::OpFAdd, type_float,
                                builder.createBinOp(spv::OpFMul, type_float,
                                                    source_color[0][0],
                                                    unorm_scale),
                                unorm_round_offset))),
                    output_fragment_stencil_ref);
              }
            }
            packed = builder.createUnaryOp(
                spv::OpConvertFToU, type_uint,
                builder.createBinOp(
                    spv::OpFAdd, type_float,
                    builder.createBinOp(
                        spv::OpFMul, type_float,
                        source_color[0][packed_component_offset], unorm_scale),
                    unorm_round_offset));
            if (mode.output != TransferOutput::kStencilBit) {
              spv::Id component_width = builder.makeUintConstant(8);
              for (uint32_t i = 1; i < 4 - packed_component_offset; ++i) {
                packed = builder.createQuadOp(
                    spv::OpBitFieldInsert, type_uint, packed,
                    builder.createUnaryOp(
                        spv::OpConvertFToU, type_uint,
                        builder.createBinOp(
                            spv::OpFAdd, type_float,
                            builder.createBinOp(
                                spv::OpFMul, type_float,
                                source_color[0][packed_component_offset + i],
                                unorm_scale),
                            unorm_round_offset)),
                    builder.makeUintConstant(8 * i), component_width);
              }
            }
          }
        } break;
        case xenos::ColorRenderTargetFormat::k_2_10_10_10:
        case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10: {
          if (dest_is_color &&
              (dest_color_format ==
                   xenos::ColorRenderTargetFormat::k_2_10_10_10 ||
               dest_color_format == xenos::ColorRenderTargetFormat::
                                        k_2_10_10_10_AS_10_10_10_10)) {
            id_vector_temp.clear();
            for (uint32_t i = 0; i < 4; ++i) {
              id_vector_temp.push_back(source_color[0][i]);
            }
            builder.createStore(builder.createCompositeConstruct(
                                    type_fragment_data, id_vector_temp),
                                output_fragment_data);
          } else {
            spv::Id unorm_round_offset = builder.makeFloatConstant(0.5f);
            spv::Id unorm_scale_rgb = builder.makeFloatConstant(1023.0f);
            packed = builder.createUnaryOp(
                spv::OpConvertFToU, type_uint,
                builder.createBinOp(
                    spv::OpFAdd, type_float,
                    builder.createBinOp(spv::OpFMul, type_float,
                                        source_color[0][0], unorm_scale_rgb),
                    unorm_round_offset));
            if (mode.output != TransferOutput::kStencilBit) {
              spv::Id width_rgb = builder.makeUintConstant(10);
              spv::Id unorm_scale_a = builder.makeFloatConstant(3.0f);
              spv::Id width_a = builder.makeUintConstant(2);
              for (uint32_t i = 1; i < 4; ++i) {
                packed = builder.createQuadOp(
                    spv::OpBitFieldInsert, type_uint, packed,
                    builder.createUnaryOp(
                        spv::OpConvertFToU, type_uint,
                        builder.createBinOp(
                            spv::OpFAdd, type_float,
                            builder.createBinOp(
                                spv::OpFMul, type_float, source_color[0][i],
                                i == 3 ? unorm_scale_a : unorm_scale_rgb),
                            unorm_round_offset)),
                    builder.makeUintConstant(10 * i),
                    i == 3 ? width_a : width_rgb);
              }
            }
          }
        } break;
        case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
        case xenos::ColorRenderTargetFormat::
            k_2_10_10_10_FLOAT_AS_16_16_16_16: {
          if (dest_is_color &&
              (dest_color_format ==
                   xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT ||
               dest_color_format == xenos::ColorRenderTargetFormat::
                                        k_2_10_10_10_FLOAT_AS_16_16_16_16)) {
            id_vector_temp.clear();
            for (uint32_t i = 0; i < 4; ++i) {
              id_vector_temp.push_back(source_color[0][i]);
            }
            builder.createStore(builder.createCompositeConstruct(
                                    type_fragment_data, id_vector_temp),
                                output_fragment_data);
          } else {
            // Float16 has a wider range for both color and alpha, also NaNs -
            // clamp and convert.
            packed = SpirvShaderTranslator::UnclampedFloat32To7e3(
                builder, source_color[0][0], ext_inst_glsl_std_450);
            if (mode.output != TransferOutput::kStencilBit) {
              spv::Id width_rgb = builder.makeUintConstant(10);
              for (uint32_t i = 1; i < 3; ++i) {
                packed = builder.createQuadOp(
                    spv::OpBitFieldInsert, type_uint, packed,
                    SpirvShaderTranslator::UnclampedFloat32To7e3(
                        builder, source_color[0][i], ext_inst_glsl_std_450),
                    builder.makeUintConstant(10 * i), width_rgb);
              }
              // Saturate and convert the alpha.
              spv::Id alpha_saturated = builder.createTriBuiltinCall(
                  type_float, ext_inst_glsl_std_450, GLSLstd450NClamp,
                  source_color[0][3], builder.makeFloatConstant(0.0f),
                  builder.makeFloatConstant(1.0f));
              packed = builder.createQuadOp(
                  spv::OpBitFieldInsert, type_uint, packed,
                  builder.createUnaryOp(
                      spv::OpConvertFToU, type_uint,
                      builder.createBinOp(
                          spv::OpFAdd, type_float,
                          builder.createBinOp(spv::OpFMul, type_float,
                                              alpha_saturated,
                                              builder.makeFloatConstant(3.0f)),
                          builder.makeFloatConstant(0.5f))),
                  builder.makeUintConstant(30), builder.makeUintConstant(2));
            }
          }
        } break;
        case xenos::ColorRenderTargetFormat::k_16_16:
        case xenos::ColorRenderTargetFormat::k_16_16_16_16:
        case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
        case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT: {
          // All 64bpp formats, and all 16 bits per component formats, are
          // represented as integers in ownership transfer for safe handling of
          // NaN encodings and -32768 / -32767.
          // TODO(Triang3l): Handle the case when that's not true (no
          // multisampled sampled images, no 16-bit UNORM, no cross-packing
          // 32bpp aliasing on a portability subset device or a 64bpp format
          // where that wouldn't help anyway).
          if (dest_is_color &&
              (dest_color_format == xenos::ColorRenderTargetFormat::k_16_16 ||
               dest_color_format ==
                   xenos::ColorRenderTargetFormat::k_16_16_FLOAT)) {
            id_vector_temp.clear();
            for (uint32_t i = 0; i < 2; ++i) {
              id_vector_temp.push_back(source_color[0][i]);
            }
            builder.createStore(builder.createCompositeConstruct(
                                    type_fragment_data, id_vector_temp),
                                output_fragment_data);
          } else {
            packed = source_color[0][0];
            if (mode.output != TransferOutput::kStencilBit) {
              spv::Id component_offset_width = builder.makeUintConstant(16);
              packed = builder.createQuadOp(
                  spv::OpBitFieldInsert, type_uint, packed, source_color[0][1],
                  component_offset_width, component_offset_width);
            }
          }
        } break;
        // Float32 is transferred as uint32 to preserve NaN encodings. However,
        // multisampled sampled image support is optional in Vulkan.
        case xenos::ColorRenderTargetFormat::k_32_FLOAT:
        case xenos::ColorRenderTargetFormat::k_32_32_FLOAT: {
          packed = source_color[0][0];
          if (!source_color_is_uint) {
            packed = builder.createUnaryOp(spv::OpBitcast, type_uint, packed);
          }
        } break;
      }
    } else if (source_depth_float[0] != spv::NoResult) {
      if (mode.output == TransferOutput::kDepth &&
          dest_depth_format == source_depth_format) {
        builder.createStore(source_depth_float[0], output_fragment_depth);
      } else {
        switch (source_depth_format) {
          case xenos::DepthRenderTargetFormat::kD24S8: {
            // Round to the nearest even integer. This seems to be the correct
            // conversion, adding +0.5 and rounding towards zero results in red
            // instead of black in the 4D5307E6 clear shader.
            packed = builder.createUnaryOp(
                spv::OpConvertFToU, type_uint,
                builder.createUnaryBuiltinCall(
                    type_float, ext_inst_glsl_std_450, GLSLstd450RoundEven,
                    builder.createBinOp(
                        spv::OpFMul, type_float, source_depth_float[0],
                        builder.makeFloatConstant(float(0xFFFFFF)))));
          } break;
          case xenos::DepthRenderTargetFormat::kD24FS8: {
            packed = SpirvShaderTranslator::PreClampedDepthTo20e4(
                builder, source_depth_float[0], depth_float24_round(), true,
                ext_inst_glsl_std_450);
          } break;
        }
        if (mode.output == TransferOutput::kDepth) {
          packed_only_depth = true;
        } else {
          // Merge depth and stencil.
          packed = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint, source_stencil[0], packed,
              builder.makeUintConstant(8), builder.makeUintConstant(24));
        }
      }
    }
    switch (mode.output) {
      case TransferOutput::kColor: {
        // Unless a special path was taken, unpack the raw 32bpp value into the
        // 32bpp color output.
        if (packed != spv::NoResult) {
          switch (dest_color_format) {
            case xenos::ColorRenderTargetFormat::k_8_8_8_8:
            case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA: {
              spv::Id component_width = builder.makeUintConstant(8);
              spv::Id unorm_scale = builder.makeFloatConstant(1.0f / 255.0f);
              id_vector_temp.clear();
              for (uint32_t i = 0; i < 4; ++i) {
                id_vector_temp.push_back(builder.createBinOp(
                    spv::OpFMul, type_float,
                    builder.createUnaryOp(
                        spv::OpConvertUToF, type_float,
                        builder.createTriOp(
                            spv::OpBitFieldUExtract, type_uint, packed,
                            builder.makeUintConstant(8 * i), component_width)),
                    unorm_scale));
              }
              builder.createStore(builder.createCompositeConstruct(
                                      type_fragment_data, id_vector_temp),
                                  output_fragment_data);
            } break;
            case xenos::ColorRenderTargetFormat::k_2_10_10_10:
            case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10: {
              spv::Id width_rgb = builder.makeUintConstant(10);
              spv::Id unorm_scale_rgb =
                  builder.makeFloatConstant(1.0f / 1023.0f);
              spv::Id width_a = builder.makeUintConstant(2);
              spv::Id unorm_scale_a = builder.makeFloatConstant(1.0f / 3.0f);
              id_vector_temp.clear();
              for (uint32_t i = 0; i < 4; ++i) {
                id_vector_temp.push_back(builder.createBinOp(
                    spv::OpFMul, type_float,
                    builder.createUnaryOp(
                        spv::OpConvertUToF, type_float,
                        builder.createTriOp(spv::OpBitFieldUExtract, type_uint,
                                            packed,
                                            builder.makeUintConstant(10 * i),
                                            i == 3 ? width_a : width_rgb)),
                    i == 3 ? unorm_scale_a : unorm_scale_rgb));
              }
              builder.createStore(builder.createCompositeConstruct(
                                      type_fragment_data, id_vector_temp),
                                  output_fragment_data);
            } break;
            case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
            case xenos::ColorRenderTargetFormat::
                k_2_10_10_10_FLOAT_AS_16_16_16_16: {
              id_vector_temp.clear();
              // Color.
              spv::Id width_rgb = builder.makeUintConstant(10);
              for (uint32_t i = 0; i < 3; ++i) {
                id_vector_temp.push_back(SpirvShaderTranslator::Float7e3To32(
                    builder, packed, 10 * i, false, ext_inst_glsl_std_450));
              }
              // Alpha.
              id_vector_temp.push_back(builder.createBinOp(
                  spv::OpFMul, type_float,
                  builder.createUnaryOp(
                      spv::OpConvertUToF, type_float,
                      builder.createTriOp(spv::OpBitFieldUExtract, type_uint,
                                          packed, builder.makeUintConstant(30),
                                          builder.makeUintConstant(2))),
                  builder.makeFloatConstant(1.0f / 3.0f)));
              builder.createStore(builder.createCompositeConstruct(
                                      type_fragment_data, id_vector_temp),
                                  output_fragment_data);
            } break;
            case xenos::ColorRenderTargetFormat::k_16_16:
            case xenos::ColorRenderTargetFormat::k_16_16_FLOAT: {
              // All 16 bits per component formats are represented as integers
              // in ownership transfer for safe handling of NaN encodings and
              // -32768 / -32767.
              // TODO(Triang3l): Handle the case when that's not true (no
              // multisampled sampled images, no 16-bit UNORM, no cross-packing
              // 32bpp aliasing on a portability subset device or a 64bpp format
              // where that wouldn't help anyway).
              spv::Id component_offset_width = builder.makeUintConstant(16);
              id_vector_temp.clear();
              for (uint32_t i = 0; i < 2; ++i) {
                id_vector_temp.push_back(builder.createTriOp(
                    spv::OpBitFieldUExtract, type_uint, packed,
                    i ? component_offset_width : builder.makeUintConstant(0),
                    component_offset_width));
              }
              builder.createStore(builder.createCompositeConstruct(
                                      type_fragment_data, id_vector_temp),
                                  output_fragment_data);
            } break;
            case xenos::ColorRenderTargetFormat::k_32_FLOAT: {
              // Float32 is transferred as uint32 to preserve NaN encodings.
              // However, multisampled sampled images are optional in Vulkan,
              // and image views of different formats can't be created
              // separately for sampled image and color attachment usages, so no
              // multisampled integer sampled image support implies no
              // multisampled integer framebuffer attachment support in Xenia.
              spv::Id float32 = packed;
              if (!dest_color_is_uint) {
                float32 =
                    builder.createUnaryOp(spv::OpBitcast, type_float, float32);
              }
              builder.createStore(float32, output_fragment_data);
            } break;
            default:
              // A 64bpp format (handled separately) or an invalid one.
              assert_unhandled_case(dest_color_format);
          }
        }
      } break;
      case TransferOutput::kDepth: {
        if (packed) {
          spv::Id guest_depth24 = packed;
          if (!packed_only_depth) {
            // Extract the depth bits.
            guest_depth24 =
                builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                    guest_depth24, builder.makeUintConstant(8));
          }
          // Load the host float32 depth, check if, when converted to the guest
          // format, it's the same as the guest source, thus up to date, and if
          // it is, write host float32 depth, otherwise do the guest -> host
          // conversion.
          spv::Id host_depth32 = spv::NoResult;
          if (host_depth_source_texture != spv::NoResult) {
            // Convert position and sample index from within the destination
            // tile to within the host depth source tile, like for the guest
            // render target, but for 32bpp -> 32bpp only.
            spv::Id host_depth_source_sample_id = dest_sample_id;
            spv::Id host_depth_source_tile_pixel_x = dest_tile_pixel_x;
            spv::Id host_depth_source_tile_pixel_y = dest_tile_pixel_y;
            if (key.host_depth_source_msaa_samples != key.dest_msaa_samples) {
              if (key.host_depth_source_msaa_samples >=
                  xenos::MsaaSamples::k4X) {
                // 4x -> 1x/2x.
                if (key.dest_msaa_samples == xenos::MsaaSamples::k2X) {
                  // 4x -> 2x.
                  // Horizontal pixels to samples. Vertical sample (1/0 in the
                  // first bit for native 2x or 0/1 in the second bit for 2x as
                  // 4x) to second sample bit.
                  if (msaa_2x_attachments_supported_) {
                    host_depth_source_sample_id = builder.createQuadOp(
                        spv::OpBitFieldInsert, type_uint, dest_tile_pixel_x,
                        builder.createBinOp(spv::OpBitwiseXor, type_uint,
                                            dest_sample_id,
                                            builder.makeUintConstant(1)),
                        builder.makeUintConstant(1),
                        builder.makeUintConstant(31));
                  } else {
                    host_depth_source_sample_id = builder.createQuadOp(
                        spv::OpBitFieldInsert, type_uint, dest_sample_id,
                        dest_tile_pixel_x, builder.makeUintConstant(0),
                        builder.makeUintConstant(1));
                  }
                  host_depth_source_tile_pixel_x = builder.createBinOp(
                      spv::OpShiftRightLogical, type_uint, dest_tile_pixel_x,
                      builder.makeUintConstant(1));
                } else {
                  // 4x -> 1x.
                  // Pixels to samples.
                  host_depth_source_sample_id = builder.createQuadOp(
                      spv::OpBitFieldInsert, type_uint,
                      builder.createBinOp(spv::OpBitwiseAnd, type_uint,
                                          dest_tile_pixel_x,
                                          builder.makeUintConstant(1)),
                      dest_tile_pixel_y, builder.makeUintConstant(1),
                      builder.makeUintConstant(1));
                  host_depth_source_tile_pixel_x = builder.createBinOp(
                      spv::OpShiftRightLogical, type_uint, dest_tile_pixel_x,
                      builder.makeUintConstant(1));
                  host_depth_source_tile_pixel_y = builder.createBinOp(
                      spv::OpShiftRightLogical, type_uint, dest_tile_pixel_y,
                      builder.makeUintConstant(1));
                }
              } else {
                // 1x/2x -> 1x/2x/4x (as long as they're different).
                // Only the X part - Y is handled by common code.
                if (key.dest_msaa_samples >= xenos::MsaaSamples::k4X) {
                  // Horizontal samples to pixels.
                  host_depth_source_tile_pixel_x = builder.createQuadOp(
                      spv::OpBitFieldInsert, type_uint, dest_sample_id,
                      dest_tile_pixel_x, builder.makeUintConstant(1),
                      builder.makeUintConstant(31));
                }
              }
              // Host depth source Y and sample index for 1x/2x AA sources.
              if (key.host_depth_source_msaa_samples <
                  xenos::MsaaSamples::k4X) {
                if (key.dest_msaa_samples >= xenos::MsaaSamples::k4X) {
                  // 1x/2x -> 4x.
                  if (key.host_depth_source_msaa_samples ==
                      xenos::MsaaSamples::k2X) {
                    // 2x -> 4x.
                    // Vertical samples (second bit) of 4x destination to
                    // vertical sample (1, 0 for native 2x, or 0, 3 for 2x as
                    // 4x) of 2x source.
                    host_depth_source_sample_id = builder.createBinOp(
                        spv::OpShiftRightLogical, type_uint, dest_sample_id,
                        builder.makeUintConstant(1));
                    if (msaa_2x_attachments_supported_) {
                      host_depth_source_sample_id =
                          builder.createBinOp(spv::OpBitwiseXor, type_uint,
                                              host_depth_source_sample_id,
                                              builder.makeUintConstant(1));
                    } else {
                      host_depth_source_sample_id =
                          builder.createQuadOp(spv::OpBitFieldInsert, type_uint,
                                               host_depth_source_sample_id,
                                               host_depth_source_sample_id,
                                               builder.makeUintConstant(1),
                                               builder.makeUintConstant(1));
                    }
                  } else {
                    // 1x -> 4x.
                    // Vertical samples (second bit) to Y pixels.
                    host_depth_source_tile_pixel_y = builder.createQuadOp(
                        spv::OpBitFieldInsert, type_uint,
                        builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                            dest_sample_id,
                                            builder.makeUintConstant(1)),
                        dest_tile_pixel_y, builder.makeUintConstant(1),
                        builder.makeUintConstant(31));
                  }
                } else {
                  // 1x/2x -> different 1x/2x.
                  if (key.host_depth_source_msaa_samples ==
                      xenos::MsaaSamples::k2X) {
                    // 2x -> 1x.
                    // Vertical pixels of 2x destination to vertical samples (1,
                    // 0 for native 2x, or 0, 3 for 2x as 4x) of 1x source.
                    host_depth_source_sample_id = builder.createBinOp(
                        spv::OpBitwiseAnd, type_uint, dest_tile_pixel_y,
                        builder.makeUintConstant(1));
                    if (msaa_2x_attachments_supported_) {
                      host_depth_source_sample_id =
                          builder.createBinOp(spv::OpBitwiseXor, type_uint,
                                              host_depth_source_sample_id,
                                              builder.makeUintConstant(1));
                    } else {
                      host_depth_source_sample_id =
                          builder.createQuadOp(spv::OpBitFieldInsert, type_uint,
                                               host_depth_source_sample_id,
                                               host_depth_source_sample_id,
                                               builder.makeUintConstant(1),
                                               builder.makeUintConstant(1));
                    }
                    host_depth_source_tile_pixel_y = builder.createBinOp(
                        spv::OpShiftRightLogical, type_uint, dest_tile_pixel_y,
                        builder.makeUintConstant(1));
                  } else {
                    // 1x -> 2x.
                    // Vertical samples (1/0 in the first bit for native 2x or
                    // 0/1 in the second bit for 2x as 4x) of 2x destination to
                    // vertical pixels of 1x source.
                    if (msaa_2x_attachments_supported_) {
                      host_depth_source_tile_pixel_y = builder.createQuadOp(
                          spv::OpBitFieldInsert, type_uint,
                          builder.createBinOp(spv::OpBitwiseXor, type_uint,
                                              dest_sample_id,
                                              builder.makeUintConstant(1)),
                          dest_tile_pixel_y, builder.makeUintConstant(1),
                          builder.makeUintConstant(31));
                    } else {
                      host_depth_source_tile_pixel_y = builder.createQuadOp(
                          spv::OpBitFieldInsert, type_uint,
                          builder.createBinOp(spv::OpShiftRightLogical,
                                              type_uint, dest_sample_id,
                                              builder.makeUintConstant(1)),
                          dest_tile_pixel_y, builder.makeUintConstant(1),
                          builder.makeUintConstant(31));
                    }
                  }
                }
              }
            }
            assert_true(push_constants_member_host_depth_address != UINT32_MAX);
            id_vector_temp.clear();
            id_vector_temp.push_back(builder.makeIntConstant(
                int32_t(push_constants_member_host_depth_address)));
            spv::Id host_depth_address_constant = builder.createLoad(
                builder.createAccessChain(spv::StorageClassPushConstant,
                                          push_constants, id_vector_temp),
                spv::NoPrecision);
            // Transform the destination tile index into the host depth source.
            // After the addition, it may be negative - in which case, the
            // transfer is done across EDRAM addressing wrapping, and
            // xenos::kEdramTileCount must be added to it, but
            // `& (xenos::kEdramTileCount - 1)` handles that regardless of the
            // sign.
            spv::Id host_depth_source_tile_index = builder.createBinOp(
                spv::OpBitwiseAnd, type_uint,
                builder.createUnaryOp(
                    spv::OpBitcast, type_uint,
                    builder.createBinOp(
                        spv::OpIAdd, type_int,
                        builder.createUnaryOp(spv::OpBitcast, type_int,
                                              dest_tile_index),
                        builder.createTriOp(
                            spv::OpBitFieldSExtract, type_int,
                            builder.createUnaryOp(spv::OpBitcast, type_int,
                                                  host_depth_address_constant),
                            builder.makeUintConstant(
                                xenos::kEdramPitchTilesBits * 2),
                            builder.makeUintConstant(
                                xenos::kEdramBaseTilesBits + 1)))),
                builder.makeUintConstant(xenos::kEdramTileCount - 1));
            // Split the host depth source tile index into X and Y tile index
            // within the source image.
            spv::Id host_depth_source_pitch_tiles = builder.createTriOp(
                spv::OpBitFieldUExtract, type_uint, host_depth_address_constant,
                builder.makeUintConstant(xenos::kEdramPitchTilesBits),
                builder.makeUintConstant(xenos::kEdramPitchTilesBits));
            spv::Id host_depth_source_tile_index_y = builder.createBinOp(
                spv::OpUDiv, type_uint, host_depth_source_tile_index,
                host_depth_source_pitch_tiles);
            spv::Id host_depth_source_tile_index_x = builder.createBinOp(
                spv::OpUMod, type_uint, host_depth_source_tile_index,
                host_depth_source_pitch_tiles);
            // Finally calculate the host depth source texture coordinates.
            spv::Id host_depth_source_pixel_x_int = builder.createUnaryOp(
                spv::OpBitcast, type_int,
                builder.createBinOp(
                    spv::OpIAdd, type_uint,
                    builder.createBinOp(spv::OpIMul, type_uint,
                                        builder.makeUintConstant(
                                            tile_width_samples >>
                                            uint32_t(key.source_msaa_samples >=
                                                     xenos::MsaaSamples::k4X)),
                                        host_depth_source_tile_index_x),
                    host_depth_source_tile_pixel_x));
            spv::Id host_depth_source_pixel_y_int = builder.createUnaryOp(
                spv::OpBitcast, type_int,
                builder.createBinOp(
                    spv::OpIAdd, type_uint,
                    builder.createBinOp(spv::OpIMul, type_uint,
                                        builder.makeUintConstant(
                                            tile_height_samples >>
                                            uint32_t(key.source_msaa_samples >=
                                                     xenos::MsaaSamples::k2X)),
                                        host_depth_source_tile_index_y),
                    host_depth_source_tile_pixel_y));
            // Load the host depth source.
            spv::Builder::TextureParameters
                host_depth_source_texture_parameters = {};
            host_depth_source_texture_parameters.sampler =
                builder.createLoad(host_depth_source_texture, spv::NoPrecision);
            id_vector_temp.clear();
            id_vector_temp.push_back(host_depth_source_pixel_x_int);
            id_vector_temp.push_back(host_depth_source_pixel_y_int);
            host_depth_source_texture_parameters.coords =
                builder.createCompositeConstruct(type_int2, id_vector_temp);
            if (key.host_depth_source_msaa_samples != xenos::MsaaSamples::k1X) {
              host_depth_source_texture_parameters.sample =
                  builder.createUnaryOp(spv::OpBitcast, type_int,
                                        host_depth_source_sample_id);
            } else {
              host_depth_source_texture_parameters.lod =
                  builder.makeIntConstant(0);
            }
            host_depth32 = builder.createCompositeExtract(
                builder.createTextureCall(spv::NoPrecision, type_float4, false,
                                          true, false, false, false,
                                          host_depth_source_texture_parameters,
                                          spv::ImageOperandsMaskNone),
                type_float, 0);
          } else if (host_depth_source_buffer != spv::NoResult) {
            // Get the address in the EDRAM scratch buffer and load from there.
            // The beginning of the buffer is (0, 0) of the destination.
            // 40-sample columns are not swapped for addressing simplicity
            // (because this is used for depth -> depth transfers, where
            // swapping isn't needed).
            // Convert samples to pixels.
            assert_true(key.host_depth_source_msaa_samples ==
                        xenos::MsaaSamples::k1X);
            spv::Id dest_tile_sample_x = dest_tile_pixel_x;
            spv::Id dest_tile_sample_y = dest_tile_pixel_y;
            if (key.dest_msaa_samples >= xenos::MsaaSamples::k2X) {
              if (key.dest_msaa_samples >= xenos::MsaaSamples::k4X) {
                // Horizontal sample index in bit 0.
                dest_tile_sample_x = builder.createQuadOp(
                    spv::OpBitFieldInsert, type_uint, dest_sample_id,
                    dest_tile_pixel_x, builder.makeUintConstant(1),
                    builder.makeUintConstant(31));
              }
              // Vertical sample index as 1 or 0 in bit 0 for true 2x or as 0
              // or 1 in bit 1 for 4x or for 2x emulated as 4x.
              dest_tile_sample_y = builder.createQuadOp(
                  spv::OpBitFieldInsert, type_uint,
                  builder.createBinOp(
                      (key.dest_msaa_samples == xenos::MsaaSamples::k2X &&
                       msaa_2x_attachments_supported_)
                          ? spv::OpBitwiseXor
                          : spv::OpShiftRightLogical,
                      type_uint, dest_sample_id, builder.makeUintConstant(1)),
                  dest_tile_pixel_y, builder.makeUintConstant(1),
                  builder.makeUintConstant(31));
            }
            // Combine the tile sample index and the tile index.
            // The tile index doesn't need to be wrapped, as the host depth is
            // written to the beginning of the buffer, without the base offset.
            spv::Id host_depth_offset = builder.createBinOp(
                spv::OpIAdd, type_uint,
                builder.createBinOp(
                    spv::OpIMul, type_uint,
                    builder.makeUintConstant(tile_width_samples *
                                             tile_height_samples),
                    dest_tile_index),
                builder.createBinOp(
                    spv::OpIAdd, type_uint,
                    builder.createBinOp(
                        spv::OpIMul, type_uint,
                        builder.makeUintConstant(tile_width_samples),
                        dest_tile_sample_y),
                    dest_tile_sample_x));
            id_vector_temp.clear();
            // The only SSBO structure member.
            id_vector_temp.push_back(builder.makeIntConstant(0));
            id_vector_temp.push_back(builder.createUnaryOp(
                spv::OpBitcast, type_int, host_depth_offset));
            // StorageBuffer since SPIR-V 1.3, but since SPIR-V 1.0 is
            // generated, it's Uniform.
            host_depth32 = builder.createUnaryOp(
                spv::OpBitcast, type_float,
                builder.createLoad(
                    builder.createAccessChain(spv::StorageClassUniform,
                                              host_depth_source_buffer,
                                              id_vector_temp),
                    spv::NoPrecision));
          }
          spv::Block* depth24_to_depth32_header = builder.getBuildPoint();
          spv::Id depth24_to_depth32_convert_id = spv::NoResult;
          spv::Block* depth24_to_depth32_merge = nullptr;
          spv::Id host_depth24 = spv::NoResult;
          if (host_depth32 != spv::NoResult) {
            // Convert the host depth value to the guest format and check if it
            // matches the value in the currently owning guest render target.
            switch (dest_depth_format) {
              case xenos::DepthRenderTargetFormat::kD24S8: {
                // Round to the nearest even integer. This seems to be the
                // correct conversion, adding +0.5 and rounding towards zero
                // results in red instead of black in the 4D5307E6 clear shader.
                host_depth24 = builder.createUnaryOp(
                    spv::OpConvertFToU, type_uint,
                    builder.createUnaryBuiltinCall(
                        type_float, ext_inst_glsl_std_450, GLSLstd450RoundEven,
                        builder.createBinOp(
                            spv::OpFMul, type_float, host_depth32,
                            builder.makeFloatConstant(float(0xFFFFFF)))));
              } break;
              case xenos::DepthRenderTargetFormat::kD24FS8: {
                host_depth24 = SpirvShaderTranslator::PreClampedDepthTo20e4(
                    builder, host_depth32, depth_float24_round(), true,
                    ext_inst_glsl_std_450);
              } break;
            }
            assert_true(host_depth24 != spv::NoResult);
            // Update the header block pointer after the conversion (to avoid
            // assuming that the conversion doesn't branch).
            depth24_to_depth32_header = builder.getBuildPoint();
            spv::Id host_depth_outdated = builder.createBinOp(
                spv::OpINotEqual, type_bool, guest_depth24, host_depth24);
            spv::Block& depth24_to_depth32_convert_entry =
                builder.makeNewBlock();
            {
              spv::Block& depth24_to_depth32_merge_block =
                  builder.makeNewBlock();
              depth24_to_depth32_merge = &depth24_to_depth32_merge_block;
            }
            builder.createSelectionMerge(depth24_to_depth32_merge,
                                         spv::SelectionControlMaskNone);
            builder.createConditionalBranch(host_depth_outdated,
                                            &depth24_to_depth32_convert_entry,
                                            depth24_to_depth32_merge);
            builder.setBuildPoint(&depth24_to_depth32_convert_entry);
          }
          // Convert the guest 24-bit depth to float32 (in an open conditional
          // if the host depth is also loaded).
          spv::Id guest_depth32 = spv::NoResult;
          switch (dest_depth_format) {
            case xenos::DepthRenderTargetFormat::kD24S8: {
              // Multiplying by 1.0 / 0xFFFFFF produces an incorrect result (for
              // 0xC00000, for instance - which is 2_10_10_10 clear to 0001) -
              // rescale from 0...0xFFFFFF to 0...0x1000000 doing what true
              // float division followed by multiplication does (on x86-64 MSVC
              // with default SSE rounding) - values starting from 0x800000
              // become bigger by 1; then accurately bias the result's exponent.
              guest_depth32 = builder.createBinOp(
                  spv::OpFMul, type_float,
                  builder.createUnaryOp(
                      spv::OpConvertUToF, type_float,
                      builder.createBinOp(
                          spv::OpIAdd, type_uint, guest_depth24,
                          builder.createBinOp(spv::OpShiftRightLogical,
                                              type_uint, guest_depth24,
                                              builder.makeUintConstant(23)))),
                  builder.makeFloatConstant(1.0f / float(1 << 24)));
            } break;
            case xenos::DepthRenderTargetFormat::kD24FS8: {
              guest_depth32 = SpirvShaderTranslator::Depth20e4To32(
                  builder, guest_depth24, 0, true, false,
                  ext_inst_glsl_std_450);
            } break;
          }
          assert_true(guest_depth32 != spv::NoResult);
          spv::Id fragment_depth32 = guest_depth32;
          if (host_depth32 != spv::NoResult) {
            assert_not_null(depth24_to_depth32_merge);
            spv::Id depth24_to_depth32_result_block_id =
                builder.getBuildPoint()->getId();
            builder.createBranch(depth24_to_depth32_merge);
            builder.setBuildPoint(depth24_to_depth32_merge);
            id_vector_temp.clear();
            id_vector_temp.push_back(guest_depth32);
            id_vector_temp.push_back(depth24_to_depth32_result_block_id);
            id_vector_temp.push_back(host_depth32);
            id_vector_temp.push_back(depth24_to_depth32_header->getId());
            fragment_depth32 =
                builder.createOp(spv::OpPhi, type_float, id_vector_temp);
          }
          builder.createStore(fragment_depth32, output_fragment_depth);
          // Unpack the stencil into the stencil reference output if needed and
          // not already written.
          if (!packed_only_depth &&
              output_fragment_stencil_ref != spv::NoResult) {
            builder.createStore(
                builder.createUnaryOp(
                    spv::OpBitcast, type_int,
                    builder.createBinOp(spv::OpBitwiseAnd, type_uint, packed,
                                        builder.makeUintConstant(UINT8_MAX))),
                output_fragment_stencil_ref);
          }
        }
      } break;
      case TransferOutput::kStencilBit: {
        if (packed) {
          // Kill the sample if the needed stencil bit is not set.
          assert_true(push_constants_member_stencil_mask != UINT32_MAX);
          id_vector_temp.clear();
          id_vector_temp.push_back(builder.makeIntConstant(
              int32_t(push_constants_member_stencil_mask)));
          spv::Id stencil_mask_constant = builder.createLoad(
              builder.createAccessChain(spv::StorageClassPushConstant,
                                        push_constants, id_vector_temp),
              spv::NoPrecision);
          SpirvBuilder::IfBuilder stencil_kill_if(
              builder.createBinOp(
                  spv::OpIEqual, type_bool,
                  builder.createBinOp(spv::OpBitwiseAnd, type_uint, packed,
                                      stencil_mask_constant),
                  builder.makeUintConstant(0)),
              spv::SelectionControlMaskNone, builder);
          builder.createNoResultOp(spv::OpKill);
          // OpKill terminates the block.
          stencil_kill_if.makeEndIf(false);
        }
      } break;
    }
  }

  // End the main function and make it the entry point.
  builder.leaveFunction();
  builder.addExecutionMode(main_function, spv::ExecutionModeOriginUpperLeft);
  if (output_fragment_depth != spv::NoResult) {
    builder.addExecutionMode(main_function, spv::ExecutionModeDepthReplacing);
  }
  if (output_fragment_stencil_ref != spv::NoResult) {
    builder.addExecutionMode(main_function,
                             spv::ExecutionModeStencilRefReplacingEXT);
  }
  spv::Instruction* entry_point =
      builder.addEntryPoint(spv::ExecutionModelFragment, main_function, "main");
  for (spv::Id interface_id : main_interface) {
    entry_point->addIdOperand(interface_id);
  }

  // Serialize the shader code.
  std::vector<unsigned int> shader_code;
  builder.dump(shader_code);

  // Create the shader module, and store the handle even if creation fails not
  // to try to create it again later.
  VkShaderModule shader_module = ui::vulkan::util::CreateShaderModule(
      vulkan_device, reinterpret_cast<const uint32_t*>(shader_code.data()),
      sizeof(uint32_t) * shader_code.size());
  if (shader_module == VK_NULL_HANDLE) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the render target ownership "
        "transfer shader 0x{:08X}",
        key.key);
  }
  transfer_shaders_.emplace(key, shader_module);
  return shader_module;
}

VkPipeline const* VulkanRenderTargetCache::GetTransferPipelines(
    TransferPipelineKey key) {
  auto pipeline_it = transfer_pipelines_.find(key);
  if (pipeline_it != transfer_pipelines_.end()) {
    return pipeline_it->second[0] != VK_NULL_HANDLE ? pipeline_it->second.data()
                                                    : nullptr;
  }

  VkRenderPass render_pass =
      GetHostRenderTargetsRenderPass(key.render_pass_key);
  VkShaderModule fragment_shader_module = GetTransferShader(key.shader_key);
  if (render_pass == VK_NULL_HANDLE ||
      fragment_shader_module == VK_NULL_HANDLE) {
    transfer_pipelines_.emplace(key, std::array<VkPipeline, 4>{});
    return nullptr;
  }

  const TransferModeInfo& mode = kTransferModes[size_t(key.shader_key.mode)];

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  const ui::vulkan::VulkanDevice::Properties& device_properties =
      vulkan_device->properties();

  uint32_t dest_sample_count = uint32_t(1)
                               << uint32_t(key.shader_key.dest_msaa_samples);
  bool dest_is_masked_sample =
      dest_sample_count > 1 && !device_properties.sampleRateShading;

  VkPipelineShaderStageCreateInfo shader_stages[2];
  shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shader_stages[0].pNext = nullptr;
  shader_stages[0].flags = 0;
  shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shader_stages[0].module = transfer_passthrough_vertex_shader_;
  shader_stages[0].pName = "main";
  shader_stages[0].pSpecializationInfo = nullptr;
  shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shader_stages[1].pNext = nullptr;
  shader_stages[1].flags = 0;
  shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shader_stages[1].module = fragment_shader_module;
  shader_stages[1].pName = "main";
  shader_stages[1].pSpecializationInfo = nullptr;
  VkSpecializationMapEntry sample_id_specialization_map_entry;
  uint32_t sample_id_specialization_constant;
  VkSpecializationInfo sample_id_specialization_info;
  if (dest_is_masked_sample) {
    sample_id_specialization_map_entry.constantID = 0;
    sample_id_specialization_map_entry.offset = 0;
    sample_id_specialization_map_entry.size = sizeof(uint32_t);
    sample_id_specialization_constant = 0;
    sample_id_specialization_info.mapEntryCount = 1;
    sample_id_specialization_info.pMapEntries =
        &sample_id_specialization_map_entry;
    sample_id_specialization_info.dataSize =
        sizeof(sample_id_specialization_constant);
    sample_id_specialization_info.pData = &sample_id_specialization_constant;
    shader_stages[1].pSpecializationInfo = &sample_id_specialization_info;
  }

  VkVertexInputBindingDescription vertex_input_binding;
  vertex_input_binding.binding = 0;
  vertex_input_binding.stride = sizeof(float) * 2;
  vertex_input_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription vertex_input_attribute;
  vertex_input_attribute.location = 0;
  vertex_input_attribute.binding = 0;
  vertex_input_attribute.format = VK_FORMAT_R32G32_SFLOAT;
  vertex_input_attribute.offset = 0;
  VkPipelineVertexInputStateCreateInfo vertex_input_state;
  vertex_input_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_state.pNext = nullptr;
  vertex_input_state.flags = 0;
  vertex_input_state.vertexBindingDescriptionCount = 1;
  vertex_input_state.pVertexBindingDescriptions = &vertex_input_binding;
  vertex_input_state.vertexAttributeDescriptionCount = 1;
  vertex_input_state.pVertexAttributeDescriptions = &vertex_input_attribute;

  VkPipelineInputAssemblyStateCreateInfo input_assembly_state;
  input_assembly_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly_state.pNext = nullptr;
  input_assembly_state.flags = 0;
  input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly_state.primitiveRestartEnable = VK_FALSE;

  // Dynamic, to stay within maxViewportDimensions while preferring a
  // power-of-two factor for converting from pixel coordinates to NDC for exact
  // precision.
  VkPipelineViewportStateCreateInfo viewport_state;
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.pNext = nullptr;
  viewport_state.flags = 0;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = nullptr;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = nullptr;

  VkPipelineRasterizationStateCreateInfo rasterization_state = {};
  rasterization_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization_state.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization_state.cullMode = VK_CULL_MODE_NONE;
  rasterization_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization_state.lineWidth = 1.0f;

  // For samples other than the first, will be changed for the pipelines for
  // other samples.
  VkSampleMask sample_mask = UINT32_MAX;
  VkPipelineMultisampleStateCreateInfo multisample_state = {};
  multisample_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample_state.rasterizationSamples =
      (dest_sample_count == 2 && !msaa_2x_attachments_supported_)
          ? VK_SAMPLE_COUNT_4_BIT
          : VkSampleCountFlagBits(dest_sample_count);
  if (dest_sample_count > 1) {
    if (device_properties.sampleRateShading) {
      multisample_state.sampleShadingEnable = VK_TRUE;
      multisample_state.minSampleShading = 1.0f;
      if (dest_sample_count == 2 && !msaa_2x_attachments_supported_) {
        // Emulating 2x MSAA as samples 0 and 3 of 4x MSAA when 2x is not
        // supported.
        sample_mask = 0b1001;
      }
    } else {
      sample_mask = 0b1;
    }
    if (sample_mask != UINT32_MAX) {
      multisample_state.pSampleMask = &sample_mask;
    }
  }

  // Whether the depth / stencil state is used depends on the presence of a
  // depth attachment in the render pass - but not making assumptions about
  // whether the render pass contains any specific attachments, so setting up
  // valid depth / stencil state unconditionally.
  VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {};
  depth_stencil_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  if (mode.output == TransferOutput::kDepth) {
    depth_stencil_state.depthTestEnable = VK_TRUE;
    depth_stencil_state.depthWriteEnable = VK_TRUE;
    depth_stencil_state.depthCompareOp = cvars::depth_transfer_not_equal_test
                                             ? VK_COMPARE_OP_NOT_EQUAL
                                             : VK_COMPARE_OP_ALWAYS;
  }
  if ((mode.output == TransferOutput::kDepth &&
       vulkan_device->extensions().ext_EXT_shader_stencil_export) ||
      mode.output == TransferOutput::kStencilBit) {
    depth_stencil_state.stencilTestEnable = VK_TRUE;
    depth_stencil_state.front.failOp = VK_STENCIL_OP_KEEP;
    depth_stencil_state.front.passOp = VK_STENCIL_OP_REPLACE;
    depth_stencil_state.front.depthFailOp = VK_STENCIL_OP_REPLACE;
    // Using ALWAYS, not NOT_EQUAL, so depth writing is unaffected by stencil
    // being different.
    depth_stencil_state.front.compareOp = VK_COMPARE_OP_ALWAYS;
    // Will be dynamic for stencil bit output.
    depth_stencil_state.front.writeMask = UINT8_MAX;
    depth_stencil_state.front.reference = UINT8_MAX;
    depth_stencil_state.back = depth_stencil_state.front;
  }

  // Whether the color blend state is used depends on the presence of color
  // attachments in the render pass - but not making assumptions about whether
  // the render pass contains any specific attachments, so setting up valid
  // color blend state unconditionally.
  VkPipelineColorBlendAttachmentState
      color_blend_attachments[xenos::kMaxColorRenderTargets] = {};
  VkPipelineColorBlendStateCreateInfo color_blend_state = {};
  color_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend_state.attachmentCount =
      32 - xe::lzcnt(key.render_pass_key.depth_and_color_used >> 1);
  color_blend_state.pAttachments = color_blend_attachments;
  if (mode.output == TransferOutput::kColor) {
    assert_true(device_properties.independentBlend);
    color_blend_attachments[key.shader_key.dest_color_rt_index].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  }

  std::array<VkDynamicState, 3> dynamic_states;
  VkPipelineDynamicStateCreateInfo dynamic_state;
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.pNext = nullptr;
  dynamic_state.flags = 0;
  dynamic_state.dynamicStateCount = 0;
  dynamic_state.pDynamicStates = dynamic_states.data();
  dynamic_states[dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
  dynamic_states[dynamic_state.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;
  if (mode.output == TransferOutput::kStencilBit) {
    dynamic_states[dynamic_state.dynamicStateCount++] =
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK;
  }

  std::array<VkPipeline, 4> pipelines{};
  VkGraphicsPipelineCreateInfo pipeline_create_info;
  pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_create_info.pNext = nullptr;
  pipeline_create_info.flags = 0;
  if (dest_is_masked_sample) {
    pipeline_create_info.flags |= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
  }
  pipeline_create_info.stageCount = uint32_t(xe::countof(shader_stages));
  pipeline_create_info.pStages = shader_stages;
  pipeline_create_info.pVertexInputState = &vertex_input_state;
  pipeline_create_info.pInputAssemblyState = &input_assembly_state;
  pipeline_create_info.pTessellationState = nullptr;
  pipeline_create_info.pViewportState = &viewport_state;
  pipeline_create_info.pRasterizationState = &rasterization_state;
  pipeline_create_info.pMultisampleState = &multisample_state;
  pipeline_create_info.pDepthStencilState = &depth_stencil_state;
  pipeline_create_info.pColorBlendState = &color_blend_state;
  pipeline_create_info.pDynamicState = &dynamic_state;
  pipeline_create_info.layout =
      transfer_pipeline_layouts_[size_t(mode.pipeline_layout)];
  pipeline_create_info.renderPass = render_pass;
  pipeline_create_info.subpass = 0;
  pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  pipeline_create_info.basePipelineIndex = -1;
  if (dfn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                    &pipeline_create_info, nullptr,
                                    &pipelines[0]) != VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the render target ownership "
        "transfer pipeline for render pass 0x{:08X}, shader 0x{:08X}",
        key.render_pass_key.key, key.shader_key.key);
    transfer_pipelines_.emplace(key, std::array<VkPipeline, 4>{});
    return nullptr;
  }
  if (dest_is_masked_sample) {
    assert_true(multisample_state.pSampleMask == &sample_mask);
    pipeline_create_info.flags = (pipeline_create_info.flags &
                                  ~VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT) |
                                 VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    pipeline_create_info.basePipelineHandle = pipelines[0];
    for (uint32_t i = 1; i < dest_sample_count; ++i) {
      // Emulating 2x MSAA as samples 0 and 3 of 4x MSAA when 2x is not
      // supported.
      uint32_t host_sample_index =
          (dest_sample_count == 2 && !msaa_2x_attachments_supported_ && i == 1)
              ? 3
              : i;
      sample_id_specialization_constant = host_sample_index;
      sample_mask = uint32_t(1) << host_sample_index;
      if (dfn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                        &pipeline_create_info, nullptr,
                                        &pipelines[i]) != VK_SUCCESS) {
        XELOGE(
            "VulkanRenderTargetCache: Failed to create the render target "
            "ownership transfer pipeline for render pass 0x{:08X}, shader "
            "0x{:08X}, sample {}",
            key.render_pass_key.key, key.shader_key.key, i);
        for (uint32_t j = 0; j < i; ++j) {
          dfn.vkDestroyPipeline(device, pipelines[j], nullptr);
        }
        transfer_pipelines_.emplace(key, std::array<VkPipeline, 4>{});
        return nullptr;
      }
    }
  }
  return transfer_pipelines_.emplace(key, pipelines).first->second.data();
}

bool VulkanRenderTargetCache::TryDeferBdFramegraphDepthTransfer(
    VulkanRenderTarget& dest, const Transfer& transfer,
    const RenderPassKey* schedule_pass_key) {
  if ((!cvars::gpu_bd_framegraph_depth &&
       !cvars::gpu_bd_framegraph_depth_shadow) ||
      bd_framegraph_flushing_legacy_ ||
      !transfer.source || transfer.source == &dest ||
      transfer.host_depth_source) {
    return false;
  }
  auto& source = *static_cast<VulkanRenderTarget*>(transfer.source);
  // A fused sampled-read / attachment-write pair must not be image feedback.
  // The recognized 2X and 4X surfaces are separate allocations, but prove that
  // from the Vulkan handles too and leave every aliasing case on rung 2.
  if (source.image() == VK_NULL_HANDLE || dest.image() == VK_NULL_HANDLE ||
      source.image() == dest.image() ||
      (source.view_depth_stencil() != VK_NULL_HANDLE &&
       source.view_depth_stencil() == dest.view_depth_stencil())) {
    return false;
  }
  RenderTargetKey source_key = source.key();
  RenderTargetKey dest_key = dest.key();

  // In shadow mode this observer sees every ownership-transfer source and
  // destination before the legacy path records it. Treat reuse of a watched
  // destination by another transfer as its first non-consumer use.
  if (cvars::gpu_bd_framegraph_depth_shadow) {
    for (BdFramegraphShadowDepthTransfer& shadow_entry :
         bd_framegraph_shadow_depth_transfers_) {
      if (!shadow_entry.matched && !shadow_entry.first_dest_use_seen &&
          (shadow_entry.dest == &source || shadow_entry.dest == &dest)) {
        LogBdFramegraphShadowFirstNonconsumer(
            shadow_entry,
            shadow_entry.dest == &source ? "transfer-source"
                                         : "transfer-destination",
            schedule_pass_key ? *schedule_pass_key : RenderPassKey());
      }
    }
  }
  // Milestone 1 deliberately recognizes only the recurring BD field edge.
  if (!source_key.is_depth || !dest_key.is_depth ||
      source_key.base_tiles != 810 || dest_key.base_tiles != 810 ||
      source_key.GetPitchTiles() != 9 || dest_key.GetPitchTiles() != 9 ||
      source_key.msaa_samples != xenos::MsaaSamples::k2X ||
      dest_key.msaa_samples != xenos::MsaaSamples::k4X ||
      !command_processor_.GetVulkanDevice()
           ->extensions()
           .ext_EXT_shader_stencil_export) {
    return false;
  }

  // Shadow mode remains the rung-1 observer and wins if both cvars are set.
  if (cvars::gpu_bd_framegraph_depth_shadow) {
    BdFramegraphShadowDepthTransfer shadow_entry;
    shadow_entry.id = bd_framegraph_shadow_next_id_++;
    shadow_entry.source = &source;
    shadow_entry.dest = &dest;
    shadow_entry.source_key = source_key;
    shadow_entry.dest_key = dest_key;
    shadow_entry.source_generation = source.bd_native_depth_generation();
    shadow_entry.dest_generation = dest.bd_native_depth_generation();
    shadow_entry.dest_view = dest.view_depth_stencil();
    shadow_entry.schedule_frame = command_processor_.GetCurrentFrame();
    shadow_entry.schedule_submission =
        command_processor_.GetCurrentSubmission();
    if (schedule_pass_key) {
      shadow_entry.schedule_pass_key = *schedule_pass_key;
    }
    shadow_entry.schedule_pass_serial = bd_framegraph_shadow_pass_serial_;
    XELOGI(
        "BD SHADOW: schedule frame={} submission={} passkey={:08X} entry={} "
        "destkey{{base={} pitchT={} msaa={} format={}}} gen={} "
        "srckey{{base={} pitchT={} msaa={} format={}}} gen={}",
        shadow_entry.schedule_frame, shadow_entry.schedule_submission,
        shadow_entry.schedule_pass_key.key, shadow_entry.id,
        dest_key.base_tiles, dest_key.GetPitchTiles(),
        uint32_t(dest_key.msaa_samples), dest_key.resource_format,
        shadow_entry.dest_generation, source_key.base_tiles,
        source_key.GetPitchTiles(), uint32_t(source_key.msaa_samples),
        source_key.resource_format, shadow_entry.source_generation);
    bd_framegraph_shadow_depth_transfers_.push_back(shadow_entry);
    ++bd_framegraph_shadow_scheduled_;
    return false;
  }
  if (!bd_framegraph_deferred_depth_transfers_.empty()) {
    FallbackBdFramegraphDepthTransfer("replacement before consumer");
  }
  BdFramegraphDeferredDepthTransfer entry(
      &source, &dest, source_key, dest_key,
      source.bd_native_depth_generation(), dest.bd_native_depth_generation(),
      transfer);
  entry.shader_key.dest_msaa_samples = dest_key.msaa_samples;
  entry.shader_key.dest_color_rt_index = 0;
  entry.shader_key.dest_resource_format = dest_key.resource_format;
  entry.shader_key.source_msaa_samples = source_key.msaa_samples;
  entry.shader_key.host_depth_source_msaa_samples =
      xenos::MsaaSamples::k1X;
  entry.shader_key.source_resource_format = source_key.resource_format;
  entry.shader_key.mode = TransferMode::kDepthToDepth;
  bd_framegraph_deferred_depth_transfers_.push_back(entry);
  bd_framegraph_rung3_scheduled.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void VulkanRenderTargetCache::LogBdFramegraphShadowFirstNonconsumer(
    BdFramegraphShadowDepthTransfer& entry, const char* use,
    RenderPassKey pass_key) {
  entry.first_dest_use_seen = true;
  ++bd_framegraph_shadow_first_nonconsumer_;
  XELOGI(
      "BD SHADOW: first-dest-use-nonconsumer frame={} passkey={:08X} "
      "entry={} use={} destkey{{base={} pitchT={} msaa={} format={}}} gen={}",
      command_processor_.GetCurrentFrame(), pass_key.key, entry.id, use,
      entry.dest_key.base_tiles, entry.dest_key.GetPitchTiles(),
      uint32_t(entry.dest_key.msaa_samples), entry.dest_key.resource_format,
      entry.dest_generation);
}

void VulkanRenderTargetCache::FallbackBdFramegraphDepthTransfer(
    const char* reason) {
  if (bd_framegraph_deferred_depth_transfers_.empty()) {
    return;
  }
  BdFramegraphDeferredDepthTransfer entry =
      bd_framegraph_deferred_depth_transfers_.front();
  bd_framegraph_deferred_depth_transfers_.clear();
  bd_framegraph_rung3_relocated_fallback.fetch_add(
      1, std::memory_order_relaxed);
  XELOGI("BD FRAMEGRAPH: rung3-fallback reason={}", reason);
  RenderTarget* render_targets[1] = {entry.dest};
  std::vector<Transfer> transfers[1];
  transfers[0].push_back(entry.transfer);
  bd_framegraph_flushing_legacy_ = true;
  PerformTransfersAndResolveClears(1, render_targets, transfers);
  bd_framegraph_flushing_legacy_ = false;
}
bool VulkanRenderTargetCache::PrepareBdFramegraphDepthConsumer(
    const Framebuffer* framebuffer, bool can_begin_new_pass) {
  if (cvars::gpu_bd_framegraph_depth_shadow) {
    ++bd_framegraph_shadow_pass_serial_;
    const RenderTarget* const* attachments =
        reinterpret_cast<const RenderTarget* const*>(
            last_update_framebuffer_attachments_);
    const RenderTarget* depth = attachments[0];
    const RenderPassKey pass_key = last_update_render_pass_key_;
    const bool dump = cvars::gpu_bd_framegraph_depth_dump;
    for (BdFramegraphShadowDepthTransfer& entry :
         bd_framegraph_shadow_depth_transfers_) {
      // Access ledger (dump mode): count every access to this edge's dest/source
      // through frame end, independent of the first-match short-circuit below, so
      // the expiry summary reveals multiple/earlier consumers, source mutation,
      // and generation drift (5.6-sol dump A: relocation vs late-consumer).
      if (dump) {
        if (entry.source && depth == entry.source) {
          ++entry.ledger_source_depth_binds;
          if (entry.source->bd_native_depth_generation() !=
              entry.source_generation) {
            ++entry.ledger_source_gen_changes;
          }
        }
        if (entry.dest && depth == entry.dest) {
          ++entry.ledger_dest_depth_binds;
          if (!entry.ledger_first_dest_depth_serial) {
            entry.ledger_first_dest_depth_serial =
                bd_framegraph_shadow_pass_serial_;
          }
          if (entry.dest->bd_native_depth_generation() != entry.dest_generation) {
            ++entry.ledger_dest_gen_changes;
          }
          const bool exact_view =
              framebuffer && framebuffer->depth_view == entry.dest_view &&
              entry.dest_view == entry.dest->view_depth_stencil() &&
              entry.dest->key().key == entry.dest_key.key &&
              entry.dest->bd_native_depth_generation() == entry.dest_generation;
          if (exact_view && !entry.ledger_match_serial) {
            entry.ledger_match_serial = bd_framegraph_shadow_pass_serial_;
          }
          // Path A Stage 0 consumer census: record the distinct color surface(s)
          // this dest-depth consumer renders into (attachments[1] = color RT 0).
          const RenderTarget* consumer_color = attachments[1];
          if (consumer_color) {
            RenderTargetKey ckey = consumer_color->key();
            bool seen = false;
            for (uint32_t c = 0; c < entry.ledger_consumer_color_count; ++c) {
              if (entry.ledger_consumer_color_keys[c].key == ckey.key) {
                seen = true;
                break;
              }
            }
            if (!seen && entry.ledger_consumer_color_count <
                             uint32_t(std::size(entry.ledger_consumer_color_keys))) {
              entry.ledger_consumer_color_keys[entry.ledger_consumer_color_count++] =
                  ckey;
            }
          }
        }
        if (entry.dest) {
          for (uint32_t i = 1; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
            if (attachments[i] == entry.dest) {
              ++entry.ledger_dest_color_binds;
              break;
            }
          }
        }
      }
      if (entry.matched || entry.first_dest_use_seen) {
        continue;
      }
      const bool identity_matches = entry.dest && depth == entry.dest;
      const bool key_matches =
          identity_matches && entry.dest->key().key == entry.dest_key.key;
      const bool generation_matches =
          key_matches && entry.dest->bd_native_depth_generation() ==
                             entry.dest_generation;
      const bool view_matches =
          generation_matches && framebuffer &&
          framebuffer->depth_view == entry.dest_view &&
          entry.dest_view == entry.dest->view_depth_stencil();
      if (view_matches) {
        entry.matched = true;
        entry.first_dest_use_seen = true;
        ++bd_framegraph_shadow_matched_;
        XELOGI(
            "BD SHADOW: consumer-match frame={} passkey={:08X} entry={} "
            "passes_since_sched={} destkey{{base={} pitchT={} msaa={} "
            "format={}}} gen={}",
            command_processor_.GetCurrentFrame(), pass_key.key, entry.id,
            bd_framegraph_shadow_pass_serial_ - entry.schedule_pass_serial,
            entry.dest_key.base_tiles, entry.dest_key.GetPitchTiles(),
            uint32_t(entry.dest_key.msaa_samples),
            entry.dest_key.resource_format, entry.dest_generation);
        continue;
      }
      bool bound_as_color = false;
      for (uint32_t i = 1; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
        bound_as_color |= attachments[i] == entry.dest;
      }
      if (bound_as_color) {
        LogBdFramegraphShadowFirstNonconsumer(entry, "color-attachment",
                                              pass_key);
      } else if (identity_matches &&
                 (!key_matches || !generation_matches ||
                  (framebuffer && framebuffer->depth_view != entry.dest_view))) {
        LogBdFramegraphShadowFirstNonconsumer(
            entry, "depth-identity-generation-view-mismatch", pass_key);
      }
    }
    // Pure observer: never prepare the deferred/in-pass execution path.
    return false;
  }
  if (!cvars::gpu_bd_framegraph_depth ||
      bd_framegraph_deferred_depth_transfers_.empty()) {
    return false;
  }
  BdFramegraphDeferredDepthTransfer& entry =
      bd_framegraph_deferred_depth_transfers_.front();
  const RenderTarget* const* attachments =
      reinterpret_cast<const RenderTarget* const*>(
          last_update_framebuffer_attachments_);
  const RenderTarget* depth = attachments[0];
  for (uint32_t i = 1; i < 1 + xenos::kMaxColorRenderTargets; ++i) {
    if (attachments[i] == entry.dest) {
      FallbackBdFramegraphDepthTransfer("destination bound as non-depth");
      return false;
    }
  }
  if (depth == entry.source) {
    VulkanRenderTarget* source = entry.source;
    FallbackBdFramegraphDepthTransfer("source generation change");
    source->MarkBdNativeDepthAuthoritative();
    return false;
  }
  const bool dest_view_seen =
      framebuffer && framebuffer->depth_view == entry.dest->view_depth_stencil();
  if (dest_view_seen != (depth == entry.dest)) {
    FallbackBdFramegraphDepthTransfer("ambiguous consumer identity");
    return false;
  }
  if (depth != entry.dest) {
    return false;
  }
  if (!can_begin_new_pass || framebuffer != last_update_framebuffer_ ||
      !dest_view_seen) {
    FallbackBdFramegraphDepthTransfer("unsupported consumer pass entry");
    return false;
  }
  if (entry.source->key().key != entry.source_key.key ||
      entry.dest->key().key != entry.dest_key.key ||
      entry.source->bd_native_depth_generation() != entry.source_generation) {
    FallbackBdFramegraphDepthTransfer("source identity or generation change");
    return false;
  }
  if (entry.dest->bd_native_depth_generation() != entry.dest_generation) {
    FallbackBdFramegraphDepthTransfer("intervening destination write");
    return false;
  }

  // Rung 2 only confirms the consumer identity here. Do not pre-transition the
  // Source transition and cached-state mutation wait until the command
  // processor has closed the previous render pass.
  entry.consumer_render_pass_key = last_update_render_pass_key_;
  entry.prepared = true;
  return true;
}

void VulkanRenderTargetCache::EndFrameBdFramegraphDepthShadow() {
  if (cvars::gpu_bd_framegraph_depth &&
      !cvars::gpu_bd_framegraph_depth_shadow &&
      ++bd_framegraph_rung3_swaps == 30) {
    XELOGI(
        "BD FRAMEGRAPH: rung3 30-swap summary scheduled={} fused={} "
        "relocated-fallback={}",
        bd_framegraph_rung3_scheduled.exchange(0, std::memory_order_relaxed),
        bd_framegraph_rung3_fused.exchange(0, std::memory_order_relaxed),
        bd_framegraph_rung3_relocated_fallback.exchange(
            0, std::memory_order_relaxed));
    bd_framegraph_rung3_swaps = 0;
  }
  if (!cvars::gpu_bd_framegraph_depth_shadow) {
    bd_framegraph_shadow_depth_transfers_.clear();
    return;
  }
  const uint64_t frame = command_processor_.GetCurrentFrame();
  for (const BdFramegraphShadowDepthTransfer& entry :
       bd_framegraph_shadow_depth_transfers_) {
    if (cvars::gpu_bd_framegraph_depth_dump) {
      // Decisive read: dest_depth_binds>1 => multiple consumers (needs one
      // resident depth, option d). first_dest_depth_serial<match_serial => an
      // earlier dest-depth pass than the fused target (late-consumer/prestate).
      // source_depth_binds>0 or *_gen_changes>0 => source mutated / versioning
      // drift after schedule (relocation structurally invalid). All zero + one
      // bind + match==first => relocation valid, look at pixel output instead.
      XELOGI(
          "BD SHADOW LEDGER: frame={} entry={} matched={} sched_serial={} "
          "match_serial={} first_dest_depth_serial={} dest_depth_binds={} "
          "dest_color_binds={} source_depth_binds={} dest_gen_changes={} "
          "source_gen_changes={} dst{{base={} pitchT={} msaa={}}} "
          "src{{base={} pitchT={} msaa={}}}",
          frame, entry.id, entry.matched ? 1 : 0, entry.schedule_pass_serial,
          entry.ledger_match_serial, entry.ledger_first_dest_depth_serial,
          entry.ledger_dest_depth_binds, entry.ledger_dest_color_binds,
          entry.ledger_source_depth_binds, entry.ledger_dest_gen_changes,
          entry.ledger_source_gen_changes, entry.dest_key.base_tiles,
          entry.dest_key.GetPitchTiles(), uint32_t(entry.dest_key.msaa_samples),
          entry.source_key.base_tiles, entry.source_key.GetPitchTiles(),
          uint32_t(entry.source_key.msaa_samples));
      // Path A Stage 0 consumer census: identify the color surfaces the
      // dest-depth consumers render into (which effects test this depth).
      for (uint32_t c = 0; c < entry.ledger_consumer_color_count; ++c) {
        const RenderTargetKey& ck = entry.ledger_consumer_color_keys[c];
        XELOGI(
            "BD SHADOW CONSUMER: frame={} entry={} idx={} of={} "
            "color{{base={} pitchT={} fmt={} msaa={}}}",
            frame, entry.id, c, entry.ledger_consumer_color_count, ck.base_tiles,
            ck.GetPitchTiles(), ck.resource_format,
            uint32_t(ck.msaa_samples));
      }
    }
    if (!entry.matched) {
      ++bd_framegraph_shadow_unmatched_;
      XELOGI(
          "BD SHADOW: expiry unmatched frame={} scheduled_frame={} entry={} "
          "first_use_nonconsumer={} destkey{{base={} pitchT={} msaa={} "
          "format={}}} gen={}",
          frame, entry.schedule_frame, entry.id,
          entry.first_dest_use_seen ? 1 : 0, entry.dest_key.base_tiles,
          entry.dest_key.GetPitchTiles(),
          uint32_t(entry.dest_key.msaa_samples),
          entry.dest_key.resource_format, entry.dest_generation);
    }
  }
  bd_framegraph_shadow_depth_transfers_.clear();
  if (++bd_framegraph_shadow_swaps_ == 30) {
    XELOGI(
        "BD SHADOW: 30-swap summary scheduled={} matched={} unmatched={} "
        "first-use-nonconsumer={}",
        bd_framegraph_shadow_scheduled_, bd_framegraph_shadow_matched_,
        bd_framegraph_shadow_unmatched_,
        bd_framegraph_shadow_first_nonconsumer_);
    bd_framegraph_shadow_swaps_ = 0;
    bd_framegraph_shadow_scheduled_ = 0;
    bd_framegraph_shadow_matched_ = 0;
    bd_framegraph_shadow_unmatched_ = 0;
    bd_framegraph_shadow_first_nonconsumer_ = 0;
  }
}

bool VulkanRenderTargetCache::ExecutePreparedBdFramegraphDepthConsumer(
    RenderPassKey consumer_render_pass_key, VkRenderPass consumer_render_pass,
    const Framebuffer* framebuffer, const VkRect2D& consumer_render_area) {
  if (bd_framegraph_deferred_depth_transfers_.empty() ||
      !bd_framegraph_deferred_depth_transfers_.front().prepared) {
    return false;
  }
  BdFramegraphDeferredDepthTransfer& entry =
      bd_framegraph_deferred_depth_transfers_.front();
  if (!framebuffer || framebuffer != last_update_framebuffer_ ||
      consumer_render_pass == VK_NULL_HANDLE ||
      consumer_render_pass != last_update_render_pass_ ||
      consumer_render_pass_key != last_update_render_pass_key_ ||
      consumer_render_pass_key != entry.consumer_render_pass_key) {
    FallbackBdFramegraphDepthTransfer("consumer render pass changed after prepare");
    return false;
  }
  if (!entry.source || !entry.dest ||
      entry.source->key().key != entry.source_key.key ||
      entry.dest->key().key != entry.dest_key.key ||
      entry.source->image() == VK_NULL_HANDLE ||
      entry.dest->image() == VK_NULL_HANDLE ||
      entry.source->image() == entry.dest->image() ||
      (entry.source->view_depth_stencil() != VK_NULL_HANDLE &&
       entry.source->view_depth_stencil() == entry.dest->view_depth_stencil()) ||
      entry.source->bd_native_depth_generation() != entry.source_generation ||
      entry.dest->bd_native_depth_generation() != entry.dest_generation ||
      framebuffer->depth_view != entry.dest->view_depth_stencil()) {
    FallbackBdFramegraphDepthTransfer("render target identity or generation changed");
    return false;
  }
  // Ordinary consumer passes LOAD. CLEAR/DONT_CARE variants cannot consume the
  // fused result; all such cases retain the proven rung-2 relocation.
  if (!(consumer_render_pass_key.depth_and_color_used & 0b1) ||
      cvars::gpu_edram_passes_dont_care) {
    FallbackBdFramegraphDepthTransfer("consumer depth loadOp is not LOAD");
    return false;
  }
  // GetTransferPipelines below is the standalone transfer factory: the fused
  // draw reuses the EXACT same pipeline/depth state the standalone pass would
  // have used for this edge (4X rasterization sample count, depth test/write,
  // and whichever compare op depth_transfer_not_equal_test selects). NOT_EQUAL
  // is xenia's normal depth-transfer mode (default on) and is reproduced
  // identically here, so it must NOT force rung 2 - only reject a genuinely
  // different edge shape.
  if (entry.source_key.msaa_samples != xenos::MsaaSamples::k2X ||
      entry.dest_key.msaa_samples != xenos::MsaaSamples::k4X ||
      entry.shader_key.source_msaa_samples != xenos::MsaaSamples::k2X ||
      entry.shader_key.dest_msaa_samples != xenos::MsaaSamples::k4X ||
      entry.shader_key.mode != TransferMode::kDepthToDepth) {
    FallbackBdFramegraphDepthTransfer(
        "unsupported fused transfer pipeline state");
    return false;
  }
  const VkPipeline* pipelines = GetTransferPipelines(
      TransferPipelineKey(consumer_render_pass_key, entry.shader_key));
  if (!pipelines) {
    FallbackBdFramegraphDepthTransfer(
        "consumer render-pass transfer pipeline unavailable");
    return false;
  }
  entry.source_descriptor_set = entry.source->GetDescriptorSetTransferSource();
  if (entry.source_descriptor_set == VK_NULL_HANDLE) {
    FallbackBdFramegraphDepthTransfer("source descriptor unavailable");
    return false;
  }

  Transfer::Rectangle rectangles[Transfer::kMaxRectanglesWithCutout];
  uint32_t rectangle_count = entry.transfer.GetRectangles(
      entry.dest_key.base_tiles, entry.dest_key.GetPitchTiles(),
      entry.dest_key.msaa_samples, entry.dest_key.Is64bpp(), rectangles, nullptr);
  if (!rectangle_count) {
    FallbackBdFramegraphDepthTransfer("empty transfer rectangle");
    return false;
  }
  // RT identity alone is insufficient: a scissor-clamped consumer render area
  // may cover only part of the destination. Every transfer destination pixel
  // must be inside both the actual consumer render area and the framebuffer.
  const int64_t render_area_x0 = consumer_render_area.offset.x;
  const int64_t render_area_y0 = consumer_render_area.offset.y;
  const int64_t render_area_x1 =
      render_area_x0 + consumer_render_area.extent.width;
  const int64_t render_area_y1 =
      render_area_y0 + consumer_render_area.extent.height;
  if (render_area_x0 < 0 || render_area_y0 < 0 ||
      render_area_x1 > int64_t(framebuffer->host_extent.width) ||
      render_area_y1 > int64_t(framebuffer->host_extent.height)) {
    FallbackBdFramegraphDepthTransfer("invalid consumer render area");
    return false;
  }
  for (uint32_t i = 0; i < rectangle_count; ++i) {
    const Transfer::Rectangle& rectangle = rectangles[i];
    const int64_t rectangle_x0 = rectangle.x_pixels;
    const int64_t rectangle_y0 = rectangle.y_pixels;
    const int64_t rectangle_x1 =
        rectangle_x0 + rectangle.width_pixels;
    const int64_t rectangle_y1 =
        rectangle_y0 + rectangle.height_pixels;
    if (rectangle_x0 < render_area_x0 || rectangle_y0 < render_area_y0 ||
        rectangle_x1 > render_area_x1 || rectangle_y1 > render_area_y1) {
      FallbackBdFramegraphDepthTransfer(
          "consumer render area does not cover transfer destination");
      return false;
    }
  }
  entry.vertex_count = 6 * rectangle_count;
  float* vertex_write = reinterpret_cast<float*>(
      transfer_vertex_buffer_pool_->Request(
          command_processor_.GetCurrentSubmission(),
          sizeof(float) * 2 * entry.vertex_count, sizeof(float),
          entry.vertex_buffer, entry.vertex_buffer_offset));
  if (!vertex_write) {
    FallbackBdFramegraphDepthTransfer("transfer vertex allocation failed");
    return false;
  }
  const ui::vulkan::VulkanDevice* vulkan_device =
      command_processor_.GetVulkanDevice();
  entry.viewport = {0.0f, 0.0f,
                    float(std::min(xe::next_pow2(framebuffer->host_extent.width),
                                   vulkan_device->properties().maxViewportDimensions[0])),
                    float(std::min(xe::next_pow2(framebuffer->host_extent.height),
                                   vulkan_device->properties().maxViewportDimensions[1])),
                    0.0f, 1.0f};
  entry.scissor.offset = {0, 0};
  entry.scissor.extent = framebuffer->host_extent;
  const float pixels_to_ndc_x = 2.0f / entry.viewport.width;
  const float pixels_to_ndc_y = 2.0f / entry.viewport.height;
  for (uint32_t i = 0; i < rectangle_count; ++i) {
    const Transfer::Rectangle& rectangle = rectangles[i];
    const float x0 = -1.0f + rectangle.x_pixels * pixels_to_ndc_x;
    const float y0 = -1.0f + rectangle.y_pixels * pixels_to_ndc_y;
    const float x1 = x0 + rectangle.width_pixels * pixels_to_ndc_x;
    const float y1 = y0 + rectangle.height_pixels * pixels_to_ndc_y;
    const float vertices[12] = {x0, y0, x0, y1, x1, y0,
                                x1, y0, x0, y1, x1, y1};
    std::memcpy(vertex_write, vertices, sizeof(vertices));
    vertex_write += xe::countof(vertices);
  }
  entry.pipeline_count = vulkan_device->properties().sampleRateShading
                             ? 1
                             : uint32_t(1) << uint32_t(entry.dest_key.msaa_samples);
  for (uint32_t i = 0; i < entry.pipeline_count; ++i) {
    if (pipelines[i] == VK_NULL_HANDLE) {
      FallbackBdFramegraphDepthTransfer("transfer sample pipeline unavailable");
      return false;
    }
    entry.pipelines[i] = pipelines[i];
  }
  entry.address_constant.dest_pitch = entry.dest_key.GetPitchTiles();
  entry.address_constant.source_pitch = entry.source_key.GetPitchTiles();
  entry.address_constant.source_to_dest =
      int32_t(entry.dest_key.base_tiles) - int32_t(entry.source_key.base_tiles);

  // Pass elision must retain the standalone pass's producer-write -> sampled-
  // read dependency. Require the source to still be the depth attachment the
  // deferred edge observed, then emit the explicit transition while no render
  // pass is open. SubmitBarriers in the command processor records it before the
  // consumer's vkCmdBeginRenderPass.
  constexpr VkPipelineStageFlags kSourceWriteStages =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  constexpr VkAccessFlags kSourceWriteAccess =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  constexpr VkImageLayout kSourceWriteLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  constexpr VkPipelineStageFlags kSourceReadStage =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  constexpr VkAccessFlags kSourceReadAccess = VK_ACCESS_SHADER_READ_BIT;
  constexpr VkImageLayout kSourceReadLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  if (entry.source->current_layout() != kSourceWriteLayout ||
      !(entry.source->current_stage_mask() & kSourceWriteStages) ||
      !(entry.source->current_access_mask() & kSourceWriteAccess)) {
    FallbackBdFramegraphDepthTransfer(
        "source is not a depth-attachment write at fusion");
    return false;
  }
  command_processor_.PushImageMemoryBarrier(
      entry.source->image(),
      ui::vulkan::util::InitializeSubresourceRange(
          VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
      kSourceWriteStages, kSourceReadStage, kSourceWriteAccess,
      kSourceReadAccess, kSourceWriteLayout, kSourceReadLayout);
  entry.source->SetUsage(kSourceReadStage, kSourceReadAccess,
                         kSourceReadLayout);
  entry.fusion_ready = true;
  return true;
}

void VulkanRenderTargetCache::RecordPreparedBdFramegraphDepthConsumer() {
  if (bd_framegraph_deferred_depth_transfers_.empty() ||
      !bd_framegraph_deferred_depth_transfers_.front().fusion_ready) {
    return;
  }
  BdFramegraphDeferredDepthTransfer& entry =
      bd_framegraph_deferred_depth_transfers_.front();
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();
  command_processor_.SetViewport(entry.viewport);
  command_processor_.SetScissor(entry.scissor);
  command_buffer.CmdVkBindVertexBuffers(0, 1, &entry.vertex_buffer,
                                       &entry.vertex_buffer_offset);
  VkPipelineLayout pipeline_layout =
      transfer_pipeline_layouts_[size_t(TransferPipelineLayoutIndex::kDepth)];
  command_buffer.CmdVkBindDescriptorSets(
      VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
      &entry.source_descriptor_set, 0, nullptr);
  command_buffer.CmdVkPushConstants(
      pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
      sizeof(entry.address_constant), &entry.address_constant);
  for (uint32_t i = 0; i < entry.pipeline_count; ++i) {
    command_processor_.BindExternalGraphicsPipeline(entry.pipelines[i]);
    command_buffer.CmdVkDraw(entry.vertex_count, 1, 0, 0);
  }

  if (cvars::gpu_bd_framegraph_depth_dump) {
    // A cheap, path-independent marker for the exact depth-transfer command.
    // The standalone path hashes the same keys, address constant, rectangles,
    // vertex count and sample-pipeline count, so equal markers prove equivalent
    // transfer inputs/state without adding a tile-store-inducing readback.
    uint64_t marker = UINT64_C(1469598103934665603);
    auto mix_u32 = [&marker](uint32_t value) {
      for (uint32_t byte = 0; byte < 4; ++byte) {
        marker ^= uint8_t(value >> (byte * 8));
        marker *= UINT64_C(1099511628211);
      }
    };
    mix_u32(entry.source_key.key);
    mix_u32(entry.dest_key.key);
    mix_u32(entry.shader_key.key);
    mix_u32(entry.address_constant.dest_pitch);
    mix_u32(entry.address_constant.source_pitch);
    mix_u32(uint32_t(entry.address_constant.source_to_dest));
    mix_u32(entry.vertex_count);
    mix_u32(entry.pipeline_count);
    Transfer::Rectangle marker_rectangles[Transfer::kMaxRectanglesWithCutout];
    uint32_t marker_rectangle_count = entry.transfer.GetRectangles(
        entry.dest_key.base_tiles, entry.dest_key.GetPitchTiles(),
        entry.dest_key.msaa_samples, entry.dest_key.Is64bpp(),
        marker_rectangles, nullptr);
    mix_u32(marker_rectangle_count);
    for (uint32_t i = 0; i < marker_rectangle_count; ++i) {
      mix_u32(marker_rectangles[i].x_pixels);
      mix_u32(marker_rectangles[i].y_pixels);
      mix_u32(marker_rectangles[i].width_pixels);
      mix_u32(marker_rectangles[i].height_pixels);
    }
    XELOGI(
        "BD FRAMEGRAPH DEPTH DUMP: path=fused marker={:016X} rects={} "
        "vertices={} samples={} dest{{base={} pitchT={} msaa={} format={}}}",
        marker, marker_rectangle_count, entry.vertex_count,
        entry.pipeline_count, entry.dest_key.base_tiles,
        entry.dest_key.GetPitchTiles(), uint32_t(entry.dest_key.msaa_samples),
        entry.dest_key.resource_format);
  }

  // Matched by the render pass's BY_REGION self-dependency. This makes the
  // transfer depth/stencil writes visible to all following depth-test draws.
  VkMemoryBarrier depth_memory_barrier = {};
  depth_memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  depth_memory_barrier.srcAccessMask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  depth_memory_barrier.dstAccessMask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  command_buffer.CmdVkPipelineBarrier(
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
      VK_DEPENDENCY_BY_REGION_BIT, 1, &depth_memory_barrier, 0, nullptr, 0,
      nullptr);
  XELOGI(
      "BD FRAMEGRAPH: rung3 fused dest{{base={} pitchT={} msaa={} format={}}} "
      "source_gen={} dest_gen={}",
      entry.dest_key.base_tiles, entry.dest_key.GetPitchTiles(),
      uint32_t(entry.dest_key.msaa_samples), entry.dest_key.resource_format,
      entry.source_generation, entry.dest_generation);
  bd_framegraph_rung3_fused.fetch_add(1, std::memory_order_relaxed);
  bd_framegraph_deferred_depth_transfers_.clear();
}

void VulkanRenderTargetCache::PerformTransfersAndResolveClears(
    uint32_t render_target_count, RenderTarget* const* render_targets,
    const std::vector<Transfer>* render_target_transfers,
    const uint64_t* render_target_resolve_clear_values,
    const Transfer::Rectangle* resolve_clear_rectangle,
    const RenderPassKey* guest_render_pass_key, VkRenderPass guest_render_pass,
    const Framebuffer* guest_framebuffer) {
  assert_true(GetPath() == Path::kHostRenderTargets);

  // Per-frame instrumentation: total EDRAM transfers processed this call (the
  // suspected source of per-draw render-pass breaks on the tiler).
  {
    uint32_t transfer_count = 0;
    if (render_target_transfers) {
      for (uint32_t i = 0; i < render_target_count; ++i) {
        if (render_targets[i]) {
          transfer_count += uint32_t(render_target_transfers[i].size());
        }
      }
    }
    command_processor_.AddRenderTargetTransferStats(
        transfer_count, render_target_resolve_clear_values != nullptr &&
                            resolve_clear_rectangle != nullptr);

    // DEPTH-TRANSFER CENSUS (gpu_bd_depth_xfer_census). Placed HERE, at the one
    // site EVERY transfer passes through, deliberately: the first attempt put it
    // inside the gpu_bd_native_depth_convert block and produced ZERO output
    // because that cvar was off - a silent no-op, not a zero result.
    // The question it answers: the confirmed 1.204x color-drop HLE still EXECUTES
    // 15 of 45 transfers/frame (device-measured) and they are the DEPTH ones. An
    // MSAA-only src/dst difference is exactly what a SAMPLE_ZERO in-pass depth
    // resolve attachment produces (so it is deletable that way); a PITCH
    // difference is a resolution downscale needing a different mechanism. The
    // msaa_only+both share is therefore the fraction of the remaining depth
    // transfers gpu_bd_native_depth_resolve can actually serve - which decides
    // whether the consumer redirect is worth building, BEFORE building it.
    if (cvars::gpu_bd_depth_xfer_census && render_target_transfers &&
        xe::Clock::QueryGuestUptimeMillis() > 135000) {
      static std::atomic<uint64_t> s_tot{0}, s_msaa_only{0}, s_pitch_only{0},
          s_both{0}, s_neither{0}, s_nonsrc{0};
      for (uint32_t i = 0; i < render_target_count; ++i) {
        RenderTarget* dest_rt = render_targets[i];
        if (!dest_rt || !dest_rt->key().is_depth) {
          continue;  // depth destinations only
        }
        for (const Transfer& t : render_target_transfers[i]) {
          if (!t.source || t.source == dest_rt) {
            s_nonsrc.fetch_add(1, std::memory_order_relaxed);
            continue;
          }
          const RenderTargetKey sk = t.source->key();
          const RenderTargetKey dk = dest_rt->key();
          const bool pd = sk.GetPitchTiles() != dk.GetPitchTiles();
          const bool md = sk.msaa_samples != dk.msaa_samples;
          if (md && !pd) {
            s_msaa_only.fetch_add(1, std::memory_order_relaxed);
          } else if (pd && !md) {
            s_pitch_only.fetch_add(1, std::memory_order_relaxed);
          } else if (pd && md) {
            s_both.fetch_add(1, std::memory_order_relaxed);
          } else {
            s_neither.fetch_add(1, std::memory_order_relaxed);
          }
          const uint64_t tot = s_tot.fetch_add(1, std::memory_order_relaxed) + 1;
          if ((tot % 2048u) == 0u) {
            const uint64_t mo = s_msaa_only.load(std::memory_order_relaxed);
            const uint64_t po = s_pitch_only.load(std::memory_order_relaxed);
            const uint64_t bo = s_both.load(std::memory_order_relaxed);
            XELOGI(
                "BD DEPTH XFER CENSUS: total={} msaa_only={} pitch_only={} "
                "both={} neither={} same_src={} | resolve-servable "
                "(msaa_only+both) = {}%",
                tot, mo, po, bo, s_neither.load(std::memory_order_relaxed),
                s_nonsrc.load(std::memory_order_relaxed),
                tot ? ((mo + bo) * 100 / tot) : 0);
          }
        }
      }
    }
  }

  // Accuracy-for-speed (Thor/Adreno): skip the EDRAM ownership-transfer GPU
  // work to cut the render-pass breaks / tile store-reload that dominate the
  // GPU frame time. Redirect to an empty per-RT transfer array so the transfer
  // loops below do nothing; resolve-clears (separate params) still run. Aliased
  // EDRAM content is NOT preserved -> may glitch. Gated, default off.
  static const std::vector<Transfer>
      kNoTransfers[1 + xenos::kMaxColorRenderTargets] = {};
  // SURGICAL skip (gpu_bd_native_skip_transfers): once the field is in the native
  // RT, the EDRAM re-alias transfers that follow are redundant (we present the
  // native RT, not the LLE surface) and are the pass-break/GMEM store-reload wall.
  // Pre-field shadow/texture transfers (bd_native_field_rendered still false) are
  // KEPT so the field's sampled textures survive. Mirrors skip_resolves.
  if (cvars::gpu_skip_edram_transfers ||
      (cvars::gpu_bd_native_skip_transfers &&
       command_processor_.bd_native_field_rendered())) {
    render_target_transfers = kNoTransfers;
  }

  // REAL-HLE SURGICAL transfer deletion (gpu_bd_native_drop_transfers): drop ONLY
  // the transfers whose DEST RT resolves to a guest address a native surface
  // already serves (the field samples that content natively, so preserving its
  // EDRAM alias is dead weight). Unlike the blunt gpu_skip_edram_transfers above
  // (which blacks the main scene it still needs), this KEEPS transfers for RTs not
  // covered natively = safe + correct. This is the real EDRAM-deletion lever.
  static std::vector<Transfer>
      s_bd_xfer_filtered[1 + xenos::kMaxColorRenderTargets];
  if ((cvars::gpu_bd_native_drop_transfers ||
       cvars::gpu_bd_native_color_lifetime_hle >= 6) &&
      render_target_transfers && render_target_transfers != kNoTransfers) {
    bool any_dropped = false;
    for (uint32_t i = 0; i < render_target_count; ++i) {
      s_bd_xfer_filtered[i] = render_target_transfers[i];  // default: keep
      RenderTarget* dest_rt = render_targets[i];
      if (!dest_rt || render_target_transfers[i].empty()) {
        continue;
      }
      const bool is_depth = dest_rt->key().is_depth;
      const VulkanCommandProcessor::ResolveEdge* edge =
          command_processor_.PersistentResolveEdgeForSrc(dest_rt->key().key);
      const bool native_served =
          edge && edge->dest_base &&
          command_processor_.BdNativeSurfaceServes(edge->dest_base);
      // LEVEL 6: drop the transfer if an L5 native producer alias covers its
      // dest AND every consumer of that dest read native last frame (present /
      // pixel-texture, no NonNative) - the EDRAM copy is then dead weight.
      const bool l5_served =
          cvars::gpu_bd_native_color_lifetime_hle >= 6 && !is_depth && edge &&
          edge->dest_base && command_processor_.BdL5DropSafe(edge->dest_base);
      // COLOR-ONLY aggressive drop (gpu_bd_native_drop_all_color_xfer): the
      // wholesale gpu_skip_edram_transfers dropped color AND DEPTH transfers -> the
      // dropped DEPTH broke the field's depth test => geometry depth-fails =>
      // collapse to a strip (Thor) / crash (strict desktop). Dropping only the
      // COLOR ownership transfers (the ~97ms wall; the field presents native color)
      // while KEEPING the depth transfers should render correct AND fast. Barrier
      // (below) preserves sync.
      const bool drop_color_only =
          cvars::gpu_bd_native_drop_all_color_xfer && !is_depth;
      // DROP-ALL WITH BARRIER (gpu_bd_native_drop_all_xfer): the blunt
      // gpu_skip_edram_transfers drops color+depth WITHOUT a barrier -> collapse.
      // Hypothesis: the collapse is the missing depth BARRIER (sync/layout), not
      // depth content. Drop EVERYTHING (incl. depth) but emit the barrier below for
      // each -> if the barrier prevents the collapse, this = 30fps + correct.
      const bool drop_all = cvars::gpu_bd_native_drop_all_xfer;
      if (native_served || drop_color_only || drop_all || l5_served) {
        // Count the entries actually removed so the trace can report EXECUTED
        // transfers (requested - dropped) - the real tile-store cost (5.6-sol).
        command_processor_.AddRenderTargetTransfersDropped(
            uint32_t(s_bd_xfer_filtered[i].size()));
        s_bd_xfer_filtered[i].clear();  // transfer dead
        any_dropped = true;
        // Preserve the SYNC the dropped transfer would have provided. The
        // vulkan_validation run proved the drop is layout-VALID (no VUID) but
        // crashes without validation = a GPU execution/memory HAZARD: the dropped
        // transfer's barrier is what ordered this RT's prior writes before its next
        // use. Emit a conservative full memory barrier (same layout, so no
        // transition — the next binding handles layout) to restore that ordering.
        auto& drt = *static_cast<VulkanRenderTarget*>(dest_rt);
        VkImageAspectFlags aspect =
            dest_rt->key().is_depth
                ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                : VK_IMAGE_ASPECT_COLOR_BIT;
        command_processor_.PushImageMemoryBarrier(
            drt.image(),
            ui::vulkan::util::InitializeSubresourceRange(aspect),
            drt.current_stage_mask(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            drt.current_access_mask(),
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            drt.current_layout(), drt.current_layout());
      }
    }
    if (any_dropped) {
      render_target_transfers = s_bd_xfer_filtered;
    }
  }

  // BD DEPTH-XFER DIAGNOSTIC: log the src/dst RT keys of every DEPTH ownership
  // transfer (the ~67ms tile-reinterpreting wall) so the next build knows EXACTLY
  // which aliased depth surfaces to convert to per-surface native RTs (the
  // resource-keyed coverage that deletes EDRAM). Gated on the native renderer,
  // capped, one-shot — harmless.
  if (cvars::gpu_bd_native_renderer && render_target_transfers &&
      render_target_transfers != kNoTransfers) {
    static std::atomic<uint32_t> s_bd_dxfer_log{0};
    for (uint32_t i = 0; i < render_target_count; ++i) {
      RenderTarget* drt = render_targets[i];
      if (!drt || !drt->key().is_depth) {
        continue;
      }
      for (const Transfer& t : render_target_transfers[i]) {
        if (!t.source || t.source == drt) {
          continue;
        }
        if (s_bd_dxfer_log.fetch_add(1) < 40) {
          RenderTargetKey sk =
              static_cast<VulkanRenderTarget*>(t.source)->key();
          RenderTargetKey dk = drt->key();
          XELOGI(
              "BD DEPTH XFER: src(base={} pitchT={} msaa={} fmt={}) -> "
              "dst(base={} pitchT={} msaa={} fmt={}) samelayout={}",
              sk.base_tiles, sk.GetPitchTiles(), uint32_t(sk.msaa_samples),
              uint32_t(sk.resource_format), dk.base_tiles, dk.GetPitchTiles(),
              uint32_t(dk.msaa_samples), uint32_t(dk.resource_format),
              (sk.base_tiles == dk.base_tiles &&
               sk.GetPitchTiles() == dk.GetPitchTiles() &&
               sk.msaa_samples == dk.msaa_samples));
        }
      }
    }
  }

  // BD DEPTH-DOWNSCALE DROP (gpu_bd_native_drop_depth_downscale): drop ONLY the
  // per-transfer DEPTH ownership transfers whose src/dst pitch DIFFERS (the 720->400
  // downscale-for-post transfers), KEEP same-pitch depth transfers (MSAA conversions
  // the depth test needs). Per-transfer (not per-dest) so the load-bearing ones stay.
  static std::vector<Transfer>
      s_bd_ddrop_filtered[1 + xenos::kMaxColorRenderTargets];
  if (cvars::gpu_bd_native_drop_depth_downscale && render_target_transfers &&
      render_target_transfers != kNoTransfers) {
    bool any_dropped = false;
    for (uint32_t i = 0; i < render_target_count; ++i) {
      s_bd_ddrop_filtered[i].clear();
      RenderTarget* drt = render_targets[i];
      for (const Transfer& t : render_target_transfers[i]) {
        bool drop = false;
        if (drt && drt->key().is_depth && t.source) {
          RenderTargetKey sk = static_cast<VulkanRenderTarget*>(t.source)->key();
          if (sk.GetPitchTiles() != drt->key().GetPitchTiles() ||
              sk.msaa_samples != drt->key().msaa_samples) {
            drop = true;  // pitch-mismatch (downscale view) OR msaa-mismatch (1x<->2x
                          // conversion) depth = a CONVERSION transfer, not identity
          }
        }
        if (drop) {
          any_dropped = true;
        } else {
          s_bd_ddrop_filtered[i].push_back(t);
        }
      }
    }
    if (any_dropped) {
      render_target_transfers = s_bd_ddrop_filtered;
    }
  }

  // BD-30 NATIVE-COPY fast-path (gpu_bd_native_copy_transfers): same-tile-layout
  // color transfers are pure 1:1 region copies. Do them as a native image blit
  // (no render-pass = NO TBDR tile-store = kills the ~51ms/transfer store that is
  // the 110ms wall) instead of the per-pixel EDRAM-address emulation shader pass.
  // Content is COPIED (preserved), not skipped -> keeps the image. Reinterpreting/
  // MSAA/depth transfers stay on the shader path. Called between guest passes (no
  // active render pass) so an image->image blit is valid. Gated, default off.
  static std::vector<Transfer>
      s_bd_copy_filtered[1 + xenos::kMaxColorRenderTargets];
  if (cvars::gpu_bd_native_copy_transfers && render_target_transfers &&
      guest_framebuffer) {
    bool any_copied = false;
    for (uint32_t i = 0; i < render_target_count; ++i) {
      s_bd_copy_filtered[i].clear();
      RenderTarget* dest_rt = render_targets[i];
      if (!dest_rt) {
        continue;
      }
      auto& dest_v = *static_cast<VulkanRenderTarget*>(dest_rt);
      RenderTargetKey dk = dest_v.key();
      for (const Transfer& t : render_target_transfers[i]) {
        bool copyable = false;
        // DEPTH now included (gpu_bd_native_copy_transfers): BD uses kHostRenderTargets
        // so the depth HOST image IS the authoritative content — a same-tile-layout
        // depth transfer is a pure 1:1 vkCmdCopyImage (NO per-pixel EDRAM-emulation
        // SHADER pass = kills the ~97ms depth-transfer wall) that PRESERVES the depth
        // (correct, no collapse). Depth uses CopyImage (can't blit-filter depth).
        const bool is_depth_copy = dk.is_depth;
        if (t.source && t.source != dest_rt &&
            dk.msaa_samples == xenos::MsaaSamples::k1X) {
          auto& src_v = *static_cast<VulkanRenderTarget*>(t.source);
          RenderTargetKey sk = src_v.key();
          copyable = sk.base_tiles == dk.base_tiles &&
                     sk.GetPitchTiles() == dk.GetPitchTiles() &&
                     sk.msaa_samples == dk.msaa_samples &&
                     sk.Is64bpp() == dk.Is64bpp() &&
                     sk.is_depth == dk.is_depth &&
                     sk.resource_format == dk.resource_format;
          if (copyable) {
            const VkImageAspectFlags aspect =
                is_depth_copy ? VK_IMAGE_ASPECT_DEPTH_BIT
                              : VK_IMAGE_ASPECT_COLOR_BIT;
            VkImageSubresourceRange range = {};
            range.aspectMask = aspect;
            range.levelCount = 1;
            range.layerCount = 1;
            command_processor_.PushImageMemoryBarrier(
                src_v.image(), range, src_v.current_stage_mask(),
                VK_PIPELINE_STAGE_TRANSFER_BIT, src_v.current_access_mask(),
                VK_ACCESS_TRANSFER_READ_BIT, src_v.current_layout(),
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            src_v.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            command_processor_.PushImageMemoryBarrier(
                dest_v.image(), range, dest_v.current_stage_mask(),
                VK_PIPELINE_STAGE_TRANSFER_BIT, dest_v.current_access_mask(),
                VK_ACCESS_TRANSFER_WRITE_BIT, dest_v.current_layout(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            dest_v.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            command_processor_.SubmitBarriers(true);
            const int32_t cw = int32_t(guest_framebuffer->host_extent.width);
            const int32_t ch = int32_t(guest_framebuffer->host_extent.height);
            if (is_depth_copy) {
              VkImageCopy creg = {};
              creg.srcSubresource.aspectMask = aspect;
              creg.srcSubresource.layerCount = 1;
              creg.dstSubresource.aspectMask = aspect;
              creg.dstSubresource.layerCount = 1;
              creg.extent = {uint32_t(cw), uint32_t(ch), 1};
              command_processor_.deferred_command_buffer().CmdVkCopyImage(
                  src_v.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  dest_v.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &creg);
            } else {
              VkImageBlit region = {};
              region.srcSubresource.aspectMask = aspect;
              region.srcSubresource.layerCount = 1;
              region.srcOffsets[1] = {cw, ch, 1};
              region.dstSubresource.aspectMask = aspect;
              region.dstSubresource.layerCount = 1;
              region.dstOffsets[1] = region.srcOffsets[1];
              command_processor_.deferred_command_buffer().CmdVkBlitImage(
                  src_v.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  dest_v.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                  VK_FILTER_NEAREST);
            }
            any_copied = true;
          }
        }
        if (!copyable) {
          s_bd_copy_filtered[i].push_back(t);
        }
      }
    }
    if (any_copied) {
      render_target_transfers = s_bd_copy_filtered;
    }
  }

  // ⭐ BD-30 NATIVE DEPTH CONVERSION (gpu_bd_native_depth_convert) — THE 30fps lever.
  // Rigorously measured 2026-07-09: BD's field GPU frame = 42.7ms = 12.6ms in-pass +
  // 30.1ms BETWEEN-pass, and the between-pass cost is the mixed-resolution DEPTH
  // conversion transfers (BD depth-tests at full 720 AND downscaled ~400 views; the
  // EDRAM emulation re-converts between them via tile-reinterpreting shader passes,
  // ~23-40/frame). Fragment levers are all dead (they only touch the 12.6ms), and
  // dropping the depth transfers collapses the field. This path REPLACES each pitch-
  // mismatch depth-conversion transfer with a NATIVE vkCmdBlitImage depth downsample
  // (src depth image -> dst depth image, scaled by the pitch ratio, NEAREST) so the
  // EDRAM tile-reinterpreting cost is eliminated while the depth content is preserved.
  // Same-MSAA / same-format only (the profile's MSAA clamp unifies samples, so the
  // remaining conversions are pure pitch downscales). Falls back to the EDRAM transfer
  // when depth blit is unsupported. ⚠️ THOR-GATED: desktop's immediate-mode cannot
  // validate depth vs Turnip's TBDR — the blit extents (pitch-ratio) + NEAREST
  // downsample correctness MUST be Thor-verified (a wrong extent collapses the field,
  // as the EDRAM-drop did). Gated off, default off; needs gpu_bd_native_renderer.
  static std::vector<Transfer>
      s_bd_dconv_filtered[1 + xenos::kMaxColorRenderTargets];
  // NOTE: independent of gpu_bd_native_renderer — this intercepts the EDRAM depth
  // transfers directly, so it can be isolated on the correct baseline config.
  if (cvars::gpu_bd_native_depth_convert && depth_blit_supported_ &&
      render_target_transfers && render_target_transfers != kNoTransfers &&
      guest_framebuffer) {
    bool any_converted = false;
    static std::atomic<uint32_t> s_bd_dconv_log{0};
    for (uint32_t i = 0; i < render_target_count; ++i) {
      s_bd_dconv_filtered[i].clear();
      RenderTarget* dest_rt = render_targets[i];
      if (!dest_rt) {
        continue;
      }
      auto& dest_v = *static_cast<VulkanRenderTarget*>(dest_rt);
      RenderTargetKey dk = dest_v.key();
      for (const Transfer& t : render_target_transfers[i]) {
        bool converted = false;
        if (dk.is_depth && t.source && t.source != dest_rt) {
          auto& src_v = *static_cast<VulkanRenderTarget*>(t.source);
          RenderTargetKey sk = src_v.key();
          // DIAGNOSTIC: log EVERY depth transfer this block sees, but ONLY in the
          // FIELD (guest uptime > 135s) so the logs are RECENT (not boot logs that
          // rotate out of the 64M logcat over a ~190s run — the rotation trap). The
          // first run's capped-at-40 logs fired at boot + rotated => empty. This
          // reveals whether the field even DOES pitch/msaa depth conversions (the
          // assumed 30ms GAP) or whether the GAP is color transfers / resolves /
          // barriers instead. vkCmdBlitImage needs single-sample, so msaa>1 or
          // msaa-mismatch conversions need a shader resolve+downsample, not a blit.
          if (xe::Clock::QueryGuestUptimeMillis() > 135000 &&
              s_bd_dconv_log.fetch_add(1) < 40) {
            XELOGI(
                "BD DEPTH XFER SEEN: src(base={} pitchT={} msaa={} fmt={}) -> "
                "dst(base={} pitchT={} msaa={} fmt={}) pitch_diff={} msaa_diff={}",
                sk.base_tiles, sk.GetPitchTiles(), uint32_t(sk.msaa_samples),
                uint32_t(sk.resource_format), dk.base_tiles, dk.GetPitchTiles(),
                uint32_t(dk.msaa_samples), uint32_t(dk.resource_format),
                sk.GetPitchTiles() != dk.GetPitchTiles(),
                sk.msaa_samples != dk.msaa_samples);
          }
          // (The depth-transfer census lives at the AddRenderTargetTransferStats
          // site instead - this block is gated by gpu_bd_native_depth_convert, so
          // a census here silently reports nothing when that cvar is off.)
          // The tile-reinterpreting DEPTH DOWNSCALE conversion = same base/msaa/
          // format, DIFFERENT pitch (720<->400). This is the measured ~30ms cost.
          // BLIT path handles ONLY single-sample (vkCmdBlitImage requires samples=1);
          // MSAA-involving conversions fall through to the EDRAM transfer until the
          // shader resolve+downsample is built (Thor-iterated next).
          if (sk.msaa_samples == dk.msaa_samples &&
              sk.msaa_samples == xenos::MsaaSamples::k1X &&
              sk.resource_format == dk.resource_format &&
              sk.is_depth == dk.is_depth &&
              sk.GetPitchTiles() != dk.GetPitchTiles() &&
              sk.GetPitchTiles() != 0 && dk.GetPitchTiles() != 0) {
            const int32_t dst_w = int32_t(guest_framebuffer->host_extent.width);
            const int32_t dst_h = int32_t(guest_framebuffer->host_extent.height);
            // Source width scales by the pitch ratio (dst pitch is the current pass
            // extent). Height is unchanged in a pitch-only downscale. THOR-VERIFY.
            const int32_t src_w = int32_t(int64_t(dst_w) * sk.GetPitchTiles() /
                                          dk.GetPitchTiles());
            const int32_t src_h = dst_h;
            if (src_w > 0 && dst_w > 0 && dst_h > 0) {
              const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
              VkImageSubresourceRange range = {};
              range.aspectMask = aspect;
              range.levelCount = 1;
              range.layerCount = 1;
              command_processor_.PushImageMemoryBarrier(
                  src_v.image(), range, src_v.current_stage_mask(),
                  VK_PIPELINE_STAGE_TRANSFER_BIT, src_v.current_access_mask(),
                  VK_ACCESS_TRANSFER_READ_BIT, src_v.current_layout(),
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
              src_v.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_ACCESS_TRANSFER_READ_BIT,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
              command_processor_.PushImageMemoryBarrier(
                  dest_v.image(), range, dest_v.current_stage_mask(),
                  VK_PIPELINE_STAGE_TRANSFER_BIT, dest_v.current_access_mask(),
                  VK_ACCESS_TRANSFER_WRITE_BIT, dest_v.current_layout(),
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
              dest_v.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_ACCESS_TRANSFER_WRITE_BIT,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
              command_processor_.SubmitBarriers(true);
              VkImageBlit region = {};
              region.srcSubresource.aspectMask = aspect;
              region.srcSubresource.layerCount = 1;
              region.srcOffsets[1] = {src_w, src_h, 1};
              region.dstSubresource.aspectMask = aspect;
              region.dstSubresource.layerCount = 1;
              region.dstOffsets[1] = {dst_w, dst_h, 1};
              command_processor_.deferred_command_buffer().CmdVkBlitImage(
                  src_v.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  dest_v.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                  VK_FILTER_NEAREST);
              converted = true;
              any_converted = true;
              if (s_bd_dconv_log.fetch_add(1) < 20) {
                XELOGI(
                    "BD NATIVE DEPTH CONVERT: src(pitchT={} {}x{}) -> dst(pitchT={} "
                    "{}x{}) native blit (was EDRAM transfer)",
                    sk.GetPitchTiles(), src_w, src_h, dk.GetPitchTiles(), dst_w,
                    dst_h);
              }
            }
          }
        }
        if (!converted) {
          s_bd_dconv_filtered[i].push_back(t);
        }
      }
    }
    if (any_converted) {
      render_target_transfers = s_bd_dconv_filtered;
    }
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  uint64_t current_submission = command_processor_.GetCurrentSubmission();
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();

  bool resolve_clear_needed =
      render_target_resolve_clear_values && resolve_clear_rectangle;
  VkClearRect resolve_clear_rect;
  if (resolve_clear_needed) {
    // Assuming the rectangle is already clamped by the setup function from the
    // common render target cache.
    resolve_clear_rect.rect.offset.x =
        int32_t(resolve_clear_rectangle->x_pixels * draw_resolution_scale_x());
    resolve_clear_rect.rect.offset.y =
        int32_t(resolve_clear_rectangle->y_pixels * draw_resolution_scale_y());
    resolve_clear_rect.rect.extent.width =
        resolve_clear_rectangle->width_pixels * draw_resolution_scale_x();
    resolve_clear_rect.rect.extent.height =
        resolve_clear_rectangle->height_pixels * draw_resolution_scale_y();
    resolve_clear_rect.baseArrayLayer = 0;
    resolve_clear_rect.layerCount = 1;
  }

  // Minimal non-adjacent framegraph edge: remove only the selected transfer
  // from this call after its complete payload has been retained. Everything
  // else continues through the legacy path unchanged.
  std::vector<Transfer>
      bd_framegraph_filtered[1 + xenos::kMaxColorRenderTargets];
  if ((cvars::gpu_bd_framegraph_depth ||
       cvars::gpu_bd_framegraph_depth_shadow) &&
      !bd_framegraph_flushing_legacy_ &&
      render_target_transfers && !resolve_clear_needed) {
    bool any_deferred = false;
    for (uint32_t i = 0; i < render_target_count; ++i) {
      bd_framegraph_filtered[i].clear();
      RenderTarget* dest_rt = render_targets[i];
      for (const Transfer& transfer : render_target_transfers[i]) {
        if (dest_rt && TryDeferBdFramegraphDepthTransfer(
                           *static_cast<VulkanRenderTarget*>(dest_rt), transfer,
                           guest_render_pass_key)) {
          any_deferred = true;
        } else {
          bd_framegraph_filtered[i].push_back(transfer);
        }
      }
    }
    if (any_deferred) {
      render_target_transfers = bd_framegraph_filtered;
    }
  }

  // In-pass EDRAM transfers (gpu_vulkan_inpass_edram_transfers): decide which
  // destinations can have their transfer draws recorded inside the provided
  // guest render pass instead of a dedicated single-attachment transfer pass
  // (avoiding the pass break + GMEM tile flush/reload on tile-based GPUs).
  // Eligibility (fail-closed to the legacy path):
  // - A guest pass hint is provided, and no resolve clear is requested.
  // - Color destinations (level >= 1) whose ownership-transfer format is not
  //   an integer reinterpretation (float16-class formats keep the legacy
  //   UINT-view pass for NaN bit-exactness).
  // - Depth destinations only at level >= 2 and only with
  //   VK_EXT_shader_stencil_export (no stencil-bit draws / mid-pass clears).
  // - No transfer source (or host depth source read as an image) may be bound
  //   as an attachment of the guest framebuffer (feedback loop), and the
  //   destination must not be a source for another destination (cross-copy
  //   ordering).
  // - Every transfer rectangle must fit in the guest framebuffer render area
  //   (the dedicated transfer pass had the destination own extent instead).
  bool dest_in_guest_pass[1 + xenos::kMaxColorRenderTargets] = {};
  bool dest_native_depth_handoff[1 + xenos::kMaxColorRenderTargets] = {};
  bool any_dest_in_guest_pass = false;
  {
    uint32_t inpass_dest_count = 0;
    uint32_t inpass_skip_format = 0;
    uint32_t inpass_skip_other = 0;
    const int32_t inpass_level = cvars::gpu_vulkan_inpass_edram_transfers;
    if ((inpass_level > 0 || cvars::gpu_bd_native_depth_handoff) &&
        guest_render_pass_key &&
        guest_render_pass != VK_NULL_HANDLE && guest_framebuffer &&
        render_target_transfers && !resolve_clear_needed &&
        render_target_count <= 1 + xenos::kMaxColorRenderTargets) {
      for (uint32_t i = 0; i < render_target_count; ++i) {
        RenderTarget* dest_rt = render_targets[i];
        if (!dest_rt) {
          continue;
        }
        const std::vector<Transfer>& dest_transfers =
            render_target_transfers[i];
        if (dest_transfers.empty()) {
          continue;
        }
        RenderTargetKey dest_rt_key =
            static_cast<VulkanRenderTarget*>(dest_rt)->key();
        // The native handoff falsifier reuses the already resource-keyed host
        // depth RTs. Fail closed unless this is exactly base0 pitch16 1x ->
        // pitch13 1x and the destination view is the following consumer pass's
        // fixed-function depth attachment. A host-depth copy sourced from the
        // destination would still require the EDRAM precision scratch path, so
        // it deliberately remains legacy too.
        bool native_depth_handoff = false;
        if (cvars::gpu_bd_native_depth_handoff && i == 0 &&
            dest_rt_key.is_depth && dest_rt_key.base_tiles == 0 &&
            dest_rt_key.GetPitchTiles() == 13 &&
            dest_rt_key.msaa_samples == xenos::MsaaSamples::k1X &&
            guest_framebuffer->depth_view ==
                static_cast<VulkanRenderTarget*>(dest_rt)->
                    view_depth_stencil()) {
          native_depth_handoff = true;
          for (const Transfer& transfer : dest_transfers) {
            if (!transfer.source || transfer.source == dest_rt ||
                transfer.host_depth_source == dest_rt) {
              native_depth_handoff = false;
              break;
            }
            RenderTargetKey source_rt_key =
                static_cast<VulkanRenderTarget*>(transfer.source)->key();
            if (!source_rt_key.is_depth || source_rt_key.base_tiles != 0 ||
                source_rt_key.GetPitchTiles() != 16 ||
                source_rt_key.msaa_samples != xenos::MsaaSamples::k1X) {
              native_depth_handoff = false;
              break;
            }
          }
        }
        bool eligible;
        if (dest_rt_key.is_depth) {
          eligible = (inpass_level >= 2 || native_depth_handoff) &&
                     vulkan_device->extensions().ext_EXT_shader_stencil_export;
        } else {
          bool is_integer = false;
          GetColorOwnershipTransferVulkanFormat(dest_rt_key.GetColorFormat(),
                                                &is_integer);
          eligible = !is_integer;
          if (is_integer) {
            ++inpass_skip_format;
          }
        }
        if (eligible) {
          // Feedback / cross-copy checks.
          for (const Transfer& transfer : dest_transfers) {
            for (uint32_t j = 0; j < render_target_count; ++j) {
              if (!render_targets[j]) {
                continue;
              }
              if (transfer.source == render_targets[j] ||
                  (transfer.host_depth_source &&
                   transfer.host_depth_source != dest_rt &&
                   transfer.host_depth_source == render_targets[j])) {
                eligible = false;
                break;
              }
            }
            if (!eligible) {
              break;
            }
          }
          for (uint32_t j = 0; eligible && j < render_target_count; ++j) {
            if (j == i || !render_targets[j]) {
              continue;
            }
            for (const Transfer& other_transfer :
                 render_target_transfers[j]) {
              if (other_transfer.source == dest_rt ||
                  other_transfer.host_depth_source == dest_rt) {
                eligible = false;
                break;
              }
            }
          }
          if (eligible) {
            // Bounds check against the guest render area.
            uint32_t dest_pitch_tiles = dest_rt_key.GetPitchTiles();
            bool dest_is_64bpp = dest_rt_key.Is64bpp();
            for (const Transfer& transfer : dest_transfers) {
              Transfer::Rectangle
                  rectangles[Transfer::kMaxRectanglesWithCutout];
              uint32_t rectangle_count = transfer.GetRectangles(
                  dest_rt_key.base_tiles, dest_pitch_tiles,
                  dest_rt_key.msaa_samples, dest_is_64bpp, rectangles,
                  nullptr);
              for (uint32_t j = 0; j < rectangle_count; ++j) {
                const Transfer::Rectangle& rectangle = rectangles[j];
                if ((rectangle.x_pixels + rectangle.width_pixels) *
                            draw_resolution_scale_x() >
                        guest_framebuffer->host_extent.width ||
                    (rectangle.y_pixels + rectangle.height_pixels) *
                            draw_resolution_scale_y() >
                        guest_framebuffer->host_extent.height) {
                  eligible = false;
                  break;
                }
              }
              if (!eligible) {
                break;
              }
            }
          }
          if (!eligible) {
            ++inpass_skip_other;
          }
        }
        if (eligible) {
          dest_in_guest_pass[i] = true;
          dest_native_depth_handoff[i] = native_depth_handoff;
          any_dest_in_guest_pass = true;
          ++inpass_dest_count;
        }
      }
    }
    command_processor_.AddInpassTransferStats(
        inpass_dest_count, inpass_skip_format, inpass_skip_other);
  }

  // gpu_vulkan_classify_img_sr_breaks (input-attachment feasibility, BD-30):
  // independently of the inpass eligibility above (and regardless of
  // gpu_vulkan_inpass_edram_transfers), measure the FEEDBACK EDRAM ownership
  // transfers - those whose SOURCE (or a distinct host-depth source) is also one
  // of the currently-bound framebuffer attachments. This is exactly the subset
  // gpu_vulkan_inpass_edram_transfers always leaves on the legacy
  // EndRenderPass(store source)+BeginRenderPass(load dest) break (BD's ~42
  // brk_img_sr breaks / the device-confirmed ~79ms). For each, decide whether the
  // source->dest transform GetTransferShader emits is SAME-PIXEL: the identity
  // map, requiring same EDRAM base (source_to_dest == 0), same pitch, same MSAA,
  // same bpp class and same color/depth class (color<->depth swaps 40-sample tile
  // columns). Same-pixel is the ONLY case a Vulkan input attachment (subpassLoad
  // reads strictly the fragment's own position) could replace the store/load. A
  // different base/pitch/class means the shader reads the source at a REMAPPED
  // texel (texelFetch of computed EDRAM coordinates) that subpassLoad cannot
  // express -> input attachments do not apply. Read-only counters + a throttled
  // detail log; byte-identical when the cvar is off.
  if (cvars::gpu_vulkan_classify_img_sr_breaks && render_target_transfers &&
      render_target_count <= 1 + xenos::kMaxColorRenderTargets) {
    for (uint32_t i = 0; i < render_target_count; ++i) {
      RenderTarget* dest_rt = render_targets[i];
      if (!dest_rt) {
        continue;
      }
      RenderTargetKey dest_rt_key =
          static_cast<VulkanRenderTarget*>(dest_rt)->key();
      for (const Transfer& transfer : render_target_transfers[i]) {
        bool feedback = false;
        for (uint32_t j = 0; j < render_target_count; ++j) {
          if (!render_targets[j]) {
            continue;
          }
          if (transfer.source == render_targets[j] ||
              (transfer.host_depth_source &&
               transfer.host_depth_source != dest_rt &&
               transfer.host_depth_source == render_targets[j])) {
            feedback = true;
            break;
          }
        }
        if (!feedback || !transfer.source) {
          continue;
        }
        RenderTargetKey source_rt_key =
            static_cast<VulkanRenderTarget*>(transfer.source)->key();
        bool same_pixel =
            source_rt_key.base_tiles == dest_rt_key.base_tiles &&
            source_rt_key.GetPitchTiles() == dest_rt_key.GetPitchTiles() &&
            source_rt_key.msaa_samples == dest_rt_key.msaa_samples &&
            source_rt_key.Is64bpp() == dest_rt_key.Is64bpp() &&
            source_rt_key.is_depth == dest_rt_key.is_depth;
        command_processor_.AddFeedbackTransferStats(same_pixel);
        if (command_processor_.ShouldLogFeedbackDetail()) {
          XELOGI(
              "EDRAM_FEEDBACK xfer: src_base={} src_pitch={} src_depth={} "
              "src_msaa={} src_fmt={} dst_base={} dst_pitch={} dst_depth={} "
              "dst_msaa={} dst_fmt={} src_to_dst={} samepix={}",
              source_rt_key.base_tiles, source_rt_key.GetPitchTiles(),
              uint32_t(source_rt_key.is_depth),
              uint32_t(source_rt_key.msaa_samples),
              source_rt_key.resource_format, dest_rt_key.base_tiles,
              dest_rt_key.GetPitchTiles(), uint32_t(dest_rt_key.is_depth),
              uint32_t(dest_rt_key.msaa_samples), dest_rt_key.resource_format,
              int32_t(dest_rt_key.base_tiles) -
                  int32_t(source_rt_key.base_tiles),
              same_pixel ? 1 : 0);
        }
      }
    }
  }

  // Do host depth storing for the depth destination (assuming there can be only
  // one depth destination) where depth destination == host depth source.
  bool host_depth_store_set_up = false;
  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* dest_rt = render_targets[i];
    if (!dest_rt) {
      continue;
    }
    auto& dest_vulkan_rt = *static_cast<VulkanRenderTarget*>(dest_rt);
    RenderTargetKey dest_rt_key = dest_vulkan_rt.key();
    if (!dest_rt_key.is_depth) {
      continue;
    }
    const std::vector<Transfer>& depth_transfers = render_target_transfers[i];
    for (const Transfer& transfer : depth_transfers) {
      if (transfer.host_depth_source != dest_rt) {
        continue;
      }
      if (!host_depth_store_set_up) {
        // Pipeline.
        command_processor_.BindExternalComputePipeline(
            host_depth_store_pipelines_[size_t(dest_rt_key.msaa_samples)]);
        // Descriptor set bindings.
        VkDescriptorSet host_depth_store_descriptor_sets[] = {
            edram_storage_buffer_descriptor_set_,
            dest_vulkan_rt.GetDescriptorSetTransferSource(),
        };
        command_buffer.CmdVkBindDescriptorSets(
            VK_PIPELINE_BIND_POINT_COMPUTE, host_depth_store_pipeline_layout_,
            0, uint32_t(xe::countof(host_depth_store_descriptor_sets)),
            host_depth_store_descriptor_sets, 0, nullptr);
        // Render target constant.
        HostDepthStoreRenderTargetConstant
            host_depth_store_render_target_constant =
                GetHostDepthStoreRenderTargetConstant(
                    dest_rt_key.pitch_tiles_at_32bpp,
                    msaa_2x_attachments_supported_);
        command_buffer.CmdVkPushConstants(
            host_depth_store_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
            uint32_t(offsetof(HostDepthStoreConstants, render_target)),
            sizeof(host_depth_store_render_target_constant),
            &host_depth_store_render_target_constant);
        // Barriers - don't need to try to combine them with the rest of
        // render target transfer barriers now - if this happens, after host
        // depth storing, SHADER_READ -> DEPTH_STENCIL_ATTACHMENT_WRITE will be
        // done anyway even in the best case, so it's not possible to have all
        // the barriers in one place here.
        UseEdramBuffer(EdramBufferUsage::kComputeWrite);
        // Always transitioning both depth and stencil, not storing separate
        // usage flags for depth and stencil.
        command_processor_.PushImageMemoryBarrier(
            dest_vulkan_rt.image(),
            ui::vulkan::util::InitializeSubresourceRange(
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
            dest_vulkan_rt.current_stage_mask(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            dest_vulkan_rt.current_access_mask(), VK_ACCESS_SHADER_READ_BIT,
            dest_vulkan_rt.current_layout(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        dest_vulkan_rt.SetUsage(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_ACCESS_SHADER_READ_BIT,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        host_depth_store_set_up = true;
      }
      Transfer::Rectangle
          transfer_rectangles[Transfer::kMaxRectanglesWithCutout];
      uint32_t transfer_rectangle_count = transfer.GetRectangles(
          dest_rt_key.base_tiles, dest_rt_key.pitch_tiles_at_32bpp,
          dest_rt_key.msaa_samples, false, transfer_rectangles,
          resolve_clear_rectangle);
      assert_not_zero(transfer_rectangle_count);
      HostDepthStoreRectangleConstant host_depth_store_rectangle_constant;
      for (uint32_t j = 0; j < transfer_rectangle_count; ++j) {
        uint32_t group_count_x, group_count_y;
        GetHostDepthStoreRectangleInfo(
            transfer_rectangles[j], dest_rt_key.msaa_samples,
            host_depth_store_rectangle_constant, group_count_x, group_count_y);
        command_buffer.CmdVkPushConstants(
            host_depth_store_pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT,
            uint32_t(offsetof(HostDepthStoreConstants, rectangle)),
            sizeof(host_depth_store_rectangle_constant),
            &host_depth_store_rectangle_constant);
        command_processor_.SubmitBarriers(true);
        command_processor_.RecordResolveTimingBracket(
            true, VulkanCommandProcessor::GpuPassKind::kHostDepthStoreDispatch);
        command_buffer.CmdVkDispatch(group_count_x, group_count_y, 1);
        command_processor_.RecordResolveTimingBracket(
            false, VulkanCommandProcessor::GpuPassKind::kHostDepthStoreDispatch);
        MarkEdramBufferModified();
      }
    }
    break;
  }

  constexpr VkPipelineStageFlags kSourceStageMask =
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  constexpr VkAccessFlags kSourceAccessMask = VK_ACCESS_SHADER_READ_BIT;
  constexpr VkImageLayout kSourceLayout =
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // Try to insert as many barriers as possible in one place, hoping that in the
  // best case (no cross-copying between current render targets), barriers will
  // need to be only inserted here, not between transfers. In case of
  // cross-copying, if the destination use is going to happen before the source
  // use, choose the destination state, otherwise the source state - to match
  // the order in which transfers will actually happen (otherwise there will be
  // just a useless switch back and forth).
  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* dest_rt = render_targets[i];
    if (!dest_rt) {
      continue;
    }
    const std::vector<Transfer>& dest_transfers = render_target_transfers[i];
    if (!resolve_clear_needed && dest_transfers.empty()) {
      continue;
    }
    // Transition the destination, only if not going to be used as a source
    // earlier.
    bool dest_used_previously_as_source = false;
    for (uint32_t j = 0; j < i; ++j) {
      for (const Transfer& previous_transfer : render_target_transfers[j]) {
        if (previous_transfer.source == dest_rt ||
            previous_transfer.host_depth_source == dest_rt) {
          dest_used_previously_as_source = true;
          break;
        }
      }
    }
    if (!dest_used_previously_as_source) {
      auto& dest_vulkan_rt = *static_cast<VulkanRenderTarget*>(dest_rt);
      VkPipelineStageFlags dest_dst_stage_mask;
      VkAccessFlags dest_dst_access_mask;
      VkImageLayout dest_new_layout;
      dest_vulkan_rt.GetDrawUsage(&dest_dst_stage_mask, &dest_dst_access_mask,
                                  &dest_new_layout);
      command_processor_.PushImageMemoryBarrier(
          dest_vulkan_rt.image(),
          ui::vulkan::util::InitializeSubresourceRange(
              dest_vulkan_rt.key().is_depth
                  ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                  : VK_IMAGE_ASPECT_COLOR_BIT),
          dest_vulkan_rt.current_stage_mask(), dest_dst_stage_mask,
          dest_vulkan_rt.current_access_mask(), dest_dst_access_mask,
          dest_vulkan_rt.current_layout(), dest_new_layout);
      dest_vulkan_rt.SetUsage(dest_dst_stage_mask, dest_dst_access_mask,
                              dest_new_layout);
    }
    // Transition the sources, only if not going to be used as destinations
    // earlier.
    for (const Transfer& transfer : dest_transfers) {
      bool source_previously_used_as_dest = false;
      bool host_depth_source_previously_used_as_dest = false;
      for (uint32_t j = 0; j < i; ++j) {
        if (render_target_transfers[j].empty()) {
          continue;
        }
        const RenderTarget* previous_rt = render_targets[j];
        if (transfer.source == previous_rt) {
          source_previously_used_as_dest = true;
        }
        if (transfer.host_depth_source == previous_rt) {
          host_depth_source_previously_used_as_dest = true;
        }
      }
      if (!source_previously_used_as_dest) {
        auto& source_vulkan_rt =
            *static_cast<VulkanRenderTarget*>(transfer.source);
        command_processor_.PushImageMemoryBarrier(
            source_vulkan_rt.image(),
            ui::vulkan::util::InitializeSubresourceRange(
                source_vulkan_rt.key().is_depth
                    ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                    : VK_IMAGE_ASPECT_COLOR_BIT),
            source_vulkan_rt.current_stage_mask(), kSourceStageMask,
            source_vulkan_rt.current_access_mask(), kSourceAccessMask,
            source_vulkan_rt.current_layout(), kSourceLayout);
        source_vulkan_rt.SetUsage(kSourceStageMask, kSourceAccessMask,
                                  kSourceLayout);
      }
      // transfer.host_depth_source == dest_rt means the EDRAM buffer will be
      // used instead, no need to transition.
      if (transfer.host_depth_source && transfer.host_depth_source != dest_rt &&
          !host_depth_source_previously_used_as_dest) {
        auto& host_depth_source_vulkan_rt =
            *static_cast<VulkanRenderTarget*>(transfer.host_depth_source);
        command_processor_.PushImageMemoryBarrier(
            host_depth_source_vulkan_rt.image(),
            ui::vulkan::util::InitializeSubresourceRange(
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
            host_depth_source_vulkan_rt.current_stage_mask(), kSourceStageMask,
            host_depth_source_vulkan_rt.current_access_mask(),
            kSourceAccessMask, host_depth_source_vulkan_rt.current_layout(),
            kSourceLayout);
        host_depth_source_vulkan_rt.SetUsage(kSourceStageMask,
                                             kSourceAccessMask, kSourceLayout);
      }
    }
  }
  if (host_depth_store_set_up) {
    // Will be reading copied host depth from the EDRAM buffer.
    UseEdramBuffer(EdramBufferUsage::kFragmentRead);
  }

  // Perform the transfers and clears.

  TransferPipelineLayoutIndex last_transfer_pipeline_layout_index =
      TransferPipelineLayoutIndex::kCount;
  uint32_t transfer_descriptor_sets_bound = 0;
  uint32_t transfer_push_constants_set = 0;
  VkDescriptorSet last_descriptor_set_host_depth_stencil_textures =
      VK_NULL_HANDLE;
  VkDescriptorSet last_descriptor_set_depth_stencil_textures = VK_NULL_HANDLE;
  VkDescriptorSet last_descriptor_set_color_texture = VK_NULL_HANDLE;
  TransferAddressConstant last_host_depth_address_constant;
  TransferAddressConstant last_address_constant;

  // Two phases when in-pass transfers are active: phase 0 records the legacy
  // (ineligible) destinations through their dedicated transfer passes first;
  // phase 1 then enters the guest render pass once for all eligible
  // destinations and leaves it open for the guest draw that follows.
  for (uint32_t loop_phase = 0;
       loop_phase < (any_dest_in_guest_pass ? 2u : 1u); ++loop_phase) {
  for (uint32_t i = 0; i < render_target_count; ++i) {
    RenderTarget* dest_rt = render_targets[i];
    if (!dest_rt) {
      continue;
    }
    if (any_dest_in_guest_pass && dest_in_guest_pass[i] != (loop_phase == 1)) {
      continue;
    }
    const bool dest_uses_guest_pass = dest_in_guest_pass[i];

    const std::vector<Transfer>& current_transfers = render_target_transfers[i];
    if (current_transfers.empty() && !resolve_clear_needed) {
      continue;
    }

    auto& dest_vulkan_rt = *static_cast<VulkanRenderTarget*>(dest_rt);
    RenderTargetKey dest_rt_key = dest_vulkan_rt.key();

    // Late barriers in case there was cross-copying that prevented merging of
    // barriers.
    {
      VkPipelineStageFlags dest_dst_stage_mask;
      VkAccessFlags dest_dst_access_mask;
      VkImageLayout dest_new_layout;
      dest_vulkan_rt.GetDrawUsage(&dest_dst_stage_mask, &dest_dst_access_mask,
                                  &dest_new_layout);
      command_processor_.PushImageMemoryBarrier(
          dest_vulkan_rt.image(),
          ui::vulkan::util::InitializeSubresourceRange(
              dest_rt_key.is_depth
                  ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                  : VK_IMAGE_ASPECT_COLOR_BIT),
          dest_vulkan_rt.current_stage_mask(), dest_dst_stage_mask,
          dest_vulkan_rt.current_access_mask(), dest_dst_access_mask,
          dest_vulkan_rt.current_layout(), dest_new_layout);
      dest_vulkan_rt.SetUsage(dest_dst_stage_mask, dest_dst_access_mask,
                              dest_new_layout);
    }

    // Get the objects needed for transfers to the destination.
    // In-pass mode (gpu_vulkan_inpass_edram_transfers, the TODO below): record
    // the transfer draws inside the guest render pass itself - the transfer
    // pipelines are keyed by the full RenderPassKey and write-mask only the
    // destination color attachment, so they are compatible with the guest pass
    // as long as the destination uses its draw format (eligibility ensured
    // that). The pass is entered here and left open for the guest draw.
    // TODO(Triang3l): Reuse the guest render pass for transfers where possible
    // (if the Vulkan format used for drawing is also usable for transfers - for
    // instance, R8G8B8A8_UNORM can be used for both, so the guest pass can be
    // reused, but R16G16B16A16_SFLOAT render targets use R16G16B16A16_UINT for
    // transfers, so the transfer pass has to be separate) to avoid stores and
    // loads on tile-based devices to make this actually applicable. Also
    // overall perform all non-cross-copying transfers for the current
    // framebuffer configuration in a single pass, to load / store only once.
    RenderPassKey transfer_render_pass_key;
    VkRenderPass transfer_render_pass;
    const Framebuffer* transfer_framebuffer;
    if (dest_uses_guest_pass) {
      transfer_render_pass_key = *guest_render_pass_key;
      transfer_render_pass = guest_render_pass;
      transfer_framebuffer = guest_framebuffer;
      // Same-format sizing instrumentation, kept consistent with the legacy
      // path (in-pass color destinations are always same-format by
      // eligibility; in-pass depth still counts as not-same-format).
      command_processor_.AddTransferFormatStats(!dest_rt_key.is_depth);
    } else {
    transfer_render_pass_key.msaa_samples = dest_rt_key.msaa_samples;
    // When the color transfer format is identical to the draw format (no integer
    // reinterpretation needed, e.g. R8G8B8A8_UNORM / A2B10G10R10), the transfer
    // pass key matches the guest draw pass key for this RT, so it doesn't need
    // color_rts_use_transfer_formats. Leaving that bit off (under
    // vulkan_coalesce_edram_transfers) makes the transfer pass key equal to a
    // guest pass key, so if that guest pass is already current the per-draw enter
    // can early-return without an end/begin -> no Adreno tile flush. Only safe
    // for same-format color transfers; depth + integer-reinterpret stay on the
    // dedicated transfer-format pass.
    bool transfer_color_same_format = false;
    if (dest_rt_key.is_depth) {
      transfer_render_pass_key.depth_and_color_used = 0b1;
      transfer_render_pass_key.depth_format = dest_rt_key.GetDepthFormat();
    } else {
      transfer_render_pass_key.depth_and_color_used = 0b1 << 1;
      transfer_render_pass_key.color_0_view_format =
          dest_rt_key.GetColorFormat();
      bool is_integer = false;
      GetColorOwnershipTransferVulkanFormat(dest_rt_key.GetColorFormat(),
                                            &is_integer);
      transfer_color_same_format =
          !is_integer && cvars::vulkan_coalesce_edram_transfers;
      transfer_render_pass_key.color_rts_use_transfer_formats =
          transfer_color_same_format ? 0 : 1;
    }
    // Instrumentation: is this transfer's render pass FORMAT-COMPATIBLE with the
    // guest draw pass (so it could reuse it and avoid a tile flush)? Color: the
    // transfer format == draw format exactly when the ownership-transfer format
    // is not an integer reinterpretation (is_integer == false). Depth: the
    // transfer always reads via a texture and may use a host depth encoding, so
    // count it as not-reusable for this sizing pass.
    {
      bool same_format = false;
      if (!dest_rt_key.is_depth) {
        bool is_integer = false;
        GetColorOwnershipTransferVulkanFormat(dest_rt_key.GetColorFormat(),
                                              &is_integer);
        same_format = !is_integer;
      }
      command_processor_.AddTransferFormatStats(same_format);
    }
    transfer_render_pass =
        GetHostRenderTargetsRenderPass(transfer_render_pass_key);
    if (transfer_render_pass == VK_NULL_HANDLE) {
      continue;
    }
    const RenderTarget*
        transfer_framebuffer_render_targets[1 + xenos::kMaxColorRenderTargets] =
            {};
    transfer_framebuffer_render_targets[dest_rt_key.is_depth ? 0 : 1] = dest_rt;
    transfer_framebuffer = GetHostRenderTargetsFramebuffer(
        transfer_render_pass_key, dest_rt_key.pitch_tiles_at_32bpp,
        transfer_framebuffer_render_targets);
    if (!transfer_framebuffer) {
      continue;
    }
    }
    // Don't enter the render pass immediately - may still insert source
    // barriers later.

    if (!current_transfers.empty()) {
      uint32_t dest_pitch_tiles = dest_rt_key.GetPitchTiles();
      bool dest_is_64bpp = dest_rt_key.Is64bpp();

      // Gather shader keys and sort to reduce pipeline state and binding
      // switches. Also gather stencil rectangles to clear if needed.
      bool need_stencil_bit_draws =
          dest_rt_key.is_depth &&
          !vulkan_device->extensions().ext_EXT_shader_stencil_export;
      current_transfer_invocations_.clear();
      current_transfer_invocations_.reserve(
          current_transfers.size() << uint32_t(need_stencil_bit_draws));
      uint32_t rt_sort_index = 0;
      TransferShaderKey new_transfer_shader_key;
      new_transfer_shader_key.dest_msaa_samples = dest_rt_key.msaa_samples;
      new_transfer_shader_key.dest_resource_format =
          dest_rt_key.resource_format;
      // In-pass transfers output to the destination's actual color attachment
      // slot in the guest render pass (the transfer fragment shader writes
      // Location = dest_color_rt_index, and the pipeline write-masks only that
      // attachment). The dedicated transfer pass always uses slot 0.
      new_transfer_shader_key.dest_color_rt_index =
          (dest_uses_guest_pass && i > 0) ? (i - 1) : 0;
      uint32_t stencil_clear_rectangle_count = 0;
      for (uint32_t j = 0; j <= uint32_t(need_stencil_bit_draws); ++j) {
        // j == 0 - color or depth.
        // j == 1 - stencil bits.
        // Stencil bit writing always requires a different root signature,
        // handle these separately. Stencil never has a host depth source.
        // Clear previously set sort indices.
        for (const Transfer& transfer : current_transfers) {
          auto host_depth_source_vulkan_rt =
              static_cast<VulkanRenderTarget*>(transfer.host_depth_source);
          if (host_depth_source_vulkan_rt) {
            host_depth_source_vulkan_rt->SetTemporarySortIndex(UINT32_MAX);
          }
          assert_not_null(transfer.source);
          auto& source_vulkan_rt =
              *static_cast<VulkanRenderTarget*>(transfer.source);
          source_vulkan_rt.SetTemporarySortIndex(UINT32_MAX);
        }
        for (const Transfer& transfer : current_transfers) {
          assert_not_null(transfer.source);
          auto& source_vulkan_rt =
              *static_cast<VulkanRenderTarget*>(transfer.source);
          VulkanRenderTarget* host_depth_source_vulkan_rt =
              j ? nullptr
                : static_cast<VulkanRenderTarget*>(transfer.host_depth_source);
          if (host_depth_source_vulkan_rt &&
              host_depth_source_vulkan_rt->temporary_sort_index() ==
                  UINT32_MAX) {
            host_depth_source_vulkan_rt->SetTemporarySortIndex(rt_sort_index++);
          }
          if (source_vulkan_rt.temporary_sort_index() == UINT32_MAX) {
            source_vulkan_rt.SetTemporarySortIndex(rt_sort_index++);
          }
          RenderTargetKey source_rt_key = source_vulkan_rt.key();
          new_transfer_shader_key.source_msaa_samples =
              source_rt_key.msaa_samples;
          new_transfer_shader_key.source_resource_format =
              source_rt_key.resource_format;
          bool host_depth_source_is_copy =
              host_depth_source_vulkan_rt == &dest_vulkan_rt;
          // The host depth copy buffer has only raw samples.
          new_transfer_shader_key.host_depth_source_msaa_samples =
              (host_depth_source_vulkan_rt && !host_depth_source_is_copy)
                  ? host_depth_source_vulkan_rt->key().msaa_samples
                  : xenos::MsaaSamples::k1X;
          if (j) {
            new_transfer_shader_key.mode =
                source_rt_key.is_depth ? TransferMode::kDepthToStencilBit
                                       : TransferMode::kColorToStencilBit;
            stencil_clear_rectangle_count +=
                transfer.GetRectangles(dest_rt_key.base_tiles, dest_pitch_tiles,
                                       dest_rt_key.msaa_samples, dest_is_64bpp,
                                       nullptr, resolve_clear_rectangle);
          } else {
            if (dest_rt_key.is_depth) {
              if (host_depth_source_vulkan_rt) {
                if (host_depth_source_is_copy) {
                  new_transfer_shader_key.mode =
                      source_rt_key.is_depth
                          ? TransferMode::kDepthAndHostDepthCopyToDepth
                          : TransferMode::kColorAndHostDepthCopyToDepth;
                } else {
                  new_transfer_shader_key.mode =
                      source_rt_key.is_depth
                          ? TransferMode::kDepthAndHostDepthToDepth
                          : TransferMode::kColorAndHostDepthToDepth;
                }
              } else {
                new_transfer_shader_key.mode =
                    source_rt_key.is_depth ? TransferMode::kDepthToDepth
                                           : TransferMode::kColorToDepth;
              }
            } else {
              new_transfer_shader_key.mode = source_rt_key.is_depth
                                                 ? TransferMode::kDepthToColor
                                                 : TransferMode::kColorToColor;
            }
          }
          current_transfer_invocations_.emplace_back(transfer,
                                                     new_transfer_shader_key);
          if (j) {
            current_transfer_invocations_.back().transfer.host_depth_source =
                nullptr;
          }
        }
      }
      std::sort(current_transfer_invocations_.begin(),
                current_transfer_invocations_.end());

      for (auto it = current_transfer_invocations_.cbegin();
           it != current_transfer_invocations_.cend(); ++it) {
        assert_not_null(it->transfer.source);
        auto& source_vulkan_rt =
            *static_cast<VulkanRenderTarget*>(it->transfer.source);
        command_processor_.PushImageMemoryBarrier(
            source_vulkan_rt.image(),
            ui::vulkan::util::InitializeSubresourceRange(
                source_vulkan_rt.key().is_depth
                    ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                    : VK_IMAGE_ASPECT_COLOR_BIT),
            source_vulkan_rt.current_stage_mask(), kSourceStageMask,
            source_vulkan_rt.current_access_mask(), kSourceAccessMask,
            source_vulkan_rt.current_layout(), kSourceLayout);
        source_vulkan_rt.SetUsage(kSourceStageMask, kSourceAccessMask,
                                  kSourceLayout);
        auto host_depth_source_vulkan_rt =
            static_cast<VulkanRenderTarget*>(it->transfer.host_depth_source);
        if (host_depth_source_vulkan_rt) {
          TransferShaderKey transfer_shader_key = it->shader_key;
          if (transfer_shader_key.mode ==
                  TransferMode::kDepthAndHostDepthCopyToDepth ||
              transfer_shader_key.mode ==
                  TransferMode::kColorAndHostDepthCopyToDepth) {
            // Reading copied host depth from the EDRAM buffer.
            UseEdramBuffer(EdramBufferUsage::kFragmentRead);
          } else {
            // Reading host depth from the texture.
            command_processor_.PushImageMemoryBarrier(
                host_depth_source_vulkan_rt->image(),
                ui::vulkan::util::InitializeSubresourceRange(
                    VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
                host_depth_source_vulkan_rt->current_stage_mask(),
                kSourceStageMask,
                host_depth_source_vulkan_rt->current_access_mask(),
                kSourceAccessMask,
                host_depth_source_vulkan_rt->current_layout(), kSourceLayout);
            host_depth_source_vulkan_rt->SetUsage(
                kSourceStageMask, kSourceAccessMask, kSourceLayout);
          }
        }
      }

      // Perform the transfers for the render target.

      command_processor_.SubmitBarriersAndEnterRenderTargetCacheRenderPass(
          transfer_render_pass, transfer_framebuffer,
          VulkanCommandProcessor::GpuPassKind::kEdramTransfer);

      if (stencil_clear_rectangle_count) {
        VkClearAttachment* stencil_clear_attachment;
        VkClearRect* stencil_clear_rect_write_ptr;
        command_buffer.CmdClearAttachmentsEmplace(1, stencil_clear_attachment,
                                                  stencil_clear_rectangle_count,
                                                  stencil_clear_rect_write_ptr);
        stencil_clear_attachment->aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        stencil_clear_attachment->colorAttachment = 0;
        stencil_clear_attachment->clearValue.depthStencil.depth = 0.0f;
        stencil_clear_attachment->clearValue.depthStencil.stencil = 0;
        for (const Transfer& transfer : current_transfers) {
          Transfer::Rectangle transfer_stencil_clear_rectangles
              [Transfer::kMaxRectanglesWithCutout];
          uint32_t transfer_stencil_clear_rectangle_count =
              transfer.GetRectangles(dest_rt_key.base_tiles, dest_pitch_tiles,
                                     dest_rt_key.msaa_samples, dest_is_64bpp,
                                     transfer_stencil_clear_rectangles,
                                     resolve_clear_rectangle);
          for (uint32_t j = 0; j < transfer_stencil_clear_rectangle_count;
               ++j) {
            const Transfer::Rectangle& stencil_clear_rectangle =
                transfer_stencil_clear_rectangles[j];
            stencil_clear_rect_write_ptr->rect.offset.x = int32_t(
                stencil_clear_rectangle.x_pixels * draw_resolution_scale_x());
            stencil_clear_rect_write_ptr->rect.offset.y = int32_t(
                stencil_clear_rectangle.y_pixels * draw_resolution_scale_y());
            stencil_clear_rect_write_ptr->rect.extent.width =
                stencil_clear_rectangle.width_pixels *
                draw_resolution_scale_x();
            stencil_clear_rect_write_ptr->rect.extent.height =
                stencil_clear_rectangle.height_pixels *
                draw_resolution_scale_y();
            stencil_clear_rect_write_ptr->baseArrayLayer = 0;
            stencil_clear_rect_write_ptr->layerCount = 1;
            ++stencil_clear_rect_write_ptr;
          }
        }
      }

      // Prefer power of two viewports for exact division by simply biasing the
      // exponent.
      VkViewport transfer_viewport;
      transfer_viewport.x = 0.0f;
      transfer_viewport.y = 0.0f;
      transfer_viewport.width =
          float(std::min(xe::next_pow2(transfer_framebuffer->host_extent.width),
                         vulkan_device->properties().maxViewportDimensions[0]));
      transfer_viewport.height = float(
          std::min(xe::next_pow2(transfer_framebuffer->host_extent.height),
                   vulkan_device->properties().maxViewportDimensions[1]));
      transfer_viewport.minDepth = 0.0f;
      transfer_viewport.maxDepth = 1.0f;
      command_processor_.SetViewport(transfer_viewport);
      float pixels_to_ndc_x = 2.0f / transfer_viewport.width;
      float pixels_to_ndc_y = 2.0f / transfer_viewport.height;
      VkRect2D transfer_scissor;
      transfer_scissor.offset.x = 0;
      transfer_scissor.offset.y = 0;
      transfer_scissor.extent = transfer_framebuffer->host_extent;
      command_processor_.SetScissor(transfer_scissor);

      for (auto it = current_transfer_invocations_.cbegin();
           it != current_transfer_invocations_.cend(); ++it) {
        const TransferInvocation& transfer_invocation_first = *it;
        // Will be merging transfers from the same source into one mesh.
        auto it_merged_first = it, it_merged_last = it;
        uint32_t transfer_rectangle_count =
            transfer_invocation_first.transfer.GetRectangles(
                dest_rt_key.base_tiles, dest_pitch_tiles,
                dest_rt_key.msaa_samples, dest_is_64bpp, nullptr,
                resolve_clear_rectangle);
        for (auto it_merge = std::next(it_merged_first);
             it_merge != current_transfer_invocations_.cend(); ++it_merge) {
          if (!transfer_invocation_first.CanBeMergedIntoOneDraw(*it_merge)) {
            break;
          }
          transfer_rectangle_count += it_merge->transfer.GetRectangles(
              dest_rt_key.base_tiles, dest_pitch_tiles,
              dest_rt_key.msaa_samples, dest_is_64bpp, nullptr,
              resolve_clear_rectangle);
          it_merged_last = it_merge;
        }
        assert_not_zero(transfer_rectangle_count);
        // Skip the merged transfers in the subsequent iterations.
        it = it_merged_last;

        assert_not_null(it->transfer.source);
        auto& source_vulkan_rt =
            *static_cast<VulkanRenderTarget*>(it->transfer.source);
        auto host_depth_source_vulkan_rt =
            static_cast<VulkanRenderTarget*>(it->transfer.host_depth_source);
        TransferShaderKey transfer_shader_key = it->shader_key;
        const TransferModeInfo& transfer_mode_info =
            kTransferModes[size_t(transfer_shader_key.mode)];
        TransferPipelineLayoutIndex transfer_pipeline_layout_index =
            transfer_mode_info.pipeline_layout;
        const TransferPipelineLayoutInfo& transfer_pipeline_layout_info =
            kTransferPipelineLayoutInfos[size_t(
                transfer_pipeline_layout_index)];
        uint32_t transfer_sample_pipeline_count =
            vulkan_device->properties().sampleRateShading
                ? 1
                : uint32_t(1) << uint32_t(dest_rt_key.msaa_samples);
        bool transfer_is_stencil_bit =
            (transfer_pipeline_layout_info.used_push_constant_dwords &
             kTransferUsedPushConstantDwordStencilMaskBit) != 0;

        uint32_t transfer_vertex_count = 6 * transfer_rectangle_count;
        VkBuffer transfer_vertex_buffer;
        VkDeviceSize transfer_vertex_buffer_offset;
        float* transfer_rectangle_write_ptr =
            reinterpret_cast<float*>(transfer_vertex_buffer_pool_->Request(
                current_submission, sizeof(float) * 2 * transfer_vertex_count,
                sizeof(float), transfer_vertex_buffer,
                transfer_vertex_buffer_offset));
        if (!transfer_rectangle_write_ptr) {
          continue;
        }
        for (auto it_merged = it_merged_first; it_merged <= it_merged_last;
             ++it_merged) {
          Transfer::Rectangle transfer_invocation_rectangles
              [Transfer::kMaxRectanglesWithCutout];
          uint32_t transfer_invocation_rectangle_count =
              it_merged->transfer.GetRectangles(
                  dest_rt_key.base_tiles, dest_pitch_tiles,
                  dest_rt_key.msaa_samples, dest_is_64bpp,
                  transfer_invocation_rectangles, resolve_clear_rectangle);
          assert_not_zero(transfer_invocation_rectangle_count);
          for (uint32_t j = 0; j < transfer_invocation_rectangle_count; ++j) {
            const Transfer::Rectangle& transfer_rectangle =
                transfer_invocation_rectangles[j];
            float transfer_rectangle_x0 =
                -1.0f + transfer_rectangle.x_pixels * pixels_to_ndc_x;
            float transfer_rectangle_y0 =
                -1.0f + transfer_rectangle.y_pixels * pixels_to_ndc_y;
            float transfer_rectangle_x1 =
                transfer_rectangle_x0 +
                transfer_rectangle.width_pixels * pixels_to_ndc_x;
            float transfer_rectangle_y1 =
                transfer_rectangle_y0 +
                transfer_rectangle.height_pixels * pixels_to_ndc_y;
            // O-*
            // |/
            // *
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_x0;
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_y0;
            // *-*
            // |/
            // O
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_x0;
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_y1;
            // *-O
            // |/
            // *
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_x1;
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_y0;
            //   O
            //  /|
            // *-*
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_x1;
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_y0;
            //   *
            //  /|
            // O-*
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_x0;
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_y1;
            //   *
            //  /|
            // *-O
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_x1;
            *(transfer_rectangle_write_ptr++) = transfer_rectangle_y1;
          }
        }
        command_buffer.CmdVkBindVertexBuffers(0, 1, &transfer_vertex_buffer,
                                              &transfer_vertex_buffer_offset);

        const VkPipeline* transfer_pipelines = GetTransferPipelines(
            TransferPipelineKey(transfer_render_pass_key, transfer_shader_key));
        if (!transfer_pipelines) {
          continue;
        }
        command_processor_.BindExternalGraphicsPipeline(transfer_pipelines[0]);
        if (last_transfer_pipeline_layout_index !=
            transfer_pipeline_layout_index) {
          last_transfer_pipeline_layout_index = transfer_pipeline_layout_index;
          transfer_descriptor_sets_bound = 0;
          transfer_push_constants_set = 0;
        }

        // Invalidate outdated bindings.
        if (transfer_pipeline_layout_info.used_descriptor_sets &
            kTransferUsedDescriptorSetHostDepthStencilTexturesBit) {
          assert_not_null(host_depth_source_vulkan_rt);
          VkDescriptorSet descriptor_set_host_depth_stencil_textures =
              host_depth_source_vulkan_rt->GetDescriptorSetTransferSource();
          if (last_descriptor_set_host_depth_stencil_textures !=
              descriptor_set_host_depth_stencil_textures) {
            last_descriptor_set_host_depth_stencil_textures =
                descriptor_set_host_depth_stencil_textures;
            transfer_descriptor_sets_bound &=
                ~kTransferUsedDescriptorSetHostDepthStencilTexturesBit;
          }
        }
        if (transfer_pipeline_layout_info.used_descriptor_sets &
            kTransferUsedDescriptorSetDepthStencilTexturesBit) {
          VkDescriptorSet descriptor_set_depth_stencil_textures =
              source_vulkan_rt.GetDescriptorSetTransferSource();
          if (last_descriptor_set_depth_stencil_textures !=
              descriptor_set_depth_stencil_textures) {
            last_descriptor_set_depth_stencil_textures =
                descriptor_set_depth_stencil_textures;
            transfer_descriptor_sets_bound &=
                ~kTransferUsedDescriptorSetDepthStencilTexturesBit;
          }
        }
        if (transfer_pipeline_layout_info.used_descriptor_sets &
            kTransferUsedDescriptorSetColorTextureBit) {
          VkDescriptorSet descriptor_set_color_texture =
              source_vulkan_rt.GetDescriptorSetTransferSource();
          if (last_descriptor_set_color_texture !=
              descriptor_set_color_texture) {
            last_descriptor_set_color_texture = descriptor_set_color_texture;
            transfer_descriptor_sets_bound &=
                ~kTransferUsedDescriptorSetColorTextureBit;
          }
        }
        if (transfer_pipeline_layout_info.used_push_constant_dwords &
            kTransferUsedPushConstantDwordHostDepthAddressBit) {
          assert_not_null(host_depth_source_vulkan_rt);
          RenderTargetKey host_depth_source_rt_key =
              host_depth_source_vulkan_rt->key();
          TransferAddressConstant host_depth_address_constant;
          host_depth_address_constant.dest_pitch = dest_pitch_tiles;
          host_depth_address_constant.source_pitch =
              host_depth_source_rt_key.GetPitchTiles();
          host_depth_address_constant.source_to_dest =
              int32_t(dest_rt_key.base_tiles) -
              int32_t(host_depth_source_rt_key.base_tiles);
          if (last_host_depth_address_constant != host_depth_address_constant) {
            last_host_depth_address_constant = host_depth_address_constant;
            transfer_push_constants_set &=
                ~kTransferUsedPushConstantDwordHostDepthAddressBit;
          }
        }
        if (transfer_pipeline_layout_info.used_push_constant_dwords &
            kTransferUsedPushConstantDwordAddressBit) {
          RenderTargetKey source_rt_key = source_vulkan_rt.key();
          TransferAddressConstant address_constant;
          address_constant.dest_pitch = dest_pitch_tiles;
          address_constant.source_pitch = source_rt_key.GetPitchTiles();
          address_constant.source_to_dest = int32_t(dest_rt_key.base_tiles) -
                                            int32_t(source_rt_key.base_tiles);
          if (last_address_constant != address_constant) {
            last_address_constant = address_constant;
            transfer_push_constants_set &=
                ~kTransferUsedPushConstantDwordAddressBit;
          }
        }

        // Apply the new bindings.
        // TODO(Triang3l): Merge binding updates into spans.
        VkPipelineLayout transfer_pipeline_layout =
            transfer_pipeline_layouts_[size_t(transfer_pipeline_layout_index)];
        uint32_t transfer_descriptor_sets_unbound =
            transfer_pipeline_layout_info.used_descriptor_sets &
            ~transfer_descriptor_sets_bound;
        if (transfer_descriptor_sets_unbound &
            kTransferUsedDescriptorSetHostDepthBufferBit) {
          command_buffer.CmdVkBindDescriptorSets(
              VK_PIPELINE_BIND_POINT_GRAPHICS, transfer_pipeline_layout,
              xe::bit_count(transfer_pipeline_layout_info.used_descriptor_sets &
                            (kTransferUsedDescriptorSetHostDepthBufferBit - 1)),
              1, &edram_storage_buffer_descriptor_set_, 0, nullptr);
          transfer_descriptor_sets_bound |=
              kTransferUsedDescriptorSetHostDepthBufferBit;
        }
        if (transfer_descriptor_sets_unbound &
            kTransferUsedDescriptorSetHostDepthStencilTexturesBit) {
          command_buffer.CmdVkBindDescriptorSets(
              VK_PIPELINE_BIND_POINT_GRAPHICS, transfer_pipeline_layout,
              xe::bit_count(
                  transfer_pipeline_layout_info.used_descriptor_sets &
                  (kTransferUsedDescriptorSetHostDepthStencilTexturesBit - 1)),
              1, &last_descriptor_set_host_depth_stencil_textures, 0, nullptr);
          transfer_descriptor_sets_bound |=
              kTransferUsedDescriptorSetHostDepthStencilTexturesBit;
        }
        if (transfer_descriptor_sets_unbound &
            kTransferUsedDescriptorSetDepthStencilTexturesBit) {
          command_buffer.CmdVkBindDescriptorSets(
              VK_PIPELINE_BIND_POINT_GRAPHICS, transfer_pipeline_layout,
              xe::bit_count(
                  transfer_pipeline_layout_info.used_descriptor_sets &
                  (kTransferUsedDescriptorSetDepthStencilTexturesBit - 1)),
              1, &last_descriptor_set_depth_stencil_textures, 0, nullptr);
          transfer_descriptor_sets_bound |=
              kTransferUsedDescriptorSetDepthStencilTexturesBit;
        }
        if (transfer_descriptor_sets_unbound &
            kTransferUsedDescriptorSetColorTextureBit) {
          command_buffer.CmdVkBindDescriptorSets(
              VK_PIPELINE_BIND_POINT_GRAPHICS, transfer_pipeline_layout,
              xe::bit_count(transfer_pipeline_layout_info.used_descriptor_sets &
                            (kTransferUsedDescriptorSetColorTextureBit - 1)),
              1, &last_descriptor_set_color_texture, 0, nullptr);
          transfer_descriptor_sets_bound |=
              kTransferUsedDescriptorSetColorTextureBit;
        }
        uint32_t transfer_push_constants_unset =
            transfer_pipeline_layout_info.used_push_constant_dwords &
            ~transfer_push_constants_set;
        if (transfer_push_constants_unset &
            kTransferUsedPushConstantDwordHostDepthAddressBit) {
          command_buffer.CmdVkPushConstants(
              transfer_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
              sizeof(uint32_t) *
                  xe::bit_count(
                      transfer_pipeline_layout_info.used_push_constant_dwords &
                      (kTransferUsedPushConstantDwordHostDepthAddressBit - 1)),
              sizeof(uint32_t), &last_host_depth_address_constant);
          transfer_push_constants_set |=
              kTransferUsedPushConstantDwordHostDepthAddressBit;
        }
        if (transfer_push_constants_unset &
            kTransferUsedPushConstantDwordAddressBit) {
          command_buffer.CmdVkPushConstants(
              transfer_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
              sizeof(uint32_t) *
                  xe::bit_count(
                      transfer_pipeline_layout_info.used_push_constant_dwords &
                      (kTransferUsedPushConstantDwordAddressBit - 1)),
              sizeof(uint32_t), &last_address_constant);
          transfer_push_constants_set |=
              kTransferUsedPushConstantDwordAddressBit;
        }

        for (uint32_t j = 0; j < transfer_sample_pipeline_count; ++j) {
          if (j) {
            command_processor_.BindExternalGraphicsPipeline(
                transfer_pipelines[j]);
          }
          for (uint32_t k = 0; k < uint32_t(transfer_is_stencil_bit ? 8 : 1);
               ++k) {
            if (transfer_is_stencil_bit) {
              uint32_t transfer_stencil_bit = uint32_t(1) << k;
              command_buffer.CmdVkPushConstants(
                  transfer_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                  sizeof(uint32_t) *
                      xe::bit_count(
                          transfer_pipeline_layout_info
                              .used_push_constant_dwords &
                          (kTransferUsedPushConstantDwordStencilMaskBit - 1)),
                  sizeof(uint32_t), &transfer_stencil_bit);
              command_buffer.CmdVkSetStencilWriteMask(
                  VK_STENCIL_FACE_FRONT_AND_BACK, transfer_stencil_bit);
            }
            command_buffer.CmdVkDraw(transfer_vertex_count, 1, 0, 0);
          }
        }
        if (cvars::gpu_bd_framegraph_depth_dump) {
          for (auto marker_it = it_merged_first; marker_it <= it_merged_last;
               ++marker_it) {
            auto& marker_source =
                *static_cast<VulkanRenderTarget*>(marker_it->transfer.source);
            RenderTargetKey marker_source_key = marker_source.key();
            if (!marker_source_key.is_depth || !dest_rt_key.is_depth ||
                marker_source_key.base_tiles != 810 ||
                dest_rt_key.base_tiles != 810 ||
                marker_source_key.GetPitchTiles() != 9 ||
                dest_rt_key.GetPitchTiles() != 9 ||
                marker_source_key.msaa_samples != xenos::MsaaSamples::k2X ||
                dest_rt_key.msaa_samples != xenos::MsaaSamples::k4X ||
                marker_it->shader_key.mode != TransferMode::kDepthToDepth) {
              continue;
            }
            Transfer::Rectangle marker_rectangles
                [Transfer::kMaxRectanglesWithCutout];
            uint32_t marker_rectangle_count =
                marker_it->transfer.GetRectangles(
                    dest_rt_key.base_tiles, dest_pitch_tiles,
                    dest_rt_key.msaa_samples, dest_is_64bpp,
                    marker_rectangles, resolve_clear_rectangle);
            uint64_t marker = UINT64_C(1469598103934665603);
            auto mix_u32 = [&marker](uint32_t value) {
              for (uint32_t byte = 0; byte < 4; ++byte) {
                marker ^= uint8_t(value >> (byte * 8));
                marker *= UINT64_C(1099511628211);
              }
            };
            mix_u32(marker_source_key.key);
            mix_u32(dest_rt_key.key);
            mix_u32(marker_it->shader_key.key);
            mix_u32(dest_pitch_tiles);
            mix_u32(marker_source_key.GetPitchTiles());
            mix_u32(uint32_t(int32_t(dest_rt_key.base_tiles) -
                             int32_t(marker_source_key.base_tiles)));
            mix_u32(6 * marker_rectangle_count);
            mix_u32(transfer_sample_pipeline_count);
            mix_u32(marker_rectangle_count);
            for (uint32_t marker_rectangle_index = 0;
                 marker_rectangle_index < marker_rectangle_count;
                 ++marker_rectangle_index) {
              const Transfer::Rectangle& marker_rectangle =
                  marker_rectangles[marker_rectangle_index];
              mix_u32(marker_rectangle.x_pixels);
              mix_u32(marker_rectangle.y_pixels);
              mix_u32(marker_rectangle.width_pixels);
              mix_u32(marker_rectangle.height_pixels);
            }
            XELOGI(
                "BD FRAMEGRAPH DEPTH DUMP: path=standalone marker={:016X} "
                "rects={} vertices={} samples={} "
                "dest{{base={} pitchT={} msaa={} format={}}}",
                marker, marker_rectangle_count, 6 * marker_rectangle_count,
                transfer_sample_pipeline_count, dest_rt_key.base_tiles,
                dest_rt_key.GetPitchTiles(),
                uint32_t(dest_rt_key.msaa_samples),
                dest_rt_key.resource_format);
          }
        }
      }
    }

    if (dest_native_depth_handoff[i] && !current_transfers.empty()) {
      uint32_t generation = dest_vulkan_rt.MarkBdNativeDepthAuthoritative();
      static std::atomic<uint32_t> s_bd_native_depth_handoff_log{0};
      if (s_bd_native_depth_handoff_log.fetch_add(1) < 20) {
        XELOGI(
            "BD NATIVE DEPTH HANDOFF: base0 pitchT16 1x -> pitchT13 1x "
            "generation={} consumer_depth_view_proven=1",
            generation);
      }
    }

    // Perform the clear.
    if (resolve_clear_needed) {
      command_processor_.SubmitBarriersAndEnterRenderTargetCacheRenderPass(
          transfer_render_pass, transfer_framebuffer,
          VulkanCommandProcessor::GpuPassKind::kResolveClear);
      VkClearAttachment resolve_clear_attachment;
      resolve_clear_attachment.colorAttachment = 0;
      std::memset(&resolve_clear_attachment.clearValue, 0,
                  sizeof(resolve_clear_attachment.clearValue));
      uint64_t clear_value = render_target_resolve_clear_values[i];
      if (dest_rt_key.is_depth) {
        resolve_clear_attachment.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        uint32_t depth_guest_clear_value =
            (uint32_t(clear_value) >> 8) & 0xFFFFFF;
        switch (dest_rt_key.GetDepthFormat()) {
          case xenos::DepthRenderTargetFormat::kD24S8:
            resolve_clear_attachment.clearValue.depthStencil.depth =
                xenos::UNorm24To32(depth_guest_clear_value);
            break;
          case xenos::DepthRenderTargetFormat::kD24FS8:
            // Taking [0, 2) -> [0, 1) remapping into account.
            resolve_clear_attachment.clearValue.depthStencil.depth =
                xenos::Float20e4To32(depth_guest_clear_value) * 0.5f;
            break;
        }
        resolve_clear_attachment.clearValue.depthStencil.stencil =
            uint32_t(clear_value) & 0xFF;
      } else {
        resolve_clear_attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        switch (dest_rt_key.GetColorFormat()) {
          case xenos::ColorRenderTargetFormat::k_8_8_8_8:
          case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA: {
            for (uint32_t j = 0; j < 4; ++j) {
              resolve_clear_attachment.clearValue.color.float32[j] =
                  ((clear_value >> (j * 8)) & 0xFF) * (1.0f / 0xFF);
            }
          } break;
          case xenos::ColorRenderTargetFormat::k_2_10_10_10:
          case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10: {
            for (uint32_t j = 0; j < 3; ++j) {
              resolve_clear_attachment.clearValue.color.float32[j] =
                  ((clear_value >> (j * 10)) & 0x3FF) * (1.0f / 0x3FF);
            }
            resolve_clear_attachment.clearValue.color.float32[3] =
                ((clear_value >> 30) & 0x3) * (1.0f / 0x3);
          } break;
          case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
          case xenos::ColorRenderTargetFormat::
              k_2_10_10_10_FLOAT_AS_16_16_16_16: {
            for (uint32_t j = 0; j < 3; ++j) {
              resolve_clear_attachment.clearValue.color.float32[j] =
                  xenos::Float7e3To32((clear_value >> (j * 10)) & 0x3FF);
            }
            resolve_clear_attachment.clearValue.color.float32[3] =
                ((clear_value >> 30) & 0x3) * (1.0f / 0x3);
          } break;
          case xenos::ColorRenderTargetFormat::k_16_16:
          case xenos::ColorRenderTargetFormat::k_16_16_FLOAT: {
            // Using uint for transfers and clears of both. Disregarding the
            // current -32...32 vs. -1...1 settings for consistency with color
            // clear via depth aliasing.
            // TODO(Triang3l): Handle cases of unsupported multisampled 16_UINT
            // and completely unsupported 16_UNORM.
            for (uint32_t j = 0; j < 2; ++j) {
              resolve_clear_attachment.clearValue.color.uint32[j] =
                  uint32_t(clear_value >> (j * 16)) & 0xFFFF;
            }
          } break;
          case xenos::ColorRenderTargetFormat::k_16_16_16_16:
          case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT: {
            // Using uint for transfers and clears of both. Disregarding the
            // current -32...32 vs. -1...1 settings for consistency with color
            // clear via depth aliasing.
            // TODO(Triang3l): Handle cases of unsupported multisampled 16_UINT
            // and completely unsupported 16_UNORM.
            for (uint32_t j = 0; j < 4; ++j) {
              resolve_clear_attachment.clearValue.color.uint32[j] =
                  uint32_t(clear_value >> (j * 16)) & 0xFFFF;
            }
          } break;
          case xenos::ColorRenderTargetFormat::k_32_FLOAT: {
            // Using uint for proper denormal and NaN handling.
            resolve_clear_attachment.clearValue.color.uint32[0] =
                uint32_t(clear_value);
          } break;
          case xenos::ColorRenderTargetFormat::k_32_32_FLOAT: {
            // Using uint for proper denormal and NaN handling.
            resolve_clear_attachment.clearValue.color.uint32[0] =
                uint32_t(clear_value);
            resolve_clear_attachment.clearValue.color.uint32[1] =
                uint32_t(clear_value >> 32);
          } break;
        }
      }
      command_buffer.CmdVkClearAttachments(1, &resolve_clear_attachment, 1,
                                           &resolve_clear_rect);
    }
  }
  }
}

VkPipeline VulkanRenderTargetCache::GetDumpPipeline(DumpPipelineKey key) {
  auto pipeline_it = dump_pipelines_.find(key);
  if (pipeline_it != dump_pipelines_.end()) {
    return pipeline_it->second;
  }

  std::vector<spv::Id> id_vector_temp;

  SpirvBuilder builder(spv::Spv_1_0,
                       (SpirvShaderTranslator::kSpirvMagicToolId << 16) | 1,
                       nullptr);
  spv::Id ext_inst_glsl_std_450 = builder.import("GLSL.std.450");
  builder.addCapability(spv::CapabilityShader);
  builder.setMemoryModel(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
  builder.setSource(spv::SourceLanguageUnknown, 0);

  spv::Id type_void = builder.makeVoidType();
  spv::Id type_int = builder.makeIntType(32);
  spv::Id type_int2 = builder.makeVectorType(type_int, 2);
  spv::Id type_uint = builder.makeUintType(32);
  spv::Id type_uint2 = builder.makeVectorType(type_uint, 2);
  spv::Id type_uint3 = builder.makeVectorType(type_uint, 3);
  spv::Id type_float = builder.makeFloatType(32);

  // Bindings.
  // EDRAM buffer.
  bool format_is_64bpp = !key.is_depth && xenos::IsColorRenderTargetFormat64bpp(
                                              key.GetColorFormat());
  id_vector_temp.clear();
  id_vector_temp.push_back(
      builder.makeRuntimeArray(format_is_64bpp ? type_uint2 : type_uint));
  // Storage buffers have std430 packing, no padding to 4-component vectors.
  builder.addDecoration(id_vector_temp.back(), spv::DecorationArrayStride,
                        sizeof(uint32_t) << uint32_t(format_is_64bpp));
  spv::Id type_edram = builder.makeStructType(id_vector_temp, "XeEdram");
  builder.addMemberName(type_edram, 0, "edram");
  builder.addMemberDecoration(type_edram, 0, spv::DecorationNonReadable);
  builder.addMemberDecoration(type_edram, 0, spv::DecorationOffset, 0);
  // Block since SPIR-V 1.3, but since SPIR-V 1.0 is generated, it's
  // BufferBlock.
  builder.addDecoration(type_edram, spv::DecorationBufferBlock);
  // StorageBuffer since SPIR-V 1.3, but since SPIR-V 1.0 is generated, it's
  // Uniform.
  spv::Id edram_buffer = builder.createVariable(
      spv::NoPrecision, spv::StorageClassUniform, type_edram, "xe_edram");
  builder.addDecoration(edram_buffer, spv::DecorationDescriptorSet,
                        kDumpDescriptorSetEdram);
  builder.addDecoration(edram_buffer, spv::DecorationBinding, 0);
  // Color or depth source.
  bool source_is_multisampled = key.msaa_samples != xenos::MsaaSamples::k1X;
  bool source_is_uint;
  if (key.is_depth) {
    source_is_uint = false;
  } else {
    GetColorOwnershipTransferVulkanFormat(key.GetColorFormat(),
                                          &source_is_uint);
  }
  spv::Id source_component_type = source_is_uint ? type_uint : type_float;
  spv::Id source_texture = builder.createVariable(
      spv::NoPrecision, spv::StorageClassUniformConstant,
      builder.makeImageType(source_component_type, spv::Dim2D, false, false,
                            source_is_multisampled, 1, spv::ImageFormatUnknown),
      "xe_edram_dump_source");
  builder.addDecoration(source_texture, spv::DecorationDescriptorSet,
                        kDumpDescriptorSetSource);
  builder.addDecoration(source_texture, spv::DecorationBinding, 0);
  // Stencil source.
  spv::Id source_stencil_texture = spv::NoResult;
  if (key.is_depth) {
    source_stencil_texture = builder.createVariable(
        spv::NoPrecision, spv::StorageClassUniformConstant,
        builder.makeImageType(type_uint, spv::Dim2D, false, false,
                              source_is_multisampled, 1,
                              spv::ImageFormatUnknown),
        "xe_edram_dump_stencil");
    builder.addDecoration(source_stencil_texture, spv::DecorationDescriptorSet,
                          kDumpDescriptorSetSource);
    builder.addDecoration(source_stencil_texture, spv::DecorationBinding, 1);
  }
  // Push constants.
  id_vector_temp.clear();
  id_vector_temp.reserve(kDumpPushConstantCount);
  for (uint32_t i = 0; i < kDumpPushConstantCount; ++i) {
    id_vector_temp.push_back(type_uint);
  }
  spv::Id type_push_constants =
      builder.makeStructType(id_vector_temp, "XeEdramDumpPushConstants");
  builder.addMemberName(type_push_constants, kDumpPushConstantPitches,
                        "pitches");
  builder.addMemberDecoration(type_push_constants, kDumpPushConstantPitches,
                              spv::DecorationOffset,
                              int(sizeof(uint32_t) * kDumpPushConstantPitches));
  builder.addMemberName(type_push_constants, kDumpPushConstantOffsets,
                        "offsets");
  builder.addMemberDecoration(type_push_constants, kDumpPushConstantOffsets,
                              spv::DecorationOffset,
                              int(sizeof(uint32_t) * kDumpPushConstantOffsets));
  builder.addDecoration(type_push_constants, spv::DecorationBlock);
  spv::Id push_constants = builder.createVariable(
      spv::NoPrecision, spv::StorageClassPushConstant, type_push_constants,
      "xe_edram_dump_push_constants");

  // gl_GlobalInvocationID input.
  spv::Id input_global_invocation_id =
      builder.createVariable(spv::NoPrecision, spv::StorageClassInput,
                             type_uint3, "gl_GlobalInvocationID");
  builder.addDecoration(input_global_invocation_id, spv::DecorationBuiltIn,
                        spv::BuiltInGlobalInvocationId);

  // Begin the main function.
  std::vector<spv::Id> main_param_types;
  std::vector<std::vector<spv::Decoration>> main_precisions;
  spv::Block* main_entry;
  spv::Function* main_function =
      builder.makeFunctionEntry(spv::NoPrecision, type_void, "main",
                                main_param_types, main_precisions, &main_entry);

  // For now, as the exact addressing in 64bpp render targets relatively to
  // 32bpp is unknown, treating 64bpp tiles as storing 40x16 samples rather than
  // 80x16 for simplicity of addressing into the texture.

  // Split the destination sample index into the 32bpp tile and the
  // 32bpp-tile-relative sample index.
  // Note that division by non-power-of-two constants will include a 4-cycle
  // 32*32 multiplication on AMD, even though so many bits are not needed for
  // the sample position - however, if an OpUnreachable path is inserted for the
  // case when the position has upper bits set, for some reason, the code for it
  // is not eliminated when compiling the shader for AMD via RenderDoc on
  // Windows, as of June 2022.
  spv::Id global_invocation_id =
      builder.createLoad(input_global_invocation_id, spv::NoPrecision);
  spv::Id rectangle_sample_x =
      builder.createCompositeExtract(global_invocation_id, type_uint, 0);
  uint32_t tile_width =
      (xenos::kEdramTileWidthSamples >> uint32_t(format_is_64bpp)) *
      draw_resolution_scale_x();
  spv::Id const_tile_width = builder.makeUintConstant(tile_width);
  spv::Id rectangle_tile_index_x = builder.createBinOp(
      spv::OpUDiv, type_uint, rectangle_sample_x, const_tile_width);
  spv::Id tile_sample_x = builder.createBinOp(
      spv::OpUMod, type_uint, rectangle_sample_x, const_tile_width);
  spv::Id rectangle_sample_y =
      builder.createCompositeExtract(global_invocation_id, type_uint, 1);
  uint32_t tile_height =
      xenos::kEdramTileHeightSamples * draw_resolution_scale_y();
  spv::Id const_tile_height = builder.makeUintConstant(tile_height);
  spv::Id rectangle_tile_index_y = builder.createBinOp(
      spv::OpUDiv, type_uint, rectangle_sample_y, const_tile_height);
  spv::Id tile_sample_y = builder.createBinOp(
      spv::OpUMod, type_uint, rectangle_sample_y, const_tile_height);

  // Get the tile index in the EDRAM relative to the dump rectangle base tile.
  id_vector_temp.clear();
  id_vector_temp.push_back(builder.makeIntConstant(kDumpPushConstantPitches));
  spv::Id pitches_constant = builder.createLoad(
      builder.createAccessChain(spv::StorageClassPushConstant, push_constants,
                                id_vector_temp),
      spv::NoPrecision);
  spv::Id const_uint_0 = builder.makeUintConstant(0);
  spv::Id const_edram_pitch_tiles_bits =
      builder.makeUintConstant(xenos::kEdramPitchTilesBits);
  spv::Id rectangle_tile_index = builder.createBinOp(
      spv::OpIAdd, type_uint,
      builder.createBinOp(
          spv::OpIMul, type_uint,
          builder.createTriOp(spv::OpBitFieldUExtract, type_uint,
                              pitches_constant, const_uint_0,
                              const_edram_pitch_tiles_bits),
          rectangle_tile_index_y),
      rectangle_tile_index_x);
  // Add the base tile in the dispatch to the dispatch-local tile index, not
  // wrapping yet so in case of a wraparound, the address relative to the base
  // in the image after subtraction of the base won't be negative.
  id_vector_temp.clear();
  id_vector_temp.push_back(builder.makeIntConstant(kDumpPushConstantOffsets));
  spv::Id offsets_constant = builder.createLoad(
      builder.createAccessChain(spv::StorageClassPushConstant, push_constants,
                                id_vector_temp),
      spv::NoPrecision);
  spv::Id const_edram_base_tiles_bits_plus_1 =
      builder.makeUintConstant(xenos::kEdramBaseTilesBits + 1);
  spv::Id edram_tile_index_non_wrapped = builder.createBinOp(
      spv::OpIAdd, type_uint,
      builder.createTriOp(spv::OpBitFieldUExtract, type_uint, offsets_constant,
                          const_uint_0, const_edram_base_tiles_bits_plus_1),
      rectangle_tile_index);

  // Combine the tile sample index and the tile index, wrapping the tile
  // addressing, into the EDRAM sample index.
  spv::Id edram_sample_address = builder.createBinOp(
      spv::OpIAdd, type_uint,
      builder.createBinOp(
          spv::OpIMul, type_uint,
          builder.makeUintConstant(tile_width * tile_height),
          builder.createBinOp(
              spv::OpBitwiseAnd, type_uint, edram_tile_index_non_wrapped,
              builder.makeUintConstant(xenos::kEdramTileCount - 1))),
      builder.createBinOp(spv::OpIAdd, type_uint,
                          builder.createBinOp(spv::OpIMul, type_uint,
                                              const_tile_width, tile_sample_y),
                          tile_sample_x));
  if (key.is_depth) {
    // Swap 40-sample columns in the depth buffer in the destination address to
    // get the final address of the sample in the EDRAM.
    uint32_t tile_width_half = tile_width >> 1;
    edram_sample_address = builder.createUnaryOp(
        spv::OpBitcast, type_uint,
        builder.createBinOp(
            spv::OpIAdd, type_int,
            builder.createUnaryOp(spv::OpBitcast, type_int,
                                  edram_sample_address),
            builder.createTriOp(
                spv::OpSelect, type_int,
                builder.createBinOp(spv::OpULessThan, builder.makeBoolType(),
                                    tile_sample_x,
                                    builder.makeUintConstant(tile_width_half)),
                builder.makeIntConstant(int32_t(tile_width_half)),
                builder.makeIntConstant(-int32_t(tile_width_half)))));
  }

  // Get the linear tile index within the source texture.
  spv::Id source_tile_index = builder.createBinOp(
      spv::OpISub, type_uint, edram_tile_index_non_wrapped,
      builder.createTriOp(
          spv::OpBitFieldUExtract, type_uint, offsets_constant,
          const_edram_base_tiles_bits_plus_1,
          builder.makeUintConstant(xenos::kEdramBaseTilesBits)));
  // Split the linear tile index in the source texture into X and Y in tiles.
  spv::Id source_pitch_tiles = builder.createTriOp(
      spv::OpBitFieldUExtract, type_uint, pitches_constant,
      const_edram_pitch_tiles_bits, const_edram_pitch_tiles_bits);
  spv::Id source_tile_index_y = builder.createBinOp(
      spv::OpUDiv, type_uint, source_tile_index, source_pitch_tiles);
  spv::Id source_tile_index_x = builder.createBinOp(
      spv::OpUMod, type_uint, source_tile_index, source_pitch_tiles);
  // Combine the source tile offset and the sample index within the tile.
  spv::Id source_sample_x = builder.createBinOp(
      spv::OpIAdd, type_uint,
      builder.createBinOp(spv::OpIMul, type_uint, const_tile_width,
                          source_tile_index_x),
      tile_sample_x);
  spv::Id source_sample_y = builder.createBinOp(
      spv::OpIAdd, type_uint,
      builder.createBinOp(spv::OpIMul, type_uint, const_tile_height,
                          source_tile_index_y),
      tile_sample_y);
  // Get the source pixel coordinate and the sample index within the pixel.
  spv::Id source_pixel_x = source_sample_x, source_pixel_y = source_sample_y;
  spv::Id source_sample_id = spv::NoResult;
  if (source_is_multisampled) {
    spv::Id const_uint_1 = builder.makeUintConstant(1);
    source_pixel_y = builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                         source_sample_y, const_uint_1);
    if (key.msaa_samples >= xenos::MsaaSamples::k4X) {
      source_pixel_x = builder.createBinOp(spv::OpShiftRightLogical, type_uint,
                                           source_sample_x, const_uint_1);
      // 4x MSAA source texture sample index - bit 0 for horizontal, bit 1 for
      // vertical.
      source_sample_id = builder.createQuadOp(
          spv::OpBitFieldInsert, type_uint,
          builder.createBinOp(spv::OpBitwiseAnd, type_uint, source_sample_x,
                              const_uint_1),
          source_sample_y, const_uint_1, const_uint_1);
    } else {
      // 2x MSAA source texture sample index - convert from the guest to
      // the Vulkan standard sample locations.
      source_sample_id = builder.createTriOp(
          spv::OpSelect, type_uint,
          builder.createBinOp(
              spv::OpINotEqual, builder.makeBoolType(),
              builder.createBinOp(spv::OpBitwiseAnd, type_uint, source_sample_y,
                                  const_uint_1),
              const_uint_0),
          builder.makeUintConstant(draw_util::GetD3D10SampleIndexForGuest2xMSAA(
              1, msaa_2x_attachments_supported_)),
          builder.makeUintConstant(draw_util::GetD3D10SampleIndexForGuest2xMSAA(
              0, msaa_2x_attachments_supported_)));
    }
  }

  // Load the source, and pack the value into one or two 32-bit integers.
  spv::Id packed[2] = {};
  spv::Builder::TextureParameters source_texture_parameters = {};
  source_texture_parameters.sampler =
      builder.createLoad(source_texture, spv::NoPrecision);
  id_vector_temp.clear();
  id_vector_temp.push_back(
      builder.createUnaryOp(spv::OpBitcast, type_int, source_pixel_x));
  id_vector_temp.push_back(
      builder.createUnaryOp(spv::OpBitcast, type_int, source_pixel_y));
  source_texture_parameters.coords =
      builder.createCompositeConstruct(type_int2, id_vector_temp);
  if (source_is_multisampled) {
    source_texture_parameters.sample =
        builder.createUnaryOp(spv::OpBitcast, type_int, source_sample_id);
  } else {
    source_texture_parameters.lod = builder.makeIntConstant(0);
  }
  spv::Id source_vec4 = builder.createTextureCall(
      spv::NoPrecision, builder.makeVectorType(source_component_type, 4), false,
      true, false, false, false, source_texture_parameters,
      spv::ImageOperandsMaskNone);
  if (key.is_depth) {
    source_texture_parameters.sampler =
        builder.createLoad(source_stencil_texture, spv::NoPrecision);
    spv::Id source_stencil = builder.createCompositeExtract(
        builder.createTextureCall(
            spv::NoPrecision, builder.makeVectorType(type_uint, 4), false, true,
            false, false, false, source_texture_parameters,
            spv::ImageOperandsMaskNone),
        type_uint, 0);
    spv::Id source_depth32 =
        builder.createCompositeExtract(source_vec4, type_float, 0);
    switch (key.GetDepthFormat()) {
      case xenos::DepthRenderTargetFormat::kD24S8: {
        // Round to the nearest even integer. This seems to be the correct
        // conversion, adding +0.5 and rounding towards zero results in red
        // instead of black in the 4D5307E6 clear shader.
        packed[0] = builder.createUnaryOp(
            spv::OpConvertFToU, type_uint,
            builder.createUnaryBuiltinCall(
                type_float, ext_inst_glsl_std_450, GLSLstd450RoundEven,
                builder.createBinOp(
                    spv::OpFMul, type_float, source_depth32,
                    builder.makeFloatConstant(float(0xFFFFFF)))));
      } break;
      case xenos::DepthRenderTargetFormat::kD24FS8: {
        packed[0] = SpirvShaderTranslator::PreClampedDepthTo20e4(
            builder, source_depth32, depth_float24_round(), true,
            ext_inst_glsl_std_450);
      } break;
    }
    packed[0] = builder.createQuadOp(
        spv::OpBitFieldInsert, type_uint, source_stencil, packed[0],
        builder.makeUintConstant(8), builder.makeUintConstant(24));
  } else {
    switch (key.GetColorFormat()) {
      case xenos::ColorRenderTargetFormat::k_8_8_8_8:
      case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA: {
        spv::Id unorm_round_offset = builder.makeFloatConstant(0.5f);
        spv::Id unorm_scale = builder.makeFloatConstant(255.0f);
        packed[0] = builder.createUnaryOp(
            spv::OpConvertFToU, type_uint,
            builder.createBinOp(
                spv::OpFAdd, type_float,
                builder.createBinOp(
                    spv::OpFMul, type_float,
                    builder.createCompositeExtract(source_vec4, type_float, 0),
                    unorm_scale),
                unorm_round_offset));
        spv::Id component_width = builder.makeUintConstant(8);
        for (uint32_t i = 1; i < 4; ++i) {
          packed[0] = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint, packed[0],
              builder.createUnaryOp(
                  spv::OpConvertFToU, type_uint,
                  builder.createBinOp(
                      spv::OpFAdd, type_float,
                      builder.createBinOp(spv::OpFMul, type_float,
                                          builder.createCompositeExtract(
                                              source_vec4, type_float, i),
                                          unorm_scale),
                      unorm_round_offset)),
              builder.makeUintConstant(8 * i), component_width);
        }
      } break;
      case xenos::ColorRenderTargetFormat::k_2_10_10_10:
      case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10: {
        spv::Id unorm_round_offset = builder.makeFloatConstant(0.5f);
        spv::Id unorm_scale_rgb = builder.makeFloatConstant(1023.0f);
        packed[0] = builder.createUnaryOp(
            spv::OpConvertFToU, type_uint,
            builder.createBinOp(
                spv::OpFAdd, type_float,
                builder.createBinOp(
                    spv::OpFMul, type_float,
                    builder.createCompositeExtract(source_vec4, type_float, 0),
                    unorm_scale_rgb),
                unorm_round_offset));
        spv::Id width_rgb = builder.makeUintConstant(10);
        spv::Id unorm_scale_a = builder.makeFloatConstant(3.0f);
        spv::Id width_a = builder.makeUintConstant(2);
        for (uint32_t i = 1; i < 4; ++i) {
          packed[0] = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint, packed[0],
              builder.createUnaryOp(
                  spv::OpConvertFToU, type_uint,
                  builder.createBinOp(
                      spv::OpFAdd, type_float,
                      builder.createBinOp(
                          spv::OpFMul, type_float,
                          builder.createCompositeExtract(source_vec4,
                                                         type_float, i),
                          i == 3 ? unorm_scale_a : unorm_scale_rgb),
                      unorm_round_offset)),
              builder.makeUintConstant(10 * i), i == 3 ? width_a : width_rgb);
        }
      } break;
      case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
      case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16: {
        // Float16 has a wider range for both color and alpha, also NaNs - clamp
        // and convert.
        packed[0] = SpirvShaderTranslator::UnclampedFloat32To7e3(
            builder, builder.createCompositeExtract(source_vec4, type_float, 0),
            ext_inst_glsl_std_450);
        spv::Id width_rgb = builder.makeUintConstant(10);
        for (uint32_t i = 1; i < 3; ++i) {
          packed[0] = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint, packed[0],
              SpirvShaderTranslator::UnclampedFloat32To7e3(
                  builder,
                  builder.createCompositeExtract(source_vec4, type_float, i),
                  ext_inst_glsl_std_450),
              builder.makeUintConstant(10 * i), width_rgb);
        }
        // Saturate and convert the alpha.
        spv::Id alpha_saturated = builder.createTriBuiltinCall(
            type_float, ext_inst_glsl_std_450, GLSLstd450NClamp,
            builder.createCompositeExtract(source_vec4, type_float, 3),
            builder.makeFloatConstant(0.0f), builder.makeFloatConstant(1.0f));
        packed[0] = builder.createQuadOp(
            spv::OpBitFieldInsert, type_uint, packed[0],
            builder.createUnaryOp(
                spv::OpConvertFToU, type_uint,
                builder.createBinOp(
                    spv::OpFAdd, type_float,
                    builder.createBinOp(spv::OpFMul, type_float,
                                        alpha_saturated,
                                        builder.makeFloatConstant(3.0f)),
                    builder.makeFloatConstant(0.5f))),
            builder.makeUintConstant(30), builder.makeUintConstant(2));
      } break;
      case xenos::ColorRenderTargetFormat::k_16_16:
      case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT: {
        // All 64bpp formats, and all 16 bits per component formats, are
        // represented as integers in ownership transfer for safe handling of
        // NaN encodings and -32768 / -32767.
        // TODO(Triang3l): Handle the case when that's not true (no multisampled
        // sampled images, no 16-bit UNORM, no cross-packing 32bpp aliasing on a
        // portability subset device or a 64bpp format where that wouldn't help
        // anyway).
        spv::Id component_offset_width = builder.makeUintConstant(16);
        for (uint32_t i = 0; i <= uint32_t(format_is_64bpp); ++i) {
          packed[i] = builder.createQuadOp(
              spv::OpBitFieldInsert, type_uint,
              builder.createCompositeExtract(source_vec4, type_uint, 2 * i),
              builder.createCompositeExtract(source_vec4, type_uint, 2 * i + 1),
              component_offset_width, component_offset_width);
        }
      } break;
      // Float32 is transferred as uint32 to preserve NaN encodings. However,
      // multisampled sampled image support is optional in Vulkan.
      case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      case xenos::ColorRenderTargetFormat::k_32_32_FLOAT: {
        for (uint32_t i = 0; i <= uint32_t(format_is_64bpp); ++i) {
          spv::Id& packed_ref = packed[i];
          packed_ref = builder.createCompositeExtract(source_vec4,
                                                      source_component_type, i);
          if (!source_is_uint) {
            packed_ref =
                builder.createUnaryOp(spv::OpBitcast, type_uint, packed_ref);
          }
        }
      } break;
    }
  }

  // Write the packed value to the EDRAM buffer.
  spv::Id store_value = packed[0];
  if (format_is_64bpp) {
    id_vector_temp.clear();
    id_vector_temp.push_back(packed[0]);
    id_vector_temp.push_back(packed[1]);
    store_value = builder.createCompositeConstruct(type_uint2, id_vector_temp);
  }
  id_vector_temp.clear();
  // The only SSBO structure member.
  id_vector_temp.push_back(builder.makeIntConstant(0));
  id_vector_temp.push_back(
      builder.createUnaryOp(spv::OpBitcast, type_int, edram_sample_address));
  // StorageBuffer since SPIR-V 1.3, but since SPIR-V 1.0 is generated, it's
  // Uniform.
  builder.createStore(store_value,
                      builder.createAccessChain(spv::StorageClassUniform,
                                                edram_buffer, id_vector_temp));

  // End the main function and make it the entry point.
  builder.leaveFunction();
  builder.addExecutionMode(main_function, spv::ExecutionModeLocalSize,
                           kDumpSamplesPerGroupX, kDumpSamplesPerGroupY, 1);
  spv::Instruction* entry_point = builder.addEntryPoint(
      spv::ExecutionModelGLCompute, main_function, "main");
  // Bindings only need to be added to the entry point's interface starting with
  // SPIR-V 1.4 - emitting 1.0 here, so only inputs / outputs.
  entry_point->addIdOperand(input_global_invocation_id);

  // Serialize the shader code.
  std::vector<unsigned int> shader_code;
  builder.dump(shader_code);

  // Create the pipeline, and store the handle even if creation fails not to try
  // to create it again later.
  VkPipeline pipeline = ui::vulkan::util::CreateComputePipeline(
      command_processor_.GetVulkanDevice(),
      key.is_depth ? dump_pipeline_layout_depth_ : dump_pipeline_layout_color_,
      reinterpret_cast<const uint32_t*>(shader_code.data()),
      sizeof(uint32_t) * shader_code.size());
  if (pipeline == VK_NULL_HANDLE) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create a render target dumping "
        "pipeline for {}-sample render targets with format {}",
        UINT32_C(1) << uint32_t(key.msaa_samples),
        key.is_depth
            ? xenos::GetDepthRenderTargetFormatName(key.GetDepthFormat())
            : xenos::GetColorRenderTargetFormatName(key.GetColorFormat()));
  }
  dump_pipelines_.emplace(key, pipeline);
  return pipeline;
}

void VulkanRenderTargetCache::DumpRenderTargets(uint32_t dump_base,
                                                uint32_t dump_row_length_used,
                                                uint32_t dump_rows,
                                                uint32_t dump_pitch) {
  assert_true(GetPath() == Path::kHostRenderTargets);

  GetResolveCopyRectanglesToDump(dump_base, dump_row_length_used, dump_rows,
                                 dump_pitch, dump_rectangles_);
  if (dump_rectangles_.empty()) {
    return;
  }
  if (ShouldTraceVulkanDumpState()) {
    XELOGI(
        "GPU resolve trace: dump begin base={} row_length_used={} rows={} "
        "pitch={} rectangles={}",
        dump_base, dump_row_length_used, dump_rows, dump_pitch,
        dump_rectangles_.size());
  }

  // Diagnostic (default off): copy the first 1xMSAA color RT image straight to a
  // host buffer (independent of the dump compute shader) and checksum it. Tells
  // whether the RT image actually holds the rendered data (so a render/store bug
  // can be told apart from a dump-shader-read bug, e.g. on Turnip). One RT per
  // dump; leaves that RT in TRANSFER_SRC so the barrier loop below transitions it
  // to SHADER_READ normally. Mirrors ReadbackEdramBufferRange.
  if (cvars::vulkan_trace_dump_rt_image) {
    // Find the WIDEST base-0 1xMSAA color RT = the main (e.g. 1280-wide) present
    // target; several aliased base-0 color RTs exist at smaller pitches and the
    // scene's color lands in the widest one.
    uint32_t rt_image_widest = 0;
    for (const ResolveCopyDumpRectangle& rectangle : dump_rectangles_) {
      auto* cand = static_cast<VulkanRenderTarget*>(rectangle.render_target);
      RenderTargetKey ck = cand->key();
      if (ck.is_depth || ck.msaa_samples != xenos::MsaaSamples::k1X ||
          ck.base_tiles != 0) {
        continue;
      }
      uint32_t cw = ck.GetWidth() * draw_resolution_scale_x();
      if (cw > rt_image_widest) {
        rt_image_widest = cw;
      }
    }
    for (const ResolveCopyDumpRectangle& rectangle : dump_rectangles_) {
      auto& vulkan_rt =
          *static_cast<VulkanRenderTarget*>(rectangle.render_target);
      RenderTargetKey rt_image_key = vulkan_rt.key();
      // Only the widest base-0 1xMSAA color RT (the present target).
      if (rt_image_key.is_depth ||
          rt_image_key.msaa_samples != xenos::MsaaSamples::k1X ||
          rt_image_key.base_tiles != 0 ||
          rt_image_key.GetWidth() * draw_resolution_scale_x() !=
              rt_image_widest) {
        continue;
      }
      const ui::vulkan::VulkanDevice* const vulkan_device =
          command_processor_.GetVulkanDevice();
      const ui::vulkan::VulkanDevice::Functions& dfn =
          vulkan_device->functions();
      const VkDevice device = vulkan_device->device();
      uint32_t rt_width = rt_image_key.GetWidth() * draw_resolution_scale_x();
      uint32_t rt_height =
          GetRenderTargetHeight(rt_image_key.pitch_tiles_at_32bpp,
                                rt_image_key.msaa_samples) *
          draw_resolution_scale_y();
      if (!rt_width || !rt_height) {
        break;
      }
      uint64_t buffer_size = uint64_t(rt_width) * uint64_t(rt_height) * 8;
      VkBuffer rt_readback_buffer = VK_NULL_HANDLE;
      VkDeviceMemory rt_readback_memory = VK_NULL_HANDLE;
      uint32_t rt_readback_memory_type = UINT32_MAX;
      VkDeviceSize rt_readback_memory_size = 0;
      if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
              vulkan_device, VkDeviceSize(buffer_size),
              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
              ui::vulkan::util::MemoryPurpose::kReadback, rt_readback_buffer,
              rt_readback_memory, &rt_readback_memory_type,
              &rt_readback_memory_size)) {
        XELOGE("GPU resolve trace: dump RT IMAGE readback buffer alloc failed");
        break;
      }
      command_processor_.PushImageMemoryBarrier(
          vulkan_rt.image(),
          ui::vulkan::util::InitializeSubresourceRange(
              VK_IMAGE_ASPECT_COLOR_BIT),
          vulkan_rt.current_stage_mask(), VK_PIPELINE_STAGE_TRANSFER_BIT,
          vulkan_rt.current_access_mask(), VK_ACCESS_TRANSFER_READ_BIT,
          vulkan_rt.current_layout(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      vulkan_rt.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      command_processor_.SubmitBarriers(true);
      VkBufferImageCopy rt_copy_region = {};
      rt_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      rt_copy_region.imageSubresource.layerCount = 1;
      rt_copy_region.imageExtent = {rt_width, rt_height, 1};
      command_processor_.deferred_command_buffer().CmdVkCopyImageToBuffer(
          vulkan_rt.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          rt_readback_buffer, 1, &rt_copy_region);
      command_processor_.PushBufferMemoryBarrier(
          rt_readback_buffer, 0, VK_WHOLE_SIZE, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_HOST_READ_BIT);
      if (command_processor_.AwaitAllQueueOperationsCompletion()) {
        void* mapping = nullptr;
        if (dfn.vkMapMemory(device, rt_readback_memory, 0, VK_WHOLE_SIZE, 0,
                            &mapping) == VK_SUCCESS) {
          if (!(vulkan_device->memory_types().host_coherent &
                (uint32_t(1) << rt_readback_memory_type))) {
            VkMappedMemoryRange mapped_range;
            mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mapped_range.pNext = nullptr;
            mapped_range.memory = rt_readback_memory;
            mapped_range.offset = 0;
            mapped_range.size = rt_readback_memory_size;
            dfn.vkInvalidateMappedMemoryRanges(device, 1, &mapped_range);
          }
          const uint8_t* bytes = reinterpret_cast<const uint8_t*>(mapping);
          // Only the first width*height*4 bytes are guaranteed written (4bpp RTs
          // write exactly that; 8bpp RTs write more) - sample within that range.
          uint64_t checksum_len = uint64_t(rt_width) * uint64_t(rt_height) * 4;
          constexpr uint64_t kSampleStride = 2048;
          uint32_t rt_samples = 0, rt_nonzero = 0, rt_varying = 0;
          uint32_t rt_first_nonzero = 0, rt_previous_word = 0;
          bool rt_have_previous = false;
          uint32_t rt_first_words[8] = {};
          for (uint32_t i = 0; i < 8 && uint64_t(i) * 4 + 4 <= checksum_len;
               ++i) {
            std::memcpy(&rt_first_words[i], bytes + i * 4, 4);
          }
          for (uint64_t offset = 0; offset + 4 <= checksum_len;
               offset += kSampleStride) {
            uint32_t word = 0;
            std::memcpy(&word, bytes + offset, 4);
            ++rt_samples;
            if (word) {
              ++rt_nonzero;
              if (!rt_first_nonzero) {
                rt_first_nonzero = word;
              }
            }
            if (rt_have_previous && word != rt_previous_word) {
              ++rt_varying;
            }
            rt_previous_word = word;
            rt_have_previous = true;
          }
          XELOGI(
              "GPU resolve trace: dump RT IMAGE checksum rt_key={:08X} "
              "base_tiles={} size={}x{} color_format={} samples={} nonzero={} "
              "varying={} first_nonzero_value={:08X} "
              "first={:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
              rt_image_key.key, rt_image_key.base_tiles, rt_width, rt_height,
              uint32_t(rt_image_key.GetColorFormat()), rt_samples, rt_nonzero,
              rt_varying, rt_first_nonzero, rt_first_words[0], rt_first_words[1],
              rt_first_words[2], rt_first_words[3], rt_first_words[4],
              rt_first_words[5], rt_first_words[6], rt_first_words[7]);
          dfn.vkUnmapMemory(device, rt_readback_memory);
        } else {
          XELOGE("GPU resolve trace: dump RT IMAGE map failed");
        }
      } else {
        XELOGE("GPU resolve trace: dump RT IMAGE await failed");
      }
      dfn.vkDestroyBuffer(device, rt_readback_buffer, nullptr);
      dfn.vkFreeMemory(device, rt_readback_memory, nullptr);
      if (!command_processor_.submission_open()) {
        command_processor_.BeginSubmission(false);
      }
      break;
    }
  }

  // Diagnostic (default off): same fixed-function readback for the DEPTH RT
  // image (depth aspect, 4 bytes/texel). Settles the failure mode: if depth has
  // many varying geometry-Z values, fragments rasterized (color-write/resolve
  // bug); if it is just the clear value, no geometry rasterized (vertex/raster).
  if (cvars::vulkan_trace_dump_depth_image) {
    for (const ResolveCopyDumpRectangle& rectangle : dump_rectangles_) {
      auto& vulkan_rt =
          *static_cast<VulkanRenderTarget*>(rectangle.render_target);
      RenderTargetKey d_key = vulkan_rt.key();
      if (!d_key.is_depth ||
          d_key.msaa_samples != xenos::MsaaSamples::k1X ||
          d_key.base_tiles != 0) {
        continue;
      }
      const ui::vulkan::VulkanDevice* const vulkan_device =
          command_processor_.GetVulkanDevice();
      const ui::vulkan::VulkanDevice::Functions& dfn =
          vulkan_device->functions();
      const VkDevice device = vulkan_device->device();
      uint32_t d_width = d_key.GetWidth() * draw_resolution_scale_x();
      uint32_t d_height =
          GetRenderTargetHeight(d_key.pitch_tiles_at_32bpp, d_key.msaa_samples) *
          draw_resolution_scale_y();
      if (!d_width || !d_height) {
        break;
      }
      uint64_t d_buffer_size = uint64_t(d_width) * uint64_t(d_height) * 4;
      VkBuffer d_buf = VK_NULL_HANDLE;
      VkDeviceMemory d_mem = VK_NULL_HANDLE;
      uint32_t d_mem_type = UINT32_MAX;
      VkDeviceSize d_mem_size = 0;
      if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
              vulkan_device, VkDeviceSize(d_buffer_size),
              VK_BUFFER_USAGE_TRANSFER_DST_BIT,
              ui::vulkan::util::MemoryPurpose::kReadback, d_buf, d_mem,
              &d_mem_type, &d_mem_size)) {
        XELOGE(
            "GPU resolve trace: dump DEPTH IMAGE readback buffer alloc failed");
        break;
      }
      command_processor_.PushImageMemoryBarrier(
          vulkan_rt.image(),
          ui::vulkan::util::InitializeSubresourceRange(
              VK_IMAGE_ASPECT_DEPTH_BIT),
          vulkan_rt.current_stage_mask(), VK_PIPELINE_STAGE_TRANSFER_BIT,
          vulkan_rt.current_access_mask(), VK_ACCESS_TRANSFER_READ_BIT,
          vulkan_rt.current_layout(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      vulkan_rt.SetUsage(VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      command_processor_.SubmitBarriers(true);
      VkBufferImageCopy d_copy = {};
      d_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      d_copy.imageSubresource.layerCount = 1;
      d_copy.imageExtent = {d_width, d_height, 1};
      command_processor_.deferred_command_buffer().CmdVkCopyImageToBuffer(
          vulkan_rt.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d_buf, 1,
          &d_copy);
      command_processor_.PushBufferMemoryBarrier(
          d_buf, 0, VK_WHOLE_SIZE, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_HOST_READ_BIT);
      if (command_processor_.AwaitAllQueueOperationsCompletion()) {
        void* mapping = nullptr;
        if (dfn.vkMapMemory(device, d_mem, 0, VK_WHOLE_SIZE, 0, &mapping) ==
            VK_SUCCESS) {
          if (!(vulkan_device->memory_types().host_coherent &
                (uint32_t(1) << d_mem_type))) {
            VkMappedMemoryRange mapped_range;
            mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mapped_range.pNext = nullptr;
            mapped_range.memory = d_mem;
            mapped_range.offset = 0;
            mapped_range.size = d_mem_size;
            dfn.vkInvalidateMappedMemoryRanges(device, 1, &mapped_range);
          }
          const uint8_t* bytes = reinterpret_cast<const uint8_t*>(mapping);
          uint64_t checksum_len = uint64_t(d_width) * uint64_t(d_height) * 4;
          constexpr uint64_t kSampleStride = 2048;
          uint32_t d_samples = 0, d_nonzero = 0, d_varying = 0;
          uint32_t d_first_nonzero = 0, d_previous_word = 0;
          bool d_have_previous = false;
          for (uint64_t offset = 0; offset + 4 <= checksum_len;
               offset += kSampleStride) {
            uint32_t word = 0;
            std::memcpy(&word, bytes + offset, 4);
            ++d_samples;
            if (word) {
              ++d_nonzero;
              if (!d_first_nonzero) {
                d_first_nonzero = word;
              }
            }
            if (d_have_previous && word != d_previous_word) {
              ++d_varying;
            }
            d_previous_word = word;
            d_have_previous = true;
          }
          XELOGI(
              "GPU resolve trace: dump DEPTH IMAGE checksum rt_key={:08X} "
              "base_tiles={} size={}x{} samples={} nonzero={} varying={} "
              "first_nonzero={:08X}",
              d_key.key, d_key.base_tiles, d_width, d_height, d_samples,
              d_nonzero, d_varying, d_first_nonzero);
          dfn.vkUnmapMemory(device, d_mem);
        } else {
          XELOGE("GPU resolve trace: dump DEPTH IMAGE map failed");
        }
      } else {
        XELOGE("GPU resolve trace: dump DEPTH IMAGE await failed");
      }
      dfn.vkDestroyBuffer(device, d_buf, nullptr);
      dfn.vkFreeMemory(device, d_mem, nullptr);
      if (!command_processor_.submission_open()) {
        command_processor_.BeginSubmission(false);
      }
      break;
    }
  }

  // Clear previously set temporary indices.
  for (const ResolveCopyDumpRectangle& rectangle : dump_rectangles_) {
    static_cast<VulkanRenderTarget*>(rectangle.render_target)
        ->SetTemporarySortIndex(UINT32_MAX);
  }
  // Gather all needed barriers and info needed to sort the invocations.
  UseEdramBuffer(EdramBufferUsage::kComputeWrite);
  dump_invocations_.clear();
  dump_invocations_.reserve(dump_rectangles_.size());
  constexpr VkPipelineStageFlags kRenderTargetDstStageMask =
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  constexpr VkAccessFlags kRenderTargetDstAccessMask =
      VK_ACCESS_SHADER_READ_BIT;
  constexpr VkImageLayout kRenderTargetNewLayout =
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  uint32_t rt_sort_index = 0;
  for (const ResolveCopyDumpRectangle& rectangle : dump_rectangles_) {
    auto& vulkan_rt =
        *static_cast<VulkanRenderTarget*>(rectangle.render_target);
    RenderTargetKey rt_key = vulkan_rt.key();
    VkPipelineStageFlags dump_src_stage = vulkan_rt.current_stage_mask();
    VkAccessFlags dump_src_access = vulkan_rt.current_access_mask();
    if (cvars::vulkan_dump_rt_full_barrier) {
      // Turnip (Mesa) can read a just-rendered render target as zero in the dump if
      // the attachment writes aren't flushed/flattened out of GMEM/CCU before the
      // compute texelFetch. Widen the source scope to a strict superset that forces
      // the flush. A superset never regresses the lax proprietary driver, and it adds
      // no dispatches, so the speed is unchanged.
      dump_src_stage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_TRANSFER_BIT;
      dump_src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    command_processor_.PushImageMemoryBarrier(
        vulkan_rt.image(),
        ui::vulkan::util::InitializeSubresourceRange(
            rt_key.is_depth
                ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                : VK_IMAGE_ASPECT_COLOR_BIT),
        dump_src_stage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        dump_src_access, VK_ACCESS_SHADER_READ_BIT,
        vulkan_rt.current_layout(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vulkan_rt.SetUsage(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (vulkan_rt.temporary_sort_index() == UINT32_MAX) {
      vulkan_rt.SetTemporarySortIndex(rt_sort_index++);
    }
    DumpPipelineKey pipeline_key;
    pipeline_key.msaa_samples = rt_key.msaa_samples;
    pipeline_key.resource_format = rt_key.resource_format;
    pipeline_key.is_depth = rt_key.is_depth;
    dump_invocations_.emplace_back(rectangle, pipeline_key);
  }

  // Sort the invocations to reduce context and binding switches.
  std::sort(dump_invocations_.begin(), dump_invocations_.end());

  // Dump the render targets.
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();
  bool edram_buffer_bound = false;
  VkDescriptorSet last_source_descriptor_set = VK_NULL_HANDLE;
  DumpPitches last_pitches;
  DumpOffsets last_offsets;
  bool pitches_bound = false, offsets_bound = false;
  for (const DumpInvocation& invocation : dump_invocations_) {
    const ResolveCopyDumpRectangle& rectangle = invocation.rectangle;
    auto& vulkan_rt =
        *static_cast<VulkanRenderTarget*>(rectangle.render_target);
    RenderTargetKey rt_key = vulkan_rt.key();
    DumpPipelineKey pipeline_key = invocation.pipeline_key;
    VkPipeline pipeline = GetDumpPipeline(pipeline_key);
    if (!pipeline) {
      continue;
    }
    if (ShouldTraceVulkanDumpState()) {
      XELOGI(
          "GPU resolve trace: dump rt key={:08X} base={} pitch={} width={} "
          "msaa={} is_depth={} fmt={} resource_format={} row_first={} "
          "rows={} row_first_start={} row_last_end={} pipeline_key={:08X} "
          "separate_transfer_view={}",
          rt_key.key, rt_key.base_tiles, rt_key.GetPitchTiles(),
          rt_key.GetWidth(), UINT32_C(1) << uint32_t(rt_key.msaa_samples),
          rt_key.is_depth != 0, rt_key.GetFormatName(),
          rt_key.resource_format, rectangle.row_first, rectangle.rows,
          rectangle.row_first_start, rectangle.row_last_end, pipeline_key.key,
          vulkan_rt.view_color_transfer_separate() != VK_NULL_HANDLE);
    }
    command_processor_.BindExternalComputePipeline(pipeline);

    VkPipelineLayout pipeline_layout = rt_key.is_depth
                                           ? dump_pipeline_layout_depth_
                                           : dump_pipeline_layout_color_;

    // Only need to bind the EDRAM buffer once (relying on pipeline layout
    // compatibility).
    if (!edram_buffer_bound) {
      edram_buffer_bound = true;
      command_buffer.CmdVkBindDescriptorSets(
          VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
          kDumpDescriptorSetEdram, 1, &edram_storage_buffer_descriptor_set_, 0,
          nullptr);
    }

    VkDescriptorSet source_descriptor_set =
        vulkan_rt.GetDescriptorSetTransferSource();
    if (last_source_descriptor_set != source_descriptor_set) {
      last_source_descriptor_set = source_descriptor_set;
      command_buffer.CmdVkBindDescriptorSets(
          VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
          kDumpDescriptorSetSource, 1, &source_descriptor_set, 0, nullptr);
    }

    DumpPitches pitches;
    pitches.dest_pitch = dump_pitch;
    pitches.source_pitch = rt_key.GetPitchTiles();
    if (last_pitches != pitches) {
      last_pitches = pitches;
      pitches_bound = false;
    }
    if (!pitches_bound) {
      pitches_bound = true;
      command_buffer.CmdVkPushConstants(
          pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
          sizeof(uint32_t) * kDumpPushConstantPitches, sizeof(last_pitches),
          &last_pitches);
    }

    DumpOffsets offsets;
    offsets.source_base_tiles = rt_key.base_tiles;
    ResolveCopyDumpRectangle::Dispatch
        dispatches[ResolveCopyDumpRectangle::kMaxDispatches];
    uint32_t dispatch_count =
        rectangle.GetDispatches(dump_pitch, dump_row_length_used, dispatches);
    for (uint32_t i = 0; i < dispatch_count; ++i) {
      const ResolveCopyDumpRectangle::Dispatch& dispatch = dispatches[i];
      offsets.dispatch_first_tile = dump_base + dispatch.offset;
      if (last_offsets != offsets) {
        last_offsets = offsets;
        offsets_bound = false;
      }
      if (!offsets_bound) {
        offsets_bound = true;
        command_buffer.CmdVkPushConstants(
            pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            sizeof(uint32_t) * kDumpPushConstantOffsets, sizeof(last_offsets),
            &last_offsets);
      }
      command_processor_.SubmitBarriers(true);
      uint32_t group_count_x =
          (draw_resolution_scale_x() *
               (xenos::kEdramTileWidthSamples >> uint32_t(rt_key.Is64bpp())) *
               dispatch.width_tiles +
           (kDumpSamplesPerGroupX - 1)) /
          kDumpSamplesPerGroupX;
      uint32_t group_count_y =
          (draw_resolution_scale_y() * xenos::kEdramTileHeightSamples *
               dispatch.height_tiles +
           (kDumpSamplesPerGroupY - 1)) /
          kDumpSamplesPerGroupY;
      if (ShouldTraceVulkanDumpState()) {
        XELOGI(
            "GPU resolve trace: dump dispatch rt={:08X} dest_first_tile={} "
            "source_base_tiles={} offset={} width_tiles={} height_tiles={} "
            "dest_pitch={} source_pitch={} groups={}x{}",
            rt_key.key, offsets.dispatch_first_tile, offsets.source_base_tiles,
            dispatch.offset, dispatch.width_tiles, dispatch.height_tiles,
            pitches.dest_pitch, pitches.source_pitch, group_count_x,
            group_count_y);
      }
      command_buffer.CmdVkDispatch(group_count_x, group_count_y, 1);
    }
    MarkEdramBufferModified();
  }
}

bool VulkanRenderTargetCache::ReadbackEdramBufferRange(
    uint32_t base_tiles, uint32_t row_length_used, uint32_t rows,
    uint32_t pitch_tiles, const draw_util::ResolveInfo& resolve_info) {
  if (!rows || !row_length_used) {
    return true;
  }

  const uint32_t tile_byte_size =
      xenos::kEdramTileWidthSamples * xenos::kEdramTileHeightSamples *
      uint32_t(sizeof(uint32_t)) * draw_resolution_scale_x() *
      draw_resolution_scale_y();
  const uint64_t edram_byte_size =
      uint64_t(xenos::kEdramSizeBytes) * draw_resolution_scale_x() *
      draw_resolution_scale_y();
  const uint64_t base_tile = base_tiles % xenos::kEdramTileCount;
  uint64_t span_tiles =
      uint64_t(rows - 1) * pitch_tiles + uint64_t(row_length_used);
  if (!span_tiles) {
    return true;
  }
  uint64_t byte_offset = base_tile * tile_byte_size;
  if (byte_offset >= edram_byte_size) {
    return true;
  }
  uint64_t byte_length = span_tiles * tile_byte_size;
  byte_length = std::min(byte_length, edram_byte_size - byte_offset);
  if (!byte_length) {
    return true;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  VkBuffer readback_buffer = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory = VK_NULL_HANDLE;
  uint32_t readback_memory_type = UINT32_MAX;
  VkDeviceSize readback_memory_size = 0;
  if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
          vulkan_device, VkDeviceSize(byte_length),
          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          ui::vulkan::util::MemoryPurpose::kReadback, readback_buffer,
          readback_memory, &readback_memory_type, &readback_memory_size)) {
    XELOGE(
        "GPU resolve trace: failed to create EDRAM readback buffer "
        "base_tiles={} rows={} row_length={} pitch={}",
        base_tiles, rows, row_length_used, pitch_tiles);
    return true;
  }

  UseEdramBuffer(EdramBufferUsage::kTransferRead);
  command_processor_.SubmitBarriers(true);
  VkBufferCopy readback_region;
  readback_region.srcOffset = VkDeviceSize(byte_offset);
  readback_region.dstOffset = 0;
  readback_region.size = VkDeviceSize(byte_length);
  command_processor_.deferred_command_buffer().CmdVkCopyBuffer(
      edram_buffer_, readback_buffer, 1, &readback_region);
  command_processor_.PushBufferMemoryBarrier(
      readback_buffer, 0, VK_WHOLE_SIZE, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_HOST_READ_BIT);

  bool succeeded = false;
  if (command_processor_.AwaitAllQueueOperationsCompletion()) {
    void* mapping = nullptr;
    if (dfn.vkMapMemory(device, readback_memory, 0, VK_WHOLE_SIZE, 0,
                        &mapping) == VK_SUCCESS) {
      if (!(vulkan_device->memory_types().host_coherent &
            (uint32_t(1) << readback_memory_type))) {
        VkMappedMemoryRange mapped_range;
        mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        mapped_range.pNext = nullptr;
        mapped_range.memory = readback_memory;
        mapped_range.offset = 0;
        mapped_range.size = readback_memory_size;
        dfn.vkInvalidateMappedMemoryRanges(device, 1, &mapped_range);
      }

      const uint8_t* bytes = reinterpret_cast<const uint8_t*>(mapping);
      constexpr uint64_t kSampleStride = 2048;
      uint64_t checksum = 1469598103934665603ull;
      uint32_t samples = 0;
      uint32_t nonzero_samples = 0;
      uint32_t varying_samples = 0;
      uint32_t first_sample_value = 0;
      uint32_t first_sample_matches = 0;
      uint64_t first_nonzero_offset = UINT64_MAX;
      uint32_t first_nonzero_value = 0;
      uint32_t previous_word = 0;
      bool have_previous_word = false;
      bool have_first_sample = false;
      for (uint64_t offset = 0; offset + sizeof(uint32_t) <= byte_length;
           offset += kSampleStride) {
        uint32_t word = 0;
        std::memcpy(&word, bytes + offset, sizeof(word));
        checksum ^= uint64_t(word) + (offset << 1);
        checksum *= 1099511628211ull;
        ++samples;
        if (!have_first_sample) {
          first_sample_value = word;
          have_first_sample = true;
        }
        if (word == first_sample_value) {
          ++first_sample_matches;
        }
        if (have_previous_word && word != previous_word) {
          ++varying_samples;
        }
        previous_word = word;
        have_previous_word = true;
        if (word) {
          ++nonzero_samples;
          if (first_nonzero_offset == UINT64_MAX) {
            first_nonzero_offset = offset;
            first_nonzero_value = word;
          }
        }
      }
      bool repeated_first_sample =
          samples >= 16 &&
          uint64_t(first_sample_matches) * 100 >= uint64_t(samples) * 90;
      bool low_variation =
          samples >= 16 &&
          uint64_t(varying_samples) * 100 <= uint64_t(samples) * 6;
      bool clear_like = repeated_first_sample || low_variation;
      uint32_t first_words[8] = {};
      uint32_t first_word_count = uint32_t(
          std::min<uint64_t>(xe::countof(first_words), byte_length / 4));
      for (uint32_t i = 0; i < first_word_count; ++i) {
        std::memcpy(&first_words[i], bytes + i * sizeof(uint32_t),
                    sizeof(uint32_t));
      }
      XELOGI(
          "GPU resolve trace: edram checksum stage=after_dump "
          "base_tiles={} row_length={} rows={} pitch={} byte_offset={:08X} "
          "byte_length={:08X} dest_base={:08X} dest_length={:08X} "
          "dest_format={} samples={} nonzero={} varying={} clear_like={} "
          "low_variation={} first_sample={:08X} first_sample_matches={} "
          "checksum={:016X} first_nonzero={} first_nonzero_value={:08X} "
          "first={:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
          base_tiles, row_length_used, rows, pitch_tiles, uint32_t(byte_offset),
          uint32_t(byte_length), resolve_info.copy_dest_extent_start,
          resolve_info.copy_dest_extent_length,
          uint32_t(resolve_info.copy_dest_info.copy_dest_format), samples,
          nonzero_samples, varying_samples, clear_like, low_variation,
          first_sample_value, first_sample_matches, checksum,
          first_nonzero_offset == UINT64_MAX ? -1
                                             : int64_t(first_nonzero_offset),
          first_nonzero_value, first_words[0], first_words[1], first_words[2],
          first_words[3], first_words[4], first_words[5], first_words[6],
          first_words[7]);
      dfn.vkUnmapMemory(device, readback_memory);
      succeeded = true;
    } else {
      XELOGE(
          "GPU resolve trace: failed to map EDRAM readback memory "
          "base_tiles={} byte_length={:08X}",
          base_tiles, uint32_t(byte_length));
    }
  } else {
    XELOGE(
        "GPU resolve trace: failed to wait for EDRAM readback "
        "base_tiles={} byte_length={:08X}",
        base_tiles, uint32_t(byte_length));
  }

  dfn.vkDestroyBuffer(device, readback_buffer, nullptr);
  dfn.vkFreeMemory(device, readback_memory, nullptr);

  if (!command_processor_.submission_open() &&
      !command_processor_.BeginSubmission(false)) {
    return false;
  }
  return succeeded;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
