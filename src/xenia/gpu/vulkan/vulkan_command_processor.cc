/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_command_processor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/mutex.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/vulkan/vulkan_pipeline_cache.h"
#include "xenia/gpu/vulkan/bd_native_renderer.h"
#include "xenia/gpu/vulkan/vulkan_render_target_cache.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/vulkan/vulkan_shared_memory.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_diagnostic_counters.h"
#include "xenia/ui/vulkan/vulkan_presenter.h"
#include "xenia/ui/vulkan/vulkan_util.h"

// Blue Dragon native-draw HLE step 2 (defined in command_processor.cc): present
// the decoupled full-surface RT the native field draws rendered into, instead of
// BD's resolved guest front buffer.
DECLARE_bool(gpu_bd_hle_present_decoupled);
DECLARE_bool(gpu_bd_native_renderer);
DECLARE_bool(gpu_bd_native_skip_resolves);

namespace xe {
namespace gpu {
namespace vulkan {

namespace {

std::atomic<int32_t> vulkan_resolve_checksum_count{0};
std::atomic<int32_t> vulkan_swap_shared_memory_checksum_count{0};
std::atomic<int32_t> vulkan_copy_state_count{0};
std::atomic<int32_t> vulkan_draw_state_count{0};
std::atomic<int32_t> vulkan_shader_constants_count{0};
std::atomic<int32_t> vulkan_texture_source_checksum_count{0};
std::atomic<int32_t> vulkan_vertex_fetch_checksum_count{0};

bool ShouldTraceVulkanResolveChecksum() {
  if (!cvars::vulkan_trace_resolve_checksum) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_resolve_checksum_budget;
  return budget < 0 || vulkan_resolve_checksum_count.fetch_add(1) < budget;
}

bool ShouldTraceVulkanSwapSharedMemoryChecksum() {
  if (!cvars::vulkan_trace_swap_shared_memory_checksum) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_swap_shared_memory_checksum_budget;
  return budget < 0 ||
         vulkan_swap_shared_memory_checksum_count.fetch_add(1) < budget;
}

bool ShouldTraceVulkanCopyState() {
  if (!cvars::vulkan_trace_copy_state) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_copy_state_budget;
  return budget < 0 || vulkan_copy_state_count.fetch_add(1) < budget;
}

bool ShouldTraceVulkanDrawState() {
  if (!cvars::vulkan_trace_draw_state) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_draw_state_budget;
  return budget < 0 || vulkan_draw_state_count.fetch_add(1) < budget;
}

bool TraceHashMatchesFilter(uint64_t hash, const std::string& filter) {
  if (filter.empty()) {
    return true;
  }
  const char* cursor = filter.c_str();
  while (*cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
      ++cursor;
    }
    if (!*cursor) {
      break;
    }
    char* end = nullptr;
    uint64_t value = std::strtoull(cursor, &end, 16);
    if (end == cursor) {
      while (*cursor && *cursor != ',') {
        ++cursor;
      }
      continue;
    }
    if (value == hash) {
      return true;
    }
    cursor = end;
  }
  return false;
}

bool TraceHashPairMatchesFilter(uint64_t vertex_shader_hash,
                                uint64_t pixel_shader_hash,
                                const std::string& filter) {
  if (filter.empty()) {
    return true;
  }
  return (vertex_shader_hash &&
          TraceHashMatchesFilter(vertex_shader_hash, filter)) ||
         (pixel_shader_hash &&
          TraceHashMatchesFilter(pixel_shader_hash, filter));
}

bool ShouldTraceVulkanDrawStateForShaders(uint64_t vertex_shader_hash,
                                          uint64_t pixel_shader_hash) {
  if (!cvars::vulkan_trace_draw_state) {
    return false;
  }
  if (!TraceHashPairMatchesFilter(vertex_shader_hash, pixel_shader_hash,
                                  cvars::vulkan_trace_draw_shader_filter)) {
    return false;
  }
  return ShouldTraceVulkanDrawState();
}

bool ShouldTraceVulkanTextureSourceChecksum() {
  if (!cvars::vulkan_trace_texture_source_checksum) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_texture_source_checksum_budget;
  return budget < 0 ||
         vulkan_texture_source_checksum_count.fetch_add(1) < budget;
}

bool ShouldTraceVulkanVertexFetchChecksum() {
  if (!cvars::vulkan_trace_vertex_fetch_checksum) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_vertex_fetch_checksum_budget;
  return budget < 0 ||
         vulkan_vertex_fetch_checksum_count.fetch_add(1) < budget;
}

bool ShouldTraceVulkanShaderConstants() {
  if (!cvars::vulkan_trace_shader_constants) {
    return false;
  }
  int32_t budget = cvars::vulkan_trace_shader_constants_budget;
  return budget < 0 || vulkan_shader_constants_count.fetch_add(1) < budget;
}

bool IsDebugPresentResolveCandidateFormat(xenos::TextureFormat format) {
  switch (format) {
    case xenos::TextureFormat::k_8_8_8_8:
    case xenos::TextureFormat::k_8_8_8_8_AS_16_16_16_16:
    case xenos::TextureFormat::k_2_10_10_10:
    case xenos::TextureFormat::k_2_10_10_10_AS_16_16_16_16:
      return true;
    default:
      return false;
  }
}

bool IsPreferredDebugPresentResolveCandidateFormat(
    xenos::TextureFormat format) {
  return format == xenos::TextureFormat::k_2_10_10_10 ||
         format == xenos::TextureFormat::k_2_10_10_10_AS_16_16_16_16;
}

}  // namespace

// Generated with `xb buildshaders`.
namespace shaders {
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/apply_gamma_pwl_fxaa_luma_ps.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/apply_gamma_pwl_ps.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/apply_gamma_table_fxaa_luma_ps.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/apply_gamma_table_ps.h"
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/fullscreen_cw_vs.h"
}  // namespace shaders

const VkDescriptorPoolSize
    VulkanCommandProcessor::kDescriptorPoolSizeUniformBuffer = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        SpirvShaderTranslator::kConstantBufferCount*
            kLinkedTypeDescriptorPoolSetCount};

const VkDescriptorPoolSize
    VulkanCommandProcessor::kDescriptorPoolSizeStorageBuffer = {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kLinkedTypeDescriptorPoolSetCount};

// gpu_vulkan_float_constants_ssbo: the constants set has 3 UNIFORM_BUFFER
// bindings (system, bool/loop, fetch) + 2 STORAGE_BUFFER bindings (float vertex,
// float pixel). kConstantBufferCount == 5; floats == 2; so UBO == 3.
const VkDescriptorPoolSize
    VulkanCommandProcessor::kDescriptorPoolSizeConstantsMixed[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         (SpirvShaderTranslator::kConstantBufferCount - 2) *
             kLinkedTypeDescriptorPoolSetCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         2 * kLinkedTypeDescriptorPoolSetCount}};

// 2x descriptors for texture images because of unsigned and signed bindings.
const VkDescriptorPoolSize
    VulkanCommandProcessor::kDescriptorPoolSizeTextures[2] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
         2 * kLinkedTypeDescriptorPoolSetCount},
        {VK_DESCRIPTOR_TYPE_SAMPLER, kLinkedTypeDescriptorPoolSetCount},
};

VulkanCommandProcessor::VulkanCommandProcessor(
    VulkanGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state),
      completion_timeline_(static_cast<const ui::vulkan::VulkanProvider*>(
                               graphics_system->provider())
                               ->vulkan_device()),
      deferred_command_buffer_(*this),
      prepass_command_buffer_(*this),
      transient_descriptor_allocator_uniform_buffer_(
          static_cast<const ui::vulkan::VulkanProvider*>(
              graphics_system->provider())
              ->vulkan_device(),
          &kDescriptorPoolSizeUniformBuffer, 1,
          kLinkedTypeDescriptorPoolSetCount),
      transient_descriptor_allocator_storage_buffer_(
          static_cast<const ui::vulkan::VulkanProvider*>(
              graphics_system->provider())
              ->vulkan_device(),
          &kDescriptorPoolSizeStorageBuffer, 1,
          kLinkedTypeDescriptorPoolSetCount),
      transient_descriptor_allocator_constants_mixed_(
          static_cast<const ui::vulkan::VulkanProvider*>(
              graphics_system->provider())
              ->vulkan_device(),
          kDescriptorPoolSizeConstantsMixed, 2,
          kLinkedTypeDescriptorPoolSetCount),
      transient_descriptor_allocator_textures_(
          static_cast<const ui::vulkan::VulkanProvider*>(
              graphics_system->provider())
              ->vulkan_device(),
          kDescriptorPoolSizeTextures,
          uint32_t(xe::countof(kDescriptorPoolSizeTextures)),
          kLinkedTypeDescriptorPoolSetCount) {}

VulkanCommandProcessor::~VulkanCommandProcessor() = default;

void VulkanCommandProcessor::ClearCaches() {
  CommandProcessor::ClearCaches();
  cache_clear_requested_ = true;
}

void VulkanCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                      uint32_t length) {
  shared_memory_->MemoryInvalidationCallback(base_ptr, length, true);
  primitive_processor_->MemoryInvalidationCallback(base_ptr, length, true);
}

void VulkanCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {}

std::string VulkanCommandProcessor::GetWindowTitleText() const {
  std::ostringstream title;
  title << "Vulkan";
  if (render_target_cache_) {
    switch (render_target_cache_->GetPath()) {
      case RenderTargetCache::Path::kHostRenderTargets:
        title << " - FBO";
        break;
      case RenderTargetCache::Path::kPixelShaderInterlock:
        title << " - FSI";
        break;
      default:
        break;
    }
    uint32_t draw_resolution_scale_x =
        texture_cache_ ? texture_cache_->draw_resolution_scale_x() : 1;
    uint32_t draw_resolution_scale_y =
        texture_cache_ ? texture_cache_->draw_resolution_scale_y() : 1;
    if (draw_resolution_scale_x > 1 || draw_resolution_scale_y > 1) {
      title << ' ' << draw_resolution_scale_x << 'x' << draw_resolution_scale_y;
    }
  }
  title << " - HEAVILY INCOMPLETE, early development";
  return title.str();
}

bool VulkanCommandProcessor::SetupContext() {
  if (!CommandProcessor::SetupContext()) {
    XELOGE("Failed to initialize base command processor context");
    return false;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  const ui::vulkan::VulkanDevice::Properties& device_properties =
      vulkan_device->properties();

  // Push descriptors (VK_KHR_push_descriptor) let the per-draw texture/sampler
  // descriptors be pushed inline into the command buffer, avoiding a transient
  // descriptor set allocation + vkUpdateDescriptorSets + vkCmdBindDescriptorSets
  // every draw. Only used for the texture sets (small binding counts, well within
  // maxPushDescriptors). Requires the texture set layouts to be created with the
  // push-descriptor flag, so this must be decided before any layout creation.
  push_descriptors_active_ =
      cvars::vulkan_push_descriptors &&
      vulkan_device->extensions().ext_KHR_push_descriptor &&
      vulkan_device->functions().vkCmdPushDescriptorSetKHR != nullptr;
  XELOGGPU("VulkanCommandProcessor: push descriptors {}",
           push_descriptors_active_ ? "ENABLED" : "disabled");

  // GPU-side frame-time timestamp queries (Thor/Adreno bring-up diagnostic).
  gpu_timestamp_period_ns_ = device_properties.timestampPeriod;
  if (gpu_timestamp_period_ns_ > 0.0f) {
    VkQueryPoolCreateInfo query_pool_create_info = {};
    query_pool_create_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    query_pool_create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_pool_create_info.queryCount = 2 * kMaxFramesInFlight;
    if (dfn.vkCreateQueryPool(device, &query_pool_create_info, nullptr,
                              &gpu_timestamp_pool_) != VK_SUCCESS) {
      gpu_timestamp_pool_ = VK_NULL_HANDLE;
    }
    // Route A per-pass timing pool (separate from the frame pool; used only when
    // vulkan_trace_pass_timestamps is on). 2 timestamps per bracket pair.
    VkQueryPoolCreateInfo pass_pool_create_info = {};
    pass_pool_create_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pass_pool_create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    pass_pool_create_info.queryCount =
        2u * kMaxPassBrackets * kMaxFramesInFlight;
    if (dfn.vkCreateQueryPool(device, &pass_pool_create_info, nullptr,
                              &gpu_pass_timestamp_pool_) != VK_SUCCESS) {
      gpu_pass_timestamp_pool_ = VK_NULL_HANDLE;
    }
  }
  XELOGGPU("VulkanCommandProcessor: GPU timestamps {} (period {} ns)",
           gpu_timestamp_pool_ != VK_NULL_HANDLE ? "ENABLED" : "disabled",
           gpu_timestamp_period_ns_);

  // The unconditional inclusion of the vertex shader stage also covers the case
  // of manual index / factor buffer fetch (the system constants and the shared
  // memory are needed for that) in the tessellation vertex shader when
  // fullDrawIndexUint32 is not supported.
  guest_shader_pipeline_stages_ = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  guest_shader_vertex_stages_ = VK_SHADER_STAGE_VERTEX_BIT;
  if (device_properties.tessellationShader) {
    guest_shader_pipeline_stages_ |=
        VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    guest_shader_vertex_stages_ |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
  }
  if (!device_properties.vertexPipelineStoresAndAtomics) {
    // For memory export from vertex shaders converted to compute shaders.
    guest_shader_pipeline_stages_ |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    guest_shader_vertex_stages_ |= VK_SHADER_STAGE_COMPUTE_BIT;
  }

  // 16384 is bigger than any single uniform buffer that Xenia needs, but is the
  // minimum maxUniformBufferRange, thus the safe minimum amount.
  uniform_buffer_pool_ = std::make_unique<ui::vulkan::VulkanUploadBufferPool>(
      vulkan_device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      xe::align(std::max(ui::GraphicsUploadBufferPool::kDefaultPageSize,
                         size_t(16384)),
                size_t(device_properties.minUniformBufferOffsetAlignment)));

  // Lever 2 cull (gpu_cull_compaction): transient INDEX-buffer ring. Created
  // unconditionally (pages allocate lazily on first Request, so the default build
  // pays nothing until the cvar is toggled on) to keep the cvar live-toggleable.
  cull_index_buffer_pool_ = std::make_unique<ui::vulkan::VulkanUploadBufferPool>(
      vulkan_device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      ui::GraphicsUploadBufferPool::kDefaultPageSize);

  // R2 (vulkan_dynamic_constants_arena): persistent UMA ring arena for the guest
  // draw constant buffers. Initialized ONLY when the cvar is on; default-off
  // leaves every ring invalid so the per-draw transient-descriptor path
  // (uniform_buffer_pool_) is used unchanged. On the Thor's UMA every heap is
  // HOST_VISIBLE|DEVICE_LOCAL; if no such memory type exists we tear all rings
  // back down and fall back rather than run a partially-armed arena.
  if (cvars::vulkan_dynamic_constants_arena) {
    const VkDeviceSize arena_align = std::max<VkDeviceSize>(
        device_properties.minUniformBufferOffsetAlignment, VkDeviceSize(1));
    // Per-binding (total capacity, descriptor range). `range` MUST match the
    // descriptor range set in the dynamic constants descriptor update below, and
    // is passed as the ring tail_padding so a fixed range can overhang a smaller
    // variable-size per-draw allocation without leaving the buffer. `capacity` is
    // sized for a heavy frame's worth of per-draw constant slots (the float
    // buffers dominate; each draw allocates only its actual constant bytes).
    struct ArenaRingSpec {
      VkDeviceSize capacity;
      VkDeviceSize range;
    };
    const VkDeviceSize kFloatRange = VkDeviceSize(sizeof(float) * 4 * 256);
    ArenaRingSpec arena_specs[SpirvShaderTranslator::kConstantBufferCount];
    arena_specs[SpirvShaderTranslator::kConstantBufferSystem] = {
        VkDeviceSize(4) * 1024 * 1024,
        VkDeviceSize(sizeof(SpirvShaderTranslator::SystemConstants))};
    arena_specs[SpirvShaderTranslator::kConstantBufferFloatVertex] = {
        VkDeviceSize(24) * 1024 * 1024, kFloatRange};
    arena_specs[SpirvShaderTranslator::kConstantBufferFloatPixel] = {
        VkDeviceSize(24) * 1024 * 1024, kFloatRange};
    arena_specs[SpirvShaderTranslator::kConstantBufferBoolLoop] = {
        VkDeviceSize(2) * 1024 * 1024,
        VkDeviceSize(sizeof(uint32_t) * (8 + 32))};
    arena_specs[SpirvShaderTranslator::kConstantBufferFetch] = {
        VkDeviceSize(6) * 1024 * 1024,
        VkDeviceSize(sizeof(uint32_t) * 6 * 32)};
    bool arena_ok = true;
    for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
      if (!dynamic_constants_rings_[i].Initialize(
              vulkan_device, arena_specs[i].capacity,
              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
              arena_align, arena_specs[i].range)) {
        arena_ok = false;
        break;
      }
    }
    if (!arena_ok) {
      for (auto& ring : dynamic_constants_rings_) {
        ring.Shutdown();
      }
      XELOGGPU(
          "VulkanCommandProcessor: vulkan_dynamic_constants_arena requested but "
          "ring init failed - falling back to the transient constant path");
    } else {
      XELOGGPU(
          "VulkanCommandProcessor: vulkan_dynamic_constants_arena ENABLED");
    }
  }

  // G1-lite (gpu_binning_deinterleave_pos): the compact de-interleaved
  // position stream ring. HOST_VISIBLE|DEVICE_LOCAL on UMA (the CPU gathers
  // directly into the persistent mapping - no staging copy, no in-pass
  // command-buffer work), plain HOST_VISIBLE as the fallback. If neither
  // exists the path stays disarmed: every draw runs verbatim and the set-0
  // compact binding points at shared memory as a dummy.
  if (cvars::gpu_binning_deinterleave_pos) {
    constexpr VkDeviceSize kCompactPosRingCapacity =
        VkDeviceSize(48) * 1024 * 1024;
    if (!compact_pos_ring_.Initialize(
            vulkan_device, kCompactPosRingCapacity,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(uint32_t)) &&
        !compact_pos_ring_.Initialize(vulkan_device, kCompactPosRingCapacity,
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                      sizeof(uint32_t))) {
      XELOGGPU(
          "VulkanCommandProcessor: gpu_binning_deinterleave_pos requested but "
          "the compact position ring init failed - draws will run verbatim");
    } else {
      XELOGGPU(
          "VulkanCommandProcessor: gpu_binning_deinterleave_pos compact "
          "position ring ENABLED ({} MB)",
          uint64_t(kCompactPosRingCapacity >> 20));
    }
  }

  // Lever 2b (vulkan_merge_draws_indirect): per-frame ring for the
  // VkDrawIndexedIndirectCommand[] arrays. Only created when the lever is enabled
  // AND the device supports batched indirect draws, so the default build pays
  // nothing. drawCount > 1 in vkCmdDraw*Indirect requires the multiDrawIndirect
  // feature (enabled at device creation when supported).
  mdi_supported_ = device_properties.multiDrawIndirect &&
                   device_properties.maxDrawIndirectCount >= 2 &&
                   vulkan_device->functions().vkCmdDrawIndexedIndirectCount !=
                       nullptr;
  // Cap commands per MDI run. Head-emit pre-sizes (and zeroes) this many indirect
  // commands per run, so a large cap wastes buffer + adds no-op iterations on short
  // runs; 32 covers the bulk of Blue Dragon's same-state runs (runlen histogram).
  mdi_max_draw_count_ =
      std::min(device_properties.maxDrawIndirectCount, uint32_t(32));
  if (cvars::vulkan_merge_draws_indirect && mdi_supported_) {
    indirect_buffer_pool_ = std::make_unique<ui::vulkan::VulkanUploadBufferPool>(
        vulkan_device, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        ui::GraphicsUploadBufferPool::kDefaultPageSize);
  }

  // Descriptor set layouts that don't depend on the setup of other subsystems.
  VkShaderStageFlags guest_shader_stages =
      guest_shader_vertex_stages_ | VK_SHADER_STAGE_FRAGMENT_BIT;
  // Empty.
  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
  descriptor_set_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_create_info.pNext = nullptr;
  descriptor_set_layout_create_info.flags = 0;
  descriptor_set_layout_create_info.bindingCount = 0;
  descriptor_set_layout_create_info.pBindings = nullptr;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_empty_) != VK_SUCCESS) {
    XELOGE("Failed to create an empty Vulkan descriptor set layout");
    return false;
  }
  // Guest draw constants.
  VkDescriptorSetLayoutBinding descriptor_set_layout_bindings_constants
      [SpirvShaderTranslator::kConstantBufferCount] = {};
  for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
    VkDescriptorSetLayoutBinding& constants_binding =
        descriptor_set_layout_bindings_constants[i];
    constants_binding.binding = i;
    constants_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    constants_binding.descriptorCount = 1;
    constants_binding.pImmutableSamplers = nullptr;
  }
  descriptor_set_layout_bindings_constants
      [SpirvShaderTranslator::kConstantBufferSystem]
          .stageFlags =
      guest_shader_stages |
      (device_properties.tessellationShader
           ? VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT
           : 0) |
      (device_properties.geometryShader ? VK_SHADER_STAGE_GEOMETRY_BIT : 0);
  descriptor_set_layout_bindings_constants
      [SpirvShaderTranslator::kConstantBufferFloatVertex]
          .stageFlags = guest_shader_vertex_stages_;
  descriptor_set_layout_bindings_constants
      [SpirvShaderTranslator::kConstantBufferFloatPixel]
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  descriptor_set_layout_bindings_constants
      [SpirvShaderTranslator::kConstantBufferBoolLoop]
          .stageFlags = guest_shader_stages;
  descriptor_set_layout_bindings_constants
      [SpirvShaderTranslator::kConstantBufferFetch]
          .stageFlags = guest_shader_stages;
  // gpu_vulkan_float_constants_ssbo: the two float-constant bindings become
  // STORAGE_BUFFER (Turnip indexes SSBOs robustly where it miscompiles dynamic
  // UBO indexing). Must match the SPIR-V storage class + the descriptor write +
  // the mixed allocator. (The arena/DYNAMIC variant below is default-off and
  // mutually exclusive in practice; left UBO_DYNAMIC.)
  if (cvars::gpu_vulkan_float_constants_ssbo) {
    descriptor_set_layout_bindings_constants
        [SpirvShaderTranslator::kConstantBufferFloatVertex]
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptor_set_layout_bindings_constants
        [SpirvShaderTranslator::kConstantBufferFloatPixel]
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }
  descriptor_set_layout_create_info.bindingCount =
      uint32_t(xe::countof(descriptor_set_layout_bindings_constants));
  descriptor_set_layout_create_info.pBindings =
      descriptor_set_layout_bindings_constants;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layout_constants_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan descriptor set layout for guest draw "
        "constant buffers");
    return false;
  }

  // R2 (vulkan_dynamic_constants_arena): build the UNIFORM_BUFFER_DYNAMIC variant
  // of the guest-constants layout (same 5 bindings/stages, DYNAMIC type) plus one
  // persistent descriptor set bound once to the arena rings - the per-draw dynamic
  // offset selects each draw's slot. Only when the cvar is on AND the rings
  // initialized; default-off leaves all three handles null and the per-draw
  // transient path unchanged. The enum constant-buffer indices are the binding
  // indices (0..kConstantBufferCount-1).
  if (cvars::vulkan_dynamic_constants_arena &&
      dynamic_constants_rings_[0].is_valid()) {
    for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
      descriptor_set_layout_bindings_constants[i].descriptorType =
          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    }
    const bool dynamic_layout_ok =
        dfn.vkCreateDescriptorSetLayout(
            device, &descriptor_set_layout_create_info, nullptr,
            &descriptor_set_layout_constants_dynamic_) == VK_SUCCESS;
    // Restore the shared bindings array to its non-dynamic type (defensive - it
    // is a local not reused after this, but keep it self-consistent).
    for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
      descriptor_set_layout_bindings_constants[i].descriptorType =
          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
    if (!dynamic_layout_ok) {
      XELOGE(
          "Failed to create the dynamic guest-constants descriptor set layout");
      return false;
    }
    VkDescriptorPoolSize dynamic_pool_size;
    dynamic_pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dynamic_pool_size.descriptorCount =
        SpirvShaderTranslator::kConstantBufferCount;
    VkDescriptorPoolCreateInfo dynamic_pool_create_info;
    dynamic_pool_create_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dynamic_pool_create_info.pNext = nullptr;
    dynamic_pool_create_info.flags = 0;
    dynamic_pool_create_info.maxSets = 1;
    dynamic_pool_create_info.poolSizeCount = 1;
    dynamic_pool_create_info.pPoolSizes = &dynamic_pool_size;
    if (dfn.vkCreateDescriptorPool(device, &dynamic_pool_create_info, nullptr,
                                   &constants_dynamic_descriptor_pool_) !=
        VK_SUCCESS) {
      XELOGE("Failed to create the dynamic guest-constants descriptor pool");
      return false;
    }
    VkDescriptorSetAllocateInfo dynamic_set_allocate_info;
    dynamic_set_allocate_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dynamic_set_allocate_info.pNext = nullptr;
    dynamic_set_allocate_info.descriptorPool =
        constants_dynamic_descriptor_pool_;
    dynamic_set_allocate_info.descriptorSetCount = 1;
    dynamic_set_allocate_info.pSetLayouts =
        &descriptor_set_layout_constants_dynamic_;
    if (dfn.vkAllocateDescriptorSets(device, &dynamic_set_allocate_info,
                                     &constants_dynamic_descriptor_set_) !=
        VK_SUCCESS) {
      XELOGE("Failed to allocate the dynamic guest-constants descriptor set");
      return false;
    }
    // Per-binding descriptor range = the MAX bytes that binding can hold; the
    // per-draw dynamic offset selects the slot. Float constants are variable per
    // draw but capped at 256 vec4.
    VkDeviceSize constant_ranges[SpirvShaderTranslator::kConstantBufferCount];
    constant_ranges[SpirvShaderTranslator::kConstantBufferSystem] =
        VkDeviceSize(sizeof(SpirvShaderTranslator::SystemConstants));
    constant_ranges[SpirvShaderTranslator::kConstantBufferFloatVertex] =
        VkDeviceSize(sizeof(float) * 4 * 256);
    constant_ranges[SpirvShaderTranslator::kConstantBufferFloatPixel] =
        VkDeviceSize(sizeof(float) * 4 * 256);
    constant_ranges[SpirvShaderTranslator::kConstantBufferBoolLoop] =
        VkDeviceSize(sizeof(uint32_t) * (8 + 32));
    constant_ranges[SpirvShaderTranslator::kConstantBufferFetch] =
        VkDeviceSize(sizeof(uint32_t) * 6 * 32);
    VkDescriptorBufferInfo
        dynamic_buffer_infos[SpirvShaderTranslator::kConstantBufferCount];
    VkWriteDescriptorSet
        dynamic_writes[SpirvShaderTranslator::kConstantBufferCount];
    for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount; ++i) {
      dynamic_buffer_infos[i].buffer = dynamic_constants_rings_[i].buffer();
      dynamic_buffer_infos[i].offset = 0;
      dynamic_buffer_infos[i].range = constant_ranges[i];
      VkWriteDescriptorSet& dynamic_write = dynamic_writes[i];
      dynamic_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      dynamic_write.pNext = nullptr;
      dynamic_write.dstSet = constants_dynamic_descriptor_set_;
      dynamic_write.dstBinding = i;
      dynamic_write.dstArrayElement = 0;
      dynamic_write.descriptorCount = 1;
      dynamic_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      dynamic_write.pImageInfo = nullptr;
      dynamic_write.pBufferInfo = &dynamic_buffer_infos[i];
      dynamic_write.pTexelBufferView = nullptr;
    }
    dfn.vkUpdateDescriptorSets(device,
                               SpirvShaderTranslator::kConstantBufferCount,
                               dynamic_writes, 0, nullptr);
    XELOGGPU(
        "VulkanCommandProcessor: dynamic constants descriptor set bound to the "
        "arena rings");
  }

  // Transient: storage buffer for compute shaders.
  VkDescriptorSetLayoutBinding descriptor_set_layout_binding_transient;
  descriptor_set_layout_binding_transient.binding = 0;
  descriptor_set_layout_binding_transient.descriptorType =
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  descriptor_set_layout_binding_transient.descriptorCount = 1;
  descriptor_set_layout_binding_transient.stageFlags =
      VK_SHADER_STAGE_COMPUTE_BIT;
  descriptor_set_layout_binding_transient.pImmutableSamplers = nullptr;
  descriptor_set_layout_create_info.bindingCount = 1;
  descriptor_set_layout_create_info.pBindings =
      &descriptor_set_layout_binding_transient;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &descriptor_set_layouts_single_transient_[size_t(
              SingleTransientDescriptorLayout::kStorageBufferCompute)]) !=
      VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan descriptor set layout for a storage buffer "
        "bound to the compute shader");
    return false;
  }

  shared_memory_ = std::make_unique<VulkanSharedMemory>(
      *this, *memory_, trace_writer_, guest_shader_pipeline_stages_);
  if (!shared_memory_->Initialize()) {
    XELOGE("Failed to initialize shared memory");
    return false;
  }

  primitive_processor_ = std::make_unique<VulkanPrimitiveProcessor>(
      *register_file_, *memory_, trace_writer_, *shared_memory_, *this);
  if (!primitive_processor_->Initialize()) {
    XELOGE("Failed to initialize the geometric primitive processor");
    return false;
  }

  uint32_t shared_memory_binding_count_log2 =
      SpirvShaderTranslator::GetSharedMemoryStorageBufferCountLog2(
          device_properties.maxStorageBufferRange);
  uint32_t shared_memory_binding_count = UINT32_C(1)
                                         << shared_memory_binding_count_log2;

  // Requires the transient descriptor set layouts.
  // TODO(Triang3l): Get the actual draw resolution scale when the texture cache
  // supports resolution scaling.
  render_target_cache_ = std::make_unique<VulkanRenderTargetCache>(
      *register_file_, *memory_, trace_writer_, 1, 1, *this);
  if (!render_target_cache_->Initialize(shared_memory_binding_count)) {
    XELOGE("Failed to initialize the render target cache");
    return false;
  }

  // Blue Dragon FULL native D3D9->Vulkan HLE renderer (gpu_bd_native_renderer):
  // instantiate the SEPARATE native path's persistent full-surface RT + one held
  // render pass (BdNativeRenderer), decoupled from the EDRAM render_target_cache
  // above. Default-off; failure is non-fatal (LLE stays the fallback). Brick 2b
  // wires the 0x82489F40 capture -> native pipelines -> this RT.
  if (cvars::gpu_bd_native_renderer) {
    bd_native_renderer_ = std::make_unique<BdNativeRenderer>(*this);
    if (!bd_native_renderer_->Initialize(1280, 720)) {
      XELOGE("BdNativeRenderer: init failed - falling back to LLE");
      bd_native_renderer_.reset();
    }
  }

  // Shared memory and EDRAM descriptor set layout.
  // THE EDRAM SOLVE, hybrid form: also expose the EDRAM SSBO descriptor (binding
  // 1) when hybrid_postprocess is on, so the composite FSI pipelines can write the
  // EDRAM buffer while the frame path stays host-RT (the main scene ignores the
  // extra binding). First cut uses the SHARED layout - note the compact_pos
  // binding index is 1 + this flag, so gpu_binning_deinterleave_pos must stay OFF
  // under hybrid until a dedicated hybrid-FSI layout lands (it's off in the BD
  // test stack). Default off => byte-identical.
  bool edram_fragment_shader_interlock =
      render_target_cache_->GetPath() ==
          RenderTargetCache::Path::kPixelShaderInterlock ||
      render_target_cache_->hybrid_postprocess();
  VkDescriptorSetLayoutBinding
      shared_memory_and_edram_descriptor_set_layout_bindings[3];
  shared_memory_and_edram_descriptor_set_layout_bindings[0].binding = 0;
  shared_memory_and_edram_descriptor_set_layout_bindings[0].descriptorType =
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  shared_memory_and_edram_descriptor_set_layout_bindings[0].descriptorCount =
      shared_memory_binding_count;
  shared_memory_and_edram_descriptor_set_layout_bindings[0].stageFlags =
      guest_shader_stages;
  shared_memory_and_edram_descriptor_set_layout_bindings[0].pImmutableSamplers =
      nullptr;
  VkDescriptorSetLayoutCreateInfo
      shared_memory_and_edram_descriptor_set_layout_create_info;
  shared_memory_and_edram_descriptor_set_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  shared_memory_and_edram_descriptor_set_layout_create_info.pNext = nullptr;
  shared_memory_and_edram_descriptor_set_layout_create_info.flags = 0;
  shared_memory_and_edram_descriptor_set_layout_create_info.pBindings =
      shared_memory_and_edram_descriptor_set_layout_bindings;
  if (edram_fragment_shader_interlock) {
    // EDRAM.
    shared_memory_and_edram_descriptor_set_layout_bindings[1].binding = 1;
    shared_memory_and_edram_descriptor_set_layout_bindings[1].descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    shared_memory_and_edram_descriptor_set_layout_bindings[1].descriptorCount =
        1;
    shared_memory_and_edram_descriptor_set_layout_bindings[1].stageFlags =
        VK_SHADER_STAGE_FRAGMENT_BIT;
    shared_memory_and_edram_descriptor_set_layout_bindings[1]
        .pImmutableSamplers = nullptr;
    shared_memory_and_edram_descriptor_set_layout_create_info.bindingCount = 2;
  } else {
    shared_memory_and_edram_descriptor_set_layout_create_info.bindingCount = 1;
  }
  if (cvars::gpu_binning_deinterleave_pos) {
    // Compact de-interleaved position stream (binding 1, or 2 under fragment
    // shader interlock - must match the SPIR-V translator).
    uint32_t compact_pos_binding_index =
        shared_memory_and_edram_descriptor_set_layout_create_info.bindingCount;
    VkDescriptorSetLayoutBinding& compact_pos_binding =
        shared_memory_and_edram_descriptor_set_layout_bindings
            [compact_pos_binding_index];
    compact_pos_binding.binding = compact_pos_binding_index;
    compact_pos_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    compact_pos_binding.descriptorCount = 1;
    compact_pos_binding.stageFlags = guest_shader_stages;
    compact_pos_binding.pImmutableSamplers = nullptr;
    ++shared_memory_and_edram_descriptor_set_layout_create_info.bindingCount;
  }
  if (dfn.vkCreateDescriptorSetLayout(
          device, &shared_memory_and_edram_descriptor_set_layout_create_info,
          nullptr,
          &descriptor_set_layout_shared_memory_and_edram_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan descriptor set layout for the shared memory "
        "and the EDRAM");
    return false;
  }

  pipeline_cache_ = std::make_unique<VulkanPipelineCache>(
      *this, *register_file_, *render_target_cache_,
      guest_shader_vertex_stages_);
  if (!pipeline_cache_->Initialize()) {
    XELOGE("Failed to initialize the graphics pipeline cache");
    return false;
  }

  // Requires the transient descriptor set layouts.
  // TODO(Triang3l): Actual draw resolution scale.
  texture_cache_ =
      VulkanTextureCache::Create(*register_file_, *shared_memory_, 1, 1, *this,
                                 guest_shader_pipeline_stages_);
  if (!texture_cache_) {
    XELOGE("Failed to initialize the texture cache");
    return false;
  }

  // Shared memory and EDRAM common bindings.
  // gpu_shared_memory_double_buffer: when the shared memory reports two
  // host-visible versions, allocate a SECOND identical descriptor set whose
  // binding 0 points at version 1, so each draw can bind the set matching the
  // version the GPU currently reads. The pool then needs room for 2 sets and 2x
  // the per-set descriptors. When off, this is exactly the single-set pool.
  const bool shared_memory_double_buffer_active =
      shared_memory_->double_buffer_active();
  const uint32_t shared_memory_descriptor_set_count =
      shared_memory_double_buffer_active ? 2u : 1u;
  VkDescriptorPoolSize descriptor_pool_sizes[1];
  descriptor_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  descriptor_pool_sizes[0].descriptorCount =
      (shared_memory_binding_count + uint32_t(edram_fragment_shader_interlock) +
       uint32_t(cvars::gpu_binning_deinterleave_pos)) *
      shared_memory_descriptor_set_count;
  VkDescriptorPoolCreateInfo descriptor_pool_create_info;
  descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_create_info.pNext = nullptr;
  descriptor_pool_create_info.flags = 0;
  descriptor_pool_create_info.maxSets = shared_memory_descriptor_set_count;
  descriptor_pool_create_info.poolSizeCount = 1;
  descriptor_pool_create_info.pPoolSizes = descriptor_pool_sizes;
  if (dfn.vkCreateDescriptorPool(device, &descriptor_pool_create_info, nullptr,
                                 &shared_memory_and_edram_descriptor_pool_) !=
      VK_SUCCESS) {
    XELOGE(
        "Failed to create the Vulkan descriptor pool for shared memory and "
        "EDRAM");
    return false;
  }
  VkDescriptorSetAllocateInfo descriptor_set_allocate_info;
  descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptor_set_allocate_info.pNext = nullptr;
  descriptor_set_allocate_info.descriptorPool =
      shared_memory_and_edram_descriptor_pool_;
  descriptor_set_allocate_info.descriptorSetCount = 1;
  descriptor_set_allocate_info.pSetLayouts =
      &descriptor_set_layout_shared_memory_and_edram_;
  if (dfn.vkAllocateDescriptorSets(device, &descriptor_set_allocate_info,
                                   &shared_memory_and_edram_descriptor_set_) !=
      VK_SUCCESS) {
    XELOGE(
        "Failed to allocate the Vulkan descriptor set for shared memory and "
        "EDRAM");
    return false;
  }
  VkDescriptorBufferInfo
      shared_memory_descriptor_buffers_info[SharedMemory::kBufferSize /
                                            (128 << 20)];
  uint32_t shared_memory_binding_range =
      SharedMemory::kBufferSize >> shared_memory_binding_count_log2;
  for (uint32_t i = 0; i < shared_memory_binding_count; ++i) {
    VkDescriptorBufferInfo& shared_memory_descriptor_buffer_info =
        shared_memory_descriptor_buffers_info[i];
    shared_memory_descriptor_buffer_info.buffer = shared_memory_->buffer();
    shared_memory_descriptor_buffer_info.offset =
        shared_memory_binding_range * i;
    shared_memory_descriptor_buffer_info.range = shared_memory_binding_range;
  }
  VkWriteDescriptorSet write_descriptor_sets[3];
  VkWriteDescriptorSet& write_descriptor_set_shared_memory =
      write_descriptor_sets[0];
  write_descriptor_set_shared_memory.sType =
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write_descriptor_set_shared_memory.pNext = nullptr;
  write_descriptor_set_shared_memory.dstSet =
      shared_memory_and_edram_descriptor_set_;
  write_descriptor_set_shared_memory.dstBinding = 0;
  write_descriptor_set_shared_memory.dstArrayElement = 0;
  write_descriptor_set_shared_memory.descriptorCount =
      shared_memory_binding_count;
  write_descriptor_set_shared_memory.descriptorType =
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write_descriptor_set_shared_memory.pImageInfo = nullptr;
  write_descriptor_set_shared_memory.pBufferInfo =
      shared_memory_descriptor_buffers_info;
  write_descriptor_set_shared_memory.pTexelBufferView = nullptr;
  VkDescriptorBufferInfo edram_descriptor_buffer_info;
  if (edram_fragment_shader_interlock) {
    edram_descriptor_buffer_info.buffer = render_target_cache_->edram_buffer();
    edram_descriptor_buffer_info.offset = 0;
    edram_descriptor_buffer_info.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet& write_descriptor_set_edram = write_descriptor_sets[1];
    write_descriptor_set_edram.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set_edram.pNext = nullptr;
    write_descriptor_set_edram.dstSet = shared_memory_and_edram_descriptor_set_;
    write_descriptor_set_edram.dstBinding = 1;
    write_descriptor_set_edram.dstArrayElement = 0;
    write_descriptor_set_edram.descriptorCount = 1;
    write_descriptor_set_edram.descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_descriptor_set_edram.pImageInfo = nullptr;
    write_descriptor_set_edram.pBufferInfo = &edram_descriptor_buffer_info;
    write_descriptor_set_edram.pTexelBufferView = nullptr;
  }
  uint32_t shared_memory_and_edram_write_count =
      1 + uint32_t(edram_fragment_shader_interlock);
  VkDescriptorBufferInfo compact_pos_descriptor_buffer_info;
  if (cvars::gpu_binning_deinterleave_pos) {
    // Compact de-interleaved position stream: the gather ring when it was
    // created, or the shared memory buffer as a safe dummy (the shader only
    // reads this binding under kSysFlag_PosFetchRedirect, which is never set
    // unless a gather into the ring succeeded).
    compact_pos_descriptor_buffer_info.buffer =
        compact_pos_ring_.is_valid() ? compact_pos_ring_.buffer()
                                     : shared_memory_->buffer();
    compact_pos_descriptor_buffer_info.offset = 0;
    compact_pos_descriptor_buffer_info.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet& write_descriptor_set_compact_pos =
        write_descriptor_sets[shared_memory_and_edram_write_count];
    write_descriptor_set_compact_pos.sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set_compact_pos.pNext = nullptr;
    write_descriptor_set_compact_pos.dstSet =
        shared_memory_and_edram_descriptor_set_;
    write_descriptor_set_compact_pos.dstBinding =
        1 + uint32_t(edram_fragment_shader_interlock);
    write_descriptor_set_compact_pos.dstArrayElement = 0;
    write_descriptor_set_compact_pos.descriptorCount = 1;
    write_descriptor_set_compact_pos.descriptorType =
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write_descriptor_set_compact_pos.pImageInfo = nullptr;
    write_descriptor_set_compact_pos.pBufferInfo =
        &compact_pos_descriptor_buffer_info;
    write_descriptor_set_compact_pos.pTexelBufferView = nullptr;
    ++shared_memory_and_edram_write_count;
  }
  dfn.vkUpdateDescriptorSets(device, shared_memory_and_edram_write_count,
                             write_descriptor_sets, 0, nullptr);

  // gpu_shared_memory_double_buffer: allocate + write the version-1 descriptor
  // set. It is IDENTICAL to the version-0 set above except binding 0 (the shared
  // memory storage buffer) points at shared memory version 1; the EDRAM and
  // compact-position bindings (if any) reference the same buffers as version 0.
  // The current version's set is selected per draw at bind time. On any failure
  // here, fall back to single-buffer behavior by leaving the v1 set null (the
  // shared memory will simply never switch off version 0 in practice - but to be
  // safe the bind path treats a null v1 set as "use version 0").
  if (shared_memory_double_buffer_active) {
    VkDescriptorSetAllocateInfo v1_allocate_info;
    v1_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    v1_allocate_info.pNext = nullptr;
    v1_allocate_info.descriptorPool = shared_memory_and_edram_descriptor_pool_;
    v1_allocate_info.descriptorSetCount = 1;
    v1_allocate_info.pSetLayouts = &descriptor_set_layout_shared_memory_and_edram_;
    if (dfn.vkAllocateDescriptorSets(
            device, &v1_allocate_info,
            &shared_memory_and_edram_descriptor_set_v1_) != VK_SUCCESS) {
      XELOGW(
          "gpu_shared_memory_double_buffer: failed to allocate the version-1 "
          "shared memory descriptor set; double-buffer bind will fall back to "
          "version 0");
      shared_memory_and_edram_descriptor_set_v1_ = VK_NULL_HANDLE;
    } else {
      // Binding 0 buffer infos -> shared memory version 1.
      VkDescriptorBufferInfo
          v1_shared_memory_descriptor_buffers_info[SharedMemory::kBufferSize /
                                                   (128 << 20)];
      for (uint32_t i = 0; i < shared_memory_binding_count; ++i) {
        VkDescriptorBufferInfo& info = v1_shared_memory_descriptor_buffers_info[i];
        info.buffer = shared_memory_->buffer_version(1);
        info.offset = shared_memory_binding_range * i;
        info.range = shared_memory_binding_range;
      }
      // Reuse the version-0 write structs, just redirect dstSet + binding-0
      // buffer info. The edram/compact-pos pBufferInfo already point at the same
      // (version-independent) buffers, so only their dstSet needs to change.
      write_descriptor_set_shared_memory.dstSet =
          shared_memory_and_edram_descriptor_set_v1_;
      write_descriptor_set_shared_memory.pBufferInfo =
          v1_shared_memory_descriptor_buffers_info;
      if (edram_fragment_shader_interlock) {
        write_descriptor_sets[1].dstSet =
            shared_memory_and_edram_descriptor_set_v1_;
      }
      if (cvars::gpu_binning_deinterleave_pos) {
        write_descriptor_sets[shared_memory_and_edram_write_count - 1].dstSet =
            shared_memory_and_edram_descriptor_set_v1_;
      }
      dfn.vkUpdateDescriptorSets(device, shared_memory_and_edram_write_count,
                                 write_descriptor_sets, 0, nullptr);
    }
  }

  // BRICK 1 native bindless render path (gpu_native_render_path). Enabled only
  // when the cvar is set AND the device enabled the descriptor-indexing features
  // it needs - kept in exact sync with SpirvShaderTranslator::Features::
  // bindless_textures so the translated shader interface matches the pipeline
  // layout + bound descriptors. On failure it stays inactive (legacy per-draw
  // path).
  native_render_path_active_ =
      cvars::gpu_native_render_path &&
      vulkan_device->properties().runtimeDescriptorArray &&
      vulkan_device->properties().descriptorBindingPartiallyBound &&
      vulkan_device->properties().descriptorBindingSampledImageUpdateAfterBind;
  if (native_render_path_active_) {
    if (InitializeBindlessDescriptors()) {
      XELOGI(
          "VulkanCommandProcessor: BRICK 1 native bindless render path ACTIVE "
          "(gpu_native_render_path) - textures/samplers via one global "
          "descriptor set, bound once per command buffer");
    } else {
      XELOGE(
          "VulkanCommandProcessor: failed to initialize the bindless descriptor "
          "set for gpu_native_render_path - falling back to the legacy per-draw "
          "path");
      native_render_path_active_ = false;
    }
  }

  // Swap objects.

  // Gamma ramp, either device-local and host-visible at once, or separate
  // device-local texel buffer and host-visible upload buffer.
  gamma_ramp_256_entry_table_current_frame_ = UINT32_MAX;
  gamma_ramp_pwl_current_frame_ = UINT32_MAX;
  // Try to create a device-local host-visible buffer first, to skip copying.
  constexpr uint32_t kGammaRampSize256EntryTable = sizeof(uint32_t) * 256;
  constexpr uint32_t kGammaRampSizePWL = sizeof(uint16_t) * 2 * 3 * 128;
  constexpr uint32_t kGammaRampSize =
      kGammaRampSize256EntryTable + kGammaRampSizePWL;
  VkBufferCreateInfo gamma_ramp_host_visible_buffer_create_info;
  gamma_ramp_host_visible_buffer_create_info.sType =
      VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  gamma_ramp_host_visible_buffer_create_info.pNext = nullptr;
  gamma_ramp_host_visible_buffer_create_info.flags = 0;
  gamma_ramp_host_visible_buffer_create_info.size =
      kGammaRampSize * kMaxFramesInFlight;
  gamma_ramp_host_visible_buffer_create_info.usage =
      VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
  gamma_ramp_host_visible_buffer_create_info.sharingMode =
      VK_SHARING_MODE_EXCLUSIVE;
  gamma_ramp_host_visible_buffer_create_info.queueFamilyIndexCount = 0;
  gamma_ramp_host_visible_buffer_create_info.pQueueFamilyIndices = nullptr;
  if (dfn.vkCreateBuffer(device, &gamma_ramp_host_visible_buffer_create_info,
                         nullptr, &gamma_ramp_buffer_) == VK_SUCCESS) {
    bool use_gamma_ramp_host_visible_buffer = false;
    VkMemoryRequirements gamma_ramp_host_visible_buffer_memory_requirements;
    dfn.vkGetBufferMemoryRequirements(
        device, gamma_ramp_buffer_,
        &gamma_ramp_host_visible_buffer_memory_requirements);
    uint32_t gamma_ramp_host_visible_buffer_memory_types =
        gamma_ramp_host_visible_buffer_memory_requirements.memoryTypeBits &
        (vulkan_device->memory_types().device_local &
         vulkan_device->memory_types().host_visible);
    VkMemoryAllocateInfo gamma_ramp_host_visible_buffer_memory_allocate_info;
    // Prefer a host-uncached (because it's write-only) memory type, but try a
    // host-cached host-visible device-local one as well.
    if (xe::bit_scan_forward(
            gamma_ramp_host_visible_buffer_memory_types &
                ~vulkan_device->memory_types().host_cached,
            &(gamma_ramp_host_visible_buffer_memory_allocate_info
                  .memoryTypeIndex)) ||
        xe::bit_scan_forward(
            gamma_ramp_host_visible_buffer_memory_types,
            &(gamma_ramp_host_visible_buffer_memory_allocate_info
                  .memoryTypeIndex))) {
      VkMemoryAllocateInfo*
          gamma_ramp_host_visible_buffer_memory_allocate_info_last =
              &gamma_ramp_host_visible_buffer_memory_allocate_info;
      gamma_ramp_host_visible_buffer_memory_allocate_info.sType =
          VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      gamma_ramp_host_visible_buffer_memory_allocate_info.pNext = nullptr;
      gamma_ramp_host_visible_buffer_memory_allocate_info.allocationSize =
          gamma_ramp_host_visible_buffer_memory_requirements.size;
      VkMemoryDedicatedAllocateInfo
          gamma_ramp_host_visible_buffer_memory_dedicated_allocate_info;
      if (vulkan_device->extensions().ext_1_1_KHR_dedicated_allocation) {
        gamma_ramp_host_visible_buffer_memory_allocate_info_last->pNext =
            &gamma_ramp_host_visible_buffer_memory_dedicated_allocate_info;
        gamma_ramp_host_visible_buffer_memory_allocate_info_last =
            reinterpret_cast<VkMemoryAllocateInfo*>(
                &gamma_ramp_host_visible_buffer_memory_dedicated_allocate_info);
        gamma_ramp_host_visible_buffer_memory_dedicated_allocate_info.sType =
            VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        gamma_ramp_host_visible_buffer_memory_dedicated_allocate_info.pNext =
            nullptr;
        gamma_ramp_host_visible_buffer_memory_dedicated_allocate_info.image =
            VK_NULL_HANDLE;
        gamma_ramp_host_visible_buffer_memory_dedicated_allocate_info.buffer =
            gamma_ramp_buffer_;
      }
      if (dfn.vkAllocateMemory(
              device, &gamma_ramp_host_visible_buffer_memory_allocate_info,
              nullptr, &gamma_ramp_buffer_memory_) == VK_SUCCESS) {
        if (dfn.vkBindBufferMemory(device, gamma_ramp_buffer_,
                                   gamma_ramp_buffer_memory_,
                                   0) == VK_SUCCESS) {
          if (dfn.vkMapMemory(device, gamma_ramp_buffer_memory_, 0,
                              VK_WHOLE_SIZE, 0,
                              &gamma_ramp_upload_mapping_) == VK_SUCCESS) {
            use_gamma_ramp_host_visible_buffer = true;
            gamma_ramp_upload_memory_size_ =
                gamma_ramp_host_visible_buffer_memory_allocate_info
                    .allocationSize;
            gamma_ramp_upload_memory_type_ =
                gamma_ramp_host_visible_buffer_memory_allocate_info
                    .memoryTypeIndex;
          }
        }
        if (!use_gamma_ramp_host_visible_buffer) {
          dfn.vkFreeMemory(device, gamma_ramp_buffer_memory_, nullptr);
          gamma_ramp_buffer_memory_ = VK_NULL_HANDLE;
        }
      }
    }
    if (!use_gamma_ramp_host_visible_buffer) {
      dfn.vkDestroyBuffer(device, gamma_ramp_buffer_, nullptr);
      gamma_ramp_buffer_ = VK_NULL_HANDLE;
    }
  }
  if (gamma_ramp_buffer_ == VK_NULL_HANDLE) {
    // Create separate buffers for the shader and uploading.
    if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
            vulkan_device, kGammaRampSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
            ui::vulkan::util::MemoryPurpose::kDeviceLocal, gamma_ramp_buffer_,
            gamma_ramp_buffer_memory_)) {
      XELOGE("Failed to create the gamma ramp buffer");
      return false;
    }
    if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
            vulkan_device, kGammaRampSize * kMaxFramesInFlight,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            ui::vulkan::util::MemoryPurpose::kUpload, gamma_ramp_upload_buffer_,
            gamma_ramp_upload_buffer_memory_, &gamma_ramp_upload_memory_type_,
            &gamma_ramp_upload_memory_size_)) {
      XELOGE("Failed to create the gamma ramp upload buffer");
      return false;
    }
    if (dfn.vkMapMemory(device, gamma_ramp_upload_buffer_memory_, 0,
                        VK_WHOLE_SIZE, 0,
                        &gamma_ramp_upload_mapping_) != VK_SUCCESS) {
      XELOGE("Failed to map the gamma ramp upload buffer");
      return false;
    }
  }

  // Gamma ramp buffer views.
  uint32_t gamma_ramp_frame_count =
      gamma_ramp_upload_buffer_ == VK_NULL_HANDLE ? kMaxFramesInFlight : 1;
  VkBufferViewCreateInfo gamma_ramp_buffer_view_create_info;
  gamma_ramp_buffer_view_create_info.sType =
      VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
  gamma_ramp_buffer_view_create_info.pNext = nullptr;
  gamma_ramp_buffer_view_create_info.flags = 0;
  gamma_ramp_buffer_view_create_info.buffer = gamma_ramp_buffer_;
  // 256-entry table.
  gamma_ramp_buffer_view_create_info.format =
      VK_FORMAT_A2B10G10R10_UNORM_PACK32;
  gamma_ramp_buffer_view_create_info.range = kGammaRampSize256EntryTable;
  for (uint32_t i = 0; i < gamma_ramp_frame_count; ++i) {
    gamma_ramp_buffer_view_create_info.offset = kGammaRampSize * i;
    if (dfn.vkCreateBufferView(device, &gamma_ramp_buffer_view_create_info,
                               nullptr, &gamma_ramp_buffer_views_[i * 2]) !=
        VK_SUCCESS) {
      XELOGE("Failed to create a 256-entry table gamma ramp buffer view");
      return false;
    }
  }
  // Piecewise linear.
  gamma_ramp_buffer_view_create_info.format = VK_FORMAT_R16G16_UINT;
  gamma_ramp_buffer_view_create_info.range = kGammaRampSizePWL;
  for (uint32_t i = 0; i < gamma_ramp_frame_count; ++i) {
    gamma_ramp_buffer_view_create_info.offset =
        kGammaRampSize * i + kGammaRampSize256EntryTable;
    if (dfn.vkCreateBufferView(device, &gamma_ramp_buffer_view_create_info,
                               nullptr, &gamma_ramp_buffer_views_[i * 2 + 1]) !=
        VK_SUCCESS) {
      XELOGE("Failed to create a PWL gamma ramp buffer view");
      return false;
    }
  }

  // Swap descriptor set layouts.
  VkDescriptorSetLayoutBinding swap_descriptor_set_layout_binding;
  swap_descriptor_set_layout_binding.binding = 0;
  swap_descriptor_set_layout_binding.descriptorCount = 1;
  swap_descriptor_set_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  swap_descriptor_set_layout_binding.pImmutableSamplers = nullptr;
  VkDescriptorSetLayoutCreateInfo swap_descriptor_set_layout_create_info;
  swap_descriptor_set_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  swap_descriptor_set_layout_create_info.pNext = nullptr;
  swap_descriptor_set_layout_create_info.flags = 0;
  swap_descriptor_set_layout_create_info.bindingCount = 1;
  swap_descriptor_set_layout_create_info.pBindings =
      &swap_descriptor_set_layout_binding;
  swap_descriptor_set_layout_binding.descriptorType =
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &swap_descriptor_set_layout_create_info, nullptr,
          &swap_descriptor_set_layout_sampled_image_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create the presentation sampled image descriptor set "
        "layout");
    return false;
  }
  swap_descriptor_set_layout_binding.descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &swap_descriptor_set_layout_create_info, nullptr,
          &swap_descriptor_set_layout_uniform_texel_buffer_) != VK_SUCCESS) {
    XELOGE(
        "Failed to create the presentation uniform texel buffer descriptor set "
        "layout");
    return false;
  }

  // Swap descriptor pool.
  std::array<VkDescriptorPoolSize, 2> swap_descriptor_pool_sizes;
  VkDescriptorPoolCreateInfo swap_descriptor_pool_create_info;
  swap_descriptor_pool_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  swap_descriptor_pool_create_info.pNext = nullptr;
  swap_descriptor_pool_create_info.flags = 0;
  swap_descriptor_pool_create_info.maxSets = 0;
  swap_descriptor_pool_create_info.poolSizeCount = 0;
  swap_descriptor_pool_create_info.pPoolSizes =
      swap_descriptor_pool_sizes.data();
  // TODO(Triang3l): FXAA combined image and sampler sources.
  {
    VkDescriptorPoolSize& swap_descriptor_pool_size_sampled_image =
        swap_descriptor_pool_sizes[swap_descriptor_pool_create_info
                                       .poolSizeCount++];
    swap_descriptor_pool_size_sampled_image.type =
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    // Source images.
    swap_descriptor_pool_size_sampled_image.descriptorCount =
        kMaxFramesInFlight;
    swap_descriptor_pool_create_info.maxSets += kMaxFramesInFlight;
  }
  // 256-entry table and PWL gamma ramps. If the gamma ramp buffer is
  // host-visible, for multiple frames.
  uint32_t gamma_ramp_buffer_view_count = 2 * gamma_ramp_frame_count;
  {
    VkDescriptorPoolSize& swap_descriptor_pool_size_uniform_texel_buffer =
        swap_descriptor_pool_sizes[swap_descriptor_pool_create_info
                                       .poolSizeCount++];
    swap_descriptor_pool_size_uniform_texel_buffer.type =
        VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    swap_descriptor_pool_size_uniform_texel_buffer.descriptorCount =
        gamma_ramp_buffer_view_count;
    swap_descriptor_pool_create_info.maxSets += gamma_ramp_buffer_view_count;
  }
  if (dfn.vkCreateDescriptorPool(device, &swap_descriptor_pool_create_info,
                                 nullptr,
                                 &swap_descriptor_pool_) != VK_SUCCESS) {
    XELOGE("Failed to create the presentation descriptor pool");
    return false;
  }

  // Swap descriptor set allocation.
  VkDescriptorSetAllocateInfo swap_descriptor_set_allocate_info;
  swap_descriptor_set_allocate_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  swap_descriptor_set_allocate_info.pNext = nullptr;
  swap_descriptor_set_allocate_info.descriptorPool = swap_descriptor_pool_;
  swap_descriptor_set_allocate_info.descriptorSetCount = 1;
  swap_descriptor_set_allocate_info.pSetLayouts =
      &swap_descriptor_set_layout_uniform_texel_buffer_;
  for (uint32_t i = 0; i < gamma_ramp_buffer_view_count; ++i) {
    if (dfn.vkAllocateDescriptorSets(device, &swap_descriptor_set_allocate_info,
                                     &swap_descriptors_gamma_ramp_[i]) !=
        VK_SUCCESS) {
      XELOGE("Failed to allocate the gamma ramp descriptor sets");
      return false;
    }
  }
  swap_descriptor_set_allocate_info.pSetLayouts =
      &swap_descriptor_set_layout_sampled_image_;
  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    if (dfn.vkAllocateDescriptorSets(device, &swap_descriptor_set_allocate_info,
                                     &swap_descriptors_source_[i]) !=
        VK_SUCCESS) {
      XELOGE(
          "Failed to allocate the presentation source image descriptor sets");
      return false;
    }
  }

  // Gamma ramp descriptor sets.
  VkWriteDescriptorSet gamma_ramp_write_descriptor_set;
  gamma_ramp_write_descriptor_set.sType =
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  gamma_ramp_write_descriptor_set.pNext = nullptr;
  gamma_ramp_write_descriptor_set.dstBinding = 0;
  gamma_ramp_write_descriptor_set.dstArrayElement = 0;
  gamma_ramp_write_descriptor_set.descriptorCount = 1;
  gamma_ramp_write_descriptor_set.descriptorType =
      VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
  gamma_ramp_write_descriptor_set.pImageInfo = nullptr;
  gamma_ramp_write_descriptor_set.pBufferInfo = nullptr;
  for (uint32_t i = 0; i < gamma_ramp_buffer_view_count; ++i) {
    gamma_ramp_write_descriptor_set.dstSet = swap_descriptors_gamma_ramp_[i];
    gamma_ramp_write_descriptor_set.pTexelBufferView =
        &gamma_ramp_buffer_views_[i];
    dfn.vkUpdateDescriptorSets(device, 1, &gamma_ramp_write_descriptor_set, 0,
                               nullptr);
  }

  // Gamma ramp application pipeline layout.
  std::array<VkDescriptorSetLayout, kSwapApplyGammaDescriptorSetCount>
      swap_apply_gamma_descriptor_set_layouts{};
  swap_apply_gamma_descriptor_set_layouts[kSwapApplyGammaDescriptorSetRamp] =
      swap_descriptor_set_layout_uniform_texel_buffer_;
  swap_apply_gamma_descriptor_set_layouts[kSwapApplyGammaDescriptorSetSource] =
      swap_descriptor_set_layout_sampled_image_;
  VkPipelineLayoutCreateInfo swap_apply_gamma_pipeline_layout_create_info;
  swap_apply_gamma_pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  swap_apply_gamma_pipeline_layout_create_info.pNext = nullptr;
  swap_apply_gamma_pipeline_layout_create_info.flags = 0;
  swap_apply_gamma_pipeline_layout_create_info.setLayoutCount =
      uint32_t(swap_apply_gamma_descriptor_set_layouts.size());
  swap_apply_gamma_pipeline_layout_create_info.pSetLayouts =
      swap_apply_gamma_descriptor_set_layouts.data();
  swap_apply_gamma_pipeline_layout_create_info.pushConstantRangeCount = 0;
  swap_apply_gamma_pipeline_layout_create_info.pPushConstantRanges = nullptr;
  if (dfn.vkCreatePipelineLayout(
          device, &swap_apply_gamma_pipeline_layout_create_info, nullptr,
          &swap_apply_gamma_pipeline_layout_) != VK_SUCCESS) {
    XELOGE("Failed to create the gamma ramp application pipeline layout");
    return false;
  }

  // Gamma application render pass. Doesn't make assumptions about outer usage
  // (explicit barriers must be used instead) for simplicity of use in different
  // scenarios with different pipelines.
  VkAttachmentDescription swap_apply_gamma_render_pass_attachment;
  swap_apply_gamma_render_pass_attachment.flags = 0;
  swap_apply_gamma_render_pass_attachment.format =
      ui::vulkan::VulkanPresenter::kGuestOutputFormat;
  swap_apply_gamma_render_pass_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  swap_apply_gamma_render_pass_attachment.loadOp =
      VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  swap_apply_gamma_render_pass_attachment.storeOp =
      VK_ATTACHMENT_STORE_OP_STORE;
  swap_apply_gamma_render_pass_attachment.stencilLoadOp =
      VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  swap_apply_gamma_render_pass_attachment.stencilStoreOp =
      VK_ATTACHMENT_STORE_OP_DONT_CARE;
  swap_apply_gamma_render_pass_attachment.initialLayout =
      VK_IMAGE_LAYOUT_UNDEFINED;
  swap_apply_gamma_render_pass_attachment.finalLayout =
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference swap_apply_gamma_render_pass_color_attachment;
  swap_apply_gamma_render_pass_color_attachment.attachment = 0;
  swap_apply_gamma_render_pass_color_attachment.layout =
      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkSubpassDescription swap_apply_gamma_render_pass_subpass = {};
  swap_apply_gamma_render_pass_subpass.pipelineBindPoint =
      VK_PIPELINE_BIND_POINT_GRAPHICS;
  swap_apply_gamma_render_pass_subpass.colorAttachmentCount = 1;
  swap_apply_gamma_render_pass_subpass.pColorAttachments =
      &swap_apply_gamma_render_pass_color_attachment;
  VkSubpassDependency swap_apply_gamma_render_pass_dependencies[2];
  for (uint32_t i = 0; i < 2; ++i) {
    VkSubpassDependency& swap_apply_gamma_render_pass_dependency =
        swap_apply_gamma_render_pass_dependencies[i];
    swap_apply_gamma_render_pass_dependency.srcSubpass =
        i ? 0 : VK_SUBPASS_EXTERNAL;
    swap_apply_gamma_render_pass_dependency.dstSubpass =
        i ? VK_SUBPASS_EXTERNAL : 0;
    swap_apply_gamma_render_pass_dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    swap_apply_gamma_render_pass_dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    swap_apply_gamma_render_pass_dependency.srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swap_apply_gamma_render_pass_dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    swap_apply_gamma_render_pass_dependency.dependencyFlags =
        VK_DEPENDENCY_BY_REGION_BIT;
  }
  VkRenderPassCreateInfo swap_apply_gamma_render_pass_create_info;
  swap_apply_gamma_render_pass_create_info.sType =
      VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  swap_apply_gamma_render_pass_create_info.pNext = nullptr;
  swap_apply_gamma_render_pass_create_info.flags = 0;
  swap_apply_gamma_render_pass_create_info.attachmentCount = 1;
  swap_apply_gamma_render_pass_create_info.pAttachments =
      &swap_apply_gamma_render_pass_attachment;
  swap_apply_gamma_render_pass_create_info.subpassCount = 1;
  swap_apply_gamma_render_pass_create_info.pSubpasses =
      &swap_apply_gamma_render_pass_subpass;
  swap_apply_gamma_render_pass_create_info.dependencyCount =
      uint32_t(xe::countof(swap_apply_gamma_render_pass_dependencies));
  swap_apply_gamma_render_pass_create_info.pDependencies =
      swap_apply_gamma_render_pass_dependencies;
  if (dfn.vkCreateRenderPass(device, &swap_apply_gamma_render_pass_create_info,
                             nullptr,
                             &swap_apply_gamma_render_pass_) != VK_SUCCESS) {
    XELOGE("Failed to create the gamma ramp application render pass");
    return false;
  }

  // Gamma ramp application pipeline.
  // Using a graphics pipeline, not a compute one, because storage image support
  // is optional for VK_FORMAT_A2B10G10R10_UNORM_PACK32.

  enum SwapApplyGammaPixelShader {
    kSwapApplyGammaPixelShader256EntryTable,
    kSwapApplyGammaPixelShaderPWL,

    kSwapApplyGammaPixelShaderCount,
  };
  std::array<VkShaderModule, kSwapApplyGammaPixelShaderCount>
      swap_apply_gamma_pixel_shaders{};
  bool swap_apply_gamma_pixel_shaders_created =
      (swap_apply_gamma_pixel_shaders[kSwapApplyGammaPixelShader256EntryTable] =
           ui::vulkan::util::CreateShaderModule(
               vulkan_device, shaders::apply_gamma_table_ps,
               sizeof(shaders::apply_gamma_table_ps))) != VK_NULL_HANDLE &&
      (swap_apply_gamma_pixel_shaders[kSwapApplyGammaPixelShaderPWL] =
           ui::vulkan::util::CreateShaderModule(
               vulkan_device, shaders::apply_gamma_pwl_ps,
               sizeof(shaders::apply_gamma_pwl_ps))) != VK_NULL_HANDLE;
  if (!swap_apply_gamma_pixel_shaders_created) {
    XELOGE("Failed to create the gamma ramp application pixel shader modules");
    for (VkShaderModule swap_apply_gamma_pixel_shader :
         swap_apply_gamma_pixel_shaders) {
      if (swap_apply_gamma_pixel_shader != VK_NULL_HANDLE) {
        dfn.vkDestroyShaderModule(device, swap_apply_gamma_pixel_shader,
                                  nullptr);
      }
    }
    return false;
  }

  VkPipelineShaderStageCreateInfo swap_apply_gamma_pipeline_stages[2];
  swap_apply_gamma_pipeline_stages[0].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  swap_apply_gamma_pipeline_stages[0].pNext = nullptr;
  swap_apply_gamma_pipeline_stages[0].flags = 0;
  swap_apply_gamma_pipeline_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  swap_apply_gamma_pipeline_stages[0].module =
      ui::vulkan::util::CreateShaderModule(vulkan_device,
                                           shaders::fullscreen_cw_vs,
                                           sizeof(shaders::fullscreen_cw_vs));
  if (swap_apply_gamma_pipeline_stages[0].module == VK_NULL_HANDLE) {
    XELOGE("Failed to create the gamma ramp application vertex shader module");
    for (VkShaderModule swap_apply_gamma_pixel_shader :
         swap_apply_gamma_pixel_shaders) {
      assert_true(swap_apply_gamma_pixel_shader != VK_NULL_HANDLE);
      dfn.vkDestroyShaderModule(device, swap_apply_gamma_pixel_shader, nullptr);
    }
  }
  swap_apply_gamma_pipeline_stages[0].pName = "main";
  swap_apply_gamma_pipeline_stages[0].pSpecializationInfo = nullptr;
  swap_apply_gamma_pipeline_stages[1].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  swap_apply_gamma_pipeline_stages[1].pNext = nullptr;
  swap_apply_gamma_pipeline_stages[1].flags = 0;
  swap_apply_gamma_pipeline_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  // The fragment shader module will be specified later.
  swap_apply_gamma_pipeline_stages[1].pName = "main";
  swap_apply_gamma_pipeline_stages[1].pSpecializationInfo = nullptr;

  VkPipelineVertexInputStateCreateInfo
      swap_apply_gamma_pipeline_vertex_input_state = {};
  swap_apply_gamma_pipeline_vertex_input_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo
      swap_apply_gamma_pipeline_input_assembly_state;
  swap_apply_gamma_pipeline_input_assembly_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  swap_apply_gamma_pipeline_input_assembly_state.pNext = nullptr;
  swap_apply_gamma_pipeline_input_assembly_state.flags = 0;
  swap_apply_gamma_pipeline_input_assembly_state.topology =
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  swap_apply_gamma_pipeline_input_assembly_state.primitiveRestartEnable =
      VK_FALSE;

  VkPipelineViewportStateCreateInfo swap_apply_gamma_pipeline_viewport_state;
  swap_apply_gamma_pipeline_viewport_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  swap_apply_gamma_pipeline_viewport_state.pNext = nullptr;
  swap_apply_gamma_pipeline_viewport_state.flags = 0;
  swap_apply_gamma_pipeline_viewport_state.viewportCount = 1;
  swap_apply_gamma_pipeline_viewport_state.pViewports = nullptr;
  swap_apply_gamma_pipeline_viewport_state.scissorCount = 1;
  swap_apply_gamma_pipeline_viewport_state.pScissors = nullptr;

  VkPipelineRasterizationStateCreateInfo
      swap_apply_gamma_pipeline_rasterization_state = {};
  swap_apply_gamma_pipeline_rasterization_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  swap_apply_gamma_pipeline_rasterization_state.polygonMode =
      VK_POLYGON_MODE_FILL;
  swap_apply_gamma_pipeline_rasterization_state.cullMode = VK_CULL_MODE_NONE;
  swap_apply_gamma_pipeline_rasterization_state.frontFace =
      VK_FRONT_FACE_CLOCKWISE;
  swap_apply_gamma_pipeline_rasterization_state.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo
      swap_apply_gamma_pipeline_multisample_state = {};
  swap_apply_gamma_pipeline_multisample_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  swap_apply_gamma_pipeline_multisample_state.rasterizationSamples =
      VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState
      swap_apply_gamma_pipeline_color_blend_attachment_state = {};
  swap_apply_gamma_pipeline_color_blend_attachment_state.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo
      swap_apply_gamma_pipeline_color_blend_state = {};
  swap_apply_gamma_pipeline_color_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  swap_apply_gamma_pipeline_color_blend_state.attachmentCount = 1;
  swap_apply_gamma_pipeline_color_blend_state.pAttachments =
      &swap_apply_gamma_pipeline_color_blend_attachment_state;

  static const VkDynamicState kSwapApplyGammaPipelineDynamicStates[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  VkPipelineDynamicStateCreateInfo swap_apply_gamma_pipeline_dynamic_state;
  swap_apply_gamma_pipeline_dynamic_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  swap_apply_gamma_pipeline_dynamic_state.pNext = nullptr;
  swap_apply_gamma_pipeline_dynamic_state.flags = 0;
  swap_apply_gamma_pipeline_dynamic_state.dynamicStateCount =
      uint32_t(xe::countof(kSwapApplyGammaPipelineDynamicStates));
  swap_apply_gamma_pipeline_dynamic_state.pDynamicStates =
      kSwapApplyGammaPipelineDynamicStates;

  VkGraphicsPipelineCreateInfo swap_apply_gamma_pipeline_create_info;
  swap_apply_gamma_pipeline_create_info.sType =
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  swap_apply_gamma_pipeline_create_info.pNext = nullptr;
  swap_apply_gamma_pipeline_create_info.flags = 0;
  swap_apply_gamma_pipeline_create_info.stageCount =
      uint32_t(xe::countof(swap_apply_gamma_pipeline_stages));
  swap_apply_gamma_pipeline_create_info.pStages =
      swap_apply_gamma_pipeline_stages;
  swap_apply_gamma_pipeline_create_info.pVertexInputState =
      &swap_apply_gamma_pipeline_vertex_input_state;
  swap_apply_gamma_pipeline_create_info.pInputAssemblyState =
      &swap_apply_gamma_pipeline_input_assembly_state;
  swap_apply_gamma_pipeline_create_info.pTessellationState = nullptr;
  swap_apply_gamma_pipeline_create_info.pViewportState =
      &swap_apply_gamma_pipeline_viewport_state;
  swap_apply_gamma_pipeline_create_info.pRasterizationState =
      &swap_apply_gamma_pipeline_rasterization_state;
  swap_apply_gamma_pipeline_create_info.pMultisampleState =
      &swap_apply_gamma_pipeline_multisample_state;
  swap_apply_gamma_pipeline_create_info.pDepthStencilState = nullptr;
  swap_apply_gamma_pipeline_create_info.pColorBlendState =
      &swap_apply_gamma_pipeline_color_blend_state;
  swap_apply_gamma_pipeline_create_info.pDynamicState =
      &swap_apply_gamma_pipeline_dynamic_state;
  swap_apply_gamma_pipeline_create_info.layout =
      swap_apply_gamma_pipeline_layout_;
  swap_apply_gamma_pipeline_create_info.renderPass =
      swap_apply_gamma_render_pass_;
  swap_apply_gamma_pipeline_create_info.subpass = 0;
  swap_apply_gamma_pipeline_create_info.basePipelineHandle = VK_NULL_HANDLE;
  swap_apply_gamma_pipeline_create_info.basePipelineIndex = -1;
  swap_apply_gamma_pipeline_stages[1].module =
      swap_apply_gamma_pixel_shaders[kSwapApplyGammaPixelShader256EntryTable];
  VkResult swap_apply_gamma_pipeline_256_entry_table_create_result =
      dfn.vkCreateGraphicsPipelines(
          device, VK_NULL_HANDLE, 1, &swap_apply_gamma_pipeline_create_info,
          nullptr, &swap_apply_gamma_256_entry_table_pipeline_);
  swap_apply_gamma_pipeline_stages[1].module =
      swap_apply_gamma_pixel_shaders[kSwapApplyGammaPixelShaderPWL];
  VkResult swap_apply_gamma_pipeline_pwl_create_result =
      dfn.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                    &swap_apply_gamma_pipeline_create_info,
                                    nullptr, &swap_apply_gamma_pwl_pipeline_);
  dfn.vkDestroyShaderModule(device, swap_apply_gamma_pipeline_stages[0].module,
                            nullptr);
  for (VkShaderModule swap_apply_gamma_pixel_shader :
       swap_apply_gamma_pixel_shaders) {
    assert_true(swap_apply_gamma_pixel_shader != VK_NULL_HANDLE);
    dfn.vkDestroyShaderModule(device, swap_apply_gamma_pixel_shader, nullptr);
  }
  if (swap_apply_gamma_pipeline_256_entry_table_create_result != VK_SUCCESS ||
      swap_apply_gamma_pipeline_pwl_create_result != VK_SUCCESS) {
    XELOGE("Failed to create the gamma ramp application pipelines");
    return false;
  }

  // Just not to expose uninitialized memory.
  std::memset(&system_constants_, 0, sizeof(system_constants_));

  return true;
}

void VulkanCommandProcessor::ShutdownContext() {
  AwaitAllQueueOperationsCompletion();

  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyQueryPool, device,
                                         gpu_timestamp_pool_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyQueryPool, device,
                                         gpu_pass_timestamp_pool_);

  DestroyScratchBuffer();

  for (SwapFramebuffer& swap_framebuffer : swap_framebuffers_) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device,
                                           swap_framebuffer.framebuffer);
  }

  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                         swap_apply_gamma_pwl_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyPipeline, device,
      swap_apply_gamma_256_entry_table_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyRenderPass, device,
                                         swap_apply_gamma_render_pass_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         swap_apply_gamma_pipeline_layout_);

  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device,
                                         swap_descriptor_pool_);

  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      swap_descriptor_set_layout_uniform_texel_buffer_);
  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      swap_descriptor_set_layout_sampled_image_);
  for (VkBufferView& gamma_ramp_buffer_view : gamma_ramp_buffer_views_) {
    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBufferView, device,
                                           gamma_ramp_buffer_view);
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                         gamma_ramp_upload_buffer_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                         gamma_ramp_upload_buffer_memory_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                         gamma_ramp_buffer_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                         gamma_ramp_buffer_memory_);

  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorPool, device,
      shared_memory_and_edram_descriptor_pool_);
  // gpu_shared_memory_double_buffer: the version-1 set is freed implicitly with
  // the pool above; just drop the handle and the cached bound version.
  shared_memory_and_edram_descriptor_set_v1_ = VK_NULL_HANDLE;
  shared_memory_descriptor_set_bound_version_ = 0;
  // R2: the dynamic constants set is freed implicitly with its pool.
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device,
                                         constants_dynamic_descriptor_pool_);
  constants_dynamic_descriptor_set_ = VK_NULL_HANDLE;

  // BRICK 1 native bindless render path: the set is freed implicitly with its
  // pool; the pipeline layout is destroyed by the pipeline_layouts_ loop below
  // (it is stored there under the fixed native key).
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorPool, device,
                                         bindless_descriptor_pool_);
  bindless_descriptor_set_ = VK_NULL_HANDLE;
  native_pipeline_layout_ = nullptr;
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout,
                                         device,
                                         bindless_descriptor_set_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout,
                                         device,
                                         bindless_empty_descriptor_set_layout_);
  bindless_image_view_slots_.clear();
  bindless_image_free_slots_.clear();
  bindless_sampler_slots_.clear();
  bindless_sampler_free_slots_.clear();

  texture_cache_.reset();

  pipeline_cache_.reset();

  render_target_cache_.reset();

  primitive_processor_.reset();

  shared_memory_.reset();

  ClearTransientDescriptorPools();

  for (const auto& pipeline_layout_pair : pipeline_layouts_) {
    dfn.vkDestroyPipelineLayout(
        device, pipeline_layout_pair.second.GetPipelineLayout(), nullptr);
  }
  pipeline_layouts_.clear();
  for (const auto& descriptor_set_layout_pair :
       descriptor_set_layouts_textures_) {
    dfn.vkDestroyDescriptorSetLayout(device, descriptor_set_layout_pair.second,
                                     nullptr);
  }
  descriptor_set_layouts_textures_.clear();

  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      descriptor_set_layout_shared_memory_and_edram_);
  for (VkDescriptorSetLayout& descriptor_set_layout_single_transient :
       descriptor_set_layouts_single_transient_) {
    ui::vulkan::util::DestroyAndNullHandle(
        dfn.vkDestroyDescriptorSetLayout, device,
        descriptor_set_layout_single_transient);
  }
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout,
                                         device,
                                         descriptor_set_layout_constants_);
  ui::vulkan::util::DestroyAndNullHandle(
      dfn.vkDestroyDescriptorSetLayout, device,
      descriptor_set_layout_constants_dynamic_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout,
                                         device, descriptor_set_layout_empty_);

  uniform_buffer_pool_.reset();
  indirect_buffer_pool_.reset();
  cull_index_buffer_pool_.reset();
  // R2: release the constant arena rings while the device is still valid (no-op
  // when the cvar left them uninitialized).
  for (auto& ring : dynamic_constants_rings_) {
    ring.Shutdown();
  }
  // G1-lite: release the compact position stream ring (no-op when the cvar
  // left it uninitialized).
  compact_pos_ring_.Shutdown();

  sparse_bind_wait_stage_mask_ = 0;
  sparse_buffer_binds_.clear();
  sparse_memory_binds_.clear();

  deferred_command_buffer_.Reset();
  // BD input-attachment merge: the captured producer BeginRenderPass position is
  // a command_stream_ index, invalid after a Reset (a stale index -> out-of-bounds
  // patch -> heap corruption). Invalidate it together with the stream.
  feedback_producer_begin_pos_ = SIZE_MAX;
  feedback_merge_active_ = false;
  // gpu_vulkan_retro_depth_none: same invalidation for the retro depth-none
  // captured begin position.
  retro_depth_begin_pos_ = SIZE_MAX;
  for (const auto& command_buffer_pair : command_buffers_submitted_) {
    dfn.vkDestroyCommandPool(device, command_buffer_pair.second.pool, nullptr);
  }
  command_buffers_submitted_.clear();
  for (const CommandBuffer& command_buffer : command_buffers_writable_) {
    dfn.vkDestroyCommandPool(device, command_buffer.pool, nullptr);
  }
  command_buffers_writable_.clear();

  for (const auto& destroy_pair : destroy_framebuffers_) {
    dfn.vkDestroyFramebuffer(device, destroy_pair.second, nullptr);
  }
  destroy_framebuffers_.clear();
  for (const auto& destroy_pair : destroy_buffers_) {
    dfn.vkDestroyBuffer(device, destroy_pair.second, nullptr);
  }
  destroy_buffers_.clear();
  for (const auto& destroy_pair : destroy_memory_) {
    dfn.vkFreeMemory(device, destroy_pair.second, nullptr);
  }
  destroy_memory_.clear();

  std::memset(closed_frame_submissions_, 0, sizeof(closed_frame_submissions_));
  frame_completed_ = 0;
  frame_current_ = 1;
  frame_open_ = false;

  for (const auto& semaphore : submissions_in_flight_semaphores_) {
    dfn.vkDestroySemaphore(device, semaphore.second, nullptr);
  }
  submissions_in_flight_semaphores_.clear();
  current_submission_wait_stage_masks_.clear();
  for (VkSemaphore semaphore : current_submission_wait_semaphores_) {
    dfn.vkDestroySemaphore(device, semaphore, nullptr);
  }
  current_submission_wait_semaphores_.clear();
  submission_open_ = false;

  for (VkSemaphore semaphore : semaphores_free_) {
    dfn.vkDestroySemaphore(device, semaphore, nullptr);
  }
  semaphores_free_.clear();

  device_lost_ = false;

  CommandProcessor::ShutdownContext();
}

void VulkanCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  CommandProcessor::WriteRegister(index, value);

  if (index >= XE_GPU_REG_SHADER_CONSTANT_000_X &&
      index <= XE_GPU_REG_SHADER_CONSTANT_511_W) {
    if (frame_open_) {
      uint32_t float_constant_index =
          (index - XE_GPU_REG_SHADER_CONSTANT_000_X) >> 2;
      if (float_constant_index >= 256) {
        float_constant_index -= 256;
        if (current_float_constant_map_pixel_[float_constant_index >> 6] &
            (1ull << (float_constant_index & 63))) {
          current_constant_buffers_up_to_date_ &= ~(
              UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatPixel);
        }
      } else {
        if (current_float_constant_map_vertex_[float_constant_index >> 6] &
            (1ull << (float_constant_index & 63))) {
          current_constant_buffers_up_to_date_ &= ~(
              UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatVertex);
        }
      }
    }
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_LOOP_31) {
    current_constant_buffers_up_to_date_ &=
        ~(UINT32_C(1) << SpirvShaderTranslator::kConstantBufferBoolLoop);
  } else if (index >= XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 &&
             index <= XE_GPU_REG_SHADER_CONSTANT_FETCH_31_5) {
    current_constant_buffers_up_to_date_ &=
        ~(UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch);
    if (texture_cache_) {
      texture_cache_->TextureFetchConstantWritten(
          (index - XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0) / 6);
    }
  }
}

void VulkanCommandProcessor::SparseBindBuffer(
    VkBuffer buffer, uint32_t bind_count, const VkSparseMemoryBind* binds,
    VkPipelineStageFlags wait_stage_mask) {
  if (!bind_count) {
    return;
  }
  SparseBufferBind& buffer_bind = sparse_buffer_binds_.emplace_back();
  buffer_bind.buffer = buffer;
  buffer_bind.bind_offset = sparse_memory_binds_.size();
  buffer_bind.bind_count = bind_count;
  sparse_memory_binds_.reserve(sparse_memory_binds_.size() + bind_count);
  sparse_memory_binds_.insert(sparse_memory_binds_.end(), binds,
                              binds + bind_count);
  sparse_bind_wait_stage_mask_ |= wait_stage_mask;
}

void VulkanCommandProcessor::OnGammaRamp256EntryTableValueWritten() {
  gamma_ramp_256_entry_table_current_frame_ = UINT32_MAX;
}

void VulkanCommandProcessor::OnGammaRampPWLValueWritten() {
  gamma_ramp_pwl_current_frame_ = UINT32_MAX;
}

bool VulkanCommandProcessor::ReadbackSharedMemoryRange(uint32_t address,
                                                       uint32_t length,
                                                       const char* label,
                                                       bool log_checksum,
                                                       bool copy_to_guest,
                                                       SharedMemoryReadbackStats*
                                                           stats) {
  if (stats) {
    *stats = SharedMemoryReadbackStats();
  }
  if (!length) {
    return true;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  VkBuffer readback_buffer = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory = VK_NULL_HANDLE;
  uint32_t readback_memory_type = UINT32_MAX;
  VkDeviceSize readback_memory_size = 0;
  if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
          vulkan_device, length, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          ui::vulkan::util::MemoryPurpose::kReadback, readback_buffer,
          readback_memory, &readback_memory_type, &readback_memory_size)) {
    XELOGE(
        "GPU {} trace: failed to create Vulkan readback buffer for "
        "address={:08X} length={:08X}",
        label, address, length);
    return false;
  }

  shared_memory_->Use(VulkanSharedMemory::Usage::kRead);
  SubmitBarriers(true);
  VkBufferCopy readback_region;
  readback_region.srcOffset = address;
  readback_region.dstOffset = 0;
  readback_region.size = length;
  deferred_command_buffer_.CmdVkCopyBuffer(shared_memory_->buffer(),
                                           readback_buffer, 1,
                                           &readback_region);
  PushBufferMemoryBarrier(readback_buffer, 0, VK_WHOLE_SIZE,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_HOST_BIT,
                          VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_ACCESS_HOST_READ_BIT);

  bool succeeded = false;
  if (AwaitAllQueueOperationsCompletion()) {
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
      if (log_checksum || stats) {
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
        for (uint64_t offset = 0; offset + sizeof(uint32_t) <= length;
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
        uint32_t score =
            nonzero_samples ? (nonzero_samples + varying_samples * 16) : 0;
        bool repeated_first_sample =
            samples >= 16 &&
            uint64_t(first_sample_matches) * 100 >= uint64_t(samples) * 90;
        bool low_variation =
            samples >= 16 &&
            uint64_t(varying_samples) * 100 <= uint64_t(samples) * 6;
        bool clear_like = repeated_first_sample || low_variation;
        if (stats) {
          stats->samples = samples;
          stats->nonzero_samples = nonzero_samples;
          stats->varying_samples = varying_samples;
          stats->first_sample_value = first_sample_value;
          stats->first_sample_matches = first_sample_matches;
          stats->first_nonzero_value = first_nonzero_value;
          stats->low_variation = low_variation;
          stats->clear_like = clear_like;
          stats->checksum = checksum;
          stats->score = score;
        }
        uint32_t first_words[8] = {};
        uint32_t first_word_count =
            uint32_t(std::min<size_t>(xe::countof(first_words), length / 4));
        for (uint32_t i = 0; i < first_word_count; ++i) {
          std::memcpy(&first_words[i], bytes + i * sizeof(uint32_t),
                      sizeof(uint32_t));
        }
        XELOGI(
            "GPU {} trace: shared-memory checksum address={:08X} "
            "length={:08X} stride={} samples={} nonzero={} varying={} "
            "score={} clear_like={} low_variation={} first_sample={:08X} "
            "first_sample_matches={} checksum={:016X} "
            "first_nonzero={} first_nonzero_value={:08X} "
            "first={:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
            label, address, length, kSampleStride, samples, nonzero_samples,
            varying_samples, score, clear_like, low_variation,
            first_sample_value,
            first_sample_matches, checksum,
            first_nonzero_offset == UINT64_MAX ? -1
                                               : int64_t(first_nonzero_offset),
            first_nonzero_value, first_words[0], first_words[1],
            first_words[2], first_words[3], first_words[4], first_words[5],
            first_words[6], first_words[7]);
      }
      if (copy_to_guest) {
        std::memcpy(memory_->TranslatePhysical(address), mapping, length);
      }
      dfn.vkUnmapMemory(device, readback_memory);
      succeeded = true;
    } else {
      XELOGE(
          "GPU {} trace: failed to map Vulkan readback memory for "
          "address={:08X} length={:08X}",
          label, address, length);
    }
  } else {
    XELOGE(
        "GPU {} trace: failed to wait for Vulkan shared-memory readback "
        "address={:08X} length={:08X}",
        label, address, length);
  }
  dfn.vkDestroyBuffer(device, readback_buffer, nullptr);
  dfn.vkFreeMemory(device, readback_memory, nullptr);
  return succeeded;
}

bool VulkanCommandProcessor::TraceTextureSourceChecksums(
    uint32_t used_texture_mask, const char* stage_label, uint64_t shader_hash) {
  if (!cvars::vulkan_trace_texture_source_checksum || !used_texture_mask) {
    return true;
  }
  if (!TraceHashMatchesFilter(
          shader_hash, cvars::vulkan_trace_texture_source_shader_filter)) {
    return true;
  }

  std::vector<VulkanTextureCache::ActiveTextureSourceRange> source_ranges;
  texture_cache_->CollectActiveTextureSourceRanges(used_texture_mask,
                                                   source_ranges);
  for (const VulkanTextureCache::ActiveTextureSourceRange& source :
       source_ranges) {
    if (!source.base_length) {
      continue;
    }
    if (!ShouldTraceVulkanTextureSourceChecksum()) {
      break;
    }
    XELOGI(
        "GPU texture-source trace: stage={} fetch={} shader={:016X} "
        "kind=base address={:08X} length={:08X} dim={} fmt={} "
        "size={}x{}x{} pitch={} tiled={} packed_mips={} scaled={} "
        "has_unsigned={} has_signed={}",
        stage_label, source.fetch_index, shader_hash, source.base_address,
        source.base_length, uint32_t(source.dimension),
        uint32_t(source.format), source.width, source.height,
        source.depth_or_array_size, source.pitch, source.tiled,
        source.packed_mips, source.scaled, source.has_unsigned,
        source.has_signed);
    ReadbackSharedMemoryRange(source.base_address, source.base_length,
                              "texture-source", true, false, nullptr);
    if (!BeginSubmission(true)) {
      XELOGE(
          "GPU texture-source trace: failed to reopen submission after "
          "base readback stage={} fetch={} address={:08X}",
          stage_label, source.fetch_index, source.base_address);
      return false;
    }

    if (source.mip_length && ShouldTraceVulkanTextureSourceChecksum()) {
      XELOGI(
          "GPU texture-source trace: stage={} fetch={} shader={:016X} "
          "kind=mips address={:08X} length={:08X} dim={} fmt={} "
          "size={}x{}x{} pitch={} tiled={} packed_mips={} scaled={} "
          "has_unsigned={} has_signed={}",
          stage_label, source.fetch_index, shader_hash, source.mip_address,
          source.mip_length, uint32_t(source.dimension),
          uint32_t(source.format), source.width, source.height,
          source.depth_or_array_size, source.pitch, source.tiled,
          source.packed_mips, source.scaled, source.has_unsigned,
          source.has_signed);
      ReadbackSharedMemoryRange(source.mip_address, source.mip_length,
                                "texture-source", true, false, nullptr);
      if (!BeginSubmission(true)) {
        XELOGE(
            "GPU texture-source trace: failed to reopen submission after "
            "mip readback stage={} fetch={} address={:08X}",
            stage_label, source.fetch_index, source.mip_address);
        return false;
      }
    }
  }
  return true;
}

void VulkanCommandProcessor::TraceShaderConstants(
    const VulkanShader& shader, const char* stage_label, bool is_pixel_shader) {
  if (!TraceHashMatchesFilter(
          shader.ucode_data_hash(),
          cvars::vulkan_trace_shader_constants_shader_filter)) {
    return;
  }
  if (!ShouldTraceVulkanShaderConstants()) {
    return;
  }

  constexpr uint32_t kMaxFloatConstantsLogged = 32;
  const RegisterFile& regs = *register_file_;
  const Shader::ConstantRegisterMap& map = shader.constant_register_map();
  XELOGI(
      "GPU shader-constant trace: stage={} shader={:016X} "
      "float_count={} float_dynamic={} "
      "float_bitmap={:016X},{:016X},{:016X},{:016X} "
      "loop_bitmap={:08X} "
      "bool_bitmap={:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X},{:08X}",
      stage_label, shader.ucode_data_hash(), map.float_count,
      map.float_dynamic_addressing, map.float_bitmap[0], map.float_bitmap[1],
      map.float_bitmap[2], map.float_bitmap[3], map.loop_bitmap,
      map.bool_bitmap[0], map.bool_bitmap[1], map.bool_bitmap[2],
      map.bool_bitmap[3], map.bool_bitmap[4], map.bool_bitmap[5],
      map.bool_bitmap[6], map.bool_bitmap[7]);

  uint32_t float_base_register =
      is_pixel_shader ? XE_GPU_REG_SHADER_CONSTANT_256_X
                      : XE_GPU_REG_SHADER_CONSTANT_000_X;
  uint32_t guest_constant_base = is_pixel_shader ? 256 : 0;
  uint32_t float_constants_logged = 0;
  for (uint32_t i = 0; i < 4; ++i) {
    uint64_t float_constant_map_entry = map.float_bitmap[i];
    uint32_t float_constant_index;
    while (xe::bit_scan_forward(float_constant_map_entry,
                                &float_constant_index)) {
      float_constant_map_entry &= ~(1ull << float_constant_index);
      uint32_t storage_index = (i << 6) + float_constant_index;
      const uint32_t* raw =
          &regs[float_base_register + (i << 8) + (float_constant_index << 2)];
      float values[4];
      std::memcpy(values, raw, sizeof(values));
      XELOGI(
          "GPU shader-constant trace: stage={} shader={:016X} "
          "float storage={} guest_c={} raw={:08X},{:08X},{:08X},{:08X} "
          "value={},{},{},{}",
          stage_label, shader.ucode_data_hash(), storage_index,
          guest_constant_base + storage_index, raw[0], raw[1], raw[2], raw[3],
          values[0], values[1], values[2], values[3]);
      ++float_constants_logged;
      if (float_constants_logged >= kMaxFloatConstantsLogged) {
        XELOGI(
            "GPU shader-constant trace: stage={} shader={:016X} "
            "float logging capped shown={} total={}",
            stage_label, shader.ucode_data_hash(), float_constants_logged,
            map.float_count);
        i = 4;
        break;
      }
    }
  }

  uint32_t loop_constants_remaining = map.loop_bitmap;
  uint32_t loop_index;
  while (xe::bit_scan_forward(loop_constants_remaining, &loop_index)) {
    loop_constants_remaining &= ~(uint32_t(1) << loop_index);
    xenos::LoopConstant loop_constant;
    loop_constant.value = regs[XE_GPU_REG_SHADER_CONSTANT_LOOP_00 + loop_index];
    XELOGI(
        "GPU shader-constant trace: stage={} shader={:016X} "
        "loop index={} raw={:08X} count={} start={} step={}",
        stage_label, shader.ucode_data_hash(), loop_index, loop_constant.value,
        loop_constant.count, loop_constant.start, loop_constant.step);
  }

  for (uint32_t bool_block = 0; bool_block < 8; ++bool_block) {
    uint32_t used_bools = map.bool_bitmap[bool_block];
    if (!used_bools) {
      continue;
    }
    uint32_t values =
        regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031 + bool_block];
    XELOGI(
        "GPU shader-constant trace: stage={} shader={:016X} "
        "bool block={} used={:08X} values={:08X}",
        stage_label, shader.ucode_data_hash(), bool_block, used_bools, values);
  }
}

void VulkanCommandProcessor::TraceVertexFetchSources(
    const VulkanShader& shader, uint32_t host_draw_vertex_count) {
  if (!cvars::vulkan_trace_vertex_fetch_checksum || !memory_) {
    return;
  }
  if (!TraceHashMatchesFilter(
          shader.ucode_data_hash(),
          cvars::vulkan_trace_vertex_fetch_shader_filter)) {
    return;
  }

  const RegisterFile& regs = *register_file_;
  for (const Shader::VertexBinding& binding : shader.vertex_bindings()) {
    if (!ShouldTraceVulkanVertexFetchChecksum()) {
      break;
    }

    xenos::xe_gpu_vertex_fetch_t vfetch_constant =
        regs.GetVertexFetch(binding.fetch_constant);
    uint32_t address = vfetch_constant.address << 2;
    uint32_t length = vfetch_constant.size << 2;
    const uint8_t* bytes =
        length ? memory_->TranslatePhysical<const uint8_t*>(address) : nullptr;

    constexpr uint32_t kSampleStride = 64;
    uint64_t checksum = 1469598103934665603ull;
    uint32_t samples = 0;
    uint32_t nonzero_samples = 0;
    uint32_t first_words_raw[8] = {};
    uint32_t first_words_be[8] = {};
    if (bytes) {
      for (uint32_t offset = 0; offset + sizeof(uint32_t) <= length;
           offset += kSampleStride) {
        uint32_t raw_word = 0;
        std::memcpy(&raw_word, bytes + offset, sizeof(raw_word));
        uint32_t be_word = xe::byte_swap(raw_word);
        checksum ^= uint64_t(be_word) + (uint64_t(offset) << 1);
        checksum *= 1099511628211ull;
        ++samples;
        if (raw_word) {
          ++nonzero_samples;
        }
      }
      uint32_t first_word_count = uint32_t(std::min<size_t>(
          xe::countof(first_words_raw), length / sizeof(uint32_t)));
      for (uint32_t i = 0; i < first_word_count; ++i) {
        std::memcpy(&first_words_raw[i], bytes + i * sizeof(uint32_t),
                    sizeof(uint32_t));
        first_words_be[i] = xe::byte_swap(first_words_raw[i]);
      }
    }

    XELOGI(
        "GPU vertex-source trace: shader={:016X} fetch={} binding={} "
        "address={:08X} length={:08X} endian={} stride_words={} "
        "host_vertices={} attr_count={} samples={} nonzero={} "
        "checksum={:016X} first_raw={:08X},{:08X},{:08X},{:08X},"
        "{:08X},{:08X},{:08X},{:08X} first_be={:08X},{:08X},{:08X},"
        "{:08X},{:08X},{:08X},{:08X},{:08X}",
        shader.ucode_data_hash(), binding.fetch_constant,
        binding.binding_index, address, length,
        static_cast<uint32_t>(vfetch_constant.endian), binding.stride_words,
        host_draw_vertex_count, binding.attributes.size(), samples,
        nonzero_samples, checksum, first_words_raw[0], first_words_raw[1],
        first_words_raw[2], first_words_raw[3], first_words_raw[4],
        first_words_raw[5], first_words_raw[6], first_words_raw[7],
        first_words_be[0], first_words_be[1], first_words_be[2],
        first_words_be[3], first_words_be[4], first_words_be[5],
        first_words_be[6], first_words_be[7]);

    for (size_t attr_index = 0; attr_index < binding.attributes.size();
         ++attr_index) {
      const ParsedVertexFetchInstruction& fetch_instr =
          binding.attributes[attr_index].fetch_instr;
      uint32_t raw_values[4] = {};
      uint32_t be_values[4] = {};
      uint32_t vertices_to_log =
          std::min<uint32_t>(4, std::max<uint32_t>(1, host_draw_vertex_count));
      for (uint32_t vertex = 0; vertex < vertices_to_log; ++vertex) {
        int64_t sample_offset =
            int64_t(vertex) * int64_t(binding.stride_words) * 4 +
            int64_t(fetch_instr.attributes.offset) * 4;
        if (bytes && sample_offset >= 0 &&
            uint64_t(sample_offset) + sizeof(uint32_t) <= length) {
          std::memcpy(&raw_values[vertex], bytes + sample_offset,
                      sizeof(uint32_t));
          be_values[vertex] = xe::byte_swap(raw_values[vertex]);
        }
      }
      XELOGI(
          "GPU vertex-source trace: attr shader={:016X} fetch={} attr={} "
          "opcode={} mini={} result_target={} result_index={} "
          "write_mask={:X} data_format={} offset={} instr_stride={} "
          "signed={} integer={} raw_v0_v3={:08X},{:08X},{:08X},{:08X} "
          "be_v0_v3={:08X},{:08X},{:08X},{:08X}",
          shader.ucode_data_hash(), binding.fetch_constant, attr_index,
          fetch_instr.opcode_name ? fetch_instr.opcode_name : "?",
          fetch_instr.is_mini_fetch,
          static_cast<uint32_t>(fetch_instr.result.storage_target),
          fetch_instr.result.storage_index, fetch_instr.result.GetUsedWriteMask(),
          static_cast<uint32_t>(fetch_instr.attributes.data_format),
          fetch_instr.attributes.offset, fetch_instr.attributes.stride,
          fetch_instr.attributes.is_signed, fetch_instr.attributes.is_integer,
          raw_values[0], raw_values[1], raw_values[2], raw_values[3],
          be_values[0], be_values[1], be_values[2], be_values[3]);
    }
  }
}

void VulkanCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                       uint32_t frontbuffer_width,
                                       uint32_t frontbuffer_height,
                                       uint32_t display_width,
                                       uint32_t display_height) {
  SCOPE_profile_cpu_f("gpu");
  ui::vulkan::VulkanPerfCountersRecordIssueSwap();

  // Blue Dragon native-draw HLE: confirm the whole field foliage pass fired
  // natively this frame via the per-frame emit counter, then reset it (mirrors
  // D3D12CommandProcessor::IssueSwap - the base-class counter is otherwise never
  // reset on the Vulkan/Thor backend, and gives no per-frame device signal).
  if (bd_native_emits_this_frame_) {
    static uint32_t s_bd_native_frame = 0;
    XELOGI("BD NATIVE-HLE: field frame {} emitted {} native foliage draws",
           s_bd_native_frame++, bd_native_emits_this_frame_);
    bd_native_emits_this_frame_ = 0;
  }

  // THE EDRAM SOLVE, hybrid form: the guest swap ends the frame - reset the
  // post-process phase so the next frame's main scene starts on host RTs and the
  // first composite re-bridges once. (Per-frame, not per-draw = no thrash across
  // BD's composites that are interleaved with main-scene draws.)
  render_target_cache_->ResetHybridPostprocessPhase();

  // Lever 2 (vulkan_merge_draws): realize any pending concatenation run before
  // the frame's present/teardown work.
  FlushPendingMergeRun();

  // Gap attribution: remember which pass bracket the guest swap lands after
  // (the current frame slot is never the completed slot the readback uses).
  gpu_swap_bracket_[frame_current_ % kMaxFramesInFlight] =
      gpu_pass_bracket_count_;

  // Optional hard freeze (for genuinely static scenes like menus). NOTE: for
  // animated cinematics this does NOT produce a static frame (the engine keeps
  // evolving the scene regardless of the guest clock), so the primary clean-A/B
  // tool is the guest_ms key logged below: guest content is a function of guest
  // uptime, so comparing gpu_frame_us at the SAME guest_ms across configs gives
  // identical content with no scene-timing confound.
  if (cvars::gpu_freeze_at_guest_ms != 0 && !gpu_scene_lock_frozen_ &&
      xe::Clock::QueryGuestUptimeMillis() >= cvars::gpu_freeze_at_guest_ms) {
    gpu_scene_lock_frozen_ = true;
    xe::Clock::set_guest_time_scalar(0.0001);
    XELOGI("GPU scene-lock: froze guest at uptime {} ms (time_scalar->0.0001)",
           xe::Clock::QueryGuestUptimeMillis());
  }

  // Single-run VRS A/B (gpu_freeze_ab_alternate_vrs): flip the VRS phase in
  // 16-frame blocks so the scene renders alternately with VRS off then on. Active
  // when FROZEN (identical scene every frame - cleanest) OR FREE-RUNNING past
  // gpu_vrs_enable_after_guest_ms (alternation starts the instant the field
  // renders - robust to BD's variable field-reach wall-time + the thermal
  // watchdog, since no precise freeze window is needed; over an idle/near-static
  // field the 16-frame blocks interleave off/on tightly enough that residual
  // scene drift affects both phases about equally). The per-draw consumer reads
  // gpu_freeze_vrs_phase_on_; the draw-outcomes path logs the phase so gpu_frame_us
  // buckets by phase (median of each block's middle frames, discard the first ~3
  // GPU-latency transition frames).
  gpu_ab_alt_active_ =
      cvars::gpu_freeze_ab_alternate_vrs &&
      (gpu_scene_lock_frozen_ ||
       (cvars::gpu_vrs_enable_after_guest_ms != 0 &&
        xe::Clock::QueryGuestUptimeMillis() >=
            uint64_t(cvars::gpu_vrs_enable_after_guest_ms)));
  if (gpu_ab_alt_active_) {
    gpu_freeze_vrs_phase_on_ =
        ((gpu_freeze_ab_frame_counter_++ / 16u) & 1u) != 0;
  }

  if (cvars::vulkan_trace_draw_outcomes_per_frame) {
    // Read back the newest GPU-timestamp pair from a frame that has completed
    // and whose slot hasn't been reused by an in-flight frame (no host stall).
    if (gpu_timestamp_pool_ != VK_NULL_HANDLE) {
      const uint64_t completed = GetCompletedFrame();
      uint64_t best_frame = 0;
      int best_slot = -1;
      for (uint32_t s = 0; s < kMaxFramesInFlight; ++s) {
        uint64_t wf = gpu_timestamp_frame_written_[s];
        if (wf != 0 && wf <= completed && wf >= best_frame) {
          best_frame = wf;
          best_slot = int(s);
        }
      }
      if (best_slot >= 0) {
        uint64_t ts[2] = {};
        const ui::vulkan::VulkanDevice::Functions& ts_dfn =
            GetVulkanDevice()->functions();
        if (ts_dfn.vkGetQueryPoolResults(
                GetVulkanDevice()->device(), gpu_timestamp_pool_,
                2u * uint32_t(best_slot), 2, sizeof(ts), ts, sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ts[1] > ts[0]) {
          gpu_frame_us_ = uint64_t(double(ts[1] - ts[0]) *
                                   double(gpu_timestamp_period_ns_) / 1000.0);
        }
        // Route A: sum this frame's per-pass spans = time INSIDE render passes.
        if (gpu_pass_timestamp_pool_ != VK_NULL_HANDLE) {
          uint32_t n = gpu_pass_count_written_[best_slot];
          if (n > kMaxPassBrackets) {
            n = kMaxPassBrackets;
          }
          if (n) {
            uint64_t pts[2u * kMaxPassBrackets] = {};
            if (ts_dfn.vkGetQueryPoolResults(
                    GetVulkanDevice()->device(), gpu_pass_timestamp_pool_,
                    uint32_t(best_slot) * 2u * kMaxPassBrackets, 2u * n,
                    sizeof(uint64_t) * 2u * n, pts, sizeof(uint64_t),
                    VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
              uint64_t sum_ticks = 0;
              // Pass-split diagnostic: largest pass spans + the inter-pass
              // GAPS (end of pass i -> begin of pass i+1; brackets are written
              // chronologically) + the head/tail edges against the whole-frame
              // pair. Attributes the dominant "between-pass" GPU time (real
              // between-pass work/stall vs in-pass under-measurement) - the
              // load-bearing unknown after in-pass transfers proved fps-flat.
              uint64_t gap_ticks = 0;
              uint64_t top_pass[3] = {};
              uint64_t top_gap[3] = {};
              auto top3_insert = [](uint64_t(&top)[3], uint64_t value) {
                if (value > top[0]) {
                  top[2] = top[1];
                  top[1] = top[0];
                  top[0] = value;
                } else if (value > top[1]) {
                  top[2] = top[1];
                  top[1] = value;
                } else if (value > top[2]) {
                  top[2] = value;
                }
              };
              uint64_t top_gap_ticks = 0;
              uint32_t top_gap_index = UINT32_MAX;
              // gpu_trace_resolve_timing: split the summed in-pass GPU time by
              // bracket kind (guest geometry vs EDRAM transfer / resolve-clear
              // pass vs resolve-copy / clear / host-depth-store compute dispatch).
              uint64_t kind_ticks[uint32_t(GpuPassKind::kCount)] = {};
              uint32_t kind_count[uint32_t(GpuPassKind::kCount)] = {};
              // Deferred TBDR tile store/render lands in the GAP after each pass
              // (not inside the bracket, which is only the pass's draw/shade
              // time). Attribute each gap to the PRECEDING pass's kind so
              // gap_by_kind[kGuestComposite] = the composite passes' deferred
              // tile-I/O = brick 2's true ceiling (compute writes the dest
              // directly, removing exactly this).
              uint64_t gap_ticks_by_kind[uint32_t(GpuPassKind::kCount)] = {};
              for (uint32_t i = 0; i < n; ++i) {
                if (pts[2u * i + 1u] > pts[2u * i]) {
                  uint64_t span_ticks = pts[2u * i + 1u] - pts[2u * i];
                  sum_ticks += span_ticks;
                  top3_insert(top_pass, span_ticks);
                  uint8_t bk = gap_snap_begin_[best_slot][i].kind;
                  if (bk < uint8_t(GpuPassKind::kCount)) {
                    kind_ticks[bk] += span_ticks;
                    ++kind_count[bk];
                  }
                }
                if (i + 1u < n && pts[2u * (i + 1u)] > pts[2u * i + 1u]) {
                  uint64_t one_gap_ticks = pts[2u * (i + 1u)] - pts[2u * i + 1u];
                  gap_ticks += one_gap_ticks;
                  uint8_t pk = gap_snap_begin_[best_slot][i].kind;
                  if (pk < uint8_t(GpuPassKind::kCount)) {
                    gap_ticks_by_kind[pk] += one_gap_ticks;
                  }
                  top3_insert(top_gap, one_gap_ticks);
                  if (one_gap_ticks > top_gap_ticks) {
                    top_gap_ticks = one_gap_ticks;
                    top_gap_index = i;
                  }
                }
              }
              // Composition of the largest gap: the commands recorded between
              // the end of pass i and the begin of pass i+1 are exactly what
              // the GPU executes in that hole (one submission per frame).
              // Saturating diffs guard against any mid-frame buffer reset.
              uint32_t gap_dispatches = 0, gap_buffer_copies = 0;
              uint32_t gap_buffer_image_copies = 0, gap_barriers = 0;
              uint64_t gap_copy_kb = 0;
              uint32_t flank_prev_draws = 0, flank_next_draws = 0;
              uint32_t flank_prev_fb = 0, flank_next_fb = 0;
              if (top_gap_index != UINT32_MAX && top_gap_index + 1u < n) {
                const PassBoundarySnap& end_snap =
                    gap_snap_end_[best_slot][top_gap_index];
                const PassBoundarySnap& begin_snap =
                    gap_snap_begin_[best_slot][top_gap_index + 1u];
                auto sat_diff32 = [](uint32_t after, uint32_t before) {
                  return after > before ? after - before : 0u;
                };
                gap_dispatches =
                    sat_diff32(begin_snap.dispatches, end_snap.dispatches);
                gap_buffer_copies =
                    sat_diff32(begin_snap.buffer_copies, end_snap.buffer_copies);
                gap_buffer_image_copies = sat_diff32(
                    begin_snap.buffer_image_copies, end_snap.buffer_image_copies);
                gap_barriers = sat_diff32(begin_snap.barriers, end_snap.barriers);
                gap_copy_kb =
                    (begin_snap.buffer_copy_bytes > end_snap.buffer_copy_bytes
                         ? begin_snap.buffer_copy_bytes -
                               end_snap.buffer_copy_bytes
                         : 0) /
                    1024u;
                // Identity of the passes flanking the dominant gap: draws in
                // pass i (end[i] - begin[i]) / pass i+1 (end[i+1] - begin[i+1])
                // and their framebuffer ids.
                const PassBoundarySnap& prev_begin_snap =
                    gap_snap_begin_[best_slot][top_gap_index];
                flank_prev_draws =
                    sat_diff32(end_snap.draws, prev_begin_snap.draws);
                flank_prev_fb = end_snap.framebuffer_id;
                if (top_gap_index + 1u < n) {
                  const PassBoundarySnap& next_end_snap =
                      gap_snap_end_[best_slot][top_gap_index + 1u];
                  flank_next_draws =
                      sat_diff32(next_end_snap.draws, begin_snap.draws);
                  flank_next_fb = begin_snap.framebuffer_id;
                }
              }
              gpu_pass_us_ = uint64_t(double(sum_ticks) *
                                      double(gpu_timestamp_period_ns_) / 1000.0);
              uint64_t head_ticks =
                  (ts[1] > ts[0] && pts[0] > ts[0]) ? pts[0] - ts[0] : 0;
              uint64_t tail_ticks = (ts[1] > ts[0] && ts[1] > pts[2u * n - 1u])
                                        ? ts[1] - pts[2u * n - 1u]
                                        : 0;
              const double tick_us =
                  double(gpu_timestamp_period_ns_) / 1000.0;
              XELOGI(
                  "GPU pass split: n={} pass_us={} gap_us={} head_us={} "
                  "tail_us={} top_pass_us=[{} {} {}] top_gap_us=[{} {} {}] "
                  "topgap[i={} disp={} bufcp={} cpkb={} b2icp={} barr={}] "
                  "swap_after={} flank[p{}fb={:04x}dr={} p{}fb={:04x}dr={}]",
                  n, uint64_t(double(sum_ticks) * tick_us),
                  uint64_t(double(gap_ticks) * tick_us),
                  uint64_t(double(head_ticks) * tick_us),
                  uint64_t(double(tail_ticks) * tick_us),
                  uint64_t(double(top_pass[0]) * tick_us),
                  uint64_t(double(top_pass[1]) * tick_us),
                  uint64_t(double(top_pass[2]) * tick_us),
                  uint64_t(double(top_gap[0]) * tick_us),
                  uint64_t(double(top_gap[1]) * tick_us),
                  uint64_t(double(top_gap[2]) * tick_us), top_gap_index,
                  gap_dispatches, gap_buffer_copies, gap_copy_kb,
                  gap_buffer_image_copies, gap_barriers,
                  gpu_swap_bracket_[best_slot], top_gap_index, flank_prev_fb,
                  flank_prev_draws, top_gap_index + 1u, flank_next_fb,
                  flank_next_draws);
              // gpu_trace_resolve_timing: per-kind GPU-time breakdown. guest =
              // geometry passes' in-pass time; xfer = EDRAM ownership-transfer
              // passes (RT store/load drawn as quads); rclear = resolve-clear
              // passes; rcopy/clrd/hds = resolve-copy / FSI-clear / host-depth-
              // store compute dispatches (otherwise hidden inside the gaps). The
              // remaining gap_us after subtracting rcopy/clrd/hds is the TBDR
              // deferred tile store/render of the preceding pass.
              if (cvars::gpu_trace_resolve_timing) {
                // gap_* = deferred TBDR tile store/render attributed to the
                // preceding pass kind. gap_composite = brick 2's measured ceiling
                // (the composite passes' tile-I/O, removed by composite->compute);
                // gap_guest = the geometry passes' tile-I/O (the residual).
                XELOGI(
                    "GPU pass kinds (us): guest={} composite={} xfer={} rclear={} "
                    "rcopy={} clrd={} hds={} gap_composite={} gap_guest={} "
                    "gap_xfer={} gap_total={} n[guest={} composite={} xfer={} "
                    "rclear={} rcopy={} clrd={} hds={}]",
                    uint64_t(double(kind_ticks[uint32_t(GpuPassKind::kGuest)]) *
                             tick_us),
                    uint64_t(
                        double(kind_ticks[uint32_t(GpuPassKind::kGuestComposite)]) *
                        tick_us),
                    uint64_t(
                        double(kind_ticks[uint32_t(GpuPassKind::kEdramTransfer)]) *
                        tick_us),
                    uint64_t(
                        double(kind_ticks[uint32_t(GpuPassKind::kResolveClear)]) *
                        tick_us),
                    uint64_t(double(kind_ticks[uint32_t(
                                 GpuPassKind::kResolveCopyDispatch)]) *
                             tick_us),
                    uint64_t(double(kind_ticks[uint32_t(
                                 GpuPassKind::kResolveClearDispatch)]) *
                             tick_us),
                    uint64_t(double(kind_ticks[uint32_t(
                                 GpuPassKind::kHostDepthStoreDispatch)]) *
                             tick_us),
                    uint64_t(double(gap_ticks_by_kind[uint32_t(
                                 GpuPassKind::kGuestComposite)]) *
                             tick_us),
                    uint64_t(
                        double(gap_ticks_by_kind[uint32_t(GpuPassKind::kGuest)]) *
                        tick_us),
                    uint64_t(double(gap_ticks_by_kind[uint32_t(
                                 GpuPassKind::kEdramTransfer)]) *
                             tick_us),
                    uint64_t(double(gap_ticks) * tick_us),
                    kind_count[uint32_t(GpuPassKind::kGuest)],
                    kind_count[uint32_t(GpuPassKind::kGuestComposite)],
                    kind_count[uint32_t(GpuPassKind::kEdramTransfer)],
                    kind_count[uint32_t(GpuPassKind::kResolveClear)],
                    kind_count[uint32_t(GpuPassKind::kResolveCopyDispatch)],
                    kind_count[uint32_t(GpuPassKind::kResolveClearDispatch)],
                    kind_count[uint32_t(GpuPassKind::kHostDepthStoreDispatch)]);
              }
            }
          }
        }
      }
    }
    // Flush the final in-progress same-pipeline run into the histogram.
    if (merge_run_len_) {
      uint32_t rl = merge_run_len_;
      uint32_t b = rl <= 1   ? 0
                   : rl == 2 ? 1
                   : rl <= 4 ? 2
                   : rl <= 8 ? 3
                   : rl <= 16 ? 4
                   : rl <= 32 ? 5
                   : rl <= 64 ? 6
                              : 7;
      ++merge_run_hist_[b];
    }
    // Flush the final in-progress true-eligible run into its histogram.
    if (merge_elig_run_len_) {
      uint32_t rl = merge_elig_run_len_;
      uint32_t b = rl <= 1   ? 0
                   : rl == 2 ? 1
                   : rl <= 4 ? 2
                   : rl <= 8 ? 3
                   : rl <= 16 ? 4
                   : rl <= 32 ? 5
                   : rl <= 64 ? 6
                              : 7;
      ++merge_elig_run_hist_[b];
    }
    // Flush the final in-progress strip-coalescer run into its histogram.
    if (merge_strip_run_len_) {
      uint32_t rl = merge_strip_run_len_;
      uint32_t b = rl <= 1    ? 0
                   : rl == 2  ? 1
                   : rl <= 4  ? 2
                   : rl <= 8  ? 3
                   : rl <= 16 ? 4
                   : rl <= 32 ? 5
                   : rl <= 64 ? 6
                              : 7;
      ++merge_strip_run_hist_[b];
    }
    // Flush the final in-progress descriptor-aware strip run into its histogram.
    if (merge_stripd_run_len_) {
      uint32_t rl = merge_stripd_run_len_;
      uint32_t b = rl <= 1    ? 0
                   : rl == 2  ? 1
                   : rl <= 4  ? 2
                   : rl <= 8  ? 3
                   : rl <= 16 ? 4
                   : rl <= 32 ? 5
                   : rl <= 64 ? 6
                              : 7;
      ++merge_stripd_run_hist_[b];
    }
    // Flush the final in-progress texture-aware strip run into its histogram.
    if (merge_stript_run_len_) {
      uint32_t rl = merge_stript_run_len_;
      uint32_t b = rl <= 1    ? 0
                   : rl == 2  ? 1
                   : rl <= 4  ? 2
                   : rl <= 8  ? 3
                   : rl <= 16 ? 4
                   : rl <= 32 ? 5
                   : rl <= 64 ? 6
                              : 7;
      ++merge_stript_run_hist_[b];
    }
    // Host draw calls recorded since the last print (vs guest `rendered`):
    // completed submissions' draw stats are folded into the accumulator at
    // Execute time, plus whatever the current recording holds.
    uint64_t host_draws_total = host_draws_recorded_accum_ +
                                deferred_command_buffer_.record_stats().draws;
    uint64_t host_draws_frame = host_draws_total - host_draws_printed_marker_;
    host_draws_printed_marker_ = host_draws_total;
    XELOGI(
        "GPU draw outcomes/frame: rendered={} skipped_no_vs={} "
        "skipped_no_rast={} copy={} total_vertices={} max_vertices={} "
        "avg_vertices={} pipeline_binds={} descriptor_binds={} "
        "rt_transfer_calls={} rt_transfers={} rt_resolve_clears={} "
        "pass_break_barrier={} pass_break_rt_change={} "
        "xfer_same_fmt={} xfer_diff_fmt={} "
        "inpass[x={} skip_fmt={} skip_oth={}] "
        "deint[elig_draws={} elig_verts={} redir_draws={} redir_verts={} "
        "gather_us={} bails={}] "
        "dc_safe[p={} att={}] depthnone_p={} retro_dn={} retro_cl={} "
        "host_draws={} "
        "cpu_issuedraw_us={} cpu_process_us={} cpu_process_pct={} "
        "cpu_tex_us={} cpu_rt_us={} cpu_pipe_us={} cpu_bind_us={} cpu_other_us={} "
        "cpu_setup_us={} cpu_emit_us={} cpu_beginsubmit_us={} "
        "cpu_real_us={} cpu_gap_us={} cpu_vfres_us={} "
        "fopen[wait_us={} inflight={} sub_pre={} sub_post={} fence_us={} "
        "await={} up={} comp={}] "
        "gpu_frame_us={} gpu_pass_us={} msaa={} surf_pitch={} "
        "brk_open={} brk_buf={} brk_img_sr={} brk_img_oth={} "
        "sr_cls[rtsrc={} tex={} fscomp={} rtfc={}] merges={} "
        "comp[opaque={} opaque_verts={} alphatest={} blended={}] guest_ms={} "
        "prim[pt={} ll={} ls={} tl={} tf={} ts={} rect={} quad={} poly={}] "
        "vtx[tiny={} sm={} med={} big={}] "
        "merge[pipe_same={} consts_same={} consts_changed={}] "
        "vf[same={} contig={} scattered={}] "
        "runlen[1={} 2={} 3-4={} 5-8={} 9-16={} 17-32={} 33-64={} 65+={}] "
        "elig_runlen[1={} 2={} 3-4={} 5-8={} 9-16={} 17-32={} 33-64={} 65+={}] "
        "strip_runlen[1={} 2={} 3-4={} 5-8={} 9-16={} 17-32={} 33-64={} 65+={}] "
        "stripd_runlen[1={} 2={} 3-4={} 5-8={} 9-16={} 17-32={} 33-64={} 65+={}] "
        "stript_runlen[1={} 2={} 3-4={} 5-8={} 9-16={} 17-32={} 33-64={} 65+={}] "
        "merge_miss[non_dma={} topo={} state={} noncontig={} other={}] "
        "mrw[ext={} head={} auto={} ndma={} nomrg={} cant={} pipe={} itype={} "
        "cap={} vgt={} end={} prim={} rst={}] "
        "cbup[sys={} fv={} fp={} bl={} f={}] "
        "dsre[smem={} cons={} texv={} texp={}] "
        "mrwf[same={} b16={} b32={} shape={}] "
        "cullable_tris={} affine_mvp_draws={} affine_mvp_verts={} "
        "affine_mvp_pos_draws={} affine_mvp_pos_verts={} "
        "pos_disq_verts[a0={} loop={} backjump={} call={} tex={} other={}] "
        "cull[branch={} skip_dyntop={} skip_qual={} draws={} dropped_tris={} "
        "bail(notdma={} tess={} notinterp={} vtxxy={} clipdis={} restart={} "
        "noidxptr={} zerodrop={} fastfail={}) slice_ops_sum={} slice_replayable={} "
        "replay[affine={} nonaffine={} unsup={} maxerr_milli={}] "
        "fastrep[engaged={} fail(noleaf={} multi={} novf={} badfmt={} recov={})] "
        "multi_lc[2={} 3={} 4={} 5p={}] "
        "wholecull[draws={} elig={} verts={}] "
        "whole_skip[draws={} verts={}]",
        draw_outcomes_rendered_, draw_outcomes_skipped_no_vs_,
        draw_outcomes_skipped_no_rast_, draw_outcomes_copy_,
        draw_outcomes_total_vertices_, draw_outcomes_max_vertices_,
        draw_outcomes_rendered_
            ? (draw_outcomes_total_vertices_ / draw_outcomes_rendered_)
            : 0,
        draw_outcomes_pipeline_binds_, draw_outcomes_descriptor_binds_,
        rt_transfer_calls_, rt_transfers_, rt_resolve_clears_,
        rt_pass_break_barrier_, rt_pass_break_rt_change_,
        rt_transfer_same_format_, rt_transfer_diff_format_,
        rt_inpass_transfer_dests_, rt_inpass_skipped_format_,
        rt_inpass_skipped_other_, draw_outcomes_deint_elig_draws_,
        draw_outcomes_deint_elig_verts_, draw_outcomes_deint_redir_draws_,
        draw_outcomes_deint_redir_verts_,
        draw_outcomes_deint_gather_ns_ / 1000, draw_outcomes_deint_bails_,
        draw_outcomes_dc_safe_passes_, draw_outcomes_dc_safe_atts_,
        draw_outcomes_depth_none_passes_, draw_outcomes_retro_depth_none_,
        draw_outcomes_retro_color_atts_, host_draws_frame,
        draw_cpu_total_ns_ / 1000, draw_cpu_process_ns_ / 1000,
        draw_cpu_total_ns_
            ? (draw_cpu_process_ns_ * 100 / draw_cpu_total_ns_)
            : 0,
        draw_cpu_textures_ns_ / 1000, draw_cpu_rt_ns_ / 1000,
        draw_cpu_pipeline_ns_ / 1000, draw_cpu_bindings_ns_ / 1000,
        (draw_cpu_total_ns_ >
         (draw_cpu_process_ns_ + draw_cpu_textures_ns_ + draw_cpu_rt_ns_ +
          draw_cpu_pipeline_ns_ + draw_cpu_bindings_ns_))
            ? (draw_cpu_total_ns_ -
               (draw_cpu_process_ns_ + draw_cpu_textures_ns_ +
                draw_cpu_rt_ns_ + draw_cpu_pipeline_ns_ +
                draw_cpu_bindings_ns_)) /
                  1000
            : 0,
        draw_cpu_setup_ns_ / 1000, draw_cpu_emit_ns_ / 1000,
        draw_cpu_beginsubmit_ns_ / 1000,
        // cpu_real = total IssueDraw minus the GPU-paced BeginSubmission wait =
        // the real CPU work; cpu_real > gpu_frame  =>  genuinely CPU-bound
        // (the reliable test - cpu_issuedraw alone bundles the throttle-wait).
        (draw_cpu_total_ns_ > draw_cpu_beginsubmit_ns_)
            ? (draw_cpu_total_ns_ - draw_cpu_beginsubmit_ns_) / 1000
            : 0,
        // cpu_gap = the truly UNACCOUNTED CPU: total minus every measured region
        // (setup[which already includes beginsubmit] + process + textures + rt +
        // pipeline + bindings + emit). The old cpu_other looked huge only because
        // it did not subtract setup/emit; this is the real untimed remainder.
        (draw_cpu_total_ns_ >
         (draw_cpu_setup_ns_ + draw_cpu_process_ns_ + draw_cpu_textures_ns_ +
          draw_cpu_rt_ns_ + draw_cpu_pipeline_ns_ + draw_cpu_bindings_ns_ +
          draw_cpu_emit_ns_))
            ? (draw_cpu_total_ns_ -
               (draw_cpu_setup_ns_ + draw_cpu_process_ns_ +
                draw_cpu_textures_ns_ + draw_cpu_rt_ns_ +
                draw_cpu_pipeline_ns_ + draw_cpu_bindings_ns_ +
                draw_cpu_emit_ns_)) /
                  1000
            : 0,
        draw_cpu_vfresidency_ns_ / 1000,
        draw_cpu_frame_open_wait_ns_ / 1000, draw_frame_open_in_flight_,
        draw_frame_open_sub_pre_, draw_frame_open_sub_post_,
        completion_fence_await_ns_ / 1000,
        draw_frame_open_await_idx_, draw_frame_open_upcoming_,
        draw_frame_open_completed_,
        gpu_frame_us_, gpu_pass_us_,
        uint32_t(register_file_->Get<reg::RB_SURFACE_INFO>().msaa_samples),
        uint32_t(register_file_->Get<reg::RB_SURFACE_INFO>().surface_pitch),
        brk_open_breaks_, brk_buffer_barriers_, brk_img_shaderread_,
        brk_img_other_, brk_img_sr_rtsrc_, brk_img_sr_texsample_,
        brk_img_sr_fscomposite_, brk_img_sr_rtsrc_fscomp_, rt_feedback_merges_,
        draw_outcomes_opaque_draws_,
        draw_outcomes_opaque_verts_, draw_outcomes_alphatest_draws_,
        draw_outcomes_blended_draws_, xe::Clock::QueryGuestUptimeMillis(),
        draw_prim_counts_[uint32_t(xenos::PrimitiveType::kPointList)],
        draw_prim_counts_[uint32_t(xenos::PrimitiveType::kLineList)],
        draw_prim_counts_[uint32_t(xenos::PrimitiveType::kLineStrip)],
        draw_prim_counts_[uint32_t(xenos::PrimitiveType::kTriangleList)],
        draw_prim_counts_[uint32_t(xenos::PrimitiveType::kTriangleFan)],
        draw_prim_counts_[uint32_t(xenos::PrimitiveType::kTriangleStrip)],
        draw_prim_counts_[uint32_t(xenos::PrimitiveType::kRectangleList)],
        draw_prim_counts_[uint32_t(xenos::PrimitiveType::kQuadList)],
        draw_prim_counts_[uint32_t(xenos::PrimitiveType::kPolygon)],
        draw_vtx_bucket_[0], draw_vtx_bucket_[1], draw_vtx_bucket_[2],
        draw_vtx_bucket_[3], merge_pipe_same_, merge_consts_same_,
        merge_consts_changed_, merge_vf_same_, merge_vf_contig_,
        merge_vf_scattered_, merge_run_hist_[0], merge_run_hist_[1],
        merge_run_hist_[2], merge_run_hist_[3], merge_run_hist_[4],
        merge_run_hist_[5], merge_run_hist_[6], merge_run_hist_[7],
        merge_elig_run_hist_[0], merge_elig_run_hist_[1],
        merge_elig_run_hist_[2], merge_elig_run_hist_[3],
        merge_elig_run_hist_[4], merge_elig_run_hist_[5],
        merge_elig_run_hist_[6], merge_elig_run_hist_[7],
        merge_strip_run_hist_[0], merge_strip_run_hist_[1],
        merge_strip_run_hist_[2], merge_strip_run_hist_[3],
        merge_strip_run_hist_[4], merge_strip_run_hist_[5],
        merge_strip_run_hist_[6], merge_strip_run_hist_[7],
        merge_stripd_run_hist_[0], merge_stripd_run_hist_[1],
        merge_stripd_run_hist_[2], merge_stripd_run_hist_[3],
        merge_stripd_run_hist_[4], merge_stripd_run_hist_[5],
        merge_stripd_run_hist_[6], merge_stripd_run_hist_[7],
        merge_stript_run_hist_[0], merge_stript_run_hist_[1],
        merge_stript_run_hist_[2], merge_stript_run_hist_[3],
        merge_stript_run_hist_[4], merge_stript_run_hist_[5],
        merge_stript_run_hist_[6], merge_stript_run_hist_[7],
        merge_miss_non_dma_, merge_miss_topology_, merge_miss_state_,
        merge_miss_noncontig_, merge_miss_other_,
        mrw_ext_, mrw_head_, mrw_auto_, mrw_ndma_, mrw_nomrg_, mrw_cant_,
        mrw_pipe_, mrw_itype_, mrw_cap_, mrw_vgt_, mrw_end_, mrw_prim_,
        mrw_rst_,
        mrw_cb_upload_[0], mrw_cb_upload_[1], mrw_cb_upload_[2],
        mrw_cb_upload_[3], mrw_cb_upload_[4],
        mrw_ds_rebind_[0], mrw_ds_rebind_[1], mrw_ds_rebind_[2],
        mrw_ds_rebind_[3],
        mrw_fetch_same_, mrw_fetch_bias16_, mrw_fetch_bias32_,
        mrw_fetch_shape_,
        draw_outcomes_cullable_tris_, draw_outcomes_affine_mvp_draws_,
        draw_outcomes_affine_mvp_vertices_, draw_outcomes_affine_mvp_pos_draws_,
        draw_outcomes_affine_mvp_pos_vertices_, draw_outcomes_pos_disq_a0_verts_,
        draw_outcomes_pos_disq_loop_verts_,
        draw_outcomes_pos_disq_backjump_verts_,
        draw_outcomes_pos_disq_call_verts_,
        draw_outcomes_pos_disq_texfetch_verts_,
        draw_outcomes_pos_disq_other_verts_, draw_outcomes_cull_branch_,
        draw_outcomes_cull_skip_dyntop_, draw_outcomes_cull_skip_qual_,
        draw_outcomes_cull_draws_, draw_outcomes_cull_dropped_tris_,
        draw_outcomes_cull_bail_[uint32_t(DrawExtentEstimator::CullBail::kNotDMA)],
        draw_outcomes_cull_bail_
            [uint32_t(DrawExtentEstimator::CullBail::kTessellation)],
        draw_outcomes_cull_bail_
            [uint32_t(DrawExtentEstimator::CullBail::kNotInterpretable)],
        draw_outcomes_cull_bail_
            [uint32_t(DrawExtentEstimator::CullBail::kVtxXyFmt)],
        draw_outcomes_cull_bail_
            [uint32_t(DrawExtentEstimator::CullBail::kClipDisable)],
        draw_outcomes_cull_bail_
            [uint32_t(DrawExtentEstimator::CullBail::kRestart)],
        draw_outcomes_cull_bail_
            [uint32_t(DrawExtentEstimator::CullBail::kNoIndexPtr)],
        draw_outcomes_cull_bail_
            [uint32_t(DrawExtentEstimator::CullBail::kZeroDropped)],
        draw_outcomes_cull_bail_
            [uint32_t(DrawExtentEstimator::CullBail::kFastSetupFail)],
        draw_outcomes_cull_slice_ops_sum_, draw_outcomes_cull_slice_replayable_,
        draw_outcomes_replay_affine_, draw_outcomes_replay_nonaffine_,
        draw_outcomes_replay_unsupported_,
        draw_outcomes_replay_max_error_milli_, draw_outcomes_cull_fast_engaged_,
        draw_outcomes_cull_fail_
            [uint32_t(DrawExtentEstimator::FastSetupFail::kNoLeaf)],
        draw_outcomes_cull_fail_
            [uint32_t(DrawExtentEstimator::FastSetupFail::kMultiLeaf)],
        draw_outcomes_cull_fail_
            [uint32_t(DrawExtentEstimator::FastSetupFail::kNoVfetchMatch)],
        draw_outcomes_cull_fail_
            [uint32_t(DrawExtentEstimator::FastSetupFail::kBadFormat)],
        draw_outcomes_cull_fail_
            [uint32_t(DrawExtentEstimator::FastSetupFail::kRecoveryFail)],
        draw_outcomes_cull_multi_lc_[2], draw_outcomes_cull_multi_lc_[3],
        draw_outcomes_cull_multi_lc_[4],
        draw_outcomes_cull_multi_lc_[5] + draw_outcomes_cull_multi_lc_[6] +
            draw_outcomes_cull_multi_lc_[7],
        draw_outcomes_wholecull_draws_, draw_outcomes_wholecull_elig_,
        draw_outcomes_wholecull_verts_, draw_outcomes_cull_whole_skip_,
        draw_outcomes_cull_whole_skip_verts_);
    XELOGI("hwvtx engage: elig={} redir={} (cvar={})",
           draw_outcomes_hwvtx_elig_draws_, draw_outcomes_hwvtx_redir_draws_,
           cvars::gpu_hw_vertex_fetch ? 1 : 0);
    // gpu_vulkan_classify_img_sr_breaks: of the feedback EDRAM ownership
    // transfers (the brk_img_sr store/load subset the inpass path can't take),
    // how many are same-pixel input-attachment (subpassLoad) eligible. If
    // same_pixel=0 across the frame, the input-attachment lever does NOT apply
    // to this scene (the transfers read the source at a remapped EDRAM texel).
    if (cvars::gpu_vulkan_classify_img_sr_breaks) {
      XELOGI(
          "EDRAM feedback transfers/frame: total={} same_pixel_eligible={} "
          "remapped={}",
          rt_feedback_transfers_, rt_feedback_samepix_,
          rt_feedback_transfers_ - rt_feedback_samepix_);
    }
    // Single-run VRS A/B: concise per-frame line so gpu_frame_us buckets by the
    // VRS phase (rendered/alphatest/blended confirm the scene matches across
    // phases - the matched-A/B precondition; in free-running mode small drift is
    // expected, so check they stay close when bucketing).
    if (gpu_ab_alt_active_) {
      XELOGI(
          "VRS_AB: phase={} gpu_frame_us={} rendered={} alphatest={} blended={} "
          "guest_ms={}",
          gpu_freeze_vrs_phase_on_ ? 1 : 0, gpu_frame_us_,
          draw_outcomes_rendered_, draw_outcomes_alphatest_draws_,
          draw_outcomes_blended_draws_, xe::Clock::QueryGuestUptimeMillis());
    }
    draw_outcomes_rendered_ = 0;
    draw_outcomes_opaque_draws_ = 0;
    draw_outcomes_opaque_verts_ = 0;
    draw_outcomes_alphatest_draws_ = 0;
    draw_outcomes_blended_draws_ = 0;
    draw_outcomes_cullable_tris_ = 0;
    draw_outcomes_wholecull_draws_ = 0;
    draw_outcomes_wholecull_elig_ = 0;
    draw_outcomes_wholecull_verts_ = 0;
    draw_outcomes_cull_whole_skip_ = 0;
    draw_outcomes_cull_whole_skip_verts_ = 0;
    draw_outcomes_affine_mvp_draws_ = 0;
    draw_outcomes_deint_elig_draws_ = 0;
    draw_outcomes_deint_elig_verts_ = 0;
    draw_outcomes_deint_redir_draws_ = 0;
    draw_outcomes_deint_redir_verts_ = 0;
    draw_outcomes_deint_gather_ns_ = 0;
    draw_outcomes_deint_bails_ = 0;
    draw_outcomes_hwvtx_elig_draws_ = 0;
    draw_outcomes_hwvtx_redir_draws_ = 0;
    draw_outcomes_dc_safe_passes_ = 0;
    draw_outcomes_dc_safe_atts_ = 0;
    draw_outcomes_depth_none_passes_ = 0;
    draw_outcomes_retro_depth_none_ = 0;
    draw_outcomes_retro_depth_none_diag_ = 0;
    draw_outcomes_retro_color_atts_ = 0;
    draw_outcomes_retro_color_diag_ = 0;
    draw_outcomes_affine_mvp_vertices_ = 0;
    draw_outcomes_affine_mvp_pos_draws_ = 0;
    draw_outcomes_affine_mvp_pos_vertices_ = 0;
    draw_outcomes_pos_disq_a0_verts_ = 0;
    draw_outcomes_pos_disq_loop_verts_ = 0;
    draw_outcomes_pos_disq_backjump_verts_ = 0;
    draw_outcomes_pos_disq_call_verts_ = 0;
    draw_outcomes_pos_disq_texfetch_verts_ = 0;
    draw_outcomes_pos_disq_other_verts_ = 0;
    draw_outcomes_cull_branch_ = 0;
    draw_outcomes_cull_skip_dyntop_ = 0;
    draw_outcomes_cull_skip_qual_ = 0;
    draw_outcomes_cull_draws_ = 0;
    draw_outcomes_cull_dropped_tris_ = 0;
    std::memset(draw_outcomes_cull_bail_, 0, sizeof(draw_outcomes_cull_bail_));
    draw_outcomes_cull_slice_ops_sum_ = 0;
    draw_outcomes_cull_slice_replayable_ = 0;
    draw_outcomes_replay_affine_ = 0;
    draw_outcomes_replay_nonaffine_ = 0;
    draw_outcomes_replay_unsupported_ = 0;
    draw_outcomes_replay_max_error_milli_ = 0;
    draw_outcomes_cull_fast_engaged_ = 0;
    std::memset(draw_outcomes_cull_fail_, 0, sizeof(draw_outcomes_cull_fail_));
    std::memset(draw_outcomes_cull_multi_lc_, 0,
                sizeof(draw_outcomes_cull_multi_lc_));
    draw_outcomes_skipped_no_vs_ = 0;
    draw_outcomes_skipped_no_rast_ = 0;
    draw_outcomes_copy_ = 0;
    foliage_draw_counter_ = 0;
    draw_outcomes_total_vertices_ = 0;
    draw_outcomes_max_vertices_ = 0;
    std::memset(draw_prim_counts_, 0, sizeof(draw_prim_counts_));
    std::memset(draw_vtx_bucket_, 0, sizeof(draw_vtx_bucket_));
    merge_pipe_same_ = 0;
    merge_consts_same_ = 0;
    merge_consts_changed_ = 0;
    merge_vf_same_ = 0;
    merge_vf_contig_ = 0;
    merge_vf_scattered_ = 0;
    merge_vf_last_addr_ = 0;
    merge_vf_last_end_ = 0;
    merge_run_len_ = 0;
    merge_run_pipeline_ = VK_NULL_HANDLE;
    std::memset(merge_run_hist_, 0, sizeof(merge_run_hist_));
    merge_elig_run_len_ = 0;
    merge_elig_run_active_ = false;
    merge_elig_run_pipeline_ = VK_NULL_HANDLE;
    merge_elig_run_next_byte_ = 0;
    std::memset(merge_elig_run_hist_, 0, sizeof(merge_elig_run_hist_));
    merge_strip_run_len_ = 0;
    merge_strip_run_active_ = false;
    merge_strip_run_pipeline_ = VK_NULL_HANDLE;
    merge_strip_run_layout_ = nullptr;
    merge_strip_run_vgt_offset_ = 0;
    std::memset(merge_strip_run_hist_, 0, sizeof(merge_strip_run_hist_));
    merge_stripd_run_len_ = 0;
    merge_stripd_run_active_ = false;
    merge_stripd_run_pipeline_ = VK_NULL_HANDLE;
    merge_stripd_run_layout_ = nullptr;
    merge_stripd_run_vgt_offset_ = 0;
    std::memset(merge_stripd_run_hist_, 0, sizeof(merge_stripd_run_hist_));
    merge_stript_run_len_ = 0;
    merge_stript_run_active_ = false;
    merge_stript_run_pipeline_ = VK_NULL_HANDLE;
    merge_stript_run_layout_ = nullptr;
    merge_stript_run_vgt_offset_ = 0;
    std::memset(merge_stript_run_hist_, 0, sizeof(merge_stript_run_hist_));
    merge_miss_non_dma_ = 0;
    merge_miss_topology_ = 0;
    merge_miss_other_ = 0;
    merge_miss_state_ = 0;
    mrw_ext_ = 0;
    mrw_head_ = 0;
    mrw_auto_ = 0;
    mrw_ndma_ = 0;
    mrw_nomrg_ = 0;
    mrw_cant_ = 0;
    mrw_pipe_ = 0;
    mrw_itype_ = 0;
    mrw_cap_ = 0;
    mrw_vgt_ = 0;
    mrw_end_ = 0;
    mrw_prim_ = 0;
    mrw_rst_ = 0;
    std::memset(mrw_cb_upload_, 0, sizeof(mrw_cb_upload_));
    std::memset(mrw_ds_rebind_, 0, sizeof(mrw_ds_rebind_));
    mrw_fetch_same_ = 0;
    mrw_fetch_bias16_ = 0;
    mrw_fetch_bias32_ = 0;
    mrw_fetch_shape_ = 0;
    merge_miss_noncontig_ = 0;
    draw_outcomes_pipeline_binds_ = 0;
    draw_outcomes_descriptor_binds_ = 0;
    if (cvars::vulkan_trace_draw_outcomes_per_frame && rt_resolve_copies_) {
      XELOGI(
          "RTtex detector: resolve_copies={} resolve_KB={} rt_fed_textures={} "
          "rt_served={} (RT-as-texture bridge eliminates this per-frame "
          "ResolveCopy compute + RAM round-trip by sampling the resident RTs "
          "directly; rt_served = fetches bound straight to a resident RT this "
          "frame via gpu_rt_as_texture)",
          rt_resolve_copies_, rt_resolve_copy_bytes_ / 1024, rt_fed_textures_,
          rt_served_textures_);
    }
    rt_transfer_calls_ = 0;
    rt_transfers_ = 0;
    rt_resolve_clears_ = 0;
    rt_resolve_copies_ = 0;
    rt_resolve_copy_bytes_ = 0;
    rt_fed_textures_ = 0;
    rt_served_textures_ = 0;
    frame_resolve_edges_.clear();
    rt_pass_break_barrier_ = 0;
    rt_pass_break_rt_change_ = 0;
    brk_open_breaks_ = 0;
    brk_buffer_barriers_ = 0;
    brk_img_shaderread_ = 0;
    brk_img_other_ = 0;
    brk_img_sr_rtsrc_ = 0;
    brk_img_sr_texsample_ = 0;
    brk_img_sr_fscomposite_ = 0;
    brk_img_sr_rtsrc_fscomp_ = 0;
    rt_feedback_merges_ = 0;
    brk_img_sr_detail_logged_ = 0;
    rt_transfer_same_format_ = 0;
    rt_transfer_diff_format_ = 0;
    rt_inpass_transfer_dests_ = 0;
    rt_inpass_skipped_format_ = 0;
    rt_inpass_skipped_other_ = 0;
    rt_feedback_transfers_ = 0;
    rt_feedback_samepix_ = 0;
    rt_feedback_detail_logged_ = 0;
    draw_cpu_total_ns_ = 0;
    draw_cpu_process_ns_ = 0;
    draw_cpu_textures_ns_ = 0;
    draw_cpu_rt_ns_ = 0;
    draw_cpu_pipeline_ns_ = 0;
    draw_cpu_bindings_ns_ = 0;
    draw_cpu_setup_ns_ = 0;
    draw_cpu_emit_ns_ = 0;
    draw_cpu_beginsubmit_ns_ = 0;
    draw_cpu_vfresidency_ns_ = 0;
    draw_cpu_frame_open_wait_ns_ = 0;
    draw_frame_open_in_flight_ = 0;
    draw_frame_open_sub_pre_ = 0;
    draw_frame_open_sub_post_ = 0;
    completion_fence_await_ns_ = 0;
  }

  if (cvars::gpu_trace_swap) {
    static std::atomic<bool> logged_vulkan_swap_cvars{false};
    if (!logged_vulkan_swap_cvars.exchange(true)) {
      XELOGI(
          "GPU swap trace: Vulkan debug cvars "
          "swap_shared_memory_checksum={} swap_shared_memory_checksum_budget={} "
          "trace_resolve={} trace_resolve_budget={} "
          "trace_resolve_checksum={} trace_resolve_checksum_budget={} "
          "shader_constants={} shader_constants_budget={} "
          "shader_constants_filter={} "
          "texture_source_checksum={} texture_source_checksum_budget={} "
          "texture_source_filter={} "
          "draw_state={} draw_state_budget={} draw_state_filter={} "
          "vertex_fetch_checksum={} vertex_fetch_checksum_budget={} "
          "vertex_fetch_filter={} "
          "readback_resolve={} recent_present={} scored_present={} "
          "scored_min={}x{} scored_budget={} scored_required_format={} "
          "scored_reject_clear_like={} forced_present={} "
          "debug_ps_output_filter={} debug_ps_output_mode={} "
          "debug_ps_output_secondary_filter={} "
          "debug_ps_output_secondary_mode={} "
          "force_2101010_rgba8={} force_signed_2101010_unorm={} "
          "disable_fetch_exp_adjust={} "
          "forced_source={:08X}+{:08X} size={}x{} pitch={} format={}",
          cvars::vulkan_trace_swap_shared_memory_checksum,
          cvars::vulkan_trace_swap_shared_memory_checksum_budget,
          cvars::vulkan_trace_resolve, cvars::vulkan_trace_resolve_budget,
          cvars::vulkan_trace_resolve_checksum,
          cvars::vulkan_trace_resolve_checksum_budget,
          cvars::vulkan_trace_shader_constants,
          cvars::vulkan_trace_shader_constants_budget,
          cvars::vulkan_trace_shader_constants_shader_filter,
          cvars::vulkan_trace_texture_source_checksum,
          cvars::vulkan_trace_texture_source_checksum_budget,
          cvars::vulkan_trace_texture_source_shader_filter,
          cvars::vulkan_trace_draw_state, cvars::vulkan_trace_draw_state_budget,
          cvars::vulkan_trace_draw_shader_filter,
          cvars::vulkan_trace_vertex_fetch_checksum,
          cvars::vulkan_trace_vertex_fetch_checksum_budget,
          cvars::vulkan_trace_vertex_fetch_shader_filter,
          cvars::vulkan_readback_resolve,
          cvars::vulkan_present_recent_resolve_on_swap,
          cvars::vulkan_present_scored_resolve_on_swap,
          cvars::vulkan_present_scored_resolve_min_width,
          cvars::vulkan_present_scored_resolve_min_height,
          cvars::vulkan_present_scored_resolve_budget,
          cvars::vulkan_present_scored_resolve_required_format,
          cvars::vulkan_present_scored_resolve_reject_clear_like,
          cvars::vulkan_present_forced_resolve_on_swap,
          cvars::vulkan_debug_pixel_shader_output_filter,
          cvars::vulkan_debug_pixel_shader_output_mode,
          cvars::vulkan_debug_pixel_shader_output_secondary_filter,
          cvars::vulkan_debug_pixel_shader_output_secondary_mode,
          cvars::vulkan_force_2101010_rgba8_fallback,
          cvars::vulkan_force_signed_2101010_unorm_fallback,
          cvars::vulkan_debug_texture_fetch_disable_exp_adjust,
          cvars::vulkan_present_forced_resolve_address,
          cvars::vulkan_present_forced_resolve_length,
          cvars::vulkan_present_forced_resolve_width,
          cvars::vulkan_present_forced_resolve_height,
          cvars::vulkan_present_forced_resolve_pitch,
          cvars::vulkan_present_forced_resolve_format);
    }
    XELOGI(
        "GPU swap trace: Vulkan IssueSwap begin frontbuffer={:08X} "
        "guest_size={}x{} display={}x{} frame_current={} frame_completed={} "
        "submission={}",
        frontbuffer_ptr, frontbuffer_width, frontbuffer_height, display_width,
        display_height, frame_current_, frame_completed_, GetCurrentSubmission());
  }

  ui::Presenter* presenter = graphics_system_->presenter();
  if (!presenter) {
    if (cvars::gpu_trace_swap) {
      XELOGI("GPU swap trace: Vulkan IssueSwap skipped, no presenter");
    }
    return;
  }

  // In case the swap command is the only one in the frame.
  if (!BeginSubmission(true)) {
    if (cvars::gpu_trace_swap) {
      XELOGI(
          "GPU swap trace: Vulkan IssueSwap skipped, BeginSubmission failed");
    }
    return;
  }
  if (ShouldTraceVulkanSwapSharedMemoryChecksum()) {
    uint64_t frontbuffer_length =
        uint64_t(frontbuffer_width) * uint64_t(frontbuffer_height) * 4;
    if (frontbuffer_length <= UINT32_MAX) {
      ReadbackSharedMemoryRange(frontbuffer_ptr, uint32_t(frontbuffer_length),
                                "swap", true, false);
      if (!BeginSubmission(true)) {
        if (cvars::gpu_trace_swap) {
          XELOGI(
              "GPU swap trace: Vulkan IssueSwap skipped after shared-memory "
              "readback, BeginSubmission failed");
        }
        return;
      }
    }
  }

  // Obtaining the actual front buffer size to pass to RefreshGuestOutput,
  // resolution-scaled if it's a resolve destination, or not otherwise.
  uint32_t frontbuffer_width_scaled, frontbuffer_height_scaled;
  xenos::TextureFormat frontbuffer_format;
  uint32_t original_swap_fetch[6] = {};
  bool using_resolve_override_for_swap = false;
  bool using_forced_resolve_for_swap = false;
  bool using_scored_resolve_for_swap = false;
  PresentResolveCandidate swap_resolve_override;
  if (cvars::vulkan_present_forced_resolve_on_swap &&
      cvars::vulkan_present_forced_resolve_address &&
      cvars::vulkan_present_forced_resolve_width &&
      cvars::vulkan_present_forced_resolve_height &&
      cvars::vulkan_present_forced_resolve_pitch) {
    swap_resolve_override.address =
        cvars::vulkan_present_forced_resolve_address;
    swap_resolve_override.length =
        cvars::vulkan_present_forced_resolve_length;
    if (!swap_resolve_override.length) {
      uint64_t forced_length =
          uint64_t(cvars::vulkan_present_forced_resolve_width) *
          uint64_t(cvars::vulkan_present_forced_resolve_height) * 4;
      swap_resolve_override.length =
          forced_length <= UINT32_MAX ? uint32_t(forced_length) : 0;
    }
    swap_resolve_override.width =
        cvars::vulkan_present_forced_resolve_width;
    swap_resolve_override.height =
        cvars::vulkan_present_forced_resolve_height;
    swap_resolve_override.pitch =
        cvars::vulkan_present_forced_resolve_pitch;
    swap_resolve_override.format = static_cast<xenos::TextureFormat>(
        cvars::vulkan_present_forced_resolve_format);
    using_forced_resolve_for_swap = true;
  } else if (cvars::vulkan_present_scored_resolve_on_swap) {
    swap_resolve_override = scored_present_resolve_candidate_;
    using_scored_resolve_for_swap = true;
  } else if (cvars::vulkan_present_recent_resolve_on_swap) {
    swap_resolve_override = recent_present_resolve_candidate_;
  }
  if (swap_resolve_override.address &&
      (using_forced_resolve_for_swap ||
       swap_resolve_override.address != frontbuffer_ptr) &&
      swap_resolve_override.width && swap_resolve_override.height &&
      swap_resolve_override.pitch) {
    constexpr uint32_t kSwapFetchRegister =
        XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
    std::memcpy(original_swap_fetch, &register_file_->values[kSwapFetchRegister],
                sizeof(original_swap_fetch));

    xenos::xe_gpu_texture_fetch_t override_fetch =
        register_file_->GetTextureFetch(0);
    override_fetch.base_address = swap_resolve_override.address >> 12;
    override_fetch.format = swap_resolve_override.format;
    override_fetch.pitch = swap_resolve_override.pitch >> 5;
    override_fetch.tiled = true;
    override_fetch.size_2d.width = swap_resolve_override.width - 1;
    override_fetch.size_2d.height = swap_resolve_override.height - 1;
    override_fetch.dimension = xenos::DataDimension::k2DOrStacked;
    override_fetch.mip_address = 0;
    override_fetch.mip_min_level = 0;
    override_fetch.mip_max_level = 0;
    std::memcpy(&register_file_->values[kSwapFetchRegister], &override_fetch,
                sizeof(override_fetch));
    using_resolve_override_for_swap = true;
    if (cvars::gpu_trace_swap) {
      XELOGI(
          "GPU swap trace: Vulkan IssueSwap using {} resolve source "
          "source={:08X}+{:08X} size={}x{} pitch={} format={} sequence={} "
          "score={} nonzero={} varying={} first_sample={:08X} "
          "first_sample_matches={} first_nonzero={:08X} low_variation={} "
          "clear_like={} instead of frontbuffer={:08X}",
          using_forced_resolve_for_swap
              ? "forced"
              : (using_scored_resolve_for_swap ? "scored" : "recent"),
          swap_resolve_override.address, swap_resolve_override.length,
          swap_resolve_override.width, swap_resolve_override.height,
          swap_resolve_override.pitch,
          static_cast<uint32_t>(swap_resolve_override.format),
          swap_resolve_override.sequence, swap_resolve_override.score,
          swap_resolve_override.nonzero_samples,
          swap_resolve_override.varying_samples,
          swap_resolve_override.first_sample_value,
          swap_resolve_override.first_sample_matches,
          swap_resolve_override.first_nonzero_value,
          swap_resolve_override.low_variation,
          swap_resolve_override.clear_like, frontbuffer_ptr);
    }
  }
  VkImageView swap_texture_view = texture_cache_->RequestSwapTexture(
      frontbuffer_width_scaled, frontbuffer_height_scaled, frontbuffer_format);
  if (using_resolve_override_for_swap) {
    constexpr uint32_t kSwapFetchRegister =
        XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0;
    std::memcpy(&register_file_->values[kSwapFetchRegister],
                original_swap_fetch, sizeof(original_swap_fetch));
  }
  // Blue Dragon native-draw HLE step 2 (gpu_bd_hle_present_decoupled): present
  // the decoupled full-surface RT the native field draws rendered into instead
  // of BD's resolved guest front buffer (whose base-0 resolve may be dropped by
  // gpu_bd_hle_drop_resolve). Substitute the captured RT's sampled color view as
  // the swap gamma-pass source; the RT is transitioned to shader-read inside the
  // guest-output submission below (before its render pass). Done before the null
  // check so a stale/absent front buffer (dropped resolve) still presents.
  if (cvars::gpu_bd_hle_present_decoupled &&
      render_target_cache_->HasDecoupledCapture()) {
    VkImageView decoupled_view =
        render_target_cache_->GetDecoupledPresentView();
    if (decoupled_view != VK_NULL_HANDLE) {
      swap_texture_view = decoupled_view;
      if (cvars::gpu_trace_swap) {
        XELOGI(
            "BD HLE present: swap source substituted with decoupled RT view={}",
            reinterpret_cast<uint64_t>(decoupled_view));
      }
    }
  }
  // Blue Dragon FULL native renderer (gpu_bd_native_renderer): record the one
  // held-open native pass into this submission (Brick 2a: clears magenta; Brick
  // 2b records the captured draws) and present its RT directly - proving the
  // native RT -> pass -> present path end-to-end on Turnip, bypassing EDRAM.
  if (cvars::gpu_bd_native_renderer && bd_native_renderer_ &&
      bd_native_renderer_->initialized() && bd_native_field_rendered_) {
    // The field draws were REDIRECTED into the native RT THIS frame (see
    // SubmitBarriersAndEnterRenderTargetCacheRenderPass) and left it in
    // SHADER_READ_ONLY - present it directly (do NOT clear over the draws).
    // Only when field draws actually rendered (else the native RT is empty =
    // black on menus/cutscenes).
    VkImageView native_view = bd_native_renderer_->color_view();
    if (native_view != VK_NULL_HANDLE) {
      swap_texture_view = native_view;
      ++bd_present_native_total_;
    }
  }
  bd_native_field_rendered_ = false;  // reset for next frame
  // DEFINITIVE persistent instrumentation: the last log (near capture) survives
  // logcat rotation and reveals whether the redirect + native present actually
  // fired over the run (gpu_bd_native_renderer only).
  if (cvars::gpu_bd_native_renderer && (++bd_swap_total_ % 30u) == 0u) {
    XELOGI("BD NATIVE totals: swaps={} redirects={} native_presents={}",
           bd_swap_total_, bd_redirect_total_, bd_present_native_total_);
  }
  if (swap_texture_view == VK_NULL_HANDLE) {
    if (cvars::gpu_trace_swap) {
      XELOGI(
          "GPU swap trace: Vulkan IssueSwap skipped, RequestSwapTexture "
          "returned null for frontbuffer={:08X}",
          frontbuffer_ptr);
    }
    return;
  }
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: Vulkan IssueSwap texture view={} scaled_size={}x{} "
        "format={} display={}x{}",
        reinterpret_cast<uint64_t>(swap_texture_view), frontbuffer_width_scaled,
        frontbuffer_height_scaled, static_cast<uint32_t>(frontbuffer_format),
        display_width, display_height);
  }

  const uint32_t guest_output_width = frontbuffer_width_scaled;
  const uint32_t guest_output_height = frontbuffer_height_scaled;
  if (cvars::gpu_trace_swap && display_width && display_height &&
      (display_width != frontbuffer_width ||
       display_height != frontbuffer_height)) {
    XELOGI(
        "GPU swap trace: Vulkan IssueSwap kept guest output at source size "
        "{}x{} for display request {}x{}; swap gamma pass is not a scaler",
        guest_output_width, guest_output_height, display_width,
        display_height);
  }

  const bool guest_output_refreshed = presenter->RefreshGuestOutput(
      guest_output_width, guest_output_height, frontbuffer_width,
      frontbuffer_height,
      [this, frontbuffer_width_scaled, frontbuffer_height_scaled,
       guest_output_width, guest_output_height, frontbuffer_format,
       swap_texture_view](
          ui::Presenter::GuestOutputRefreshContext& context) -> bool {
        // In case the swap command is the only one in the frame.
        if (!BeginSubmission(true)) {
          if (cvars::gpu_trace_swap) {
            XELOGI(
                "GPU swap trace: Vulkan guest output callback skipped, "
                "BeginSubmission failed");
          }
          return false;
        }

        auto& vulkan_context = static_cast<
            ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext&>(
            context);
        uint64_t guest_output_image_version = vulkan_context.image_version();
        if (cvars::gpu_trace_swap) {
          XELOGI(
              "GPU swap trace: Vulkan guest output callback image_version={} "
              "image_ever_written={} source_size={}x{} output_size={}x{} "
              "format={}",
              guest_output_image_version,
              vulkan_context.image_ever_written_previously(),
              frontbuffer_width_scaled, frontbuffer_height_scaled,
              guest_output_width, guest_output_height,
              static_cast<uint32_t>(frontbuffer_format));
        }

        const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
        const ui::vulkan::VulkanDevice::Functions& dfn =
            vulkan_device->functions();
        const VkDevice device = vulkan_device->device();

        uint32_t swap_frame_index =
            uint32_t(frame_current_ % kMaxFramesInFlight);

        // This is according to D3D::InitializePresentationParameters from a
        // game executable, which initializes the 256-entry table gamma ramp for
        // 8_8_8_8 output and the PWL gamma ramp for 2_10_10_10.
        // TODO(Triang3l): Choose between the table and PWL based on
        // DC_LUTA_CONTROL, support both for all formats (and also different
        // increments for PWL).
        bool use_pwl_gamma_ramp =
            frontbuffer_format == xenos::TextureFormat::k_2_10_10_10 ||
            frontbuffer_format ==
                xenos::TextureFormat::k_2_10_10_10_AS_16_16_16_16;

        // TODO(Triang3l): FXAA can result in more than 8 bits of precision.
        context.SetIs8bpc(!use_pwl_gamma_ramp);

        // Update the gamma ramp if it's out of date.
        uint32_t& gamma_ramp_frame_index_ref =
            use_pwl_gamma_ramp ? gamma_ramp_pwl_current_frame_
                               : gamma_ramp_256_entry_table_current_frame_;
        if (gamma_ramp_frame_index_ref == UINT32_MAX) {
          constexpr uint32_t kGammaRampSize256EntryTable =
              sizeof(uint32_t) * 256;
          constexpr uint32_t kGammaRampSizePWL = sizeof(uint16_t) * 2 * 3 * 128;
          constexpr uint32_t kGammaRampSize =
              kGammaRampSize256EntryTable + kGammaRampSizePWL;
          uint32_t gamma_ramp_offset_in_frame =
              use_pwl_gamma_ramp ? kGammaRampSize256EntryTable : 0;
          uint32_t gamma_ramp_upload_offset =
              kGammaRampSize * swap_frame_index + gamma_ramp_offset_in_frame;
          uint32_t gamma_ramp_size = use_pwl_gamma_ramp
                                         ? kGammaRampSizePWL
                                         : kGammaRampSize256EntryTable;
          void* gamma_ramp_frame_upload =
              reinterpret_cast<uint8_t*>(gamma_ramp_upload_mapping_) +
              gamma_ramp_upload_offset;
          if (std::endian::native != std::endian::little &&
              use_pwl_gamma_ramp) {
            // R16G16 is first R16, where the shader expects the base, and
            // second G16, where the delta should be, but gamma_ramp_pwl_rgb()
            // is an array of 32-bit DC_LUT_PWL_DATA registers - swap 16 bits in
            // each 32.
            auto gamma_ramp_pwl_upload =
                reinterpret_cast<reg::DC_LUT_PWL_DATA*>(
                    gamma_ramp_frame_upload);
            const reg::DC_LUT_PWL_DATA* gamma_ramp_pwl = gamma_ramp_pwl_rgb();
            for (size_t i = 0; i < 128 * 3; ++i) {
              reg::DC_LUT_PWL_DATA& gamma_ramp_pwl_upload_entry =
                  gamma_ramp_pwl_upload[i];
              reg::DC_LUT_PWL_DATA gamma_ramp_pwl_entry = gamma_ramp_pwl[i];
              gamma_ramp_pwl_upload_entry.base = gamma_ramp_pwl_entry.delta;
              gamma_ramp_pwl_upload_entry.delta = gamma_ramp_pwl_entry.base;
            }
          } else {
            std::memcpy(
                gamma_ramp_frame_upload,
                use_pwl_gamma_ramp
                    ? static_cast<const void*>(gamma_ramp_pwl_rgb())
                    : static_cast<const void*>(gamma_ramp_256_entry_table()),
                gamma_ramp_size);
          }
          bool gamma_ramp_has_upload_buffer =
              gamma_ramp_upload_buffer_memory_ != VK_NULL_HANDLE;
          ui::vulkan::util::FlushMappedMemoryRange(
              vulkan_device,
              gamma_ramp_has_upload_buffer ? gamma_ramp_upload_buffer_memory_
                                           : gamma_ramp_buffer_memory_,
              gamma_ramp_upload_memory_type_, gamma_ramp_upload_offset,
              gamma_ramp_upload_memory_size_, gamma_ramp_size);
          if (gamma_ramp_has_upload_buffer) {
            // Copy from the host-visible buffer to the device-local one.
            PushBufferMemoryBarrier(
                gamma_ramp_buffer_, gamma_ramp_offset_in_frame, gamma_ramp_size,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED, false);
            SubmitBarriers(true);
            VkBufferCopy gamma_ramp_buffer_copy;
            gamma_ramp_buffer_copy.srcOffset = gamma_ramp_upload_offset;
            gamma_ramp_buffer_copy.dstOffset = gamma_ramp_offset_in_frame;
            gamma_ramp_buffer_copy.size = gamma_ramp_size;
            deferred_command_buffer_.CmdVkCopyBuffer(gamma_ramp_upload_buffer_,
                                                     gamma_ramp_buffer_, 1,
                                                     &gamma_ramp_buffer_copy);
            PushBufferMemoryBarrier(
                gamma_ramp_buffer_, gamma_ramp_offset_in_frame, gamma_ramp_size,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, false);
          }
          // The device-local, but not host-visible, gamma ramp buffer doesn't
          // have per-frame sets of gamma ramps.
          gamma_ramp_frame_index_ref =
              gamma_ramp_has_upload_buffer ? 0 : swap_frame_index;
        }

        // Make sure a framebuffer is available for the current guest output
        // image version.
        size_t swap_framebuffer_index = SIZE_MAX;
        size_t swap_framebuffer_new_index = SIZE_MAX;
        // Try to find the existing framebuffer for the current guest output
        // image version, or an unused (without an existing framebuffer, or with
        // one, but that has never actually been used dynamically) slot.
        for (size_t i = 0; i < swap_framebuffers_.size(); ++i) {
          const SwapFramebuffer& existing_swap_framebuffer =
              swap_framebuffers_[i];
          if (existing_swap_framebuffer.framebuffer != VK_NULL_HANDLE &&
              existing_swap_framebuffer.version == guest_output_image_version) {
            swap_framebuffer_index = i;
            break;
          }
          if (existing_swap_framebuffer.framebuffer == VK_NULL_HANDLE ||
              !existing_swap_framebuffer.last_submission) {
            swap_framebuffer_new_index = i;
          }
        }
        if (swap_framebuffer_index == SIZE_MAX) {
          if (swap_framebuffer_new_index == SIZE_MAX) {
            // Replace the earliest used framebuffer.
            swap_framebuffer_new_index = 0;
            for (size_t i = 1; i < swap_framebuffers_.size(); ++i) {
              if (swap_framebuffers_[i].last_submission <
                  swap_framebuffers_[swap_framebuffer_new_index]
                      .last_submission) {
                swap_framebuffer_new_index = i;
              }
            }
          }
          swap_framebuffer_index = swap_framebuffer_new_index;
          SwapFramebuffer& new_swap_framebuffer =
              swap_framebuffers_[swap_framebuffer_new_index];
          if (new_swap_framebuffer.framebuffer != VK_NULL_HANDLE) {
            if (GetCompletedSubmission() >=
                new_swap_framebuffer.last_submission) {
              dfn.vkDestroyFramebuffer(device, new_swap_framebuffer.framebuffer,
                                       nullptr);
            } else {
              destroy_framebuffers_.emplace_back(
                  new_swap_framebuffer.last_submission,
                  new_swap_framebuffer.framebuffer);
            }
            new_swap_framebuffer.framebuffer = VK_NULL_HANDLE;
          }
          VkImageView guest_output_image_view = vulkan_context.image_view();
          VkFramebufferCreateInfo swap_framebuffer_create_info;
          swap_framebuffer_create_info.sType =
              VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
          swap_framebuffer_create_info.pNext = nullptr;
          swap_framebuffer_create_info.flags = 0;
          swap_framebuffer_create_info.renderPass =
              swap_apply_gamma_render_pass_;
          swap_framebuffer_create_info.attachmentCount = 1;
          swap_framebuffer_create_info.pAttachments = &guest_output_image_view;
          swap_framebuffer_create_info.width = guest_output_width;
          swap_framebuffer_create_info.height = guest_output_height;
          swap_framebuffer_create_info.layers = 1;
          if (dfn.vkCreateFramebuffer(
                  device, &swap_framebuffer_create_info, nullptr,
                  &new_swap_framebuffer.framebuffer) != VK_SUCCESS) {
            XELOGE("Failed to create the Vulkan framebuffer for presentation");
            if (cvars::gpu_trace_swap) {
              XELOGI(
                  "GPU swap trace: Vulkan guest output callback failed to "
                  "create framebuffer for image_version={}",
                  guest_output_image_version);
            }
            return false;
          }
          new_swap_framebuffer.version = guest_output_image_version;
          // The actual submission index will be set if the framebuffer is
          // actually used, not dropped due to some error.
          new_swap_framebuffer.last_submission = 0;
        }

        if (vulkan_context.image_ever_written_previously()) {
          // Insert a barrier after the last presenter's usage of the guest
          // output image. Will be overwriting all the contents, so oldLayout
          // layout is UNDEFINED. The render pass will do the layout transition,
          // but newLayout must not be UNDEFINED.
          PushImageMemoryBarrier(
              vulkan_context.image(),
              ui::vulkan::util::InitializeSubresourceRange(),
              ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask,
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }

        // Blue Dragon native-draw HLE decoupled present: move the captured
        // decoupled color RT from its draw usage to fragment-shader sampled read
        // so the gamma pass can sample it. Pushed here so the barrier is flushed
        // by the SubmitBarriers below (barriers cannot be inside a render pass).
        if (cvars::gpu_bd_hle_present_decoupled &&
            render_target_cache_->HasDecoupledCapture()) {
          render_target_cache_->TransitionDecoupledRTToShaderRead();
        }

        // End the current render pass before inserting barriers and starting a
        // new one, and insert the barrier.
        SubmitBarriers(true);

        SwapFramebuffer& swap_framebuffer =
            swap_framebuffers_[swap_framebuffer_index];
        swap_framebuffer.last_submission = GetCurrentSubmission();

        VkRenderPassBeginInfo render_pass_begin_info;
        render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.pNext = nullptr;
        render_pass_begin_info.renderPass = swap_apply_gamma_render_pass_;
        render_pass_begin_info.framebuffer = swap_framebuffer.framebuffer;
        render_pass_begin_info.renderArea.offset.x = 0;
        render_pass_begin_info.renderArea.offset.y = 0;
        render_pass_begin_info.renderArea.extent.width =
            guest_output_width;
        render_pass_begin_info.renderArea.extent.height =
            guest_output_height;
        render_pass_begin_info.clearValueCount = 0;
        render_pass_begin_info.pClearValues = nullptr;
        deferred_command_buffer_.CmdVkBeginRenderPass(
            &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        if (cvars::vulkan_debug_solid_guest_output) {
          VkClearAttachment solid_clear_attachment;
          solid_clear_attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          solid_clear_attachment.colorAttachment = 0;
          solid_clear_attachment.clearValue.color.float32[0] = 0.95f;
          solid_clear_attachment.clearValue.color.float32[1] = 0.05f;
          solid_clear_attachment.clearValue.color.float32[2] = 0.65f;
          solid_clear_attachment.clearValue.color.float32[3] = 1.0f;
          VkClearRect solid_clear_rect;
          solid_clear_rect.rect.offset.x = 0;
          solid_clear_rect.rect.offset.y = 0;
          solid_clear_rect.rect.extent.width = guest_output_width;
          solid_clear_rect.rect.extent.height = guest_output_height;
          solid_clear_rect.baseArrayLayer = 0;
          solid_clear_rect.layerCount = 1;
          deferred_command_buffer_.CmdVkClearAttachments(
              1, &solid_clear_attachment, 1, &solid_clear_rect);
          deferred_command_buffer_.CmdVkEndRenderPass();
          PushImageMemoryBarrier(
              vulkan_context.image(),
              ui::vulkan::util::InitializeSubresourceRange(),
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask,
              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout);
          EndSubmission(true);
          if (cvars::gpu_trace_swap) {
            XELOGI(
                "GPU swap trace: Vulkan guest output callback submitted solid "
                "debug color submission={} frame_current={}",
                GetCurrentSubmission(), frame_current_);
          }
          return true;
        }

        VkViewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = float(guest_output_width);
        viewport.height = float(guest_output_height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        SetViewport(viewport);
        VkRect2D scissor;
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = guest_output_width;
        scissor.extent.height = guest_output_height;
        SetScissor(scissor);

        BindExternalGraphicsPipeline(
            use_pwl_gamma_ramp ? swap_apply_gamma_pwl_pipeline_
                               : swap_apply_gamma_256_entry_table_pipeline_);

        VkDescriptorSet swap_descriptor_source =
            swap_descriptors_source_[swap_frame_index];
        VkDescriptorImageInfo swap_descriptor_source_image_info;
        swap_descriptor_source_image_info.sampler = VK_NULL_HANDLE;
        swap_descriptor_source_image_info.imageView = swap_texture_view;
        swap_descriptor_source_image_info.imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet swap_descriptor_source_write;
        swap_descriptor_source_write.sType =
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        swap_descriptor_source_write.pNext = nullptr;
        swap_descriptor_source_write.dstSet = swap_descriptor_source;
        swap_descriptor_source_write.dstBinding = 0;
        swap_descriptor_source_write.dstArrayElement = 0;
        swap_descriptor_source_write.descriptorCount = 1;
        swap_descriptor_source_write.descriptorType =
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        swap_descriptor_source_write.pImageInfo =
            &swap_descriptor_source_image_info;
        swap_descriptor_source_write.pBufferInfo = nullptr;
        swap_descriptor_source_write.pTexelBufferView = nullptr;
        dfn.vkUpdateDescriptorSets(device, 1, &swap_descriptor_source_write, 0,
                                   nullptr);

        std::array<VkDescriptorSet, kSwapApplyGammaDescriptorSetCount>
            swap_descriptor_sets{};
        swap_descriptor_sets[kSwapApplyGammaDescriptorSetRamp] =
            swap_descriptors_gamma_ramp_[2 * gamma_ramp_frame_index_ref +
                                         uint32_t(use_pwl_gamma_ramp)];
        swap_descriptor_sets[kSwapApplyGammaDescriptorSetSource] =
            swap_descriptor_source;
        // TODO(Triang3l): Red / blue swap without imageViewFormatSwizzle.
        deferred_command_buffer_.CmdVkBindDescriptorSets(
            VK_PIPELINE_BIND_POINT_GRAPHICS, swap_apply_gamma_pipeline_layout_,
            0, uint32_t(swap_descriptor_sets.size()),
            swap_descriptor_sets.data(), 0, nullptr);

        deferred_command_buffer_.CmdVkDraw(3, 1, 0, 0);

        deferred_command_buffer_.CmdVkEndRenderPass();

        // Insert the release barrier.
        PushImageMemoryBarrier(
            vulkan_context.image(),
            ui::vulkan::util::InitializeSubresourceRange(),
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            ui::vulkan::VulkanPresenter::kGuestOutputInternalStageMask,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            ui::vulkan::VulkanPresenter::kGuestOutputInternalAccessMask,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            ui::vulkan::VulkanPresenter::kGuestOutputInternalLayout);

        // Need to submit all the commands before giving the image back to the
        // presenter so it can submit its own commands for displaying it to the
        // queue, and also need to submit the release barrier.
        EndSubmission(true);
        if (cvars::gpu_trace_swap) {
          XELOGI(
              "GPU swap trace: Vulkan guest output callback submitted "
              "submission={} frame_current={}",
              GetCurrentSubmission(), frame_current_);
        }
        return true;
      });

  // End the frame even if did not present for any reason (the image refresher
  // was not called), to prevent leaking per-frame resources.
  EndSubmission(true);

  // Blue Dragon native-draw HLE decoupled present: clear the per-frame latch so
  // the next frame recaptures (and a frame with no native draws presents BD's
  // own front buffer normally).
  render_target_cache_->ClearDecoupledCapture();

  if (cvars::gpu_trace_vd_swap) {
    XELOGI(
        "VulkanPresenter: IssueSwap compact state refreshed={} "
        "frame_current={} frame_completed={} submission={}",
        guest_output_refreshed ? 1 : 0, frame_current_, frame_completed_,
        GetCurrentSubmission());
    ui::vulkan::VulkanPerfCountersLogSnapshot("vdswap", true);
  }
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: Vulkan IssueSwap end frame_current={} "
        "frame_completed={} submission={}",
        frame_current_, frame_completed_, GetCurrentSubmission());
  }
  ui::vulkan::VulkanPerfCountersLogSnapshot("issue_swap");
}

bool VulkanCommandProcessor::PushBufferMemoryBarrier(
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
    VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
    VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
    uint32_t src_queue_family_index, uint32_t dst_queue_family_index,
    bool skip_if_equal) {
  if (skip_if_equal && src_stage_mask == dst_stage_mask &&
      src_access_mask == dst_access_mask &&
      src_queue_family_index == dst_queue_family_index) {
    return false;
  }

  // Separate different barriers for overlapping buffer ranges into different
  // pipeline barrier commands.
  for (const VkBufferMemoryBarrier& other_buffer_memory_barrier :
       pending_barriers_buffer_memory_barriers_) {
    if (other_buffer_memory_barrier.buffer != buffer ||
        (size != VK_WHOLE_SIZE &&
         offset + size <= other_buffer_memory_barrier.offset) ||
        (other_buffer_memory_barrier.size != VK_WHOLE_SIZE &&
         other_buffer_memory_barrier.offset +
                 other_buffer_memory_barrier.size <=
             offset)) {
      continue;
    }
    if (other_buffer_memory_barrier.offset == offset &&
        other_buffer_memory_barrier.size == size &&
        other_buffer_memory_barrier.srcAccessMask == src_access_mask &&
        other_buffer_memory_barrier.dstAccessMask == dst_access_mask &&
        other_buffer_memory_barrier.srcQueueFamilyIndex ==
            src_queue_family_index &&
        other_buffer_memory_barrier.dstQueueFamilyIndex ==
            dst_queue_family_index) {
      // The barrier is already pending.
      current_pending_barrier_.src_stage_mask |= src_stage_mask;
      current_pending_barrier_.dst_stage_mask |= dst_stage_mask;
      return true;
    }
    SplitPendingBarrier();
    break;
  }

  current_pending_barrier_.src_stage_mask |= src_stage_mask;
  current_pending_barrier_.dst_stage_mask |= dst_stage_mask;
  VkBufferMemoryBarrier& buffer_memory_barrier =
      pending_barriers_buffer_memory_barriers_.emplace_back();
  buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  buffer_memory_barrier.pNext = nullptr;
  buffer_memory_barrier.srcAccessMask = src_access_mask;
  buffer_memory_barrier.dstAccessMask = dst_access_mask;
  buffer_memory_barrier.srcQueueFamilyIndex = src_queue_family_index;
  buffer_memory_barrier.dstQueueFamilyIndex = dst_queue_family_index;
  buffer_memory_barrier.buffer = buffer;
  buffer_memory_barrier.offset = offset;
  buffer_memory_barrier.size = size;
  ui::vulkan::VulkanPerfCountersRecordBufferBarrier();
  return true;
}

bool VulkanCommandProcessor::PushImageMemoryBarrier(
    VkImage image, const VkImageSubresourceRange& subresource_range,
    VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
    VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
    VkImageLayout old_layout, VkImageLayout new_layout,
    uint32_t src_queue_family_index, uint32_t dst_queue_family_index,
    bool skip_if_equal) {
  if (skip_if_equal && src_stage_mask == dst_stage_mask &&
      src_access_mask == dst_access_mask && old_layout == new_layout &&
      src_queue_family_index == dst_queue_family_index) {
    return false;
  }

  // Separate different barriers for overlapping image subresource ranges into
  // different pipeline barrier commands.
  for (const VkImageMemoryBarrier& other_image_memory_barrier :
       pending_barriers_image_memory_barriers_) {
    if (other_image_memory_barrier.image != image ||
        !(other_image_memory_barrier.subresourceRange.aspectMask &
          subresource_range.aspectMask) ||
        (subresource_range.levelCount != VK_REMAINING_MIP_LEVELS &&
         subresource_range.baseMipLevel + subresource_range.levelCount <=
             other_image_memory_barrier.subresourceRange.baseMipLevel) ||
        (other_image_memory_barrier.subresourceRange.levelCount !=
             VK_REMAINING_MIP_LEVELS &&
         other_image_memory_barrier.subresourceRange.baseMipLevel +
                 other_image_memory_barrier.subresourceRange.levelCount <=
             subresource_range.baseMipLevel) ||
        (subresource_range.layerCount != VK_REMAINING_ARRAY_LAYERS &&
         subresource_range.baseArrayLayer + subresource_range.layerCount <=
             other_image_memory_barrier.subresourceRange.baseArrayLayer) ||
        (other_image_memory_barrier.subresourceRange.layerCount !=
             VK_REMAINING_ARRAY_LAYERS &&
         other_image_memory_barrier.subresourceRange.baseArrayLayer +
                 other_image_memory_barrier.subresourceRange.layerCount <=
             subresource_range.baseArrayLayer)) {
      continue;
    }
    if (other_image_memory_barrier.subresourceRange.aspectMask ==
            subresource_range.aspectMask &&
        other_image_memory_barrier.subresourceRange.baseMipLevel ==
            subresource_range.baseMipLevel &&
        other_image_memory_barrier.subresourceRange.levelCount ==
            subresource_range.levelCount &&
        other_image_memory_barrier.subresourceRange.baseArrayLayer ==
            subresource_range.baseArrayLayer &&
        other_image_memory_barrier.subresourceRange.layerCount ==
            subresource_range.layerCount &&
        other_image_memory_barrier.srcAccessMask == src_access_mask &&
        other_image_memory_barrier.dstAccessMask == dst_access_mask &&
        other_image_memory_barrier.oldLayout == old_layout &&
        other_image_memory_barrier.newLayout == new_layout &&
        other_image_memory_barrier.srcQueueFamilyIndex ==
            src_queue_family_index &&
        other_image_memory_barrier.dstQueueFamilyIndex ==
            dst_queue_family_index) {
      // The barrier is already pending.
      current_pending_barrier_.src_stage_mask |= src_stage_mask;
      current_pending_barrier_.dst_stage_mask |= dst_stage_mask;
      return true;
    }
    SplitPendingBarrier();
    break;
  }

  current_pending_barrier_.src_stage_mask |= src_stage_mask;
  current_pending_barrier_.dst_stage_mask |= dst_stage_mask;
  VkImageMemoryBarrier& image_memory_barrier =
      pending_barriers_image_memory_barriers_.emplace_back();
  image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  image_memory_barrier.pNext = nullptr;
  image_memory_barrier.srcAccessMask = src_access_mask;
  image_memory_barrier.dstAccessMask = dst_access_mask;
  image_memory_barrier.oldLayout = old_layout;
  image_memory_barrier.newLayout = new_layout;
  image_memory_barrier.srcQueueFamilyIndex = src_queue_family_index;
  image_memory_barrier.dstQueueFamilyIndex = dst_queue_family_index;
  image_memory_barrier.image = image;
  image_memory_barrier.subresourceRange = subresource_range;
  ui::vulkan::VulkanPerfCountersRecordImageBarrier();
  return true;
}

bool VulkanCommandProcessor::SubmitBarriers(bool force_end_render_pass) {
  assert_true(submission_open_);
  SplitPendingBarrier();
  if (pending_barriers_.empty()) {
    if (force_end_render_pass) {
      EndRenderPass();
    }
    return false;
  }
  // Attribution: this SubmitBarriers is ending a LIVE render pass to flush
  // barriers (a tiler break). Tally what kinds of barriers forced it so the
  // tiler rewrite targets the real cause (buffer/shared-memory vs texture
  // shader-read vs RT/other image transitions).
  if (cvars::vulkan_trace_draw_outcomes_per_frame &&
      current_render_pass_ != VK_NULL_HANDLE) {
    ++brk_open_breaks_;
    brk_buffer_barriers_ +=
        uint32_t(pending_barriers_buffer_memory_barriers_.size());
    // gpu_vulkan_classify_img_sr_breaks: does THIS break's triggering guest draw
    // look like a full-screen composite (the same-pixel input-attachment
    // candidate)? last_guest_draw_desc_ holds the draw whose barriers are ending
    // this pass (captured earlier in IssueDraw). A composite is a rect-list or a
    // small (<=6-vertex) quad that runs a pixel shader and writes color - the
    // shape of a post-process / blend pass sampling the prior render target.
    // Read-only classification; per-break detail logged (throttled) so the
    // consumer shader can be dumped and its texcoord confirmed same-pixel.
    bool classify_img_sr = cvars::gpu_vulkan_classify_img_sr_breaks;
    bool consumer_is_fullscreen_composite = false;
    if (classify_img_sr) {
      const GuestDrawDesc& d = last_guest_draw_desc_;
      bool rect_or_quad =
          d.prim_type == uint32_t(xenos::PrimitiveType::kRectangleList) ||
          (d.host_vertex_count >= 3 && d.host_vertex_count <= 6);
      consumer_is_fullscreen_composite =
          rect_or_quad && d.ps_hash != 0 && d.color_mask != 0;
    }
    bool any_texsample_this_break = false;
    bool any_rtsrc_this_break = false;
    for (const VkImageMemoryBarrier& imb :
         pending_barriers_image_memory_barriers_) {
      if (imb.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        ++brk_img_shaderread_;
        if (classify_img_sr) {
          // A render target transitions to SHADER_READ from a color/depth
          // ATTACHMENT layout (it was just rendered) - i.e. an EDRAM
          // ownership-transfer or resolve SOURCE read. A guest texture loaded
          // from shared memory transitions from TRANSFER_DST / UNDEFINED /
          // PREINITIALIZED (an upload), so it is a sampled texture (e.g. a
          // resolved-scene composite source - the GMEM-residency rework's
          // target). GENERAL is treated as a render target (FSI/aliasing RTs).
          if (imb.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
              imb.oldLayout ==
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
              imb.oldLayout ==
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
              imb.oldLayout == VK_IMAGE_LAYOUT_GENERAL) {
            ++brk_img_sr_rtsrc_;
            any_rtsrc_this_break = true;
          } else {
            ++brk_img_sr_texsample_;
            any_texsample_this_break = true;
          }
          // Throttled per-break detail (consumer identity + the broken image's
          // layout transition) for offline same-pixel confirmation via dumped
          // shaders. Capped per frame to keep the log readable.
          if (brk_img_sr_detail_logged_ < 48) {
            ++brk_img_sr_detail_logged_;
            const GuestDrawDesc& d = last_guest_draw_desc_;
            XELOGI(
                "IMG_SR break: consumer prim={} host_verts={} idx={} "
                "ps_hash={:016X} vs_hash={:016X} blendctl0={:08X} "
                "colormask={:04X} fscomp={} rtsrc={} oldlayout={} newlayout={} "
                "dst_color0_info={:08X} dst_depth_info={:08X} producer_img={:#x}",
                d.prim_type, d.host_vertex_count, d.index_count, d.ps_hash,
                d.vs_hash, d.blendcontrol0, d.color_mask,
                consumer_is_fullscreen_composite ? 1 : 0,
                (imb.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
                 imb.oldLayout ==
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
                 imb.oldLayout ==
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
                 imb.oldLayout == VK_IMAGE_LAYOUT_GENERAL)
                    ? 1
                    : 0,
                uint32_t(imb.oldLayout), uint32_t(imb.newLayout),
                d.color0_info, d.depth_info,
                reinterpret_cast<uintptr_t>(imb.image));
          }
        }
      } else {
        ++brk_img_other_;
      }
    }
    // fscomposite counts a break only when it carries an actual texture-sample
    // barrier (not a pure RT-source transfer/resolve, where last_guest_draw_desc_
    // would be a stale previous draw) AND its consumer draw is a full-screen
    // composite - the same-pixel input-attachment candidate.
    if (classify_img_sr && any_texsample_this_break &&
        consumer_is_fullscreen_composite) {
      ++brk_img_sr_fscomposite_;
    }
    // The render-to-texture (rtsrc) eligible set: a just-rendered RT goes
    // ATTACHMENT->SHADER_READ and the consumer is a full-screen composite. This
    // is the input-attachment / GMEM-residency candidate for BD's village field
    // (where every brk_img_sr is rtsrc, so sr_fscomposite_ above counts 0). The
    // consumer ps_hash in the detail log lets the same-pixel texcoord be
    // confirmed per shader before the merge is built.
    if (classify_img_sr && any_rtsrc_this_break &&
        consumer_is_fullscreen_composite) {
      ++brk_img_sr_rtsrc_fscomp_;
    }
  }
  EndRenderPass();
  for (auto it = pending_barriers_.cbegin(); it != pending_barriers_.cend();
       ++it) {
    auto it_next = std::next(it);
    bool is_last = it_next == pending_barriers_.cend();
    // .data() + offset, not &[offset], for buffer and image barriers, because
    // if there are no buffer or image memory barriers in the last pipeline
    // barriers, the offsets may be equal to the sizes of the vectors.
    deferred_command_buffer_.CmdVkPipelineBarrier(
        it->src_stage_mask ? it->src_stage_mask
                           : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        it->dst_stage_mask ? it->dst_stage_mask
                           : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr,
        uint32_t((is_last ? pending_barriers_buffer_memory_barriers_.size()
                          : it_next->buffer_memory_barriers_offset) -
                 it->buffer_memory_barriers_offset),
        pending_barriers_buffer_memory_barriers_.data() +
            it->buffer_memory_barriers_offset,
        uint32_t((is_last ? pending_barriers_image_memory_barriers_.size()
                          : it_next->image_memory_barriers_offset) -
                 it->image_memory_barriers_offset),
        pending_barriers_image_memory_barriers_.data() +
            it->image_memory_barriers_offset);
  }
  ui::vulkan::VulkanPerfCountersRecordBarrierSubmit(
      uint32_t(pending_barriers_.size()),
      uint32_t(pending_barriers_buffer_memory_barriers_.size()),
      uint32_t(pending_barriers_image_memory_barriers_.size()),
      force_end_render_pass);
  pending_barriers_.clear();
  pending_barriers_buffer_memory_barriers_.clear();
  pending_barriers_image_memory_barriers_.clear();
  current_pending_barrier_.buffer_memory_barriers_offset = 0;
  current_pending_barrier_.image_memory_barriers_offset = 0;
  return true;
}

void VulkanCommandProcessor::SubmitBarriersAndEnterRenderTargetCacheRenderPass(
    VkRenderPass render_pass,
    const VulkanRenderTargetCache::Framebuffer* framebuffer,
    GpuPassKind pass_kind) {
  // Blue Dragon FULL native renderer (gpu_bd_native_renderer): redirect BD's
  // field draws (pitch 720) into the native single-pass RT so the REAL geometry
  // renders natively - reusing xenia's translated pipelines (RGBA8 is format-
  // compatible with BD's k_8_8_8_8 field color; load/store ops don't affect
  // render-pass compatibility). All field draws share this render_pass +
  // framebuffer, so they stay in ONE held-open pass (no per-tile break, no EDRAM
  // xfer/resolve) = the pass collapse. Gated, default-off. NEXT: tiling window-
  // offset handling + bin-once so both tiles land correctly in the one RT.
  auto bd_rb_surface_info = register_file_->Get<reg::RB_SURFACE_INFO>();
  bool bd_native_gate = false;
  if (cvars::gpu_bd_native_renderer && bd_native_renderer_ &&
      bd_native_renderer_->initialized() &&
      bd_rb_surface_info.surface_pitch == 720) {
    // Diagnostic: log the actual msaa + color format for pitch-720 field draws
    // to see WHY the redirect gate excludes them (redirects=0 was measured).
    auto bd_color_format =
        register_file_
            ->Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[0])
            .color_format;
    VkFormat bd_vk_format =
        render_target_cache_->GetColorVulkanFormat(bd_color_format);
    // The field is k2X (2x MSAA) - MsaaSamples enum: k1X=0, k2X=1, k4X=2. Match
    // the native RT samples to the field (render-pass compat). Bands when presented
    // directly are the MSAA - the render pass resolves color to a single-sample
    // present image (bd_native_renderer_ resolve attachment).
    VkSampleCountFlagBits bd_samples =
        bd_rb_surface_info.msaa_samples == xenos::MsaaSamples::k4X
            ? VK_SAMPLE_COUNT_4_BIT
            : (bd_rb_surface_info.msaa_samples == xenos::MsaaSamples::k2X
                   ? VK_SAMPLE_COUNT_2_BIT
                   : VK_SAMPLE_COUNT_1_BIT);
    VkFormat bd_depth_vk = render_target_cache_->GetDepthVulkanFormat(
        register_file_->Get<reg::RB_DEPTH_INFO>().depth_format);
    bool bd_fmt_ok = bd_native_renderer_->EnsureColorFormat(
        bd_vk_format, bd_depth_vk, bd_samples);
    static std::atomic<uint32_t> s_bd_gate_diag{0};
    if (s_bd_gate_diag.fetch_add(1) < 8) {
      // DIMENSIONS: scissor BR (br_x/br_y = the actual field draw extent),
      // window offset (tile placement), surface pitch/height - to resolve whether
      // BD's field is 720-wide portrait, 1280-wide tiled, rotated, or a crop.
      uint32_t scissor_br =
          register_file_->values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
      uint32_t scissor_tl =
          register_file_->values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];
      uint32_t win_off =
          register_file_->values[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
      XELOGI(
          "BD NATIVE gate@pitch720: msaa={} vk_format={} fmt_ok={} | DIMS "
          "pitch={} scissor_tl={:08X} scissor_br={:08X} (br_x={} br_y={}) "
          "win_off={:08X}",
          uint32_t(bd_rb_surface_info.msaa_samples), uint32_t(bd_vk_format),
          bd_fmt_ok, uint32_t(bd_rb_surface_info.surface_pitch), scissor_tl,
          scissor_br, scissor_br & 0x7FFF, (scissor_br >> 16) & 0x7FFF, win_off);
    }
    bd_native_gate = bd_fmt_ok;
  }
  if (bd_native_gate) {
    static VulkanRenderTargetCache::Framebuffer s_bd_native_fb;
    s_bd_native_fb.framebuffer = bd_native_renderer_->framebuffer();
    s_bd_native_fb.host_extent = VkExtent2D{bd_native_renderer_->width(),
                                            bd_native_renderer_->height()};
    s_bd_native_fb.color_view = bd_native_renderer_->color_view();
    // First field draw of the frame CLEARs the native RT; all subsequent
    // (including re-begins after non-720 draws break the pass) LOAD it so the
    // geometry ACCUMULATES instead of being wiped (the black-render fix).
    render_pass = bd_native_field_rendered_
                      ? bd_native_renderer_->render_pass_load()
                      : bd_native_renderer_->render_pass();
    framebuffer = &s_bd_native_fb;
    bd_native_field_rendered_ = true;
    // NOTE: the window offset is the CORRECT screen transform (NOT a tile shift) -
    // ignoring it shifts the geometry wrong (Shu -> center). The clean left region
    // already has correct screen positioning. Right-region-black is a separate
    // issue (tile-2 not landing at 608..1280 even with the offset applied).
    ++bd_redirect_total_;
    static std::atomic<uint32_t> s_bd_redirect_log{0};
    if (s_bd_redirect_log.fetch_add(1) < 3) {
      XELOGI("BD NATIVE renderer: redirected a field draw into the native pass");
    }
  }
  // BD input-attachment merge (redirect): this draw was detected as a same-pixel
  // composite consumer of the current (producer) pass's RT. Instead of ending the
  // producer pass + barrier + a new pass, merge them: patch the producer's
  // recorded BeginRenderPass to the 2-subpass feedback render pass + a dual-RT
  // framebuffer {producer, consumer}, and advance to subpass 1 - the consumer
  // reads the producer as an INPUT ATTACHMENT (GMEM-resident, no store->DRAM->
  // sample). The producer's color->input layout move is the render pass's 0->1
  // subpass dependency, so the pending producer->SHADER_READ barrier is discarded.
  if (feedback_merge_active_ && current_render_pass_ != VK_NULL_HANDLE &&
      feedback_producer_begin_pos_ != SIZE_MAX &&
      deferred_command_buffer_.IsCommandPositionInRange(
          feedback_producer_begin_pos_) &&
      current_framebuffer_ &&
      current_framebuffer_->color_view != VK_NULL_HANDLE && framebuffer &&
      framebuffer->color_view != VK_NULL_HANDLE &&
      render_pass != current_render_pass_) {
    // BD merge diag: the input attachment is read 1:1 with the framebuffer, so
    // the producer + consumer RTs must be the SAME extent or subpassLoad samples
    // the wrong texels (BD's composites render to oversized tile-rounded RTs of
    // varying heights). Log + gate on it.
    bool feedback_extent_match =
        current_framebuffer_->host_extent.width ==
            framebuffer->host_extent.width &&
        current_framebuffer_->host_extent.height ==
            framebuffer->host_extent.height;
    // Also require DISTINCT producer/consumer color views: identical views = an
    // in-place feedback (attachment 0 == attachment 1) which is degenerate for a
    // 2-attachment render pass (read+write the same image in subpass 1).
    bool feedback_distinct_views =
        current_framebuffer_->color_view != framebuffer->color_view;
    XELOGI(
        "feedback merge attempt: producer {}x{} pv={:#x} consumer {}x{} cv={:#x} "
        "extent_match={} distinct={} texcount={}",
        current_framebuffer_->host_extent.width,
        current_framebuffer_->host_extent.height,
        reinterpret_cast<uintptr_t>(current_framebuffer_->color_view),
        framebuffer->host_extent.width, framebuffer->host_extent.height,
        reinterpret_cast<uintptr_t>(framebuffer->color_view), feedback_extent_match,
        feedback_distinct_views, feedback_merge_texcount_);
    // In-place feedback (producer == consumer RT) uses the 1-attachment self-
    // dependency variant; distinct producer/consumer RTs use the 2-attachment
    // variant. BD's composites are all in-place.
    bool feedback_in_place = !feedback_distinct_views;
    VulkanRenderTargetCache::RenderPassKey consumer_key =
        render_target_cache_->last_update_render_pass_key();
    VkRenderPass feedback_render_pass = render_target_cache_->GetFeedbackRenderPass(
        consumer_key.color_0_view_format, consumer_key.color_0_view_format,
        consumer_key.msaa_samples, feedback_in_place);
    VkFramebuffer feedback_framebuffer =
        feedback_render_pass != VK_NULL_HANDLE
            ? render_target_cache_->GetFeedbackFramebuffer(
                  current_framebuffer_->color_view, framebuffer->color_view,
                  framebuffer->host_extent, feedback_render_pass, feedback_in_place)
            : VK_NULL_HANDLE;
    if (feedback_merge_texcount_ == 1 && feedback_extent_match &&
        feedback_render_pass != VK_NULL_HANDLE &&
        feedback_framebuffer != VK_NULL_HANDLE) {
      feedback_merge_in_place_ = feedback_in_place;
      FlushPendingMergeRun();
      // Discard the pending barriers (the producer->SHADER_READ is the render
      // pass's job; emitting it would move the producer out of COLOR_ATTACHMENT).
      pending_barriers_.clear();
      pending_barriers_buffer_memory_barriers_.clear();
      pending_barriers_image_memory_barriers_.clear();
      current_pending_barrier_.buffer_memory_barriers_offset = 0;
      current_pending_barrier_.image_memory_barriers_offset = 0;
      deferred_command_buffer_.PatchBeginRenderPassTargets(
          feedback_producer_begin_pos_, feedback_render_pass,
          feedback_framebuffer);
      deferred_command_buffer_.CmdVkNextSubpass(VK_SUBPASS_CONTENTS_INLINE);
      current_render_pass_ = feedback_render_pass;
      current_framebuffer_ = framebuffer;
      feedback_producer_begin_pos_ = SIZE_MAX;
      ++rt_feedback_merges_;
      return;
    }
  }
  // Instrumentation: attribute per-draw render-pass breaks. A break here is
  // caused either by SubmitBarriers ending the pass for a pending barrier, or by
  // the render pass / framebuffer changing (RT reconfiguration).
  bool was_in_render_pass = current_render_pass_ != VK_NULL_HANDLE;
  SubmitBarriers(false);
  if (was_in_render_pass && current_render_pass_ == VK_NULL_HANDLE) {
    // SubmitBarriers ended the pass to emit a pending barrier.
    ++rt_pass_break_barrier_;
  }
  if (current_render_pass_ == render_pass &&
      current_framebuffer_ == framebuffer) {
    return;
  }
  if (current_render_pass_ != VK_NULL_HANDLE) {
    // Ending because the render pass / framebuffer changed (RT reconfig).
    // Lever 2: this direct CmdVkEndRenderPass bypasses EndRenderPass(), so flush
    // the pending concatenation run (it belongs to the old pass) before ending it.
    FlushPendingMergeRun();
    ++rt_pass_break_rt_change_;
    // gpu_vulkan_retro_depth_none: this direct end bypasses EndRenderPass() -
    // apply the hindsight depth-none patch for the ending pass here too.
    RetroPatchDepthNoneAtPassEnd();
    deferred_command_buffer_.CmdVkEndRenderPass();
    // End-of-pass timestamp AFTER EndRenderPass so it captures the TBDR tile
    // store/flush (the deferred binning+fragment work), not just draw recording.
    RecordPassTimestamp(false);
    // gpu_trace_resolve_timing: identify the small-draw oversized-RT pass that
    // just ended (current_pass_kind_ / current_framebuffer_ still the old pass).
    MaybeLogSmallGuestPass();
  }
  current_render_pass_ = render_pass;
  current_framebuffer_ = framebuffer;
  // Safe DONT_CARE: if the draw opening this pass provably overwrites the
  // whole render area for some attachments, begin with a load-DONT_CARE
  // variant (compatible - load/store ops don't affect render pass
  // compatibility, so pipelines and the framebuffer remain valid, and
  // current_render_pass_ keeps tracking the original for the resume compare).
  VkRenderPass begin_render_pass = render_pass;
  // gpu_vulkan_skip_unused_depth_store: when the draw opening this GUEST pass
  // provably never touches depth/stencil, begin the depth attachment with
  // loadOp=DONT_CARE + storeOp=NONE (skips its oversized tile load+store while
  // preserving the depth EDRAM memory). Guarded to guest passes so transfer /
  // resolve passes - which DO write their attachment - stay normal. The draw path
  // breaks this pass before any depth-using draw, so the attachment is never
  // accessed (STORE_OP_NONE validity) and its memory is reloaded by that next
  // normal pass. Same-variant compatibility as the dc_safe variant above.
  current_pass_depth_store_none_ = false;
  if (cvars::gpu_vulkan_skip_unused_depth_store && depth_store_none_pending_ &&
      pass_kind == GpuPassKind::kGuest) {
    VkRenderPass depth_none_variant =
        render_target_cache_->GetDepthStoreNoneVariantForLastUpdate(render_pass);
    if (depth_none_variant != render_pass) {
      begin_render_pass = depth_none_variant;
      current_pass_depth_store_none_ = true;
      ++draw_outcomes_depth_none_passes_;
    }
  }
  if (begin_render_pass == render_pass && dc_safe_pending_state_mask_) {
    const VkExtent2D& dc_safe_extent = framebuffer->host_extent;
    if (dc_safe_pending_rect_[0] <= 0 && dc_safe_pending_rect_[1] <= 0 &&
        dc_safe_pending_rect_[2] >= int32_t(dc_safe_extent.width) &&
        dc_safe_pending_rect_[3] >= int32_t(dc_safe_extent.height)) {
      VkRenderPass dc_safe_variant =
          render_target_cache_->GetLoadDontCareVariantForLastUpdate(
              render_pass, dc_safe_pending_state_mask_);
      if (dc_safe_variant != render_pass) {
        begin_render_pass = dc_safe_variant;
        ++draw_outcomes_dc_safe_passes_;
        draw_outcomes_dc_safe_atts_ += xe::bit_count(
            dc_safe_pending_state_mask_ &
            render_target_cache_->last_update_render_pass_key()
                .depth_and_color_used);
      }
    }
  }
  VkRenderPassBeginInfo render_pass_begin_info;
  render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin_info.pNext = nullptr;
  render_pass_begin_info.renderPass = begin_render_pass;
  render_pass_begin_info.framebuffer = framebuffer->framebuffer;
  render_pass_begin_info.renderArea.offset.x = 0;
  render_pass_begin_info.renderArea.offset.y = 0;
  // TODO(Triang3l): Actual dirty width / height in the deferred command
  // buffer.
  render_pass_begin_info.renderArea.extent = framebuffer->host_extent;
  // BD-30 lever: the EDRAM RT cache allocates host RTs at the EDRAM-tile-rounded
  // height (1280x720 guest -> 1280x2048 host, some up to x8192), and renderArea =
  // full host_extent makes the Adreno TBDR load/store/bin the oversized off-screen
  // rows every pass = the dominant GPU emulation overhead (per-pass timestamps:
  // a 1-draw pass over a 720x1824 RT cost 51ms). Clamp renderArea to the guest
  // scissor's max extent so only the active region is tiled. Gated for safe A/B;
  // validate render correctness (a too-small renderArea would clip later draws).
  if (cvars::gpu_clamp_renderarea_to_scissor) {
    draw_util::Scissor scissor;
    draw_util::GetScissor(*register_file_, scissor);
    uint32_t max_x = scissor.offset[0] + scissor.extent[0];
    uint32_t max_y = scissor.offset[1] + scissor.extent[1];
    if (max_x > 0 && max_x < render_pass_begin_info.renderArea.extent.width) {
      render_pass_begin_info.renderArea.extent.width = max_x;
    }
    if (max_y > 0 && max_y < render_pass_begin_info.renderArea.extent.height) {
      render_pass_begin_info.renderArea.extent.height = max_y;
    }
  }
  // LRZ spike: when forcing depth loadOp=CLEAR (gpu_lrz_spike_depth_clear), a clear
  // value must be supplied for the depth attachment (always attachments[0]). Clear
  // to the far plane so the opaque depth establishes valid LRZ for foliage to
  // early-reject against. CmdVkBeginRenderPass deep-copies pClearValues.
  VkClearValue lrz_spike_clear_values[1];
  if (cvars::gpu_lrz_spike_depth_clear &&
      (render_target_cache_->last_update_render_pass_key().depth_and_color_used &
       0b1)) {
    lrz_spike_clear_values[0].depthStencil.depth = 1.0f;
    lrz_spike_clear_values[0].depthStencil.stencil = 0;
    render_pass_begin_info.clearValueCount = 1;
    render_pass_begin_info.pClearValues = lrz_spike_clear_values;
  } else {
    render_pass_begin_info.clearValueCount = 0;
    render_pass_begin_info.pClearValues = nullptr;
  }
  // BD input-attachment merge (gpu_vulkan_feedback_merge): capture this pass's
  // BeginRenderPass stream position BEFORE recording it, so a same-pixel
  // composite consumer detected at the next break can repoint this command to
  // the 2-subpass feedback render pass (this pass becomes producer subpass 0).
  feedback_producer_begin_pos_ =
      cvars::gpu_vulkan_feedback_merge
          ? deferred_command_buffer_.command_stream_size_elements()
          : SIZE_MAX;
  // gpu_vulkan_retro_depth_none (frame-graph recompiler increment A): capture
  // this pass's recorded begin position + its depth-none render-pass variant so
  // EndRenderPass can retroactively patch the begin if hindsight shows no draw
  // in the pass touched depth/stencil. Guest/composite passes only (EDRAM
  // transfer passes write attachments outside guest depth state); skipped when
  // another variant was already chosen for this begin, when the LRZ depth-clear
  // forces depth writes, when in-pass transfers may fold depth-writing quads
  // into this pass, or when the opaque depth prepass splices depth writes in.
  retro_depth_begin_pos_ = SIZE_MAX;
  retro_pass_depth_used_ = false;
  retro_depth_variant_ = VK_NULL_HANDLE;
  {
    VulkanRenderTargetCache::RenderPassKey retro_key =
        render_target_cache_->last_update_render_pass_key();
    bool retro_want_depth = cvars::gpu_vulkan_retro_depth_none &&
                            (retro_key.depth_and_color_used & 0b1) != 0;
    bool retro_want_color = cvars::gpu_vulkan_retro_color_dontcare &&
                            (retro_key.depth_and_color_used >> 1) != 0;
    if ((retro_want_depth || retro_want_color) &&
        begin_render_pass == render_pass &&
        (pass_kind == GpuPassKind::kGuest ||
         pass_kind == GpuPassKind::kGuestComposite) &&
        !cvars::gpu_lrz_spike_depth_clear &&
        !cvars::gpu_vulkan_inpass_edram_transfers &&
        !cvars::gpu_opaque_depth_prepass) {
      if (retro_want_depth) {
        // Captured at begin: encodes the Vulkan 1.3 STORE_OP_NONE availability
        // gate; also proves this render pass is last_update's (variant != it).
        VkRenderPass retro_depth_variant =
            render_target_cache_->GetDepthStoreNoneVariantForLastUpdate(
                render_pass);
        retro_depth_variant_ = (retro_depth_variant != render_pass)
                                   ? retro_depth_variant
                                   : VK_NULL_HANDLE;
      }
      for (RetroCoverage& retro_cov : retro_color_cov_) {
        retro_cov.Reset();
      }
      retro_pass_key_ = retro_key;
      retro_pass_extent_ = framebuffer->host_extent;
      retro_depth_framebuffer_ = framebuffer->framebuffer;
      retro_depth_begin_pos_ =
          deferred_command_buffer_.command_stream_size_elements();
    }
  }
  deferred_command_buffer_.CmdVkBeginRenderPass(&render_pass_begin_info,
                                                VK_SUBPASS_CONTENTS_INLINE);
  // Opaque depth pre-pass: mark the splice point right AFTER BeginRenderPass so
  // the captured opaque depth-only draws land before the color stream. Unit 3
  // fills prepass_command_buffer_; until then this is inert (empty splice).
  if (cvars::gpu_opaque_depth_prepass) {
    prepass_command_buffer_.Reset();
    prepass_insert_pos_ = deferred_command_buffer_.command_stream_size_elements();
    prepass_active_ = true;
    // BLACK-FRAME FIX: the prepass records SELF-CONTAINED opaque draws (full
    // pipeline + descriptor + dynamic-state binds) into prepass_command_buffer_,
    // which EndRenderPass splices to the FRONT of this pass. That spliced block
    // leaves a full set of host binds in effect at execute time, but the first
    // ORIGINAL color command was recorded (emit-on-change) assuming the normal
    // fresh-pass leading state - so it would NOT re-emit those binds and would
    // execute against the prepass block's inherited (wrong) pipeline/descriptors/
    // dynamic state = black/garbage. Invalidate the host-state tracker here so the
    // first color draw of this pass re-emits ALL binds (mirrors the BeginSubmission
    // reset at the top of BeginSubmission), overwriting whatever the spliced block
    // left bound. Do NOT touch current_render_pass_/current_framebuffer_ (owned by
    // this BeginRenderPass). Only runs when the cvar is on (default-off = no cost).
    current_guest_graphics_pipeline_ = VK_NULL_HANDLE;
    current_external_graphics_pipeline_ = VK_NULL_HANDLE;
    current_guest_graphics_pipeline_layout_ = nullptr;
    current_graphics_descriptor_sets_bound_up_to_date_ = 0;
    dynamic_viewport_update_needed_ = true;
    dynamic_scissor_update_needed_ = true;
    dynamic_depth_bias_update_needed_ = true;
    dynamic_blend_constants_update_needed_ = true;
    dynamic_stencil_compare_mask_front_update_needed_ = true;
    dynamic_stencil_compare_mask_back_update_needed_ = true;
    dynamic_stencil_write_mask_front_update_needed_ = true;
    dynamic_stencil_write_mask_back_update_needed_ = true;
    dynamic_stencil_reference_front_update_needed_ = true;
    dynamic_stencil_reference_back_update_needed_ = true;
  }
  RecordPassTimestamp(true, pass_kind);
  // gpu_trace_resolve_timing: remember this pass's kind + its draw-count baseline
  // so MaybeLogSmallGuestPass can compute the pass draw count at end. Set only on
  // an actual begin (the early-return above keeps the first enter's kind).
  if (cvars::gpu_trace_resolve_timing) {
    current_pass_kind_ = pass_kind;
    pass_begin_draws_ = deferred_command_buffer_.record_stats().draws;
  }
  ui::vulkan::VulkanPerfCountersRecordRenderPassBegin(false);
}

bool VulkanCommandProcessor::RetroCoverage::AddInterval(int32_t a, int32_t b) {
  if (a >= b) {
    return true;
  }
  // Merge [a,b) with any overlapping or adjacent existing intervals; restart
  // the scan after each merge because the widened interval may now touch
  // earlier entries.
  for (uint32_t i = 0; i < interval_count;) {
    if (x0[i] <= b && a <= x1[i]) {
      a = std::min(a, x0[i]);
      b = std::max(b, x1[i]);
      --interval_count;
      x0[i] = x0[interval_count];
      x1[i] = x1[interval_count];
      i = 0;
      continue;
    }
    ++i;
  }
  if (interval_count >= kMaxIntervals) {
    return false;
  }
  x0[interval_count] = a;
  x1[interval_count] = b;
  ++interval_count;
  return true;
}

void VulkanCommandProcessor::RetroPatchDepthNoneAtPassEnd() {
  // Frame-graph recompiler retro patch (increments A + B(a)): hindsight
  // load/store elision for the ending pass. Combines:
  // - gpu_vulkan_retro_depth_none: if NO draw enabled depth test/write or
  //   stencil, the depth attachment gets loadOp=DONT_CARE + storeOp=NONE
  //   (STORE_OP_NONE preserves the contents the pass never accessed).
  // - gpu_vulkan_retro_color_dontcare: color attachments whose coverage UNION
  //   of provable replace draws spans the whole render area (without any
  //   dst-reading write before that) get loadOp=DONT_CARE (the load is dead -
  //   every texel is unconditionally overwritten, and replace writes never
  //   read what the load would have brought in).
  // The recorded BeginRenderPass is repointed to a render-pass VARIANT derived
  // from the KEY captured at begin (at the RT-change end path last_update has
  // already moved to the NEXT pass, so ForLastUpdate getters must not be used
  // here). Load/store ops do not affect Vulkan render pass compatibility, so
  // the recorded framebuffer and all pipelines stay valid. Never breaks a pass
  // - an ineligible pass simply keeps its original begin.
  if (retro_depth_begin_pos_ == SIZE_MAX) {
    return;
  }
  size_t begin_pos = retro_depth_begin_pos_;
  retro_depth_begin_pos_ = SIZE_MAX;
  uint32_t load_dont_care_mask = 0;
  if (cvars::gpu_vulkan_retro_color_dontcare) {
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      const RetroCoverage& cov = retro_color_cov_[i];
      if (cov.complete && !cov.poisoned) {
        load_dont_care_mask |= uint32_t(1) << (1 + i);
      }
    }
    // Diagnostic (own budget - the depth diag exhausts the shared one): per
    // ended captured pass, the coverage state of every bound color attachment,
    // so a zero-engagement run shows exactly where the chain breaks (no
    // contributors at all vs partial union vs poisoned).
    if (draw_outcomes_retro_color_diag_ < 8 &&
        (retro_pass_key_.depth_and_color_used >> 1)) {
      ++draw_outcomes_retro_color_diag_;
      XELOGI(
          "retro_color end: used={:02x} n=[{} {} {} {}] first0=[{},{}) "
          "poi=[{}{}{}{}] comp=[{}{}{}{}] ext={}x{}",
          retro_pass_key_.depth_and_color_used, retro_color_cov_[0].interval_count,
          retro_color_cov_[1].interval_count, retro_color_cov_[2].interval_count,
          retro_color_cov_[3].interval_count,
          retro_color_cov_[0].interval_count ? retro_color_cov_[0].x0[0] : 0,
          retro_color_cov_[0].interval_count ? retro_color_cov_[0].x1[0] : 0,
          uint32_t(retro_color_cov_[0].poisoned),
          uint32_t(retro_color_cov_[1].poisoned),
          uint32_t(retro_color_cov_[2].poisoned),
          uint32_t(retro_color_cov_[3].poisoned),
          uint32_t(retro_color_cov_[0].complete),
          uint32_t(retro_color_cov_[1].complete),
          uint32_t(retro_color_cov_[2].complete),
          uint32_t(retro_color_cov_[3].complete), retro_pass_extent_.width,
          retro_pass_extent_.height);
    }
  }
  bool depth_none = cvars::gpu_vulkan_retro_depth_none &&
                    retro_depth_variant_ != VK_NULL_HANDLE &&
                    !retro_pass_depth_used_;
  if (!load_dont_care_mask && !depth_none) {
    return;
  }
  if (!deferred_command_buffer_.IsCommandPositionInRange(begin_pos)) {
    return;
  }
  VkRenderPass variant = render_target_cache_->GetHostRenderTargetsRenderPass(
      retro_pass_key_, load_dont_care_mask, depth_none);
  if (variant == VK_NULL_HANDLE) {
    return;
  }
  deferred_command_buffer_.PatchBeginRenderPassTargets(begin_pos, variant,
                                                       retro_depth_framebuffer_);
  if (depth_none) {
    ++draw_outcomes_retro_depth_none_;
  }
  draw_outcomes_retro_color_atts_ += xe::bit_count(load_dont_care_mask);
}

void VulkanCommandProcessor::EndRenderPass() {
  assert_true(submission_open_);
  if (current_render_pass_ == VK_NULL_HANDLE) {
    return;
  }
  // gpu_vulkan_retro_depth_none: hindsight depth-none patch for the ending pass.
  RetroPatchDepthNoneAtPassEnd();
  // Lever 2 (vulkan_merge_draws): the pending draw-concatenation run's draws
  // belong to THIS render pass - realize them before it ends. This is the master
  // pass-end flush (covers SubmitBarriers force/barrier breaks and the IssueCopy/
  // IssueSwap/EndSubmission pass teardowns). No-op when no run is pending, and
  // never called between mergeable draws (the pass stays open), so coalescing is
  // preserved.
  FlushPendingMergeRun();
  // Opaque depth pre-pass: splice the captured opaque depth-only draws in right
  // after BeginRenderPass (before the color stream), so the color draws early-Z
  // reject against the primed depth. No-op while prepass_command_buffer_ is
  // empty (Unit 3 not yet filling it).
  if (prepass_active_) {
    deferred_command_buffer_.InsertStreamFrom(prepass_insert_pos_,
                                              prepass_command_buffer_);
    prepass_active_ = false;
  }
  deferred_command_buffer_.CmdVkEndRenderPass();
  // End-of-pass timestamp AFTER EndRenderPass to capture the TBDR tile store.
  RecordPassTimestamp(false);
  // gpu_trace_resolve_timing: identify the small-draw oversized-RT pass that just
  // ended (current_pass_kind_ / current_framebuffer_ still valid here).
  MaybeLogSmallGuestPass();
  current_render_pass_ = VK_NULL_HANDLE;
  current_framebuffer_ = nullptr;
  // gpu_vulkan_skip_unused_depth_store: the depth-store-NONE state belongs to the
  // pass that just ended; clear it so a stale value can't be observed before the
  // next begin (which re-derives it).
  current_pass_depth_store_none_ = false;
}

void VulkanCommandProcessor::RecordPassTimestamp(bool is_begin,
                                                 GpuPassKind kind) {
  if (!cvars::vulkan_trace_pass_timestamps ||
      gpu_pass_timestamp_pool_ == VK_NULL_HANDLE ||
      gpu_pass_bracket_count_ >= kMaxPassBrackets) {
    return;
  }
  uint32_t pass_base =
      uint32_t(frame_current_ % kMaxFramesInFlight) * (2u * kMaxPassBrackets);
  uint32_t slot =
      pass_base + 2u * gpu_pass_bracket_count_ + (is_begin ? 0u : 1u);
  deferred_command_buffer_.CmdVkWriteTimestamp(
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpu_pass_timestamp_pool_, slot);
  // Composition snapshot for the inter-pass gap attribution.
  {
    const DeferredCommandBuffer::RecordStats& record_stats =
        deferred_command_buffer_.record_stats();
    PassBoundarySnap& snap =
        (is_begin ? gap_snap_begin_
                  : gap_snap_end_)[frame_current_ % kMaxFramesInFlight]
                                  [gpu_pass_bracket_count_];
    snap.dispatches = record_stats.dispatches;
    snap.buffer_copies = record_stats.buffer_copies;
    snap.buffer_image_copies = record_stats.buffer_image_copies;
    snap.barriers = record_stats.barriers;
    snap.draws = record_stats.draws;
    snap.framebuffer_id = uint32_t(
        (reinterpret_cast<uintptr_t>(current_framebuffer_) >> 4) & 0xFFFFu);
    snap.buffer_copy_bytes = record_stats.buffer_copy_bytes;
    // gpu_trace_resolve_timing: tag the begin bracket with its kind so the
    // readback can split GPU time into guest / transfer / resolve / store. The
    // end bracket's kind is unused (the begin snap carries it).
    if (is_begin) {
      snap.kind = uint8_t(kind);
    }
  }
  // Identify each pass's render target (correlate fb id from "GPU pass split" to
  // its host dimensions) to find what an anomalously expensive 1-draw pass renders.
  if (is_begin && current_framebuffer_) {
    XELOGI("PASS fb={:04x} {}x{}",
           uint32_t((reinterpret_cast<uintptr_t>(current_framebuffer_) >> 4) &
                    0xFFFFu),
           current_framebuffer_->host_extent.width,
           current_framebuffer_->host_extent.height);
  }
  if (!is_begin) {
    ++gpu_pass_bracket_count_;
  }
}

void VulkanCommandProcessor::RecordResolveTimingBracket(bool is_begin,
                                                        GpuPassKind kind) {
  if (!cvars::gpu_trace_resolve_timing) {
    return;
  }
  // Reuses the per-pass timestamp pool/counter (so it also needs
  // vulkan_trace_pass_timestamps for the readback). These resolve / clear /
  // host-depth-store dispatches run with no render pass open, so each
  // begin/end pair is a self-contained balanced bracket in the chronological
  // stream, and its measured span is attributed OUT of the inter-pass gap.
  RecordPassTimestamp(is_begin, kind);
}

void VulkanCommandProcessor::MaybeLogSmallGuestPass() {
  if (!cvars::gpu_trace_resolve_timing) {
    return;
  }
  // Only guest-geometry passes carry a meaningful last_guest_draw_desc_.
  if (current_pass_kind_ != GpuPassKind::kGuest || !current_framebuffer_) {
    return;
  }
  uint32_t pass_draws_now = deferred_command_buffer_.record_stats().draws;
  uint32_t pass_draws = pass_draws_now >= pass_begin_draws_
                            ? pass_draws_now - pass_begin_draws_
                            : 0;
  if (pass_draws < 1 || pass_draws > kResolveTimingSmallPassDraws) {
    return;
  }
  // Only the EDRAM-tile-oversized RTs (a 720p game gets 720x1824 / 1280x2048 /
  // x4096 / x8192 host RTs) are the ~52ms deferred-store suspects; skip the
  // normal-height passes to keep the log readable.
  uint32_t host_w = current_framebuffer_->host_extent.width;
  uint32_t host_h = current_framebuffer_->host_extent.height;
  if (host_h < 1024) {
    return;
  }
  const GuestDrawDesc& d = last_guest_draw_desc_;
  XELOGI(
      "SMALLPASS fb={:04x} {}x{} draws={} host_verts={} idx={} prim={} "
      "ps_hash={:016X} vs_hash={:016X} blendctl0={:08X} colorctl={:08X} "
      "colormask={:04X} depthctl={:08X} color0_info={:08X} depth_info={:08X} "
      "ps_zwrite={} ps_kill={}",
      uint32_t((reinterpret_cast<uintptr_t>(current_framebuffer_) >> 4) &
               0xFFFFu),
      host_w, host_h, pass_draws, d.host_vertex_count, d.index_count,
      d.prim_type, d.ps_hash, d.vs_hash, d.blendcontrol0, d.colorcontrol,
      d.color_mask, d.depthcontrol, d.color0_info, d.depth_info,
      d.ps_writes_depth, d.ps_kills);
}

VkDescriptorSet VulkanCommandProcessor::AllocateSingleTransientDescriptor(
    SingleTransientDescriptorLayout transient_descriptor_layout) {
  assert_true(frame_open_);
  VkDescriptorSet descriptor_set;
  std::vector<VkDescriptorSet>& transient_descriptors_free =
      single_transient_descriptors_free_[size_t(transient_descriptor_layout)];
  if (!transient_descriptors_free.empty()) {
    descriptor_set = transient_descriptors_free.back();
    transient_descriptors_free.pop_back();
  } else {
    const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
    const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
    const VkDevice device = vulkan_device->device();
    bool is_storage_buffer =
        transient_descriptor_layout ==
        SingleTransientDescriptorLayout::kStorageBufferCompute;
    ui::vulkan::LinkedTypeDescriptorSetAllocator&
        transient_descriptor_allocator =
            is_storage_buffer ? transient_descriptor_allocator_storage_buffer_
                              : transient_descriptor_allocator_uniform_buffer_;
    VkDescriptorPoolSize descriptor_count;
    descriptor_count.type = is_storage_buffer
                                ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_count.descriptorCount = 1;
    descriptor_set = transient_descriptor_allocator.Allocate(
        GetSingleTransientDescriptorLayout(transient_descriptor_layout),
        &descriptor_count, 1);
    if (descriptor_set == VK_NULL_HANDLE) {
      return VK_NULL_HANDLE;
    }
  }
  UsedSingleTransientDescriptor used_descriptor;
  used_descriptor.frame = frame_current_;
  used_descriptor.layout = transient_descriptor_layout;
  used_descriptor.set = descriptor_set;
  single_transient_descriptors_used_.emplace_back(used_descriptor);
  return descriptor_set;
}

VkDescriptorSetLayout VulkanCommandProcessor::GetTextureDescriptorSetLayout(
    bool is_vertex, size_t texture_count, size_t sampler_count,
    bool input_attachment) {
  size_t binding_count = texture_count + sampler_count;
  if (!binding_count) {
    return descriptor_set_layout_empty_;
  }

  TextureDescriptorSetLayoutKey texture_descriptor_set_layout_key;
  texture_descriptor_set_layout_key.texture_count = uint32_t(texture_count);
  texture_descriptor_set_layout_key.sampler_count = uint32_t(sampler_count);
  texture_descriptor_set_layout_key.pixel_textures_input_attachment =
      input_attachment ? 1 : 0;
  texture_descriptor_set_layout_key.is_vertex = uint32_t(is_vertex);
  auto it_existing =
      descriptor_set_layouts_textures_.find(texture_descriptor_set_layout_key);
  if (it_existing != descriptor_set_layouts_textures_.end()) {
    return it_existing->second;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  descriptor_set_layout_bindings_.clear();
  descriptor_set_layout_bindings_.reserve(binding_count);
  VkShaderStageFlags stage_flags =
      is_vertex ? guest_shader_vertex_stages_ : VK_SHADER_STAGE_FRAGMENT_BIT;
  for (size_t i = 0; i < texture_count; ++i) {
    VkDescriptorSetLayoutBinding& descriptor_set_layout_binding =
        descriptor_set_layout_bindings_.emplace_back();
    descriptor_set_layout_binding.binding = uint32_t(i);
    descriptor_set_layout_binding.descriptorType =
        input_attachment ? VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
                         : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptor_set_layout_binding.descriptorCount = 1;
    descriptor_set_layout_binding.stageFlags = stage_flags;
  }
  for (size_t i = 0; i < sampler_count; ++i) {
    VkDescriptorSetLayoutBinding& descriptor_set_layout_binding =
        descriptor_set_layout_bindings_.emplace_back();
    descriptor_set_layout_binding.binding = uint32_t(texture_count + i);
    descriptor_set_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptor_set_layout_binding.descriptorCount = 1;
    descriptor_set_layout_binding.stageFlags = stage_flags;
  }
  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info;
  descriptor_set_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_create_info.pNext = nullptr;
  // Texture/sampler sets are pushed inline (vkCmdPushDescriptorSetKHR) when push
  // descriptors are active, which requires this layout flag and means the set is
  // never allocated from a pool or bound normally.
  descriptor_set_layout_create_info.flags =
      push_descriptors_active_
          ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR
          : 0;
  descriptor_set_layout_create_info.bindingCount = uint32_t(binding_count);
  descriptor_set_layout_create_info.pBindings =
      descriptor_set_layout_bindings_.data();
  VkDescriptorSetLayout texture_descriptor_set_layout;
  if (dfn.vkCreateDescriptorSetLayout(
          device, &descriptor_set_layout_create_info, nullptr,
          &texture_descriptor_set_layout) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  descriptor_set_layouts_textures_.emplace(texture_descriptor_set_layout_key,
                                           texture_descriptor_set_layout);
  return texture_descriptor_set_layout;
}

const VulkanPipelineCache::PipelineLayoutProvider*
VulkanCommandProcessor::GetPipelineLayout(size_t texture_count_pixel,
                                          size_t sampler_count_pixel,
                                          size_t texture_count_vertex,
                                          size_t sampler_count_vertex,
                                          bool pixel_textures_input_attachment) {
  // BRICK 1 native bindless render path: every guest draw shares ONE pipeline
  // layout (set 0 shared memory, set 1 constants, set 2 the global bindless
  // texture/sampler set, set 3 empty, + the bindless push-constant range). Built
  // once and returned regardless of per-shader texture/sampler counts, removing
  // per-texture pipeline-layout variance.
  if (native_render_path_active_) {
    if (native_pipeline_layout_) {
      return native_pipeline_layout_;
    }
    const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
    const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
    const VkDevice device = vulkan_device->device();
    VkDescriptorSetLayout
        descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetCount];
    descriptor_set_layouts
        [SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram] =
            descriptor_set_layout_shared_memory_and_edram_;
    descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetConstants] =
        (descriptor_set_layout_constants_dynamic_ != VK_NULL_HANDLE)
            ? descriptor_set_layout_constants_dynamic_
            : descriptor_set_layout_constants_;
    descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetTexturesVertex] =
        bindless_descriptor_set_layout_;
    descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetTexturesPixel] =
        bindless_empty_descriptor_set_layout_;
    VkPushConstantRange push_constant_range;
    push_constant_range.stageFlags =
        guest_shader_vertex_stages_ | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size =
        SpirvShaderTranslator::kBindlessPushConstantsSize;
    VkPipelineLayoutCreateInfo pipeline_layout_create_info;
    pipeline_layout_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_create_info.pNext = nullptr;
    pipeline_layout_create_info.flags = 0;
    pipeline_layout_create_info.setLayoutCount =
        uint32_t(xe::countof(descriptor_set_layouts));
    pipeline_layout_create_info.pSetLayouts = descriptor_set_layouts;
    pipeline_layout_create_info.pushConstantRangeCount = 1;
    pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
    VkPipelineLayout pipeline_layout;
    if (dfn.vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                                   &pipeline_layout) != VK_SUCCESS) {
      XELOGE("Failed to create the Vulkan bindless guest pipeline layout");
      return nullptr;
    }
    PipelineLayoutKey native_key;
    native_key.texture_count_pixel = 0;
    native_key.sampler_count_pixel = 0;
    native_key.texture_count_vertex = 0;
    native_key.sampler_count_vertex = 0;
    auto emplaced_pair = pipeline_layouts_.emplace(
        std::piecewise_construct, std::forward_as_tuple(native_key),
        std::forward_as_tuple(pipeline_layout, bindless_descriptor_set_layout_,
                              bindless_empty_descriptor_set_layout_));
    native_pipeline_layout_ = &emplaced_pair.first->second;
    return native_pipeline_layout_;
  }

  PipelineLayoutKey pipeline_layout_key;
  // BD input-attachment merge: distinguish the feedback variant (pixel textures
  // as input attachments) in the cache key. texture_count_pixel here is only
  // used for hashing (the real count goes to GetTextureDescriptorSetLayout
  // below), and guest texture counts are small, so the high bit is free.
  pipeline_layout_key.texture_count_pixel =
      uint16_t(texture_count_pixel) |
      (pixel_textures_input_attachment ? uint16_t(0x8000) : uint16_t(0));
  pipeline_layout_key.sampler_count_pixel = uint16_t(sampler_count_pixel);
  pipeline_layout_key.texture_count_vertex = uint16_t(texture_count_vertex);
  pipeline_layout_key.sampler_count_vertex = uint16_t(sampler_count_vertex);
  {
    auto it = pipeline_layouts_.find(pipeline_layout_key);
    if (it != pipeline_layouts_.end()) {
      return &it->second;
    }
  }

  VkDescriptorSetLayout descriptor_set_layout_textures_vertex =
      GetTextureDescriptorSetLayout(true, texture_count_vertex,
                                    sampler_count_vertex);
  if (descriptor_set_layout_textures_vertex == VK_NULL_HANDLE) {
    XELOGE(
        "Failed to obtain a Vulkan descriptor set layout for {} sampled images "
        "and {} samplers for guest vertex shaders",
        texture_count_vertex, sampler_count_vertex);
    return nullptr;
  }
  VkDescriptorSetLayout descriptor_set_layout_textures_pixel =
      GetTextureDescriptorSetLayout(false, texture_count_pixel,
                                    sampler_count_pixel,
                                    pixel_textures_input_attachment);
  if (descriptor_set_layout_textures_pixel == VK_NULL_HANDLE) {
    XELOGE(
        "Failed to obtain a Vulkan descriptor set layout for {} sampled images "
        "and {} samplers for guest pixel shaders",
        texture_count_pixel, sampler_count_pixel);
    return nullptr;
  }

  VkDescriptorSetLayout
      descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetCount];
  // Immutable layouts.
  descriptor_set_layouts
      [SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram] =
          descriptor_set_layout_shared_memory_and_edram_;
  // R2 (vulkan_dynamic_constants_arena): when the arena is armed (its dynamic
  // layout was created at init), EVERY guest pipeline uses the
  // UNIFORM_BUFFER_DYNAMIC constants layout so the one persistent dynamic set can
  // be bound with per-draw dynamic offsets. The arena state is fixed for the run,
  // so this substitution is identical across every pipeline and every bind - no
  // variant bit in PipelineLayoutKey is needed. Default-off (handle null) keeps
  // the static UNIFORM_BUFFER layout, byte-identical to before.
  descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetConstants] =
      (descriptor_set_layout_constants_dynamic_ != VK_NULL_HANDLE)
          ? descriptor_set_layout_constants_dynamic_
          : descriptor_set_layout_constants_;
  // Mutable layouts.
  descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetTexturesVertex] =
      descriptor_set_layout_textures_vertex;
  descriptor_set_layouts[SpirvShaderTranslator::kDescriptorSetTexturesPixel] =
      descriptor_set_layout_textures_pixel;

  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  VkPipelineLayoutCreateInfo pipeline_layout_create_info;
  pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.pNext = nullptr;
  pipeline_layout_create_info.flags = 0;
  pipeline_layout_create_info.setLayoutCount =
      uint32_t(xe::countof(descriptor_set_layouts));
  pipeline_layout_create_info.pSetLayouts = descriptor_set_layouts;
  pipeline_layout_create_info.pushConstantRangeCount = 0;
  pipeline_layout_create_info.pPushConstantRanges = nullptr;
  VkPipelineLayout pipeline_layout;
  if (dfn.vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                                 &pipeline_layout) != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan pipeline layout for guest drawing with {} "
        "pixel shader and {} vertex shader textures",
        texture_count_pixel, texture_count_vertex);
    return nullptr;
  }
  auto emplaced_pair = pipeline_layouts_.emplace(
      std::piecewise_construct, std::forward_as_tuple(pipeline_layout_key),
      std::forward_as_tuple(pipeline_layout,
                            descriptor_set_layout_textures_vertex,
                            descriptor_set_layout_textures_pixel));
  // unordered_map insertion doesn't invalidate element references.
  return &emplaced_pair.first->second;
}

bool VulkanCommandProcessor::InitializeBindlessDescriptors() {
  // BRICK 1 native bindless render path. Creates one global descriptor set with
  // runtime arrays of sampled images (2D array / 3D / cube at bindings 0/1/2)
  // and samplers (binding 3), all PARTIALLY_BOUND + UPDATE_AFTER_BIND, plus an
  // empty layout for the now-unused kDescriptorSetTexturesPixel slot.
  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  const VkShaderStageFlags stage_flags =
      guest_shader_vertex_stages_ | VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutBinding bindings[4] = {};
  for (uint32_t i = 0; i < 3; ++i) {
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[i].descriptorCount = kBindlessImageCapacity;
    bindings[i].stageFlags = stage_flags;
    bindings[i].pImmutableSamplers = nullptr;
  }
  bindings[3].binding = 3;
  bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  bindings[3].descriptorCount = kBindlessSamplerCapacity;
  bindings[3].stageFlags = stage_flags;
  bindings[3].pImmutableSamplers = nullptr;

  const VkDescriptorBindingFlags binding_flag =
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
  VkDescriptorBindingFlags binding_flags[4] = {binding_flag, binding_flag,
                                               binding_flag, binding_flag};
  VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info;
  binding_flags_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
  binding_flags_create_info.pNext = nullptr;
  binding_flags_create_info.bindingCount = 4;
  binding_flags_create_info.pBindingFlags = binding_flags;

  VkDescriptorSetLayoutCreateInfo layout_create_info;
  layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_create_info.pNext = &binding_flags_create_info;
  layout_create_info.flags =
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
  layout_create_info.bindingCount = 4;
  layout_create_info.pBindings = bindings;
  if (dfn.vkCreateDescriptorSetLayout(device, &layout_create_info, nullptr,
                                      &bindless_descriptor_set_layout_) !=
      VK_SUCCESS) {
    XELOGE("Failed to create the bindless descriptor set layout");
    return false;
  }

  // Empty layout for the unused pixel-textures set slot (set 3).
  VkDescriptorSetLayoutCreateInfo empty_layout_create_info;
  empty_layout_create_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  empty_layout_create_info.pNext = nullptr;
  empty_layout_create_info.flags = 0;
  empty_layout_create_info.bindingCount = 0;
  empty_layout_create_info.pBindings = nullptr;
  if (dfn.vkCreateDescriptorSetLayout(device, &empty_layout_create_info, nullptr,
                                      &bindless_empty_descriptor_set_layout_) !=
      VK_SUCCESS) {
    XELOGE("Failed to create the empty bindless pixel-textures set layout");
    return false;
  }

  VkDescriptorPoolSize pool_sizes[2];
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  pool_sizes[0].descriptorCount = kBindlessImageCapacity * 3;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
  pool_sizes[1].descriptorCount = kBindlessSamplerCapacity;
  VkDescriptorPoolCreateInfo pool_create_info;
  pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_create_info.pNext = nullptr;
  pool_create_info.flags =
      VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  pool_create_info.maxSets = 1;
  pool_create_info.poolSizeCount = 2;
  pool_create_info.pPoolSizes = pool_sizes;
  if (dfn.vkCreateDescriptorPool(device, &pool_create_info, nullptr,
                                 &bindless_descriptor_pool_) != VK_SUCCESS) {
    XELOGE("Failed to create the bindless descriptor pool");
    return false;
  }

  VkDescriptorSetAllocateInfo set_allocate_info;
  set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_allocate_info.pNext = nullptr;
  set_allocate_info.descriptorPool = bindless_descriptor_pool_;
  set_allocate_info.descriptorSetCount = 1;
  set_allocate_info.pSetLayouts = &bindless_descriptor_set_layout_;
  if (dfn.vkAllocateDescriptorSets(device, &set_allocate_info,
                                   &bindless_descriptor_set_) != VK_SUCCESS) {
    XELOGE("Failed to allocate the bindless descriptor set");
    return false;
  }
  return true;
}

uint32_t VulkanCommandProcessor::UseBindlessImageSlot(VkImageView image_view,
                                                      uint32_t binding) {
  if (image_view == VK_NULL_HANDLE) {
    return 0;
  }
  auto it = bindless_image_view_slots_.find(image_view);
  if (it != bindless_image_view_slots_.end()) {
    return it->second;
  }
  uint32_t slot;
  if (!bindless_image_free_slots_.empty()) {
    slot = bindless_image_free_slots_.back();
    bindless_image_free_slots_.pop_back();
  } else {
    if (bindless_image_next_slot_ >= kBindlessImageCapacity) {
      // Capacity exhausted (very long session of unique textures) - index 0 is
      // undefined-but-safe under partiallyBound. Should not happen for a field.
      return 0;
    }
    slot = bindless_image_next_slot_++;
  }
  bindless_image_view_slots_.emplace(image_view, slot);
  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  VkDescriptorImageInfo image_info;
  image_info.sampler = VK_NULL_HANDLE;
  image_info.imageView = image_view;
  image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkWriteDescriptorSet write;
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.pNext = nullptr;
  write.dstSet = bindless_descriptor_set_;
  write.dstBinding = binding;
  write.dstArrayElement = slot;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  write.pImageInfo = &image_info;
  write.pBufferInfo = nullptr;
  write.pTexelBufferView = nullptr;
  dfn.vkUpdateDescriptorSets(vulkan_device->device(), 1, &write, 0, nullptr);
  return slot;
}

uint32_t VulkanCommandProcessor::UseBindlessSamplerSlot(VkSampler sampler) {
  if (sampler == VK_NULL_HANDLE) {
    return 0;
  }
  auto it = bindless_sampler_slots_.find(sampler);
  if (it != bindless_sampler_slots_.end()) {
    return it->second;
  }
  uint32_t slot;
  if (!bindless_sampler_free_slots_.empty()) {
    slot = bindless_sampler_free_slots_.back();
    bindless_sampler_free_slots_.pop_back();
  } else {
    if (bindless_sampler_next_slot_ >= kBindlessSamplerCapacity) {
      return 0;
    }
    slot = bindless_sampler_next_slot_++;
  }
  bindless_sampler_slots_.emplace(sampler, slot);
  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  VkDescriptorImageInfo image_info;
  image_info.sampler = sampler;
  image_info.imageView = VK_NULL_HANDLE;
  image_info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkWriteDescriptorSet write;
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.pNext = nullptr;
  write.dstSet = bindless_descriptor_set_;
  write.dstBinding = SpirvShaderTranslator::kBindlessBindingSampler;
  write.dstArrayElement = slot;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  write.pImageInfo = &image_info;
  write.pBufferInfo = nullptr;
  write.pTexelBufferView = nullptr;
  dfn.vkUpdateDescriptorSets(vulkan_device->device(), 1, &write, 0, nullptr);
  return slot;
}

void VulkanCommandProcessor::ReleaseBindlessImageView(VkImageView image_view) {
  if (!native_render_path_active_ || image_view == VK_NULL_HANDLE) {
    return;
  }
  auto it = bindless_image_view_slots_.find(image_view);
  if (it == bindless_image_view_slots_.end()) {
    return;
  }
  bindless_image_free_slots_.push_back(it->second);
  bindless_image_view_slots_.erase(it);
}

void VulkanCommandProcessor::ReleaseBindlessSampler(VkSampler sampler) {
  if (!native_render_path_active_ || sampler == VK_NULL_HANDLE) {
    return;
  }
  auto it = bindless_sampler_slots_.find(sampler);
  if (it == bindless_sampler_slots_.end()) {
    return;
  }
  bindless_sampler_free_slots_.push_back(it->second);
  bindless_sampler_slots_.erase(it);
}

VulkanCommandProcessor::ScratchBufferAcquisition
VulkanCommandProcessor::AcquireScratchGpuBuffer(
    VkDeviceSize size, VkPipelineStageFlags initial_stage_mask,
    VkAccessFlags initial_access_mask) {
  assert_true(submission_open_);
  assert_false(scratch_buffer_used_);
  if (!submission_open_ || scratch_buffer_used_ || !size) {
    return ScratchBufferAcquisition();
  }

  uint64_t submission_current = GetCurrentSubmission();

  if (scratch_buffer_ != VK_NULL_HANDLE && size <= scratch_buffer_size_) {
    // Already used previously - transition.
    PushBufferMemoryBarrier(scratch_buffer_, 0, VK_WHOLE_SIZE,
                            scratch_buffer_last_stage_mask_, initial_stage_mask,
                            scratch_buffer_last_access_mask_,
                            initial_access_mask);
    scratch_buffer_last_stage_mask_ = initial_stage_mask;
    scratch_buffer_last_access_mask_ = initial_access_mask;
    scratch_buffer_last_usage_submission_ = submission_current;
    scratch_buffer_used_ = true;
    return ScratchBufferAcquisition(*this, scratch_buffer_, initial_stage_mask,
                                    initial_access_mask);
  }

  size = xe::align(size, kScratchBufferSizeIncrement);

  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();

  VkDeviceMemory new_scratch_buffer_memory;
  VkBuffer new_scratch_buffer;
  // VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT for
  // texture loading.
  if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
          vulkan_device, size,
          VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          ui::vulkan::util::MemoryPurpose::kDeviceLocal, new_scratch_buffer,
          new_scratch_buffer_memory)) {
    XELOGE(
        "VulkanCommandProcessor: Failed to create a {} MB scratch GPU buffer",
        size >> 20);
    return ScratchBufferAcquisition();
  }

  if (GetCompletedSubmission() >= scratch_buffer_last_usage_submission_) {
    const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
    const VkDevice device = vulkan_device->device();
    if (scratch_buffer_ != VK_NULL_HANDLE) {
      dfn.vkDestroyBuffer(device, scratch_buffer_, nullptr);
    }
    if (scratch_buffer_memory_ != VK_NULL_HANDLE) {
      dfn.vkFreeMemory(device, scratch_buffer_memory_, nullptr);
    }
  } else {
    if (scratch_buffer_ != VK_NULL_HANDLE) {
      destroy_buffers_.emplace_back(scratch_buffer_last_usage_submission_,
                                    scratch_buffer_);
    }
    if (scratch_buffer_memory_ != VK_NULL_HANDLE) {
      destroy_memory_.emplace_back(scratch_buffer_last_usage_submission_,
                                   scratch_buffer_memory_);
    }
  }

  scratch_buffer_memory_ = new_scratch_buffer_memory;
  scratch_buffer_ = new_scratch_buffer;
  scratch_buffer_size_ = size;
  // Not used yet, no need for a barrier.
  scratch_buffer_last_stage_mask_ = initial_access_mask;
  scratch_buffer_last_access_mask_ = initial_stage_mask;
  scratch_buffer_last_usage_submission_ = submission_current;
  scratch_buffer_used_ = true;
  return ScratchBufferAcquisition(*this, new_scratch_buffer, initial_stage_mask,
                                  initial_access_mask);
}

void VulkanCommandProcessor::BindExternalGraphicsPipeline(
    VkPipeline pipeline, bool keep_dynamic_depth_bias,
    bool keep_dynamic_blend_constants, bool keep_dynamic_stencil_mask_ref) {
  // Lever 2 (vulkan_merge_draws): an external pipeline replaces the guest pipeline
  // a pending concatenation run was built against (it NULLs
  // current_guest_graphics_pipeline_ below), so realize the run first.
  FlushPendingMergeRun();
  if (!keep_dynamic_depth_bias) {
    dynamic_depth_bias_update_needed_ = true;
  }
  if (!keep_dynamic_blend_constants) {
    dynamic_blend_constants_update_needed_ = true;
  }
  if (!keep_dynamic_stencil_mask_ref) {
    dynamic_stencil_compare_mask_front_update_needed_ = true;
    dynamic_stencil_compare_mask_back_update_needed_ = true;
    dynamic_stencil_write_mask_front_update_needed_ = true;
    dynamic_stencil_write_mask_back_update_needed_ = true;
    dynamic_stencil_reference_front_update_needed_ = true;
    dynamic_stencil_reference_back_update_needed_ = true;
  }
  // EDS cull/front-face (Lever 1b) are not preserved across an external pipeline
  // (it uses static rasterizer state), so force re-emission before the next
  // guest draw. Harmless when the promotion cvar is off (flags never consumed).
  dynamic_cull_mode_update_needed_ = true;
  dynamic_front_face_update_needed_ = true;
  dynamic_primitive_topology_update_needed_ = true;
  dynamic_primitive_restart_enable_update_needed_ = true;
  dynamic_depth_test_enable_update_needed_ = true;
  dynamic_depth_write_enable_update_needed_ = true;
  dynamic_depth_compare_op_update_needed_ = true;
  dynamic_stencil_test_enable_update_needed_ = true;
  dynamic_stencil_op_update_needed_ = true;
  if (current_external_graphics_pipeline_ == pipeline) {
    return;
  }
  deferred_command_buffer_.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             pipeline);
  current_external_graphics_pipeline_ = pipeline;
  current_guest_graphics_pipeline_ = VK_NULL_HANDLE;
  current_guest_graphics_pipeline_layout_ = VK_NULL_HANDLE;
}

void VulkanCommandProcessor::BindExternalComputePipeline(VkPipeline pipeline) {
  if (current_external_compute_pipeline_ == pipeline) {
    return;
  }
  deferred_command_buffer_.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                                             pipeline);
  current_external_compute_pipeline_ = pipeline;
}

void VulkanCommandProcessor::SetViewport(const VkViewport& viewport) {
  if (!dynamic_viewport_update_needed_) {
    dynamic_viewport_update_needed_ |= dynamic_viewport_.x != viewport.x;
    dynamic_viewport_update_needed_ |= dynamic_viewport_.y != viewport.y;
    dynamic_viewport_update_needed_ |=
        dynamic_viewport_.width != viewport.width;
    dynamic_viewport_update_needed_ |=
        dynamic_viewport_.height != viewport.height;
    dynamic_viewport_update_needed_ |=
        dynamic_viewport_.minDepth != viewport.minDepth;
    dynamic_viewport_update_needed_ |=
        dynamic_viewport_.maxDepth != viewport.maxDepth;
  }
  if (dynamic_viewport_update_needed_) {
    dynamic_viewport_ = viewport;
    deferred_command_buffer_.CmdVkSetViewport(0, 1, &dynamic_viewport_);
    dynamic_viewport_update_needed_ = false;
  }
}

void VulkanCommandProcessor::SetScissor(const VkRect2D& scissor) {
  if (!dynamic_scissor_update_needed_) {
    dynamic_scissor_update_needed_ |=
        dynamic_scissor_.offset.x != scissor.offset.x;
    dynamic_scissor_update_needed_ |=
        dynamic_scissor_.offset.y != scissor.offset.y;
    dynamic_scissor_update_needed_ |=
        dynamic_scissor_.extent.width != scissor.extent.width;
    dynamic_scissor_update_needed_ |=
        dynamic_scissor_.extent.height != scissor.extent.height;
  }
  if (dynamic_scissor_update_needed_) {
    dynamic_scissor_ = scissor;
    deferred_command_buffer_.CmdVkSetScissor(0, 1, &dynamic_scissor_);
    dynamic_scissor_update_needed_ = false;
  }
}

Shader* VulkanCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                           uint32_t guest_address,
                                           const uint32_t* host_address,
                                           uint32_t dword_count) {
  return pipeline_cache_->LoadShader(shader_type, host_address, dword_count);
}

void VulkanCommandProcessor::BdArmDecoupledCapture(bool armed) {
  // Armed by the base CommandProcessor around the synthetic native draw (which
  // has redirected RB_COLOR_INFO to a non-aliasing EDRAM base). While armed, the
  // next covered IssueDraw latches the dedicated full-surface color host RT it
  // renders into so IssueSwap can present it.
  bd_capture_armed_ = armed;
}

bool VulkanCommandProcessor::IssueDraw(xenos::PrimitiveType prim_type,
                                       uint32_t index_count,
                                       IndexBufferInfo* index_buffer_info,
                                       bool major_mode_explicit) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  const RegisterFile& regs = *register_file_;

  // Blue Dragon native-draw HLE: count every IssueDraw ENTRY (before any early
  // return), mirroring D3D12CommandProcessor::IssueDraw. Makes gpu_bd_native_hle
  // issuedraw_delta a real per-backend signal on the Thor.
  ++bd_issuedraw_count_;

  // Lever 2 (vulkan_merge_draws): snapshot the deferred-command cursor at the top
  // of IssueDraw. Compared just before the draw-emit block to detect whether ANY
  // command (descriptor/dynamic-state/render-pass/barrier/pipeline) was recorded
  // for this draw - if so a pending concatenation run cannot be extended. Cheap;
  // computed unconditionally, consumed only when the cvar is on (Step 4).
  const size_t merge_cmd_cursor_at_entry =
      deferred_command_buffer_.command_stream_size_elements();

  // Per-draw CPU timing (command-processor throughput diagnostic). Captures the
  // whole IssueDraw cost (accumulated only for rendered draws) and the
  // PrimitiveProcessor::Process sub-step, to localize the per-draw gate.
  const bool trace_draw_cpu = cvars::vulkan_trace_draw_outcomes_per_frame;
  std::chrono::steady_clock::time_point draw_cpu_t0;
  std::chrono::steady_clock::time_point draw_cpu_emit_t0;
  if (trace_draw_cpu) {
    draw_cpu_t0 = std::chrono::steady_clock::now();
  }

  xenos::EdramMode edram_mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
  if (edram_mode == xenos::EdramMode::kCopy) {
    // Special copy handling.
    ++draw_outcomes_copy_;
    return IssueCopy();
  }

  // BOTTLENECK-ISOLATION DIAGNOSTIC (default-off, breaks pixels on purpose):
  // gpu_diag_skip_alpha_test_draws drops the alpha-test (foliage) draws, and
  // gpu_diag_skip_draws_min_indices drops any draw with >= N indices (heavy
  // geometry). Since the field proved NOT fill-bound (resolution downscale was
  // inert), a large frame-time drop here localizes the cost to vertex/geometry
  // processing of those draws (=> foliage LOD/decimation is the BD-30 lever);
  // a small drop means the cost is per-draw overhead / binning instead.
  const bool draw_is_alpha_test =
      regs.Get<reg::RB_COLORCONTROL>().alpha_test_enable != 0;
  if (cvars::gpu_diag_skip_alpha_test_draws && draw_is_alpha_test) {
    // RIGOROUS single-run A/B: when the alternator is active (gpu_freeze_ab_
    // alternate_vrs + reach), skip foliage ONLY in the "off" phase so one run
    // measures foliage-on vs foliage-off on the SAME scene (the draw-outcomes
    // log buckets gpu_frame_us by phase). Set gpu_vrs_foliage_rate=0 to isolate
    // from VRS. Outside the alternator, skip unconditionally (static test).
    if (!gpu_ab_alt_active_ || !gpu_freeze_vrs_phase_on_) {
      return true;
    }
  }
  if (cvars::gpu_diag_skip_draws_min_indices > 0 &&
      index_count >= uint32_t(cvars::gpu_diag_skip_draws_min_indices)) {
    return true;
  }
  // gpu_foliage_decimate_pct (shippable Performance lever): BD's field is
  // foliage-vertex-bound (~15ms; device-proven not fill-bound). Skip this
  // percent of the alpha-test (foliage) draws, spread deterministically by a
  // per-frame foliage-draw counter, thinning foliage density to cut vertex
  // cost. Keeps the closest/densest foliage (lowest indices via the counter
  // stride) for the least-visible thinning. 0 = off (default, full foliage).
  if (cvars::gpu_foliage_decimate_pct > 0 && draw_is_alpha_test) {
    uint32_t pct = std::min<uint32_t>(uint32_t(cvars::gpu_foliage_decimate_pct),
                                      100u);
    // Skip pct out of every 100 foliage draws, evenly spread by the per-frame
    // counter (reset at swap).
    if ((foliage_draw_counter_++ % 100u) < pct) {
      return true;
    }
  }

  const ui::vulkan::VulkanDevice::Properties& device_properties =
      GetVulkanDevice()->properties();

  memexport_ranges_.clear();

  // Vertex shader analysis.
  auto vertex_shader = static_cast<VulkanShader*>(active_vertex_shader());
  if (!vertex_shader) {
    // Always need a vertex shader.
    ++draw_outcomes_skipped_no_vs_;
    if (ShouldTraceVulkanDrawStateForShaders(0, 0)) {
      XELOGI(
          "GPU draw trace: skipped no vertex shader prim={} index_count={} "
          "edram_mode={}",
          uint32_t(prim_type), index_count, uint32_t(edram_mode));
    }
    return false;
  }
  pipeline_cache_->AnalyzeShaderUcode(*vertex_shader);
  uint64_t vertex_shader_hash = vertex_shader->ucode_data_hash();
  // TODO(Triang3l): If the shader uses memory export, but
  // vertexPipelineStoresAndAtomics is not supported, convert the vertex shader
  // to a compute shader and dispatch it after the draw if the draw doesn't use
  // tessellation.
  if (vertex_shader->memexport_eM_written() != 0 &&
      device_properties.vertexPipelineStoresAndAtomics) {
    draw_util::AddMemExportRanges(regs, *vertex_shader, memexport_ranges_);
  }

  // Pixel shader analysis.
  bool primitive_polygonal = draw_util::IsPrimitivePolygonal(regs);
  bool is_rasterization_done =
      draw_util::IsRasterizationPotentiallyDone(regs, primitive_polygonal);
  VulkanShader* pixel_shader = nullptr;
  if (is_rasterization_done) {
    // See xenos::EdramMode for explanation why the pixel shader is only used
    // when it's kColorDepth here.
    if (edram_mode == xenos::EdramMode::kColorDepth) {
      pixel_shader = static_cast<VulkanShader*>(active_pixel_shader());
      if (pixel_shader) {
        pipeline_cache_->AnalyzeShaderUcode(*pixel_shader);
        if (!draw_util::IsPixelShaderNeededWithRasterization(*pixel_shader,
                                                             regs)) {
          pixel_shader = nullptr;
        }
      }
    }
  } else {
    // Disabling pixel shader for this case is also required by the pipeline
    // cache.
    if (memexport_ranges_.empty()) {
      // This draw has no effect.
      ++draw_outcomes_skipped_no_rast_;
      if (ShouldTraceVulkanDrawStateForShaders(vertex_shader_hash, 0)) {
        XELOGI(
            "GPU draw trace: skipped no rasterization/no memexport prim={} "
            "index_count={} edram_mode={} vs_hash={:016X}",
            uint32_t(prim_type), index_count, uint32_t(edram_mode),
            vertex_shader->ucode_data_hash());
      }
      return true;
    }
  }
  if (pixel_shader && pixel_shader->memexport_eM_written() != 0 &&
      device_properties.fragmentStoresAndAtomics) {
    draw_util::AddMemExportRanges(regs, *pixel_shader, memexport_ranges_);
  }
  uint64_t pixel_shader_hash =
      pixel_shader ? pixel_shader->ucode_data_hash() : 0;
  bool trace_draw_state =
      ShouldTraceVulkanDrawStateForShaders(vertex_shader_hash,
                                           pixel_shader_hash);

  uint32_t ps_param_gen_pos = UINT32_MAX;
  uint32_t interpolator_mask =
      pixel_shader ? (vertex_shader->writes_interpolators() &
                      pixel_shader->GetInterpolatorInputMask(
                          regs.Get<reg::SQ_PROGRAM_CNTL>(),
                          regs.Get<reg::SQ_CONTEXT_MISC>(), ps_param_gen_pos))
                   : 0;

  PrimitiveProcessor::ProcessingResult primitive_processing_result;
  SpirvShaderTranslator::Modification vertex_shader_modification;
  SpirvShaderTranslator::Modification pixel_shader_modification;
  VulkanShader::VulkanTranslation* vertex_shader_translation;
  VulkanShader::VulkanTranslation* pixel_shader_translation;

  // Two iterations because a submission (even the current one - in which case
  // it needs to be ended, and a new one must be started) may need to be awaited
  // in case of a sampler count overflow, and if that happens, all subsystem
  // updates done previously must be performed again because the updates done
  // before the awaiting may be referencing objects destroyed by
  // CompletedSubmissionUpdated.
  for (uint32_t i = 0; i < 2; ++i) {
    std::chrono::steady_clock::time_point bs_t0;
    if (trace_draw_cpu) {
      bs_t0 = std::chrono::steady_clock::now();
    }
    if (!BeginSubmission(true)) {
      return false;
    }
    if (trace_draw_cpu) {
      // BeginSubmission carries the frame-await throttle-wait; time it apart from
      // the rest of setup so the GPU-paced wait is separable from real CPU setup.
      draw_cpu_beginsubmit_ns_ += uint64_t(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - bs_t0)
              .count());
    }

    // Process primitives.
    std::chrono::steady_clock::time_point proc_t0;
    if (trace_draw_cpu) {
      proc_t0 = std::chrono::steady_clock::now();
      // Setup = everything from IssueDraw entry up to here (shader ucode
      // analysis + early state). Captured only for draws that reach Process.
      draw_cpu_setup_ns_ += uint64_t(
          std::chrono::duration_cast<std::chrono::nanoseconds>(proc_t0 -
                                                               draw_cpu_t0)
              .count());
    }
    bool process_ok = primitive_processor_->Process(primitive_processing_result);
    if (trace_draw_cpu) {
      draw_cpu_process_ns_ += uint64_t(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - proc_t0)
              .count());
    }
    if (!process_ok) {
      return false;
    }
    if (!primitive_processing_result.host_draw_vertex_count) {
      // Nothing to draw.
      if (trace_draw_state) {
        XELOGI(
            "GPU draw trace: skipped empty processed draw prim={} "
            "index_count={} edram_mode={} vs_hash={:016X}",
            uint32_t(prim_type), index_count, uint32_t(edram_mode),
            vertex_shader->ucode_data_hash());
      }
      return true;
    }
    // TODO(Triang3l): Tessellation, geometry-type-specific vertex shader,
    // vertex shader as compute.
    if (primitive_processing_result.host_vertex_shader_type !=
            Shader::HostVertexShaderType::kVertex &&
        primitive_processing_result.host_vertex_shader_type !=
            Shader::HostVertexShaderType::kPointListAsTriangleStrip) {
      return false;
    }

    // BD input-attachment merge (detection): before computing the pixel shader
    // modification, detect whether THIS draw is a same-pixel composite consumer
    // of the current (producer) pass's render target, so 4a/4b/4c engage. v1
    // heuristic: a rect-list / small (<=6-vert) primitive whose pixel shader
    // samples a texture, while a producer pass is captured. Producer fetch
    // constant = the shader's first texture binding; producer view = the current
    // (producer) framebuffer's color view (stable until the pass entry redirect).
    feedback_merge_active_ = false;
    if (cvars::gpu_vulkan_feedback_merge && pixel_shader &&
        feedback_producer_begin_pos_ != SIZE_MAX && current_framebuffer_ &&
        current_framebuffer_->color_view != VK_NULL_HANDLE) {
      bool composite_prim =
          prim_type == xenos::PrimitiveType::kRectangleList ||
          (index_count >= 3 && index_count <= 6);
      const auto& ps_textures = pixel_shader->texture_bindings();
      // Multi-texture: the producer is the FIRST texture (the primary input =
      // the just-rendered RT); other textures stay sampled (mixed descriptor set,
      // wired below). Detection fires for ALL composites so the redirect can LOG
      // their texcount + in-place/2-RT structure; the modification + merge are
      // gated to texcount==1 until the mixed descriptor path is validated.
      if (composite_prim && !ps_textures.empty()) {
        feedback_merge_active_ = true;
        feedback_merge_producer_fetch_constant_ = ps_textures[0].fetch_constant;
        feedback_merge_producer_view_ = current_framebuffer_->color_view;
        feedback_merge_texcount_ = uint32_t(ps_textures.size());
      }
    }

    // Shader modifications.
    vertex_shader_modification =
        pipeline_cache_->GetCurrentVertexShaderModification(
            *vertex_shader, primitive_processing_result.host_vertex_shader_type,
            interpolator_mask, ps_param_gen_pos != UINT32_MAX);
    pixel_shader_modification =
        pixel_shader ? pipeline_cache_->GetCurrentPixelShaderModification(
                           *pixel_shader, interpolator_mask, ps_param_gen_pos)
                     : SpirvShaderTranslator::Modification(0);
    // BD input-attachment merge (4a): when this draw is the merge consumer, flag
    // its pixel shader to read the producer fetch constant as a Vulkan INPUT
    // ATTACHMENT (subpassLoad). Selects the variant shader (subpassInput), the
    // feedback pipeline at subpass 1, and the input-attachment descriptor.
    if (feedback_merge_active_ && feedback_merge_texcount_ == 1 && pixel_shader) {
      pixel_shader_modification.pixel.feedback_input_attachment =
          feedback_merge_producer_fetch_constant_ + 1;
    }

    // THE EDRAM SOLVE, hybrid form (gpu_vulkan_hybrid_postprocess): route a
    // full-screen post-process composite (rect-list / <=6-vertex quad whose pixel
    // shader samples a texture - a bloom/blur/tonemap pass reading a prior render
    // target) through the EDRAM-buffer/SSBO ROP path, so its result lands in the
    // EDRAM buffer with NO render-to-texture pass (collapsing the tile-I/O
    // pass-break) while the overdraw main scene keeps host-RT GMEM ROP. The
    // modification bit selects the FSI-mode translation (EnsureShadersTranslated);
    // hybrid_current_draw_composite_ makes the draw helpers take the FSI path.
    hybrid_current_draw_composite_ = false;
    // Composite-consumer detection (rect-list / <=6-vertex quad whose pixel
    // shader samples a texture = a bloom/blur/tonemap pass reading a prior RT).
    // Computed UNGATED for the gpu_trace_resolve_timing brick-2 measurement (times
    // these passes under kGuestComposite); the hybrid routing below reuses it.
    current_draw_is_composite_consumer_ =
        pixel_shader &&
        (prim_type == xenos::PrimitiveType::kRectangleList ||
         (index_count >= 3 && index_count <= 6)) &&
        !pixel_shader->texture_bindings().empty();
    if (render_target_cache_->hybrid_postprocess() &&
        current_draw_is_composite_consumer_) {
      hybrid_current_draw_composite_ = true;
      pixel_shader_modification.pixel.hybrid_fsi_composite = 1;
    }

    // Translate the shaders now to obtain the sampler bindings.
    vertex_shader_translation = static_cast<VulkanShader::VulkanTranslation*>(
        vertex_shader->GetOrCreateTranslation(
            vertex_shader_modification.value));
    pixel_shader_translation =
        pixel_shader ? static_cast<VulkanShader::VulkanTranslation*>(
                           pixel_shader->GetOrCreateTranslation(
                               pixel_shader_modification.value))
                     : nullptr;
    if (!pipeline_cache_->EnsureShadersTranslated(vertex_shader_translation,
                                                  pixel_shader_translation)) {
      return false;
    }

    // Obtain the samplers. Note that the bindings don't depend on the shader
    // modification, so if on the second iteration of this loop it becomes
    // different for some reason (like a race condition with the guest in index
    // buffer processing in the primitive processor resulting in different host
    // vertex shader types), the bindings will stay the same.
    // TODO(Triang3l): Sampler caching and reuse for adjacent draws within one
    // submission.
    uint32_t samplers_overflowed_count = 0;
    for (uint32_t j = 0; j < 2; ++j) {
      std::vector<std::pair<VulkanTextureCache::SamplerParameters, VkSampler>>&
          shader_samplers =
              j ? current_samplers_pixel_ : current_samplers_vertex_;
      if (!i) {
        shader_samplers.clear();
      }
      const VulkanShader* shader = j ? pixel_shader : vertex_shader;
      if (!shader) {
        continue;
      }
      const std::vector<VulkanShader::SamplerBinding>& shader_sampler_bindings =
          shader->GetSamplerBindingsAfterTranslation();
      if (!i) {
        shader_samplers.reserve(shader_sampler_bindings.size());
        for (const VulkanShader::SamplerBinding& shader_sampler_binding :
             shader_sampler_bindings) {
          shader_samplers.emplace_back(
              texture_cache_->GetSamplerParameters(shader_sampler_binding),
              VK_NULL_HANDLE);
        }
      }
      for (std::pair<VulkanTextureCache::SamplerParameters, VkSampler>&
               shader_sampler_pair : shader_samplers) {
        // UseSampler calls are needed even on the second iteration in case the
        // submission was broken (and thus the last usage submission indices for
        // the used samplers need to be updated) due to an overflow within one
        // submission. Though sampler overflow is a very rare situation overall.
        bool sampler_overflowed;
        VkSampler shader_sampler = texture_cache_->UseSampler(
            shader_sampler_pair.first, sampler_overflowed);
        shader_sampler_pair.second = shader_sampler;
        if (shader_sampler == VK_NULL_HANDLE) {
          if (!sampler_overflowed || i) {
            // If !sampler_overflowed, just failed to create a sampler for some
            // reason.
            // If i == 1, an overflow has happened twice, can't recover from it
            // anymore (would enter an infinite loop otherwise if the number of
            // attempts was not limited to 2). Possibly too many unique samplers
            // in one draw, or failed to await submission completion.
            return false;
          }
          ++samplers_overflowed_count;
        }
      }
    }
    if (!samplers_overflowed_count) {
      break;
    }
    assert_zero(i);
    // Free space for as many samplers as how many haven't been allocated
    // successfully - obtain the submission index that needs to be awaited to
    // reuse `samplers_overflowed_count` slots. This must be done after all the
    // UseSampler calls, not inside the loop calling UseSampler, because earlier
    // UseSampler calls may "mark for deletion" some samplers that later
    // UseSampler calls in the loop may actually demand.
    uint64_t sampler_overflow_await_submission =
        texture_cache_->GetSubmissionToAwaitOnSamplerOverflow(
            samplers_overflowed_count);
    assert_true(sampler_overflow_await_submission <= GetCurrentSubmission());
    CheckSubmissionCompletionAndDeviceLoss(sampler_overflow_await_submission);
  }

  // Set up the render targets - this may perform dispatches and draws.
  reg::RB_DEPTHCONTROL normalized_depth_control =
      draw_util::GetNormalizedDepthControl(regs);
  // Lever A (gpu_foliage_lrz_force_depth): force the overdraw-heavy alpha-test
  // foliage to depth-TEST (z<, write-OFF) against the opaque depth field (best
  // when primed by gpu_opaque_depth_prepass) so foliage behind opaque geometry
  // early-Z-rejects on Adreno without polluting depth. zfunc MUST be kLess - a
  // write-off + kAlways is normalized to depth-OFF by GetNormalizedDepthControl
  // (see :4484), silently disabling the test. Host-RT path only; the cvar is
  // default-off so this is inert + zero-cost by default. Quality tradeoff: it
  // changes foliage depth sorting (test/write semantics), validate visually.
  if (cvars::gpu_foliage_lrz_force_depth &&
      regs.Get<reg::RB_COLORCONTROL>().alpha_test_enable != 0 &&
      render_target_cache_->GetPath() ==
          RenderTargetCache::Path::kHostRenderTargets) {
    normalized_depth_control.z_enable = 1;
    normalized_depth_control.z_write_enable = 0;
    normalized_depth_control.zfunc = xenos::CompareFunction::kLess;
  }
  // Lever A' (gpu_foliage_lrz_feedback): the discard/LRZ-defeat OVERDRAW FIX.
  // Alpha-test foliage emits OpKill -> Turnip has_kill -> LRZ off in the binning
  // pass, so the 43% foliage SELF-overdraw fully shades every overlapping leaf.
  // Per Mesa's freedreno LRZ doc, a discarding draw that WRITES depth still feeds
  // LRZ during the RENDERING pass (A7xx LRZ feedback), so later foliage layers
  // early-Z-reject earlier ones. So force depth-WRITE-ON for the alpha-test class
  // (opaque-where-passed -> writing depth is valid). Only when the draw already
  // depth-tests with a REAL comparison: keep the guest zfunc (A7xx bidirectional
  // LRZ handles less/greater, preserving reverse-Z) and skip kNever/kAlways, where
  // forcing a write would corrupt depth. Distinct from Lever A above (write-OFF
  // vs opaque-primed depth, device-killed); mutually exclusive with it.
  // When the single-run A/B validator is alternating (gpu_freeze_ab_alternate_vrs),
  // gate this lever on the phase too, so one frozen run measures lrz_feedback
  // off vs on (set gpu_vrs_foliage_rate=0 to isolate it from VRS). Inert when the
  // validator is off (gpu_ab_alt_active_ false -> always applies).
  if (cvars::gpu_foliage_lrz_feedback && !cvars::gpu_foliage_lrz_force_depth &&
      (!gpu_ab_alt_active_ || gpu_freeze_vrs_phase_on_) &&
      regs.Get<reg::RB_COLORCONTROL>().alpha_test_enable != 0 &&
      normalized_depth_control.z_enable &&
      normalized_depth_control.zfunc != xenos::CompareFunction::kNever &&
      normalized_depth_control.zfunc != xenos::CompareFunction::kAlways &&
      render_target_cache_->GetPath() ==
          RenderTargetCache::Path::kHostRenderTargets) {
    normalized_depth_control.z_write_enable = 1;
  }
  // gpu_vulkan_skip_unused_depth_store: does THIS draw provably never test or
  // write depth/stencil? Computed from the FINAL normalized depth control (after
  // the foliage-LRZ overrides above), so it reflects what the draw will actually
  // do. Consumed at pass begin (skip the depth tile load+store) and reused below
  // to break a depth-store-NONE pass before a depth-using draw. Conservative -
  // any test/write/stencil use leaves it false. Skipped when the LRZ depth-clear
  // lever is forcing a depth loadOp=CLEAR (that path writes depth), and when
  // in-pass EDRAM transfers are enabled (a depth ownership-transfer would be
  // recorded inside this guest pass and its depth write would be dropped by
  // STORE_OP_NONE) - mutually exclusive, mirroring the dc_safe gate.
  depth_store_none_pending_ = cvars::gpu_vulkan_skip_unused_depth_store &&
                              !cvars::gpu_lrz_spike_depth_clear &&
                              !cvars::gpu_vulkan_inpass_edram_transfers &&
                              !normalized_depth_control.z_enable &&
                              !normalized_depth_control.z_write_enable &&
                              !normalized_depth_control.stencil_enable;
  uint32_t normalized_color_mask =
      pixel_shader ? draw_util::GetNormalizedColorMask(
                         regs, pixel_shader->writes_color_targets())
                   : 0;

  // Update the textures before render-target setup and most other work in the
  // submission because samplers depend on this, and debug source readbacks can
  // deliberately split the submission.
  uint32_t used_texture_mask_vertex =
      vertex_shader->GetUsedTextureMaskAfterTranslation();
  uint32_t used_texture_mask_pixel =
      pixel_shader != nullptr ? pixel_shader->GetUsedTextureMaskAfterTranslation()
                              : 0;
  uint32_t used_texture_mask =
      used_texture_mask_vertex | used_texture_mask_pixel;
  std::chrono::steady_clock::time_point tex_t0;
  if (trace_draw_cpu) {
    tex_t0 = std::chrono::steady_clock::now();
  }
  texture_cache_->RequestTextures(used_texture_mask);
  if (trace_draw_cpu) {
    draw_cpu_textures_ns_ += uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - tex_t0)
            .count());
  }
  // RT-as-texture detector (increment 1b): count texture fetches whose source falls
  // in a resolve dest written so far THIS frame = the re-sampled render-to-texture
  // subset the RT-as-texture bridge would serve from the resident RT (the resolve
  // precedes the sample within a frame). Trace-gated, read-only.
  if (cvars::vulkan_trace_draw_outcomes_per_frame &&
      !frame_resolve_edges_.empty()) {
    std::vector<VulkanTextureCache::ActiveTextureSourceRange> rtfed_src;
    texture_cache_->CollectActiveTextureSourceRanges(used_texture_mask, rtfed_src);
    for (const VulkanTextureCache::ActiveTextureSourceRange& s : rtfed_src) {
      if (s.base_length && IsResolveFedTextureBase(s.base_address)) {
        AddRtFedTextureStat();
      }
    }
  }
  if (pixel_shader && normalized_color_mask &&
      cvars::vulkan_trace_texture_source_checksum) {
    if (used_texture_mask_vertex &&
        !TraceTextureSourceChecksums(used_texture_mask_vertex, "vertex",
                                     vertex_shader->ucode_data_hash())) {
      return false;
    }
    if (used_texture_mask_pixel &&
        !TraceTextureSourceChecksums(used_texture_mask_pixel, "pixel",
                                     pixel_shader->ucode_data_hash())) {
      return false;
    }
  }

  // Safe DONT_CARE (gpu_edram_passes_dont_care_safe): if this draw is a
  // provable full-render-area unconditional overwrite (the guest clear idiom -
  // a one-rect rectangle list with always-pass writes), record which
  // attachments' prior contents are dead. Consumed only if THIS draw opens the
  // render pass - those attachments' tile loads are then elided via a
  // load-DONT_CARE render pass variant. Any uncertainty leaves the mask 0 and
  // the pass loads normally.
  dc_safe_pending_state_mask_ = 0;
  if (cvars::gpu_edram_passes_dont_care_safe &&
      !cvars::gpu_edram_passes_dont_care &&
      !cvars::gpu_vulkan_inpass_edram_transfers &&
      prim_type == xenos::PrimitiveType::kRectangleList && index_count == 3 &&
      pixel_shader &&
      !pixel_shader->kills_pixels() &&
      !draw_util::DoesCoverageDependOnAlpha(
          regs.Get<reg::RB_COLORCONTROL>())) {
    // MSAA-safe (was gated msaa==1X): the coverage estimator rounds INWARD to
    // FULLY-sample-covered pixels at 2x/4x (round_min/max=255), and the
    // load-DONT_CARE variant is applied only when the covered rect spans the
    // WHOLE render area (~line 4041) - so a proven full overwrite is load-dead
    // for EVERY sample. The old 1x gate is exactly why dc_safe caught 0 of BD's
    // clears (2E372EA28CC404B7 etc. run at the scene's 2x MSAA) - device dump
    // 2026-06-30. Allowing MSAA lets the ~15 guest clears elide their tile load.
    uint32_t dc_safe_state_mask = 0;
    // Depth/stencil: every covered sample takes an unconditional depth write,
    // and stencil is not in use.
    if (normalized_depth_control.z_enable &&
        normalized_depth_control.z_write_enable &&
        normalized_depth_control.zfunc == xenos::CompareFunction::kAlways &&
        !normalized_depth_control.stencil_enable) {
      dc_safe_state_mask |= 0b1;
    }
    // Color: written by the pixel shader, full write mask, blending disabled
    // (source ONE, destination ZERO, op ADD - a pure replace).
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      if (!pixel_shader->writes_color_target(i)) {
        continue;
      }
      if (((normalized_color_mask >> (i * 4)) & 0xF) != 0xF) {
        continue;
      }
      if ((regs[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFF) !=
          0x00010001) {
        continue;
      }
      dc_safe_state_mask |= uint32_t(1) << (1 + i);
    }
    if (dc_safe_state_mask) {
      if (!cull_extent_estimator_) {
        cull_extent_estimator_ = std::make_unique<DrawExtentEstimator>(
            *register_file_, *memory_, nullptr);
      }
      int32_t cover_x0, cover_y0, cover_x1, cover_y1;
      if (cull_extent_estimator_->EstimateRectListCoverage(
              *vertex_shader, cover_x0, cover_y0, cover_x1, cover_y1)) {
        // The write only happens inside the draw's scissor.
        draw_util::Scissor dc_safe_scissor;
        draw_util::GetScissor(regs, dc_safe_scissor);
        cover_x0 = std::max(cover_x0, int32_t(dc_safe_scissor.offset[0]));
        cover_y0 = std::max(cover_y0, int32_t(dc_safe_scissor.offset[1]));
        cover_x1 = std::min(
            cover_x1,
            int32_t(dc_safe_scissor.offset[0] + dc_safe_scissor.extent[0]));
        cover_y1 = std::min(
            cover_y1,
            int32_t(dc_safe_scissor.offset[1] + dc_safe_scissor.extent[1]));
        if (cover_x0 < cover_x1 && cover_y0 < cover_y1) {
          dc_safe_pending_state_mask_ = dc_safe_state_mask;
          dc_safe_pending_rect_[0] = cover_x0;
          dc_safe_pending_rect_[1] = cover_y0;
          dc_safe_pending_rect_[2] = cover_x1;
          dc_safe_pending_rect_[3] = cover_y1;
        }
      }
    }
  }
  // dc_safe note (device-measured 2026-06-30): on BD's heavy field the detection
  // fires (~16896 draws) but the covered rect is [0,0,320,720] = a 320-wide strip
  // of the 1280-wide RT (partial, not full-screen), so the full-area check (~4041)
  // correctly rejects the load-DONT_CARE - dc_safe[p=0]. BD's pass-openers are
  // partial overwrites; the safe load-skip is not applicable to this title.

  std::chrono::steady_clock::time_point rt_t0;
  if (trace_draw_cpu) {
    rt_t0 = std::chrono::steady_clock::now();
  }
  bool rt_update_ok;
  if (hybrid_current_draw_composite_) {
    // THE EDRAM SOLVE, hybrid form: a post-process composite renders PASS-LESS into
    // edram_buffer_. On the FIRST composite of the frame, end the open host render
    // pass, bridge the main scene INTO edram_buffer_ + hand EDRAM ownership to the
    // buffer (BeginHybridPostprocessPhase); then use the FSI (edram_buffer_) RT
    // update so this + every following composite has NO host-RT transfer/pass and
    // resolves its producer FROM edram_buffer_ (no producer-sample pass-break).
    // Bypasses the host-RT gate/Update entirely.
    if (!render_target_cache_->hybrid_postprocess_phase_active()) {
      EndRenderPass();
      render_target_cache_->BeginHybridPostprocessPhase();
    }
    rt_update_ok = render_target_cache_->UpdateForHybridPostprocessComposite();
    rt_gate_valid_ = false;
  } else if (cvars::vulkan_gate_rt_update) {
    // Skip the redundant per-draw RenderTargetCache::Update when the RT config
    // is byte-identical to the last real Update AND the render pass is still
    // open. A pass break, EDRAM transfer, or frame/submission boundary nulls
    // current_render_pass_, forcing a real Update (which re-enters the pass and
    // runs any EDRAM ownership transfers). The snapshot covers every register
    // Update reads plus its normalized inputs.
    uint32_t gate_surface =
        register_file_->Get<reg::RB_SURFACE_INFO>().value;
    uint32_t gate_depth_info = register_file_->Get<reg::RB_DEPTH_INFO>().value;
    uint32_t gate_color_info[4];
    for (uint32_t i = 0; i < 4; ++i) {
      gate_color_info[i] =
          register_file_
              ->Get<reg::RB_COLOR_INFO>(
                  reg::RB_COLOR_INFO::rt_register_indices[i])
              .value;
    }
    uint64_t gate_vs_hash = vertex_shader->ucode_data_hash();
    bool snapshot_match =
        rt_gate_valid_ && current_render_pass_ != VK_NULL_HANDLE &&
        rt_gate_surface_info_ == gate_surface &&
        rt_gate_depth_control_ == normalized_depth_control.value &&
        rt_gate_color_mask_ == normalized_color_mask &&
        rt_gate_depth_info_ == gate_depth_info &&
        rt_gate_color_info_[0] == gate_color_info[0] &&
        rt_gate_color_info_[1] == gate_color_info[1] &&
        rt_gate_color_info_[2] == gate_color_info[2] &&
        rt_gate_color_info_[3] == gate_color_info[3] &&
        rt_gate_vs_hash_ == gate_vs_hash &&
        rt_gate_is_raster_done_ == is_rasterization_done;
    if (snapshot_match) {
      rt_update_ok = rt_gate_last_ok_;
    } else {
      rt_update_ok = render_target_cache_->Update(
          is_rasterization_done, normalized_depth_control,
          normalized_color_mask, *vertex_shader);
      rt_gate_valid_ = true;
      rt_gate_surface_info_ = gate_surface;
      rt_gate_depth_control_ = normalized_depth_control.value;
      rt_gate_color_mask_ = normalized_color_mask;
      rt_gate_depth_info_ = gate_depth_info;
      rt_gate_color_info_[0] = gate_color_info[0];
      rt_gate_color_info_[1] = gate_color_info[1];
      rt_gate_color_info_[2] = gate_color_info[2];
      rt_gate_color_info_[3] = gate_color_info[3];
      rt_gate_vs_hash_ = gate_vs_hash;
      rt_gate_is_raster_done_ = is_rasterization_done;
      rt_gate_last_ok_ = rt_update_ok;
    }
  } else {
    rt_update_ok = render_target_cache_->Update(
        is_rasterization_done, normalized_depth_control, normalized_color_mask,
        *vertex_shader);
  }
  if (trace_draw_cpu) {
    draw_cpu_rt_ns_ += uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - rt_t0)
            .count());
  }
  if (!rt_update_ok) {
    return false;
  }

  // Create the pipeline using the render pass selected by the render target
  // cache.
  VkPipeline pipeline;
  const VulkanPipelineCache::PipelineLayoutProvider* pipeline_layout_provider;
  std::chrono::steady_clock::time_point pipe_t0;
  if (trace_draw_cpu) {
    pipe_t0 = std::chrono::steady_clock::now();
  }
  bool configure_pipeline_ok = pipeline_cache_->ConfigurePipeline(
      vertex_shader_translation, pixel_shader_translation,
      primitive_processing_result, normalized_depth_control,
      normalized_color_mask,
      render_target_cache_->last_update_render_pass_key(), pipeline,
      pipeline_layout_provider);
  if (trace_draw_cpu) {
    draw_cpu_pipeline_ns_ += uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - pipe_t0)
            .count());
  }
  if (!configure_pipeline_ok) {
    return false;
  }

  // Update the graphics pipeline, and if the new graphics pipeline has a
  // different layout, invalidate incompatible descriptor sets before updating
  // current_guest_graphics_pipeline_layout_.
  if (current_guest_graphics_pipeline_ != pipeline) {
    deferred_command_buffer_.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                               pipeline);
    current_guest_graphics_pipeline_ = pipeline;
    current_external_graphics_pipeline_ = VK_NULL_HANDLE;
    ++draw_outcomes_pipeline_binds_;
  }
  auto pipeline_layout =
      static_cast<const PipelineLayout*>(pipeline_layout_provider);
  if (current_guest_graphics_pipeline_layout_ != pipeline_layout) {
    if (current_guest_graphics_pipeline_layout_) {
      // Keep descriptor set layouts for which the new pipeline layout is
      // compatible with the previous one (pipeline layouts are compatible for
      // set N if set layouts 0 through N are compatible).
      uint32_t descriptor_sets_kept =
          uint32_t(SpirvShaderTranslator::kDescriptorSetCount);
      if (current_guest_graphics_pipeline_layout_
              ->descriptor_set_layout_textures_vertex_ref() !=
          pipeline_layout->descriptor_set_layout_textures_vertex_ref()) {
        descriptor_sets_kept = std::min(
            descriptor_sets_kept,
            uint32_t(SpirvShaderTranslator::kDescriptorSetTexturesVertex));
      }
      if (current_guest_graphics_pipeline_layout_
              ->descriptor_set_layout_textures_pixel_ref() !=
          pipeline_layout->descriptor_set_layout_textures_pixel_ref()) {
        descriptor_sets_kept = std::min(
            descriptor_sets_kept,
            uint32_t(SpirvShaderTranslator::kDescriptorSetTexturesPixel));
      }
    } else {
      // No or unknown pipeline layout previously bound - all bindings are in an
      // indeterminate state.
      current_graphics_descriptor_sets_bound_up_to_date_ = 0;
    }
    current_guest_graphics_pipeline_layout_ = pipeline_layout;
  }

  bool host_render_targets_used = render_target_cache_->GetPath() ==
                                  RenderTargetCache::Path::kHostRenderTargets;

  // Blue Dragon native-draw HLE decoupled present (Half B): when the base
  // CommandProcessor armed capture for this synthetic draw (RB_COLOR_INFO
  // redirected to the non-aliasing decouple base), have the RT cache latch the
  // dedicated full-surface color[0] host RT it just bound - IssueSwap presents
  // it. Mirrors D3D12's GetBoundColorResourceForCapture, but here it reaches the
  // screen (and the RT cache owns the protected RenderTarget* type).
  if (bd_capture_armed_ && host_render_targets_used) {
    render_target_cache_->LatchBoundColorRTForDecoupledCapture();
  }

  // Get dynamic rasterizer state.
  draw_util::ViewportInfo viewport_info;
  // Just handling maxViewportDimensions is enough - viewportBoundsRange[1] must
  // be at least 2 * max(maxViewportDimensions[0...1]) - 1, and
  // maxViewportDimensions must be greater than or equal to the size of the
  // largest possible framebuffer attachment (if the viewport has positive
  // offset and is between maxViewportDimensions and viewportBoundsRange[1],
  // GetHostViewportInfo will adjust ndc_scale/ndc_offset to clamp it, and the
  // clamped range will be outside the largest possible framebuffer anyway.
  // FIXME(Triang3l): Possibly handle maxViewportDimensions and
  // viewportBoundsRange separately because when using fragment shader
  // interlocks, framebuffers are not used, while the range may be wider than
  // dimensions? Though viewport bigger than 4096 - the smallest possible
  // maximum dimension (which is below the 8192 texture size limit on the Xbox
  // 360) - and with offset, is probably a situation that never happens in real
  // life. Or even disregard the viewport bounds range in the fragment shader
  // interlocks case completely - apply the viewport and the scissor offset
  // directly to pixel address and to things like ps_param_gen.
  draw_util::GetHostViewportInfo(
      regs, 1, 1, false, device_properties.maxViewportDimensions[0],
      device_properties.maxViewportDimensions[1], true,
      normalized_depth_control, false, host_render_targets_used,
      pixel_shader && pixel_shader->writes_depth(), viewport_info);

  // Update dynamic graphics pipeline state.
  UpdateDynamicState(viewport_info, primitive_polygonal,
                     normalized_depth_control,
                     primitive_processing_result.host_primitive_type,
                     primitive_processing_result.host_primitive_reset_enabled,
                     normalized_color_mask);

  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (trace_draw_state) {
    auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
    auto rb_modecontrol = regs.Get<reg::RB_MODECONTROL>();
    auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
    auto rb_color_mask = regs.Get<reg::RB_COLOR_MASK>();
    auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
    auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
    auto pa_cl_clip_cntl = regs.Get<reg::PA_CL_CLIP_CNTL>();
    auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
    auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
    auto pa_sc_window_scissor_tl = regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>();
    auto pa_sc_window_scissor_br = regs.Get<reg::PA_SC_WINDOW_SCISSOR_BR>();
    auto pa_sc_screen_scissor_tl = regs.Get<reg::PA_SC_SCREEN_SCISSOR_TL>();
    auto pa_sc_screen_scissor_br = regs.Get<reg::PA_SC_SCREEN_SCISSOR_BR>();
    draw_util::Scissor draw_scissor;
    draw_util::GetScissor(regs, draw_scissor);
    auto color0 = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[0]);
    auto color1 = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[1]);
    auto color2 = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[2]);
    auto color3 = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[3]);
    uint32_t ps_writes =
        pixel_shader ? pixel_shader->writes_color_targets() : 0;
    XELOGI(
        "GPU draw trace: prim={} index_count={} host_vertices={} "
        "host_prim={} index_type={} guest_index_base={:08X} "
        "host_vs_type={} edram_mode={} rb_mode={:08X} raster={} pixel={} "
        "ps_writes={:X} ps_depth={} ps_kills={} vs_memexport={:02X} "
        "ps_memexport={:02X} normalized_color_mask={:04X} "
        "used_textures={:08X} vs_textures={:08X} ps_textures={:08X} "
        "vs_hash={:016X} ps_hash={:016X} "
        "viewport={}x{}+{},{} z={}..{} surface={:08X} pitch={} msaa={} "
        "colorcontrol={:08X} color_mask={:04X} depthcontrol={:08X} "
        "depth_info={:08X}",
        uint32_t(prim_type), index_count,
        primitive_processing_result.host_draw_vertex_count,
        uint32_t(primitive_processing_result.host_primitive_type),
        uint32_t(primitive_processing_result.index_buffer_type),
        primitive_processing_result.guest_index_base,
        static_cast<uint32_t>(
            primitive_processing_result.host_vertex_shader_type),
        uint32_t(edram_mode), rb_modecontrol.value, is_rasterization_done,
        pixel_shader != nullptr, ps_writes,
        pixel_shader ? pixel_shader->writes_depth() : false,
        pixel_shader ? pixel_shader->kills_pixels() : false,
        vertex_shader->memexport_eM_written(),
        pixel_shader ? pixel_shader->memexport_eM_written() : 0,
        normalized_color_mask, used_texture_mask, used_texture_mask_vertex,
        used_texture_mask_pixel,
        vertex_shader->ucode_data_hash(),
        pixel_shader ? pixel_shader->ucode_data_hash() : 0,
        viewport_info.xy_extent[0], viewport_info.xy_extent[1],
        viewport_info.xy_offset[0], viewport_info.xy_offset[1],
        viewport_info.z_min, viewport_info.z_max, rb_surface_info.value,
        uint32_t(rb_surface_info.surface_pitch),
        uint32_t(rb_surface_info.msaa_samples), rb_colorcontrol.value,
        rb_color_mask.value & 0xFFFF, normalized_depth_control.value,
        rb_depth_info.value);
    XELOGI(
        "GPU draw trace: rt color0={:08X} fmt={} mask={:X} base={} "
        "color1={:08X} fmt={} mask={:X} base={} "
        "color2={:08X} fmt={} mask={:X} base={} "
        "color3={:08X} fmt={} mask={:X} base={}",
        color0.value, uint32_t(color0.color_format), rb_color_mask.value & 0xF,
        color0.color_base, color1.value, uint32_t(color1.color_format),
        (rb_color_mask.value >> 4) & 0xF, color1.color_base, color2.value,
        uint32_t(color2.color_format), (rb_color_mask.value >> 8) & 0xF,
        color2.color_base, color3.value, uint32_t(color3.color_format),
        (rb_color_mask.value >> 12) & 0xF, color3.color_base);
    XELOGI(
        "GPU draw trace: raster viewport={}x{}+{},{} z={}..{} "
        "scissor={}x{}+{},{} window_tl={},{} window_br={},{} "
        "screen_tl={},{} screen_br={},{} window_offset={},{} "
        "clip={:08X} vte={:08X} su_sc={:08X} cull_front={} cull_back={} "
        "clip_disable={} dx_clip={} vtx_window_offset={} z_enable={} "
        "z_write={} zfunc={} stencil={} alpha_test={}",
        viewport_info.xy_extent[0], viewport_info.xy_extent[1],
        viewport_info.xy_offset[0], viewport_info.xy_offset[1],
        viewport_info.z_min, viewport_info.z_max, draw_scissor.extent[0],
        draw_scissor.extent[1], draw_scissor.offset[0],
        draw_scissor.offset[1], pa_sc_window_scissor_tl.tl_x,
        pa_sc_window_scissor_tl.tl_y, pa_sc_window_scissor_br.br_x,
        pa_sc_window_scissor_br.br_y, pa_sc_screen_scissor_tl.tl_x,
        pa_sc_screen_scissor_tl.tl_y, pa_sc_screen_scissor_br.br_x,
        pa_sc_screen_scissor_br.br_y,
        int32_t(pa_sc_window_offset.window_x_offset),
        int32_t(pa_sc_window_offset.window_y_offset), pa_cl_clip_cntl.value,
        pa_cl_vte_cntl.value, pa_su_sc_mode_cntl.value,
        pa_su_sc_mode_cntl.cull_front != 0,
        pa_su_sc_mode_cntl.cull_back != 0, pa_cl_clip_cntl.clip_disable != 0,
        pa_cl_clip_cntl.dx_clip_space_def != 0,
        pa_su_sc_mode_cntl.vtx_window_offset_enable != 0,
        normalized_depth_control.z_enable != 0,
        normalized_depth_control.z_write_enable != 0,
        uint32_t(normalized_depth_control.zfunc),
        normalized_depth_control.stencil_enable != 0,
        rb_colorcontrol.alpha_test_enable != 0);
    if (used_texture_mask_vertex) {
      texture_cache_->TraceActiveTextureState(used_texture_mask_vertex,
                                              "vertex");
    }
    if (used_texture_mask_pixel) {
      texture_cache_->TraceActiveTextureState(used_texture_mask_pixel, "pixel");
    }
  }
  // gpu_trace_resolve_timing: cheaply snapshot this guest draw's identity + ROP
  // state so a small-draw oversized-RT pass can be characterized at pass end
  // (which guest draw the anomalous 52ms 1-draw pass actually renders). Cheap
  // register reads; gated -> byte-identical when off. Also captured under
  // gpu_vulkan_classify_img_sr_breaks, which reads last_guest_draw_desc_ to tell
  // whether a brk_img_sr break's consumer is a full-screen composite.
  if (cvars::gpu_trace_resolve_timing ||
      cvars::gpu_vulkan_classify_img_sr_breaks) {
    GuestDrawDesc& d = last_guest_draw_desc_;
    d.ps_hash = pixel_shader ? pixel_shader->ucode_data_hash() : 0;
    d.vs_hash = vertex_shader->ucode_data_hash();
    d.blendcontrol0 = regs[reg::RB_BLENDCONTROL::rt_register_indices[0]];
    d.colorcontrol = regs.Get<reg::RB_COLORCONTROL>().value;
    d.color_mask = normalized_color_mask;
    d.depthcontrol = normalized_depth_control.value;
    d.color0_info =
        regs.Get<reg::RB_COLOR_INFO>(reg::RB_COLOR_INFO::rt_register_indices[0])
            .value;
    d.depth_info = regs.Get<reg::RB_DEPTH_INFO>().value;
    d.host_vertex_count =
        primitive_processing_result.host_draw_vertex_count;
    d.index_count = index_count;
    d.prim_type = uint32_t(prim_type);
    d.ps_writes_depth = pixel_shader && pixel_shader->writes_depth() ? 1u : 0u;
    d.ps_kills = pixel_shader && pixel_shader->kills_pixels() ? 1u : 0u;
  }
  if (pixel_shader && normalized_color_mask &&
      cvars::vulkan_trace_shader_constants) {
    TraceShaderConstants(*pixel_shader, "pixel", true);
  }

  // Whether to load the guest 32-bit (usually big-endian) vertex index
  // indirectly in the vertex shader if full 32-bit indices are not supported by
  // the host.
  bool shader_32bit_index_dma =
      !device_properties.fullDrawIndexUint32 &&
      primitive_processing_result.index_buffer_type ==
          PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA &&
      vgt_draw_initiator.index_size == xenos::IndexFormat::kInt32 &&
      primitive_processing_result.host_vertex_shader_type ==
          Shader::HostVertexShaderType::kVertex;

  // G1-lite (gpu_binning_deinterleave_pos): gather this draw's tagged
  // position vfetch words into the compact de-interleaved ring. Any bail
  // leaves compact_pos_current_base_dwords_ = UINT32_MAX, so
  // UpdateSystemConstantValues keeps kSysFlag_PosFetchRedirect clear and the
  // draw runs verbatim off the interleaved stream. The gather reads guest CPU
  // memory; ranges written only by the GPU (resolves/memexport) are not
  // detected yet - the F1 pixel-correctness gate covers that edge until the
  // cross-frame invalidation unit lands.
  compact_pos_current_base_dwords_ = UINT32_MAX;
  if (compact_pos_ring_.is_valid() &&
      vertex_shader->position_vfetch_tag().valid) {
    const Shader::PositionVfetchTag& pos_tag =
        vertex_shader->position_vfetch_tag();
    std::chrono::steady_clock::time_point gather_t0 =
        std::chrono::steady_clock::now();
    xenos::xe_gpu_vertex_fetch_t pos_vfetch =
        regs.GetVertexFetch(pos_tag.fetch_constant);
    uint32_t pos_needed_words = xenos::GetVertexFormatNeededWords(
        pos_tag.format, pos_tag.used_result_components);
    uint32_t pos_word_count = xe::bit_count(pos_needed_words);
    if (pos_vfetch.type == xenos::FetchConstantType::kVertex &&
        pos_word_count && pos_tag.stride_words) {
      if (compact_pos_cache_frame_ != frame_current_) {
        compact_pos_cache_.clear();
        compact_pos_cache_frame_ = frame_current_;
      }
      uint32_t gathered_base_dwords = UINT32_MAX;
      for (const CompactPosCacheEntry& cache_entry : compact_pos_cache_) {
        if (cache_entry.fc_dword_0 == pos_vfetch.dword_0 &&
            cache_entry.fc_dword_1 == pos_vfetch.dword_1 &&
            cache_entry.stride_words == pos_tag.stride_words &&
            cache_entry.offset_words == pos_tag.offset_words &&
            cache_entry.needed_words == pos_needed_words) {
          gathered_base_dwords = cache_entry.base_dwords;
          break;
        }
      }
      if (gathered_base_dwords == UINT32_MAX) {
        // Whole elements the fetch constant can supply: element e reads up to
        // word e*stride + offset + highest_needed_word, within `size` dwords.
        uint32_t pos_last_word = 31 - xe::lzcnt(pos_needed_words);
        uint32_t pos_span = pos_tag.offset_words + pos_last_word + 1;
        uint32_t fc_size_dwords = pos_vfetch.size;
        uint32_t element_count =
            fc_size_dwords >= pos_span
                ? (fc_size_dwords - pos_span) / pos_tag.stride_words + 1
                : 0;
        // Cap a single gather so a runaway fetch constant range cannot blow
        // the frame segment - bail to the verbatim draw instead.
        constexpr uint64_t kCompactPosMaxGatherBytes = uint64_t(4) << 20;
        uint64_t gather_bytes =
            uint64_t(element_count) * pos_word_count * sizeof(uint32_t);
        if (element_count && gather_bytes <= kCompactPosMaxGatherBytes) {
          bool ring_alloc_ok = false;
          VkDeviceSize ring_offset = compact_pos_ring_.Allocate(
              VkDeviceSize(gather_bytes), &ring_alloc_ok);
          if (ring_alloc_ok) {
            const uint32_t* gather_src =
                memory_->TranslatePhysical<const uint32_t*>(pos_vfetch.address
                                                            << 2);
            uint32_t* gather_dst = reinterpret_cast<uint32_t*>(
                compact_pos_ring_.host_mapping() + ring_offset);
            const uint32_t* element_src = gather_src + pos_tag.offset_words;
            uint32_t stride_words = pos_tag.stride_words;
            // Both paths write the needed words in ascending word order - the
            // layout contract with the shader's redirect arm. Contiguous masks
            // (a packed position vector at one offset - the common case) take
            // the straight strided copy; the generic loop covers sparse masks.
            uint32_t first_word;
            xe::bit_scan_forward(pos_needed_words, &first_word);
            bool words_contiguous =
                pos_needed_words ==
                (((uint32_t(1) << pos_word_count) - 1) << first_word);
            if (words_contiguous) {
              const uint32_t* element_words = element_src + first_word;
              for (uint32_t e = 0; e < element_count; ++e) {
                for (uint32_t w = 0; w < pos_word_count; ++w) {
                  gather_dst[w] = element_words[w];
                }
                gather_dst += pos_word_count;
                element_words += stride_words;
              }
            } else {
              for (uint32_t e = 0; e < element_count; ++e) {
                const uint32_t* element_words = element_src + e * stride_words;
                uint32_t words_remaining = pos_needed_words;
                uint32_t word_index;
                while (xe::bit_scan_forward(words_remaining, &word_index)) {
                  words_remaining &= ~(uint32_t(1) << word_index);
                  *gather_dst++ = element_words[word_index];
                }
              }
            }
            compact_pos_ring_.FlushRange(ring_offset,
                                         VkDeviceSize(gather_bytes));
            gathered_base_dwords = uint32_t(ring_offset >> 2);
            CompactPosCacheEntry new_entry;
            new_entry.fc_dword_0 = pos_vfetch.dword_0;
            new_entry.fc_dword_1 = pos_vfetch.dword_1;
            new_entry.stride_words = pos_tag.stride_words;
            new_entry.offset_words = pos_tag.offset_words;
            new_entry.needed_words = pos_needed_words;
            new_entry.base_dwords = gathered_base_dwords;
            compact_pos_cache_.push_back(new_entry);
          } else {
            ++draw_outcomes_deint_bails_;
          }
        } else {
          ++draw_outcomes_deint_bails_;
        }
      }
      if (gathered_base_dwords != UINT32_MAX) {
        compact_pos_current_base_dwords_ = gathered_base_dwords;
        ++draw_outcomes_deint_redir_draws_;
        draw_outcomes_deint_redir_verts_ +=
            primitive_processing_result.host_draw_vertex_count;
      }
    } else {
      ++draw_outcomes_deint_bails_;
    }
    draw_outcomes_deint_gather_ns_ +=
        uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::steady_clock::now() - gather_t0)
                     .count());
  }

  // Update system constants before uploading them.
  UpdateSystemConstantValues(primitive_polygonal, primitive_processing_result,
                             shader_32bit_index_dma, viewport_info,
                             used_texture_mask, normalized_depth_control,
                             normalized_color_mask);

  // Update uniform buffers and descriptor sets after binding the pipeline with
  // the new layout.
  std::chrono::steady_clock::time_point bind_t0;
  if (trace_draw_cpu) {
    bind_t0 = std::chrono::steady_clock::now();
  }
  bool update_bindings_ok = UpdateBindings(vertex_shader, pixel_shader);
  if (trace_draw_cpu) {
    draw_cpu_emit_t0 = std::chrono::steady_clock::now();
    draw_cpu_bindings_ns_ += uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(draw_cpu_emit_t0 -
                                                             bind_t0)
            .count());
  }
  if (!update_bindings_ok) {
    return false;
  }

  // Hoist the SharedMemory global lock across the vertex + memexport
  // RequestRange calls below, so the per-call reentrant Acquire()s inside
  // RequestRange become cheap recursive re-locks instead of full round-trips on
  // the hot draw-issue path. The mutex is recursive, so holding it across the
  // loop is semantically identical to per-call locking. Released right after the
  // memexport loop (not held across the rest of the draw). Port of the lock-
  // hoist half of xenia-edge c2674b19d. Gated; default off.
  std::unique_lock<std::recursive_mutex> request_range_hoisted_lock(
      global_critical_region::mutex(), std::defer_lock);
  if (cvars::vulkan_hoist_request_range_lock) {
    request_range_hoisted_lock.lock();
  }

  // Ensure vertex buffers are resident.
  // TODO(Triang3l): Cache residency for ranges in a way similar to how texture
  // validity is tracked.
  std::chrono::steady_clock::time_point vfres_t0;
  if (trace_draw_cpu) {
    vfres_t0 = std::chrono::steady_clock::now();
  }
  // Frame-scoped residency cache: clear it when the frame index changes so each
  // guest vertex-buffer write is re-uploaded next frame. Only when the cvar is
  // on (otherwise the cache stays empty and the path below is unchanged).
  if (cvars::vulkan_cache_vertex_residency &&
      vertex_residency_cache_frame_ != frame_current_) {
    vertex_residency_cache_.clear();
    vertex_residency_cache_frame_ = frame_current_;
  }
  uint64_t vertex_buffers_resident[2] = {};
  for (const Shader::VertexBinding& vertex_binding :
       vertex_shader->vertex_bindings()) {
    uint32_t vfetch_index = vertex_binding.fetch_constant;
    if (vertex_buffers_resident[vfetch_index >> 6] &
        (uint64_t(1) << (vfetch_index & 63))) {
      continue;
    }
    xenos::xe_gpu_vertex_fetch_t vfetch_constant =
        regs.GetVertexFetch(vfetch_index);
    switch (vfetch_constant.type) {
      case xenos::FetchConstantType::kVertex:
        break;
      case xenos::FetchConstantType::kInvalidVertex:
        if (cvars::gpu_allow_invalid_fetch_constants) {
          break;
        }
        XELOGW(
            "Vertex fetch constant {} ({:08X} {:08X}) has \"invalid\" type! "
            "This "
            "is incorrect behavior, but you can try bypassing this by "
            "launching Xenia with --gpu_allow_invalid_fetch_constants=true.",
            vfetch_index, vfetch_constant.dword_0, vfetch_constant.dword_1);
        return false;
      default:
        XELOGW(
            "Vertex fetch constant {} ({:08X} {:08X}) is completely invalid!",
            vfetch_index, vfetch_constant.dword_0, vfetch_constant.dword_1);
        return false;
    }
    uint32_t vf_address = vfetch_constant.address << 2;
    uint32_t vf_size = vfetch_constant.size << 2;
    if (cvars::vulkan_cache_vertex_residency) {
      uint64_t vf_key = (uint64_t(vf_address) << 32) | uint64_t(vf_size);
      if (vertex_residency_cache_.find(vf_key) !=
          vertex_residency_cache_.end()) {
        // Already made resident earlier this frame - skip the redundant
        // RequestRange (its per-call dirty-page check + bookkeeping).
        vertex_buffers_resident[vfetch_index >> 6] |= uint64_t(1)
                                                      << (vfetch_index & 63);
        continue;
      }
      if (!shared_memory_->RequestRange(vf_address, vf_size)) {
        XELOGE(
            "Failed to request vertex buffer at 0x{:08X} (size {}) in the "
            "shared memory",
            vf_address, vf_size);
        return false;
      }
      vertex_residency_cache_.insert(vf_key);
      vertex_buffers_resident[vfetch_index >> 6] |= uint64_t(1)
                                                    << (vfetch_index & 63);
      continue;
    }
    if (!shared_memory_->RequestRange(vf_address, vf_size)) {
      XELOGE(
          "Failed to request vertex buffer at 0x{:08X} (size {}) in the shared "
          "memory",
          vf_address, vf_size);
      return false;
    }
    vertex_buffers_resident[vfetch_index >> 6] |= uint64_t(1)
                                                  << (vfetch_index & 63);
  }
  if (trace_draw_cpu) {
    draw_cpu_vfresidency_ns_ += uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - vfres_t0)
            .count());
  }
  if (cvars::vulkan_trace_vertex_fetch_checksum) {
    TraceVertexFetchSources(*vertex_shader,
                            primitive_processing_result.host_draw_vertex_count);
  }

  // Synchronize the memory pages backing memory scatter export streams, and
  // calculate the range that includes the streams for the buffer barrier.
  uint32_t memexport_extent_start = UINT32_MAX, memexport_extent_end = 0;
  for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
    uint32_t memexport_range_base_bytes = memexport_range.base_address_dwords
                                          << 2;
    if (!shared_memory_->RequestRange(memexport_range_base_bytes,
                                      memexport_range.size_bytes)) {
      XELOGE(
          "Failed to request memexport stream at 0x{:08X} (size {}) in the "
          "shared memory",
          memexport_range_base_bytes, memexport_range.size_bytes);
      return false;
    }
    memexport_extent_start =
        std::min(memexport_extent_start, memexport_range_base_bytes);
    memexport_extent_end =
        std::max(memexport_extent_end,
                 memexport_range_base_bytes + memexport_range.size_bytes);
  }
  // Done with RequestRange - release the hoisted global lock before the rest of
  // the draw (barriers, descriptor updates, emission) so it isn't serialized.
  if (request_range_hoisted_lock.owns_lock()) {
    request_range_hoisted_lock.unlock();
  }

  // gpu_shared_memory_double_buffer: the RequestRange uploads above may have
  // switched the shared-memory version the GPU reads (MaybeSwitchVersionForWrite
  // in the direct path). The shared-memory-and-EDRAM descriptor set was bound by
  // UpdateBindings BEFORE those uploads, so it may now point at the wrong
  // version. Re-point it to the current version and, since this draw is about to
  // be recorded, immediately re-bind it so this draw's vfetch/index reads hit the
  // version the new data was written into. No-op (and byte-identical) when off.
  if (shared_memory_->double_buffer_active()) {
    UpdateSharedMemoryDescriptorSetForCurrentVersion();
    constexpr uint32_t kSharedMemorySet =
        uint32_t(SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram);
    if (current_guest_graphics_pipeline_layout_ != nullptr &&
        !(current_graphics_descriptor_sets_bound_up_to_date_ &
          (UINT32_C(1) << kSharedMemorySet))) {
      deferred_command_buffer_.CmdVkBindDescriptorSets(
          VK_PIPELINE_BIND_POINT_GRAPHICS,
          current_guest_graphics_pipeline_layout_->GetPipelineLayout(),
          kSharedMemorySet, 1,
          current_graphics_descriptor_sets_ + kSharedMemorySet, 0, nullptr);
      current_graphics_descriptor_sets_bound_up_to_date_ |=
          UINT32_C(1) << kSharedMemorySet;
    }
  }

  // Insert the shared memory barrier if needed.
  // TODO(Triang3l): Find some PM4 command that can be used for indication of
  // when memexports should be awaited instead of inserting the barrier in Use
  // every time if memory export was done in the previous draw?
  if (memexport_extent_start < memexport_extent_end) {
    shared_memory_->Use(
        VulkanSharedMemory::Usage::kGuestDrawReadWrite,
        std::make_pair(memexport_extent_start,
                       memexport_extent_end - memexport_extent_start));
  } else {
    shared_memory_->Use(VulkanSharedMemory::Usage::kRead);
  }

  // gpu_vulkan_skip_unused_depth_store: if the open guest pass began in the
  // depth-store-NONE mode (its depth attachment skips load+store) but THIS draw
  // tests or writes depth/stencil, end that pass first. STORE_OP_NONE preserved
  // the depth EDRAM memory (no draw in the pass touched depth), so the re-enter
  // below reloads it correctly via the normal render pass. Without this the draw
  // would read the un-loaded (DONT_CARE) depth or lose its write to STORE_OP_NONE.
  if (current_pass_depth_store_none_ &&
      current_render_pass_ != VK_NULL_HANDLE && !depth_store_none_pending_) {
    EndRenderPass();
  }

  // After all commands that may dispatch, copy or insert barriers, submit the
  // barriers (may end the render pass), and (re)enter the render pass before
  // drawing.
  // THE EDRAM SOLVE, hybrid form: a post-process composite renders PASS-LESS into
  // the EDRAM buffer via the 0-attachment FSI render pass (so it has no render-to-
  // texture pass-break) while the main scene used the host-RT pass above. Entering
  // the FSI pass here ends the last host-RT pass (the phase-2 transition) - the one
  // unavoidable break where the first composite reads the finished main scene.
  if (hybrid_current_draw_composite_) {
    SubmitBarriersAndEnterRenderTargetCacheRenderPass(
        render_target_cache_->GetFragmentShaderInterlockRenderPass(),
        render_target_cache_->GetFragmentShaderInterlockFramebuffer());
  } else {
    // Time composite-consumer guest passes under kGuestComposite (brick-2 ceiling
    // measurement) vs kGuest for ordinary geometry. Classification only - the
    // render pass object + rendering are identical either way.
    SubmitBarriersAndEnterRenderTargetCacheRenderPass(
        render_target_cache_->last_update_render_pass(),
        render_target_cache_->last_update_framebuffer(),
        current_draw_is_composite_consumer_ ? GpuPassKind::kGuestComposite
                                            : GpuPassKind::kGuest);
  }
  // gpu_vulkan_retro_depth_none: mark depth/stencil use of THIS draw in the now-
  // open pass. Placed AFTER the pass enter so the pass-OPENING draw's use is not
  // lost to the begin-capture reset. Uses the final normalized depth control
  // (post foliage-LRZ overrides), i.e. what the draw actually does. Refined
  // predicate: z_enable with zfunc==ALWAYS and z_write off never touches the
  // attachment (every fragment passes without a comparison read, nothing is
  // written; in Vulkan depth writes require the test to be enabled AND
  // depthWriteEnable) - such draws leave the pass eligible.
  if (retro_depth_begin_pos_ != SIZE_MAX) {
    bool draw_uses_depth =
        (normalized_depth_control.z_enable &&
         (normalized_depth_control.zfunc != xenos::CompareFunction::kAlways ||
          normalized_depth_control.z_write_enable)) ||
        normalized_depth_control.stencil_enable;
    if (draw_uses_depth) {
      retro_pass_depth_used_ = true;
      // Diagnostic (throttled): why this pass will not be depth-elided - the
      // first depth-using draw's control. Shows what BD's composite passes set.
      if (draw_outcomes_retro_depth_none_diag_ < 12) {
        ++draw_outcomes_retro_depth_none_diag_;
        XELOGI(
            "retro_depth_none: pass NOT eligible - draw z_en={} zfunc={} "
            "z_wr={} stencil={} prim={} idx={}",
            uint32_t(normalized_depth_control.z_enable),
            uint32_t(normalized_depth_control.zfunc),
            uint32_t(normalized_depth_control.z_write_enable),
            uint32_t(normalized_depth_control.stencil_enable),
            uint32_t(prim_type), index_count);
      }
    }
  }
  // gpu_vulkan_retro_color_dontcare (increment B(a)): per-attachment coverage
  // union for retro color-load elision. Poison rules (conservative): a write
  // that can READ the pre-pass content - partial write mask (unwritten CHANNELS
  // of covered texels keep would-be-loaded data) or non-replace blending -
  // poisons the attachment while its union is incomplete. A full-mask replace
  // write never reads dst, so a conditional/partial-area one is merely neutral
  // (contributes no coverage). Only unconditional 3-index rect-list draws with
  // no kills/alpha-coverage contribute, clamped to their scissor; only
  // full-render-area-HEIGHT rects enter the X-interval union (the guest
  // strip-clear idiom - e.g. BD clears in vertical strips no single-draw check
  // can prove). Once complete, later draws read this pass's own writes - fine.
  if (cvars::gpu_vulkan_retro_color_dontcare &&
      retro_depth_begin_pos_ != SIZE_MAX && pixel_shader) {
    bool retro_contributor_shape =
        prim_type == xenos::PrimitiveType::kRectangleList && index_count == 3 &&
        !pixel_shader->kills_pixels() &&
        !draw_util::DoesCoverageDependOnAlpha(regs.Get<reg::RB_COLORCONTROL>());
    // 0 = not yet estimated for this draw, 1 = valid, 2 = failed.
    uint32_t retro_cover_state = 0;
    int32_t retro_x0 = 0, retro_y0 = 0, retro_x1 = 0, retro_y1 = 0;
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      if (!(retro_pass_key_.depth_and_color_used & (uint32_t(1) << (1 + i)))) {
        continue;
      }
      RetroCoverage& retro_cov = retro_color_cov_[i];
      if (retro_cov.complete || retro_cov.poisoned) {
        continue;
      }
      if (!pixel_shader->writes_color_target(i)) {
        continue;
      }
      bool retro_reads_dst =
          ((normalized_color_mask >> (i * 4)) & 0xF) != 0xF ||
          (regs[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFF) !=
              0x00010001;
      if (retro_reads_dst) {
        retro_cov.poisoned = true;
        continue;
      }
      if (!retro_contributor_shape) {
        // Full-mask replace but conditional/non-rect: no read, no coverage.
        continue;
      }
      if (retro_cover_state == 0) {
        if (!cull_extent_estimator_) {
          cull_extent_estimator_ = std::make_unique<DrawExtentEstimator>(
              *register_file_, *memory_, nullptr);
        }
        if (cull_extent_estimator_->EstimateRectListCoverage(
                *vertex_shader, retro_x0, retro_y0, retro_x1, retro_y1)) {
          draw_util::Scissor retro_scissor;
          draw_util::GetScissor(regs, retro_scissor);
          retro_x0 = std::max(retro_x0, int32_t(retro_scissor.offset[0]));
          retro_y0 = std::max(retro_y0, int32_t(retro_scissor.offset[1]));
          retro_x1 = std::min(retro_x1, int32_t(retro_scissor.offset[0] +
                                                retro_scissor.extent[0]));
          retro_y1 = std::min(retro_y1, int32_t(retro_scissor.offset[1] +
                                                retro_scissor.extent[1]));
          retro_cover_state = 1;
        } else {
          retro_cover_state = 2;
        }
      }
      if (retro_cover_state != 1) {
        continue;
      }
      // Height tolerance: the host image is EDRAM-tile-rounded taller than the
      // guest surface (720 guest -> 768 host rows); guest clears stop at the
      // guest height. The tolerance admits them, leaving only the surface's
      // own final-tile padding rows undefined (see the cvar comment).
      int32_t retro_required_height =
          int32_t(retro_pass_extent_.height) -
          int32_t(cvars::gpu_vulkan_retro_color_height_tolerance);
      if (retro_y0 <= 0 && retro_y1 >= retro_required_height &&
          retro_x0 < retro_x1) {
        if (!retro_cov.AddInterval(retro_x0, retro_x1)) {
          retro_cov.poisoned = true;
        } else if (retro_cov.Covers(int32_t(retro_pass_extent_.width))) {
          retro_cov.complete = true;
        }
      }
    }
  }

  // gpu_hw_vertex_fetch: bind the shared-memory buffer as the Vulkan vertex
  // buffers the translated VS's fixed-function inputs read. Bound for every
  // host-type-kVertex draw when the cvar is on (the per-draw
  // kSysFlag_HwVertexFetch decides whether the shader actually consumes them);
  // the binding must exist whenever the pipeline declares vertex input. The
  // enumeration is identical to the pipeline cache's, so the compact binding
  // numbers agree; the per-binding offset is the fetch constant's base address
  // (the vertex ranges were already made resident by the loop above). Recorded
  // before the merge-cursor snapshot so it counts as a state change - this
  // conservatively prevents draw merging from stitching across it rather than
  // corrupting a run (the two levers aren't combined). Byte-identical when off.
  if (cvars::gpu_hw_vertex_fetch &&
      primitive_processing_result.host_vertex_shader_type ==
          Shader::HostVertexShaderType::kVertex) {
    const std::vector<SpirvShaderTranslator::HwVertexFetchAttribute>
        hw_vf_attributes =
            SpirvShaderTranslator::GetHwVertexFetchAttributes(*vertex_shader);
    if (!hw_vf_attributes.empty()) {
      VkBuffer hw_vf_buffers[SpirvShaderTranslator::kMaxHwVertexFetchBindings];
      VkDeviceSize
          hw_vf_offsets[SpirvShaderTranslator::kMaxHwVertexFetchBindings];
      uint32_t hw_vf_binding_count = 0;
      VkBuffer shared_memory_buffer = shared_memory_->buffer();
      for (const SpirvShaderTranslator::HwVertexFetchAttribute& hw_attr :
           hw_vf_attributes) {
        uint32_t binding = hw_attr.binding;
        if (binding >= SpirvShaderTranslator::kMaxHwVertexFetchBindings) {
          continue;
        }
        if (binding + 1 > hw_vf_binding_count) {
          hw_vf_binding_count = binding + 1;
        }
        xenos::xe_gpu_vertex_fetch_t vfetch_constant =
            regs.GetVertexFetch(hw_attr.fetch_constant);
        hw_vf_buffers[binding] = shared_memory_buffer;
        hw_vf_offsets[binding] = VkDeviceSize(vfetch_constant.address << 2);
      }
      if (hw_vf_binding_count) {
        deferred_command_buffer_.CmdVkBindVertexBuffers(
            0, hw_vf_binding_count, hw_vf_buffers, hw_vf_offsets);
      }
    }
  }

  // Lever 2 (vulkan_merge_draws): did this draw record ANY command (state setup,
  // descriptors, dynamic state, render-pass begin/end, barriers, pipeline bind)
  // since IssueDraw entry? If so, state changed -> a pending concatenation run
  // cannot be extended by this draw. (When the pipeline is unchanged - the only
  // mergeable case - no CmdVkBindPipeline is recorded, so a cursor move means a
  // genuine state change; a pipeline change is also caught by the predicate.)
  // Computed here; consumed by the merge interceptor in a later increment.
  merge_cannot_extend_this_draw_ =
      deferred_command_buffer_.command_stream_size_elements() !=
      merge_cmd_cursor_at_entry;

  // Draw.
  // Measurement/perf lever: optionally skip the GPU draw command for tiny draws
  // (all state setup already done above). Sizes the GPU cost of the ~1200 tiny
  // draws/frame and acts as a crude accuracy-for-speed lever.
  const bool skip_tiny_draw =
      cvars::gpu_skip_draws_below_verts > 0 &&
      primitive_processing_result.host_draw_vertex_count <
          uint32_t(cvars::gpu_skip_draws_below_verts);
  if (skip_tiny_draw) {
    // A skipped draw advances the guest index pointer with NO host draw, so it is
    // a HARD merge boundary (Levers 2 / 2b): flush the pending run so it is never
    // stitched across the hole. Emit nothing. (FlushPendingMergeRun also flushes
    // any pending MDI run.)
    if (cvars::vulkan_merge_draws || cvars::vulkan_merge_draws_rewrite ||
        cvars::vulkan_merge_draws_indirect) {
      FlushPendingMergeRun();
    }
  } else if (primitive_processing_result.index_buffer_type ==
                 PrimitiveProcessor::ProcessedIndexBufferType::kNone ||
             shader_32bit_index_dma) {
    // Non-indexed (auto / shader-32bit DMA): breaks a kGuestDMA index run.
    if (cvars::vulkan_merge_draws || cvars::vulkan_merge_draws_rewrite ||
        cvars::vulkan_merge_draws_indirect) {
      FlushPendingMergeRun();
    }
    if (cvars::vulkan_merge_draws_rewrite) {
      ++mrw_auto_;
    }
    deferred_command_buffer_.CmdVkDraw(
        primitive_processing_result.host_draw_vertex_count, 1, 0, 0);
  } else {
    std::pair<VkBuffer, VkDeviceSize> index_buffer;
    switch (primitive_processing_result.index_buffer_type) {
      case PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA:
        index_buffer.first = shared_memory_->buffer();
        index_buffer.second = primitive_processing_result.guest_index_base;
        break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostConverted:
        index_buffer = primitive_processor_->GetConvertedIndexBuffer(
            primitive_processing_result.host_index_buffer_handle);
        break;
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltinForAuto:
      case PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltinForDMA:
        index_buffer = primitive_processor_->GetBuiltinIndexBuffer(
            primitive_processing_result.host_index_buffer_handle);
        break;
      default:
        assert_unhandled_case(primitive_processing_result.index_buffer_type);
        return false;
    }
    VkIndexType index_type = primitive_processing_result.host_index_format ==
                                     xenos::IndexFormat::kInt16
                                 ? VK_INDEX_TYPE_UINT16
                                 : VK_INDEX_TYPE_UINT32;
    if (cvars::gpu_cull_compaction &&
        primitive_processing_result.index_buffer_type ==
            PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA &&
        cull_index_buffer_pool_) {
      // Lever 2 cull: route each kGuestDMA indexed draw through a transient index
      // buffer. STEP 2 (cull): if the draw qualifies and a flattened LIST can be
      // drawn (dynamic topology active), drop clearly-backface/offscreen triangles
      // and emit only the survivors. STEP 1 (NO-OP) otherwise: copy the raw guest
      // indices VERBATIM (byte-identical). NO endian swap / mask / base-add (the
      // VS does those at spirv_shader_translator.cc:1592/1606); vertexOffset stays
      // 0 (VGT_INDX_OFFSET via system constants). Mutually exclusive with merges.
      if (cvars::vulkan_merge_draws || cvars::vulkan_merge_draws_rewrite ||
        cvars::vulkan_merge_draws_indirect) {
        FlushPendingMergeRun();
      }
      const uint32_t idx_count =
          primitive_processing_result.host_draw_vertex_count;
      const uint32_t stride = index_type == VK_INDEX_TYPE_UINT16 ? 2u : 4u;
      const xenos::PrimitiveType cull_prim_type =
          primitive_processing_result.host_primitive_type;
      // A flattened LIST can only be drawn when the pipeline was built with dynamic
      // topology (same gate as the topology emit in UpdateDynamicState). Else the
      // cull falls back to the verbatim NO-OP so geometry is never scrambled.
      const bool can_emit_list =
          (cvars::vulkan_dynamic_state_topology ||
           cvars::gpu_cull_compaction) &&
          GetVulkanDevice()->properties().apiVersion >=
              VK_MAKE_API_VERSION(0, 1, 3, 0) &&
          (cull_prim_type == xenos::PrimitiveType::kTriangleList ||
           cull_prim_type == xenos::PrimitiveType::kTriangleStrip);
      ++draw_outcomes_cull_branch_;
      if (frame_current_ != cull_budget_frame_) {
        cull_budget_frame_ = frame_current_;
        cull_draws_this_frame_ = 0;
      }
      // Step 2b-i sanity stats over qualifying draws (read-only).
      if (vertex_shader->is_position_affine_mvp_candidate()) {
        draw_outcomes_cull_slice_ops_sum_ +=
            vertex_shader->position_slice_ops().size();
        if (vertex_shader->position_slice_replayable()) {
          ++draw_outcomes_cull_slice_replayable_;
        }
      }
      bool draw_emitted = false;
      if (!can_emit_list) {
        ++draw_outcomes_cull_skip_dyntop_;
      } else if (!vertex_shader->is_position_affine_mvp_candidate()) {
        ++draw_outcomes_cull_skip_qual_;
      } else if (cvars::gpu_cull_max_per_frame != 0 &&
                 cull_draws_this_frame_ >= cvars::gpu_cull_max_per_frame) {
        // gpu_cull_max_per_frame validation throttle: budget spent this frame,
        // draw verbatim so the heavy scene still renders for the correctness check.
      } else {
        ++cull_draws_this_frame_;
        if (!cull_extent_estimator_) {
          cull_extent_estimator_ = std::make_unique<DrawExtentEstimator>(
              *register_file_, *memory_, nullptr);
        }
        if (cvars::gpu_cull_replay_validate) {
          switch (cull_extent_estimator_->ValidateAffinePositionReplay(
              *vertex_shader)) {
            case DrawExtentEstimator::AffineValidateStatus::kAffine:
              ++draw_outcomes_replay_affine_;
              break;
            case DrawExtentEstimator::AffineValidateStatus::kNonAffine:
              ++draw_outcomes_replay_nonaffine_;
              break;
            case DrawExtentEstimator::AffineValidateStatus::kUnsupported:
              ++draw_outcomes_replay_unsupported_;
              break;
            default:
              break;
          }
          uint32_t e_milli = uint32_t(
              cull_extent_estimator_->affine_validate_max_error() * 1000.0f);
          if (e_milli > draw_outcomes_replay_max_error_milli_) {
            draw_outcomes_replay_max_error_milli_ = e_milli;
          }
        }
        bool built = cull_extent_estimator_->BuildCulledIndexList(*vertex_shader);
        // Fast-engaged vs fallback-format histogram for this draw.
        if (cull_extent_estimator_->last_used_fast_replay()) {
          ++draw_outcomes_cull_fast_engaged_;
        } else {
          DrawExtentEstimator::FastSetupFail fail =
              cull_extent_estimator_->last_setup_fail();
          ++draw_outcomes_cull_fail_[uint32_t(fail) & 7u];
          if (fail == DrawExtentEstimator::FastSetupFail::kMultiLeaf) {
            // Bucket multi-leaf by leaf count: 2 hints fixable slice over-
            // inclusion, 3+ hints genuine multi-input/skinned position.
            ++draw_outcomes_cull_multi_lc_
                [std::min(cull_extent_estimator_->last_leaf_count(), 7u)];
          }
        }
        if (built &&
            cull_extent_estimator_->culled_index_stride() == stride) {
          ++draw_outcomes_cull_draws_;
          draw_outcomes_cull_dropped_tris_ +=
              cull_extent_estimator_->culled_dropped_triangles();
          const uint32_t culled_count =
              cull_extent_estimator_->culled_index_count();
          if (culled_count == 0) {
            // Every triangle dropped - draw nothing (the GPU never bins it). This
            // is the WHOLE-DRAW cull: a fully off-screen draw, skipped before
            // binning, saving its full per-draw cost. Count it to size the lever.
            ++draw_outcomes_cull_whole_skip_;
            draw_outcomes_cull_whole_skip_verts_ += idx_count;
            draw_emitted = true;
          } else {
            const size_t culled_bytes =
                cull_extent_estimator_->culled_index_byte_size();
            VkBuffer cull_index_buffer = VK_NULL_HANDLE;
            VkDeviceSize cull_index_offset = 0;
            uint8_t* cull_mapping = cull_index_buffer_pool_->Request(
                frame_current_, culled_bytes, stride, cull_index_buffer,
                cull_index_offset);
            if (cull_mapping) {
              std::memcpy(cull_mapping,
                          cull_extent_estimator_->culled_index_data(),
                          culled_bytes);
              // The survivors are a triangle LIST - override the topology and
              // keep the tracker in sync (the next draw re-sets its own).
              deferred_command_buffer_.CmdVkSetPrimitiveTopology(
                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
              dynamic_primitive_topology_ =
                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
              deferred_command_buffer_.CmdVkBindIndexBuffer(
                  cull_index_buffer, cull_index_offset, index_type);
              deferred_command_buffer_.CmdVkDrawIndexed(culled_count, 1, 0, 0,
                                                        0);
              draw_emitted = true;
            }
          }
        } else {
          uint32_t bail =
              uint32_t(cull_extent_estimator_->culled_bail_reason());
          if (bail < 12u) {
            ++draw_outcomes_cull_bail_[bail];
          }
        }
      }
      if (!draw_emitted) {
        // STEP 1 NO-OP path: verbatim copy of the raw guest indices.
        const size_t copy_bytes = size_t(idx_count) * stride;
        VkBuffer cull_index_buffer = VK_NULL_HANDLE;
        VkDeviceSize cull_index_offset = 0;
        uint8_t* cull_mapping = cull_index_buffer_pool_->Request(
            frame_current_, copy_bytes, stride, cull_index_buffer,
            cull_index_offset);
        const uint8_t* guest_indices =
            memory_->TranslatePhysical<const uint8_t*>(
                uint32_t(index_buffer.second));
        if (cull_mapping && guest_indices) {
          std::memcpy(cull_mapping, guest_indices, copy_bytes);
          deferred_command_buffer_.CmdVkBindIndexBuffer(
              cull_index_buffer, cull_index_offset, index_type);
          deferred_command_buffer_.CmdVkDrawIndexed(idx_count, 1, 0, 0, 0);
        } else {
          deferred_command_buffer_.CmdVkBindIndexBuffer(
              index_buffer.first, index_buffer.second, index_type);
          deferred_command_buffer_.CmdVkDrawIndexed(idx_count, 1, 0, 0, 0);
        }
      }
    } else if ((cvars::vulkan_merge_draws ||
                cvars::vulkan_merge_draws_rewrite) &&
        primitive_processing_result.index_buffer_type ==
            PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA) {
      // Lever 2 Step 4: zero-copy draw concatenation. EXTEND the pending run if
      // this draw shares ALL state with it and indexes the contiguous next byte
      // range; else flush and start a new head (or emit standalone if this draw
      // is itself non-mergeable). cvar-on correctness is UNVERIFIABLE without the
      // device - validated by the on-reconnect A/B (read the frame is identical).
      const uint32_t idx_count =
          primitive_processing_result.host_draw_vertex_count;
      const uint32_t idx_base = uint32_t(index_buffer.second);
      const uint32_t stride = index_type == VK_INDEX_TYPE_UINT16 ? 2u : 4u;
      const int32_t vgt_indx_offset =
          regs.Get<int32_t>(XE_GPU_REG_VGT_INDX_OFFSET);
      const xenos::Endian index_endian =
          primitive_processing_result.host_shader_index_endian;
      const xenos::PrimitiveType prim_type =
          primitive_processing_result.host_primitive_type;
      // Rewrite mode (vulkan_merge_draws_rewrite, wins if both set): the run's
      // indices are COPIED verbatim into a fixed-cap transient index block, so
      // SCATTERED guest index ranges concatenate too - the contiguity term is
      // replaced by block-capacity room in the predicate.
      const bool merge_rewrite = cvars::vulkan_merge_draws_rewrite;
      // LIST topologies concatenate verbatim (no cross-draw stitching).
      // Primitive-restart lists are excluded (restart with list topologies is
      // not valid Vulkan without an extension). TRIANGLE STRIPS additionally
      // concatenate in rewrite mode when vulkan_merge_draws_rewrite_strips is
      // on: the extend path JOINS strips inside the rewritten block (restart
      // marker / degenerate triangles), so stitching is handled, not avoided.
      // Memexport (GPU side effects) always breaks mergeability.
      const bool strip_concat =
          merge_rewrite && cvars::vulkan_merge_draws_rewrite_strips &&
          prim_type == xenos::PrimitiveType::kTriangleStrip;
      const bool list_mergeable =
          (prim_type == xenos::PrimitiveType::kTriangleList ||
           prim_type == xenos::PrimitiveType::kLineList ||
           prim_type == xenos::PrimitiveType::kPointList) &&
          !primitive_processing_result.host_primitive_reset_enabled;
      const bool mergeable = (list_mergeable || strip_concat) &&
                             memexport_extent_start >= memexport_extent_end;
      const size_t copy_bytes = size_t(idx_count) * stride;
      // Strip joins write up to 3 extra indices ahead of the incoming draw's
      // bytes - reserve room for them in the capacity term.
      const size_t join_reserve_bytes =
          strip_concat ? size_t(3) * stride : size_t(0);
      const bool can_extend =
          merge_pending_active_ && mergeable &&
          !merge_cannot_extend_this_draw_ &&
          current_guest_graphics_pipeline_ == merge_pending_pipeline_ &&
          current_guest_graphics_pipeline_layout_ ==
              merge_pending_pipeline_layout_ &&
          index_type == merge_pending_index_type_ &&
          (merge_rewrite
               ? (merge_pending_rewrite_mapping_ != nullptr &&
                  merge_pending_rewrite_used_bytes_ + copy_bytes +
                          join_reserve_bytes <=
                      kMergeRewriteBlockBytes)
               : idx_base == merge_pending_next_byte_) &&
          uint32_t(vgt_indx_offset) == merge_pending_vertex_base_index_ &&
          index_endian == merge_pending_vertex_index_endian_ &&
          prim_type == merge_pending_prim_type_ &&
          primitive_processing_result.host_primitive_reset_enabled ==
              merge_pending_reset_enabled_;
      // Live attribution: why didn't this draw extend the active run? First
      // failing gate only; read from the mrw[] outcomes-line counters.
      if (can_extend) {
        ++mrw_ext_;
      } else if (!mergeable) {
        ++mrw_nomrg_;
      } else if (merge_pending_active_) {
        if (merge_cannot_extend_this_draw_) {
          ++mrw_cant_;
        } else if (current_guest_graphics_pipeline_ !=
                       merge_pending_pipeline_ ||
                   current_guest_graphics_pipeline_layout_ !=
                       merge_pending_pipeline_layout_) {
          ++mrw_pipe_;
        } else if (index_type != merge_pending_index_type_) {
          ++mrw_itype_;
        } else if (merge_rewrite
                       ? (merge_pending_rewrite_mapping_ == nullptr ||
                          merge_pending_rewrite_used_bytes_ + copy_bytes +
                                  join_reserve_bytes >
                              kMergeRewriteBlockBytes)
                       : idx_base != merge_pending_next_byte_) {
          ++mrw_cap_;
        } else if (uint32_t(vgt_indx_offset) !=
                   merge_pending_vertex_base_index_) {
          ++mrw_vgt_;
        } else if (index_endian != merge_pending_vertex_index_endian_) {
          ++mrw_end_;
        } else if (prim_type != merge_pending_prim_type_) {
          ++mrw_prim_;
        } else {
          ++mrw_rst_;
        }
      }
      // Rebase-feasibility classifier (read-only, mrwf[]): this draw's vertex
      // fetch constants vs the previous merge-class draw's. same = fetch is
      // not the run breaker; b16/b32 = only a uniform stride-aligned base
      // shift (the index-rebasing widener's coverage; split by whether the
      // bias magnitude fits 16-bit indices); shape = real fetch change.
      {
        const auto& fetch_bindings = vertex_shader->vertex_bindings();
        const bool shape_usable =
            !fetch_bindings.empty() && fetch_bindings.size() <= 8;
        MrwFetchSnap cur[8];
        uint32_t binding_count = 0;
        if (shape_usable) {
          for (const Shader::VertexBinding& b : fetch_bindings) {
            xenos::xe_gpu_vertex_fetch_t vf =
                regs.GetVertexFetch(b.fetch_constant);
            MrwFetchSnap& s = cur[binding_count++];
            s.fetch_constant = b.fetch_constant;
            s.address_bytes = uint32_t(vf.address) << 2;
            s.dword_1 = vf.dword_1;
            s.type = uint32_t(vf.type);
            s.stride_bytes = b.stride_words * 4;
          }
        }
        if (shape_usable && mrw_prev_fetch_valid_ &&
            binding_count == mrw_prev_fetch_count_) {
          bool same = true;
          bool biasable = true;
          int64_t bias_indices = INT64_MAX;  // unset
          for (uint32_t i = 0; i < binding_count; ++i) {
            const MrwFetchSnap& p = mrw_prev_fetch_[i];
            const MrwFetchSnap& c = cur[i];
            if (c.fetch_constant != p.fetch_constant || c.type != p.type ||
                c.dword_1 != p.dword_1 || c.stride_bytes != p.stride_bytes) {
              same = false;
              biasable = false;
              break;
            }
            if (c.address_bytes != p.address_bytes) {
              same = false;
              const int64_t delta =
                  int64_t(c.address_bytes) - int64_t(p.address_bytes);
              if (!c.stride_bytes ||
                  (delta % int64_t(c.stride_bytes)) != 0) {
                biasable = false;
                break;
              }
              const int64_t k = delta / int64_t(c.stride_bytes);
              if (bias_indices == INT64_MAX) {
                bias_indices = k;
              } else if (bias_indices != k) {
                biasable = false;
                break;
              }
            }
          }
          if (same) {
            ++mrw_fetch_same_;
          } else if (biasable && bias_indices != INT64_MAX) {
            if (bias_indices >= -65535 && bias_indices <= 65535) {
              ++mrw_fetch_bias16_;
            } else {
              ++mrw_fetch_bias32_;
            }
          } else {
            ++mrw_fetch_shape_;
          }
        }
        if (shape_usable) {
          std::memcpy(mrw_prev_fetch_, cur,
                      sizeof(MrwFetchSnap) * binding_count);
          mrw_prev_fetch_count_ = binding_count;
          mrw_prev_fetch_valid_ = true;
        } else {
          mrw_prev_fetch_valid_ = false;
        }
      }
      if (can_extend) {
        // Concatenate this draw's index range into the run by growing the
        // HEAD-EMITTED draw command in place. Recording anything here would
        // be wrong: this draw recorded no state (the extend precondition),
        // and the head draw is already in the stream at the correct position
        // relative to its own state.
        if (merge_rewrite) {
          const uint8_t* extend_guest_indices =
              memory_->TranslatePhysical<const uint8_t*>(idx_base);
          uint8_t* dst =
              merge_pending_rewrite_mapping_ + merge_pending_rewrite_used_bytes_;
          uint32_t join_indices = 0;
          if (prim_type == xenos::PrimitiveType::kTriangleStrip) {
            if (merge_pending_reset_enabled_) {
              // Restart-enabled strip run: one all-FF restart marker (the host
              // reset index is always 0xFFFF/0xFFFFFFFF for kGuestDMA, and the
              // fixed-function restart compare reads the raw buffer bytes, so
              // all-FF is endian-immune). Restart also resets the strip's
              // winding parity, so no parity bookkeeping is needed.
              std::memset(dst, 0xFF, stride);
              join_indices = 1;
            } else {
              // Restart-disabled strip run: classic degenerate join - repeat
              // the run's last index, then the incoming strip's first index.
              // Every triangle spanning the junction contains a duplicated
              // vertex (zero area, rasterizes nothing). When the accumulated
              // index count is odd, a third duplicate keeps the incoming
              // strip's first real triangle at even parity so its winding
              // matches what it would be standalone.
              const uint8_t* last_src = dst - stride;
              join_indices = (merge_pending_index_count_ & 1u) ? 3u : 2u;
              std::memcpy(dst, last_src, stride);
              if (join_indices == 3u) {
                std::memcpy(dst + stride, last_src, stride);
                std::memcpy(dst + 2u * stride, extend_guest_indices, stride);
              } else {
                std::memcpy(dst + stride, extend_guest_indices, stride);
              }
            }
            dst += size_t(join_indices) * stride;
          }
          std::memcpy(dst, extend_guest_indices, copy_bytes);
          merge_pending_rewrite_used_bytes_ +=
              size_t(join_indices) * stride + copy_bytes;
          merge_pending_index_count_ += join_indices;
        }
        merge_pending_index_count_ += idx_count;
        merge_pending_next_byte_ += idx_count * stride;
        deferred_command_buffer_.PatchVkDrawIndexedIndexCount(
            merge_pending_draw_args_offset_, merge_pending_index_count_);
      } else {
        FlushPendingMergeRun();
        bool run_started = false;
        if (mergeable && merge_rewrite && copy_bytes <= kMergeRewriteBlockBytes) {
          // Start a rewrite run: reserve a fixed-cap transient block, copy the
          // head draw's raw guest index bytes verbatim (no swap/mask - the VS
          // applies the index endian via system constants), and head-emit the
          // run's bind + draw against the block.
          VkBuffer rewrite_buffer = VK_NULL_HANDLE;
          VkDeviceSize rewrite_offset = 0;
          uint8_t* rewrite_mapping = cull_index_buffer_pool_->Request(
              frame_current_, kMergeRewriteBlockBytes, stride, rewrite_buffer,
              rewrite_offset);
          const uint8_t* head_guest_indices =
              memory_->TranslatePhysical<const uint8_t*>(idx_base);
          if (rewrite_mapping && head_guest_indices) {
            std::memcpy(rewrite_mapping, head_guest_indices, copy_bytes);
            deferred_command_buffer_.CmdVkBindIndexBuffer(
                rewrite_buffer, rewrite_offset, index_type);
            merge_pending_draw_args_offset_ =
                deferred_command_buffer_.CmdVkDrawIndexedRetained(idx_count, 1,
                                                                  0, 0, 0);
            merge_pending_rewrite_mapping_ = rewrite_mapping;
            merge_pending_rewrite_used_bytes_ = copy_bytes;
            run_started = true;
          }
        } else if (mergeable && !merge_rewrite) {
          // Start a zero-copy run, EMITTING its bind + draw at the head (so
          // later draws' state setup can never be recorded ahead of the run's
          // draw). Extensions only patch the recorded index count.
          deferred_command_buffer_.CmdVkBindIndexBuffer(
              index_buffer.first, index_buffer.second, index_type);
          merge_pending_draw_args_offset_ =
              deferred_command_buffer_.CmdVkDrawIndexedRetained(idx_count, 1,
                                                                0, 0, 0);
          merge_pending_rewrite_mapping_ = nullptr;
          merge_pending_rewrite_used_bytes_ = 0;
          run_started = true;
        }
        if (run_started) {
          ++mrw_head_;
          merge_pending_active_ = true;
          merge_pending_index_buffer_ = index_buffer.first;
          merge_pending_index_base_ = index_buffer.second;
          merge_pending_index_type_ = index_type;
          merge_pending_index_count_ = idx_count;
          merge_pending_next_byte_ = idx_base + idx_count * stride;
          merge_pending_pipeline_ = current_guest_graphics_pipeline_;
          merge_pending_pipeline_layout_ =
              current_guest_graphics_pipeline_layout_;
          merge_pending_vertex_base_index_ = uint32_t(vgt_indx_offset);
          merge_pending_vertex_index_endian_ = index_endian;
          merge_pending_prim_type_ = prim_type;
          merge_pending_reset_enabled_ =
              primitive_processing_result.host_primitive_reset_enabled;
        } else {
          // Non-mergeable kGuestDMA draw (strip/fan/memexport/restart), or the
          // rewrite block could not be reserved: standalone.
          deferred_command_buffer_.CmdVkBindIndexBuffer(
              index_buffer.first, index_buffer.second, index_type);
          deferred_command_buffer_.CmdVkDrawIndexed(idx_count, 1, 0, 0, 0);
        }
      }
    } else if (cvars::vulkan_merge_draws_indirect && mdi_supported_ &&
               primitive_processing_result.index_buffer_type ==
                   PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA) {
      // Lever 2b: MDI draw batching. Unlike concatenation (Lever 2), each
      // accumulated draw becomes a SEPARATE VkDrawIndexedIndirectCommand, so
      // triangle/line STRIPS batch too (no cross-draw primitive stitching) and
      // index ranges need NOT be contiguous (per-command firstIndex) - exactly
      // the Blue Dragon intro case (all strips, scattered). vertexOffset stays 0
      // (VGT_INDX_OFFSET is applied via system constants, like the per-draw path),
      // so a run still requires unchanged state and equal VGT_INDX_OFFSET; the
      // bytes drawn are identical, only batched. cvar-on correctness is validated
      // by the device A/B (read the frame is identical to default-off).
      const uint32_t idx_count =
          primitive_processing_result.host_draw_vertex_count;
      const uint32_t idx_base = uint32_t(index_buffer.second);
      const uint32_t stride = index_type == VK_INDEX_TYPE_UINT16 ? 2u : 4u;
      const int32_t vgt_indx_offset =
          regs.Get<int32_t>(XE_GPU_REG_VGT_INDX_OFFSET);
      const xenos::Endian index_endian =
          primitive_processing_result.host_shader_index_endian;
      const xenos::PrimitiveType prim_type =
          primitive_processing_result.host_primitive_type;
      // firstIndex = guest_index_base / stride is exact only when the base is
      // stride-aligned (the GPU/Vulkan bind requires aligned index fetch anyway);
      // be defensive and skip MDI otherwise. Memexport (GPU side effects/ordering)
      // breaks a run. LIST and STRIP are both fine (each command is its own draw).
      const bool mdi_mergeable =
          (prim_type == xenos::PrimitiveType::kTriangleList ||
           prim_type == xenos::PrimitiveType::kTriangleStrip ||
           prim_type == xenos::PrimitiveType::kLineList ||
           prim_type == xenos::PrimitiveType::kLineStrip ||
           prim_type == xenos::PrimitiveType::kPointList) &&
          memexport_extent_start >= memexport_extent_end &&
          (idx_base % stride) == 0;
      const bool can_extend =
          merge_mdi_active_ && mdi_mergeable && merge_mdi_mapping_ &&
          !merge_cannot_extend_this_draw_ &&
          index_buffer.first == merge_mdi_index_buffer_ &&
          current_guest_graphics_pipeline_ == merge_mdi_pipeline_ &&
          current_guest_graphics_pipeline_layout_ ==
              merge_mdi_pipeline_layout_ &&
          index_type == merge_mdi_index_type_ &&
          uint32_t(vgt_indx_offset) == merge_mdi_vertex_base_index_ &&
          index_endian == merge_mdi_index_endian_ &&
          merge_mdi_count_ < mdi_max_draw_count_;
      VkDrawIndexedIndirectCommand cmd;
      cmd.indexCount = idx_count;
      cmd.instanceCount = 1;
      cmd.firstIndex = idx_base / stride;
      cmd.vertexOffset = 0;
      cmd.firstInstance = 0;
      if (can_extend) {
        // Fill the next slot of the run's already-emitted (head) indirect array,
        // and bump the live count the GPU reads at submit (vkCmdDrawIndexedIndirectCount).
        merge_mdi_mapping_[merge_mdi_count_++] = cmd;
        *merge_mdi_count_ptr_ = merge_mdi_count_;
      } else {
        // Close the previous run (its indirect draw was already emitted at its
        // head; nothing to flush), then try to open a new run HEAD here.
        merge_mdi_active_ = false;
        merge_mdi_mapping_ = nullptr;
        merge_mdi_count_ptr_ = nullptr;
        merge_mdi_count_ = 0;
        bool opened = false;
        if (mdi_mergeable && indirect_buffer_pool_) {
          const size_t array_bytes =
              size_t(mdi_max_draw_count_) * sizeof(VkDrawIndexedIndirectCommand);
          VkBuffer indirect_buffer = VK_NULL_HANDLE;
          VkDeviceSize indirect_offset = 0;
          uint8_t* mapping = indirect_buffer_pool_->Request(
              frame_current_, array_bytes, sizeof(uint32_t), indirect_buffer,
              indirect_offset);
          // Separate 4-byte slot holding the live draw count consumed by
          // vkCmdDrawIndexedIndirectCount. No zero padding: the GPU reads exactly
          // merge_mdi_count_ commands at submit, so growing the run only bumps this
          // value (the 32-slot zero padding is what made the fixed-count MDI a net
          // loss; see docs/research/20260602-mdi-device-ab-result.md).
          VkBuffer count_buffer = VK_NULL_HANDLE;
          VkDeviceSize count_offset = 0;
          uint8_t* count_mapping =
              mapping ? indirect_buffer_pool_->Request(
                            frame_current_, sizeof(uint32_t), sizeof(uint32_t),
                            count_buffer, count_offset)
                      : nullptr;
          if (mapping && count_mapping) {
            auto* cmds = reinterpret_cast<VkDrawIndexedIndirectCommand*>(mapping);
            cmds[0] = cmd;
            auto* count_ptr = reinterpret_cast<uint32_t*>(count_mapping);
            *count_ptr = 1;
            // Emit the batched draw NOW (head position), correctly ordered after
            // this draw's state. Bind the index buffer at offset 0 (each command
            // carries its absolute firstIndex); the count buffer bounds the draw to
            // exactly the filled slots - no no-op padding.
            deferred_command_buffer_.CmdVkBindIndexBuffer(index_buffer.first, 0,
                                                          index_type);
            deferred_command_buffer_.CmdVkDrawIndexedIndirectCount(
                indirect_buffer, indirect_offset, count_buffer, count_offset,
                mdi_max_draw_count_,
                uint32_t(sizeof(VkDrawIndexedIndirectCommand)));
            merge_mdi_active_ = true;
            merge_mdi_mapping_ = cmds;
            merge_mdi_count_ptr_ = count_ptr;
            merge_mdi_count_ = 1;
            merge_mdi_index_buffer_ = index_buffer.first;
            merge_mdi_index_type_ = index_type;
            merge_mdi_pipeline_ = current_guest_graphics_pipeline_;
            merge_mdi_pipeline_layout_ = current_guest_graphics_pipeline_layout_;
            merge_mdi_vertex_base_index_ = uint32_t(vgt_indx_offset);
            merge_mdi_index_endian_ = index_endian;
            opened = true;
          }
        }
        if (!opened) {
          // Non-mergeable or allocation failed: standalone, identical to the
          // per-draw path.
          deferred_command_buffer_.CmdVkBindIndexBuffer(
              index_buffer.first, index_buffer.second, index_type);
          const bool is_at_merge =
              register_file_->Get<reg::RB_COLORCONTROL>().alpha_test_enable != 0;
          deferred_command_buffer_.CmdVkDrawIndexed(
              (cvars::gpu_force_tiny_draws ||
               (cvars::gpu_collapse_alphatest_coverage && is_at_merge) ||
               (cvars::gpu_foliage_thin_factor >= 2 && is_at_merge &&
                (draw_outcomes_alphatest_draws_ %
                 uint32_t(cvars::gpu_foliage_thin_factor)) != 0))
                  ? std::min<uint32_t>(idx_count, 3u)
                  : idx_count,
              1, 0, 0, 0);
        }
      }
    } else {
      // cvar off, or a non-kGuestDMA indexed type (kHostConverted/kHostBuiltin,
      // which use per-draw backend index handles and are non-mergeable): flush
      // any pending run (concat or MDI), then emit standalone exactly as before.
      if (cvars::vulkan_merge_draws || cvars::vulkan_merge_draws_rewrite ||
        cvars::vulkan_merge_draws_indirect) {
        FlushPendingMergeRun();
      }
      if (cvars::vulkan_merge_draws_rewrite &&
          primitive_processing_result.index_buffer_type !=
              PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA) {
        ++mrw_ndma_;
      }
      deferred_command_buffer_.CmdVkBindIndexBuffer(
          index_buffer.first, index_buffer.second, index_type);
      // DIAGNOSTIC gpu_force_tiny_draws: clamp index count to 3 (one triangle)
      // to collapse per-VERTEX work while keeping all per-DRAW work, isolating
      // the per-draw overhead floor. gpu_collapse_alphatest_coverage does the
      // same clamp but ONLY for alpha-test draws (the BD foliage), isolating the
      // alpha-test foliage's per-vertex+fragment cost. gpu_foliage_thin_factor N
      // is the SPEED HACK: keep 1 of every N alpha-test draws, collapse the rest
      // (foliage density vs fps). All default off.
      const bool is_alphatest_draw =
          register_file_->Get<reg::RB_COLORCONTROL>().alpha_test_enable != 0;
      bool collapse_this_draw =
          cvars::gpu_force_tiny_draws ||
          (cvars::gpu_collapse_alphatest_coverage && is_alphatest_draw) ||
          (cvars::gpu_collapse_alphatest_depthonly && is_alphatest_draw &&
           normalized_color_mask == 0) ||
          (cvars::gpu_foliage_thin_factor >= 2 && is_alphatest_draw &&
           (draw_outcomes_alphatest_draws_ %
            uint32_t(cvars::gpu_foliage_thin_factor)) != 0);
      if (!collapse_this_draw && !is_alphatest_draw &&
          (cvars::gpu_collapse_blended_coverage ||
           cvars::gpu_collapse_opaque_coverage ||
           cvars::gpu_blended_thin_factor >= 2)) {
        // Per-class diagnostic collapse / blended-thin speed hack for the
        // non-alpha-test classes (mirrors the draw-outcomes classifier below).
        auto bc0 = register_file_->Get<reg::RB_BLENDCONTROL>();
        const bool blends_draw =
            !(bc0.color_srcblend == xenos::BlendFactor::kOne &&
              bc0.color_destblend == xenos::BlendFactor::kZero &&
              bc0.color_comb_fcn == xenos::BlendOp::kAdd &&
              bc0.alpha_srcblend == xenos::BlendFactor::kOne &&
              bc0.alpha_destblend == xenos::BlendFactor::kZero &&
              bc0.alpha_comb_fcn == xenos::BlendOp::kAdd);
        if (cvars::gpu_collapse_blended_coverage && blends_draw) {
          collapse_this_draw = true;
        } else if (cvars::gpu_blended_thin_factor >= 2 && blends_draw &&
                   (draw_outcomes_blended_draws_ %
                    uint32_t(cvars::gpu_blended_thin_factor)) != 0) {
          collapse_this_draw = true;
        } else if (cvars::gpu_collapse_opaque_coverage && !blends_draw &&
                   normalized_depth_control.z_write_enable) {
          collapse_this_draw = true;
        }
      }
      // VRS (gpu_vrs_foliage_rate, Thor novel-hardware lever): coarse-shade the
      // overdraw-heavy foliage class (alpha-test OR blended) at 2x2/4x4 to cut
      // per-covered-fragment FS + alpha-test-discard + texture-fetch work on
      // Adreno; 1x1 (full rate) for every other draw so only foliage coarsens.
      // The pipeline declares the dynamic state only when this cvar is on (see
      // vulkan_pipeline_cache.cc), so default-off (rate 0) short-circuits here =
      // fully inert + zero per-draw overhead.
      if (cvars::gpu_vrs_foliage_rate > 0 &&
          GetVulkanDevice()->extensions().ext_KHR_fragment_shading_rate) {
        // Diagnostic: suppress VRS (1x1) until guest uptime crosses the gate, so
        // a title can boot+navigate VRS-off (matching the VRS-off baseline's nav
        // pacing + reaching the same scene) then enable VRS only in the target
        // scene for a matched-scene A/B. The dynamic state stays set (to 1x1) so
        // there is no undefined-shading-rate hazard.
        bool vrs_active =
            cvars::gpu_vrs_enable_after_guest_ms == 0 ||
            xe::Clock::QueryGuestUptimeMillis() >=
                uint64_t(cvars::gpu_vrs_enable_after_guest_ms);
        // Single-run A/B: when the alternation is live (frozen or free-running
        // past the gate), the per-frame phase decides VRS on/off.
        if (gpu_ab_alt_active_) {
          vrs_active = gpu_freeze_vrs_phase_on_;
        }
        uint32_t vrs_rate = !vrs_active                        ? 1u
                            : cvars::gpu_vrs_foliage_rate >= 4 ? 4u
                            : cvars::gpu_vrs_foliage_rate >= 2 ? 2u
                                                              : 1u;
        bool vrs_foliage = cvars::gpu_vrs_all_draws || is_alphatest_draw;
        if (!vrs_foliage) {
          auto bc_vrs = register_file_->Get<reg::RB_BLENDCONTROL>();
          vrs_foliage =
              !(bc_vrs.color_srcblend == xenos::BlendFactor::kOne &&
                bc_vrs.color_destblend == xenos::BlendFactor::kZero &&
                bc_vrs.color_comb_fcn == xenos::BlendOp::kAdd &&
                bc_vrs.alpha_srcblend == xenos::BlendFactor::kOne &&
                bc_vrs.alpha_destblend == xenos::BlendFactor::kZero &&
                bc_vrs.alpha_comb_fcn == xenos::BlendOp::kAdd);
        }
        const uint32_t r = vrs_foliage ? vrs_rate : 1u;
        VkExtent2D frag_size = {r, r};
        deferred_command_buffer_.CmdVkSetFragmentShadingRate(
            frag_size, VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
            VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR);
      }
      // BD-30 post-process lever (gpu_skip_bloom, user-approved lower bloom/blur):
      // skip ADDITIVE full-screen composite draws (bloom / glow accumulation = a
      // <=6-vertex quad whose blend destination factor is ONE, i.e. it ADDS to the
      // scene). Transparent FOLIAGE is also blended but submits many vertices
      // (excluded -> foliage stays full); the alpha-blended HUD uses dest=INV_SRC_ALPHA
      // (not ONE, so kept); the opaque tonemap/copy isn't blended (kept). Hypothesis:
      // xenia resolves RTs lazily on SAMPLE, so dropping the consumer that samples a
      // bloom producer RT also drops that producer's ~1.7ms per-pass tile-resolve.
      bool skip_bloom_draw = false;
      if (cvars::gpu_skip_bloom && !collapse_this_draw &&
          primitive_processing_result.host_draw_vertex_count <= 6) {
        auto bc_bloom = register_file_->Get<reg::RB_BLENDCONTROL>();
        skip_bloom_draw =
            bc_bloom.color_destblend == xenos::BlendFactor::kOne &&
            bc_bloom.color_srcblend != xenos::BlendFactor::kZero;
      }
      if (!skip_bloom_draw) {
        deferred_command_buffer_.CmdVkDrawIndexed(
            collapse_this_draw
                ? std::min<uint32_t>(
                      primitive_processing_result.host_draw_vertex_count, 3u)
                : primitive_processing_result.host_draw_vertex_count,
            1, 0, 0, 0);
      }
      // Opaque depth pre-pass (Unit 3): if this is an opaque candidate, emit a
      // self-contained copy to the FRONT of the pass (spliced at EndRenderPass)
      // so alpha-test foliage behind it early-Z-rejects. v1 gated to EDS-off +
      // host-RT so the reused pipeline's baked depth/cull/topology/stencil are
      // correct. Default-off cvar; safe no-op otherwise.
      if (prepass_active_ && cvars::gpu_opaque_depth_prepass &&
          !cvars::vulkan_dynamic_state_depth &&
          !cvars::vulkan_dynamic_state_topology &&
          !cvars::vulkan_dynamic_state_cull_front &&
          !cvars::vulkan_dynamic_state_stencil && !cvars::gpu_cull_compaction &&
          render_target_cache_->GetPath() ==
              RenderTargetCache::Path::kHostRenderTargets &&
          normalized_depth_control.z_write_enable) {
        auto rb_cc_prepass = register_file_->Get<reg::RB_COLORCONTROL>();
        auto bc0_prepass = register_file_->Get<reg::RB_BLENDCONTROL>();
        const bool blends_prepass =
            !(bc0_prepass.color_srcblend == xenos::BlendFactor::kOne &&
              bc0_prepass.color_destblend == xenos::BlendFactor::kZero &&
              bc0_prepass.color_comb_fcn == xenos::BlendOp::kAdd &&
              bc0_prepass.alpha_srcblend == xenos::BlendFactor::kOne &&
              bc0_prepass.alpha_destblend == xenos::BlendFactor::kZero &&
              bc0_prepass.alpha_comb_fcn == xenos::BlendOp::kAdd);
        if (!rb_cc_prepass.alpha_test_enable && !blends_prepass) {
          EmitOpaquePrepassDraw(
              index_buffer.first, index_buffer.second, index_type,
              primitive_processing_result.host_draw_vertex_count);
        }
      }
    }
  }

  ++draw_outcomes_rendered_;
  // Depth-prepass eligibility classification (Unit 1 of the opaque depth
  // pre-pass). Opaque = writes depth, no alpha-test/discard, RT0 not blending
  // (src=One,dst=Zero,op=Add for both color and alpha) - these can be rendered
  // depth-only first so the color pass early-Z-rejects their occluded fragments.
  {
    auto rb_colorcontrol_cls = register_file_->Get<reg::RB_COLORCONTROL>();
    auto rb_blendcontrol0 = register_file_->Get<reg::RB_BLENDCONTROL>();
    const bool blends =
        !(rb_blendcontrol0.color_srcblend == xenos::BlendFactor::kOne &&
          rb_blendcontrol0.color_destblend == xenos::BlendFactor::kZero &&
          rb_blendcontrol0.color_comb_fcn == xenos::BlendOp::kAdd &&
          rb_blendcontrol0.alpha_srcblend == xenos::BlendFactor::kOne &&
          rb_blendcontrol0.alpha_destblend == xenos::BlendFactor::kZero &&
          rb_blendcontrol0.alpha_comb_fcn == xenos::BlendOp::kAdd);
    if (rb_colorcontrol_cls.alpha_test_enable) {
      ++draw_outcomes_alphatest_draws_;
    } else if (blends) {
      ++draw_outcomes_blended_draws_;
    } else if (normalized_depth_control.z_write_enable) {
      ++draw_outcomes_opaque_draws_;
      draw_outcomes_opaque_verts_ +=
          primitive_processing_result.host_draw_vertex_count;
    }
  }
  if (cvars::gpu_trace_cullable_tris) {
    // Front B read-only counter: how many triangles a CPU cull WOULD drop (C1
    // stub returns 0). Never mutates geometry.
    draw_outcomes_cullable_tris_ += CountCullableTriangles(*vertex_shader);
    // Whole-draw frustum-cull potential: a draw entirely off-screen (behind the
    // camera or beyond one XY clip plane) could be dropped before the GPU bins
    // it, saving its full per-draw cost - the lever per-triangle culling can't
    // reach. Read-only sizing.
    if (cull_extent_estimator_->last_draw_valid_verts()) {
      ++draw_outcomes_wholecull_elig_;
      if (cull_extent_estimator_->last_draw_whole_cullable()) {
        ++draw_outcomes_wholecull_draws_;
        draw_outcomes_wholecull_verts_ +=
            cull_extent_estimator_->last_draw_valid_verts();
      }
    }
  }
  if (cvars::vulkan_trace_draw_outcomes_per_frame) {
    // Lever 2 Step 0 feasibility signals (read-only; never mutates geometry):
    // 0a whole-shader (coarse, under-counts) and 0b position-export-slice
    // (precise - ignores color/UV math, the number that sizes the cull's reach).
    if (vertex_shader->is_affine_mvp_candidate()) {
      ++draw_outcomes_affine_mvp_draws_;
      draw_outcomes_affine_mvp_vertices_ +=
          primitive_processing_result.host_draw_vertex_count;
    }
    // G1-lite eligibility sizing (read-only): draws/vertices whose position
    // fetch is statically redirectable to a compact binning stream
    // (Shader::position_vfetch_tag) - the coverage ceiling for the
    // de-interleaved binning position stream, measurable before it is built.
    if (vertex_shader->position_vfetch_tag().valid) {
      ++draw_outcomes_deint_elig_draws_;
      draw_outcomes_deint_elig_verts_ +=
          primitive_processing_result.host_draw_vertex_count;
    }
    if (vertex_shader->is_position_affine_mvp_candidate()) {
      ++draw_outcomes_affine_mvp_pos_draws_;
      draw_outcomes_affine_mvp_pos_vertices_ +=
          primitive_processing_result.host_draw_vertex_count;
    } else {
      // 0c: bucket the non-qualifying draw's verts by why its position slice
      // failed - the number that decides the cull's true ceiling on this title.
      uint32_t hv = primitive_processing_result.host_draw_vertex_count;
      switch (vertex_shader->position_mvp_disqual_reason()) {
        case Shader::PositionMvpDisqualReason::kDynamicAddressing:
          draw_outcomes_pos_disq_a0_verts_ += hv;
          break;
        case Shader::PositionMvpDisqualReason::kControlFlowLoop:
          draw_outcomes_pos_disq_loop_verts_ += hv;
          break;
        case Shader::PositionMvpDisqualReason::kBackwardJump:
          draw_outcomes_pos_disq_backjump_verts_ += hv;
          break;
        case Shader::PositionMvpDisqualReason::kSubroutineCall:
          draw_outcomes_pos_disq_call_verts_ += hv;
          break;
        case Shader::PositionMvpDisqualReason::kTextureFetch:
          draw_outcomes_pos_disq_texfetch_verts_ += hv;
          break;
        default:
          draw_outcomes_pos_disq_other_verts_ += hv;
          break;
      }
    }
  }
  if (trace_draw_cpu) {
    const auto draw_cpu_end = std::chrono::steady_clock::now();
    draw_cpu_total_ns_ += uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(draw_cpu_end -
                                                             draw_cpu_t0)
            .count());
    // Emit = after UpdateBindings to here (draw-command recording + any wait).
    draw_cpu_emit_ns_ += uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(draw_cpu_end -
                                                             draw_cpu_emit_t0)
            .count());
  }
  draw_outcomes_total_vertices_ +=
      primitive_processing_result.host_draw_vertex_count;
  draw_outcomes_max_vertices_ =
      std::max(draw_outcomes_max_vertices_,
               primitive_processing_result.host_draw_vertex_count);
  if (cvars::vulkan_trace_draw_outcomes_per_frame) {
    ++draw_prim_counts_[uint32_t(prim_type) & 0xF];
    uint32_t hv = primitive_processing_result.host_draw_vertex_count;
    ++draw_vtx_bucket_[hv < 16 ? 0 : hv < 64 ? 1 : hv < 256 ? 2 : 3];
    // vfetch contiguity: classify this draw's guest index/vertex source vs the
    // previous draw (only meaningful within a same-pipeline run). Populates the
    // previously-dead merge_vf_* counters so the merge feasibility is real data.
    uint32_t vf_addr = primitive_processing_result.guest_index_base;
    if (cvars::gpu_merge_vf_index_stride_fix > 0) {
      // Stride-correct contiguity (measurement-only). guest_index_base is a BYTE
      // address, so the run end is addr + count*stride, not addr + count. Only
      // kGuestDMA draws have a guest index byte-range to compare; other sources
      // (kNone/kHostConverted/kHostBuiltin*) are skipped entirely and do NOT
      // update last_addr/last_end, so they neither get mis-counted as scattered
      // nor inject a false break into the next guest-DMA draw's comparison.
      if (primitive_processing_result.index_buffer_type ==
          PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA) {
        uint32_t stride = primitive_processing_result.host_index_format ==
                                  xenos::IndexFormat::kInt16
                              ? 2u
                              : 4u;
        uint32_t vf_end = vf_addr + hv * stride;
        if (vf_addr == merge_vf_last_addr_) {
          ++merge_vf_same_;
        } else if (vf_addr == merge_vf_last_end_) {
          ++merge_vf_contig_;
        } else {
          ++merge_vf_scattered_;
        }
        merge_vf_last_addr_ = vf_addr;
        merge_vf_last_end_ = vf_end;
      }
    } else {
      uint32_t vf_end = vf_addr + hv;
      if (vf_addr == merge_vf_last_addr_) {
        ++merge_vf_same_;
      } else if (vf_addr == merge_vf_last_end_) {
        ++merge_vf_contig_;
      } else {
        ++merge_vf_scattered_;
      }
      merge_vf_last_addr_ = vf_addr;
      merge_vf_last_end_ = vf_end;
    }
    // Run-length histogram: maximal runs of consecutive draws sharing the same
    // guest graphics pipeline. The go/no-go signal for batching feasibility.
    if (current_guest_graphics_pipeline_ == merge_run_pipeline_ &&
        merge_run_pipeline_ != VK_NULL_HANDLE) {
      ++merge_run_len_;
    } else {
      if (merge_run_len_) {
        uint32_t rl = merge_run_len_;
        uint32_t b = rl <= 1   ? 0
                     : rl == 2 ? 1
                     : rl <= 4 ? 2
                     : rl <= 8 ? 3
                     : rl <= 16 ? 4
                     : rl <= 32 ? 5
                     : rl <= 64 ? 6
                                : 7;
        ++merge_run_hist_[b];
      }
      merge_run_pipeline_ = current_guest_graphics_pipeline_;
      merge_run_len_ = 1;
    }
    // True-eligible-run histogram: like the pipeline-run histogram above, but a
    // run only extends when the DOMINANT merge gates also hold - same pipeline,
    // byte-contiguous guest index range, and list-mergeable topology - so it
    // reflects the real draw-concatenation potential (an upper bound) at baseline,
    // independent of whether vulkan_merge_draws is on.
    {
      const xenos::PrimitiveType elig_prim =
          primitive_processing_result.host_primitive_type;
      const bool elig_mergeable =
          primitive_processing_result.index_buffer_type ==
              PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA &&
          (elig_prim == xenos::PrimitiveType::kTriangleList ||
           elig_prim == xenos::PrimitiveType::kLineList ||
           elig_prim == xenos::PrimitiveType::kPointList) &&
          memexport_extent_start >= memexport_extent_end &&
          !primitive_processing_result.host_primitive_reset_enabled;
      const uint32_t elig_stride =
          primitive_processing_result.host_index_format ==
                  xenos::IndexFormat::kInt16
              ? 2u
              : 4u;
      const uint32_t elig_base = primitive_processing_result.guest_index_base;
      const uint32_t elig_end = elig_base + hv * elig_stride;
      const bool elig_extend =
          merge_elig_run_active_ && elig_mergeable &&
          current_guest_graphics_pipeline_ == merge_elig_run_pipeline_ &&
          elig_prim == merge_elig_run_prim_type_ &&
          elig_base == merge_elig_run_next_byte_;
      if (elig_extend) {
        ++merge_elig_run_len_;
        merge_elig_run_next_byte_ = elig_end;
      } else {
        // Merge-miss attribution: if a run was active, classify why THIS draw
        // breaks it (first failing gate).
        if (merge_elig_run_active_) {
          if (primitive_processing_result.index_buffer_type !=
              PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA) {
            ++merge_miss_non_dma_;
          } else if (elig_prim != xenos::PrimitiveType::kTriangleList &&
                     elig_prim != xenos::PrimitiveType::kLineList &&
                     elig_prim != xenos::PrimitiveType::kPointList) {
            ++merge_miss_topology_;
          } else if (!elig_mergeable) {
            // kGuestDMA + list-mergeable topology, but memexport or restart.
            ++merge_miss_other_;
          } else if (current_guest_graphics_pipeline_ !=
                         merge_elig_run_pipeline_ ||
                     elig_prim != merge_elig_run_prim_type_) {
            ++merge_miss_state_;
          } else {
            ++merge_miss_noncontig_;
          }
        }
        if (merge_elig_run_len_) {
          uint32_t rl = merge_elig_run_len_;
          uint32_t b = rl <= 1   ? 0
                       : rl == 2 ? 1
                       : rl <= 4 ? 2
                       : rl <= 8 ? 3
                       : rl <= 16 ? 4
                       : rl <= 32 ? 5
                       : rl <= 64 ? 6
                                  : 7;
          ++merge_elig_run_hist_[b];
        }
        if (elig_mergeable) {
          merge_elig_run_active_ = true;
          merge_elig_run_len_ = 1;
          merge_elig_run_pipeline_ = current_guest_graphics_pipeline_;
          merge_elig_run_prim_type_ = elig_prim;
          merge_elig_run_next_byte_ = elig_end;
        } else {
          merge_elig_run_active_ = false;
          merge_elig_run_len_ = 0;
        }
      }
    }
    // Strip-coalescer-eligible run histogram: admits kTriangleStrip and DROPS the
    // byte-contiguity gate (the draw-coalescer stitches non-contiguous strips via
    // a merged restart-separated index buffer), keyed on
    // pipeline+pipeline_layout+VGT_INDX_OFFSET. Sizes BD's strip draw-merge factor.
    {
      const xenos::PrimitiveType strip_prim =
          primitive_processing_result.host_primitive_type;
      const int32_t strip_vgt_offset =
          regs.Get<int32_t>(XE_GPU_REG_VGT_INDX_OFFSET);
      const bool strip_mergeable =
          primitive_processing_result.index_buffer_type ==
              PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA &&
          strip_prim == xenos::PrimitiveType::kTriangleStrip &&
          memexport_extent_start >= memexport_extent_end;
      const bool strip_extend =
          merge_strip_run_active_ && strip_mergeable &&
          current_guest_graphics_pipeline_ == merge_strip_run_pipeline_ &&
          current_guest_graphics_pipeline_layout_ == merge_strip_run_layout_ &&
          strip_vgt_offset == merge_strip_run_vgt_offset_;
      if (strip_extend) {
        ++merge_strip_run_len_;
      } else {
        if (merge_strip_run_len_) {
          uint32_t rl = merge_strip_run_len_;
          uint32_t b = rl <= 1    ? 0
                       : rl == 2  ? 1
                       : rl <= 4  ? 2
                       : rl <= 8  ? 3
                       : rl <= 16 ? 4
                       : rl <= 32 ? 5
                       : rl <= 64 ? 6
                                  : 7;
          ++merge_strip_run_hist_[b];
        }
        if (strip_mergeable) {
          merge_strip_run_active_ = true;
          merge_strip_run_len_ = 1;
          merge_strip_run_pipeline_ = current_guest_graphics_pipeline_;
          merge_strip_run_layout_ = current_guest_graphics_pipeline_layout_;
          merge_strip_run_vgt_offset_ = strip_vgt_offset;
        } else {
          merge_strip_run_active_ = false;
          merge_strip_run_len_ = 0;
        }
      }
    }
    // Strip-coalescer DESCRIPTOR-aware run histogram (stripd_runlen): identical to
    // the strip run above but a run extends only when the vertex float constants
    // (the per-mesh transform) are ALSO unchanged - the TRUE rank-3 merge factor.
    // A constant change marks a new object whose strip CANNOT be stitched into the
    // same physical draw (it has a different transform), so it breaks the run. The
    // gap to strip_runlen = what the rank-4/5 wideners (bindless / pre-transform)
    // would have to recover for the full strip-merge factor.
    {
      const xenos::PrimitiveType stripd_prim =
          primitive_processing_result.host_primitive_type;
      const int32_t stripd_vgt_offset =
          regs.Get<int32_t>(XE_GPU_REG_VGT_INDX_OFFSET);
      const bool stripd_mergeable =
          primitive_processing_result.index_buffer_type ==
              PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA &&
          stripd_prim == xenos::PrimitiveType::kTriangleStrip &&
          memexport_extent_start >= memexport_extent_end;
      const bool stripd_extend =
          merge_stripd_run_active_ && stripd_mergeable &&
          merge_draw_vfetch_consts_same_ &&
          current_guest_graphics_pipeline_ == merge_stripd_run_pipeline_ &&
          current_guest_graphics_pipeline_layout_ == merge_stripd_run_layout_ &&
          stripd_vgt_offset == merge_stripd_run_vgt_offset_;
      if (stripd_extend) {
        ++merge_stripd_run_len_;
      } else {
        if (merge_stripd_run_len_) {
          uint32_t rl = merge_stripd_run_len_;
          uint32_t b = rl <= 1    ? 0
                       : rl == 2  ? 1
                       : rl <= 4  ? 2
                       : rl <= 8  ? 3
                       : rl <= 16 ? 4
                       : rl <= 32 ? 5
                       : rl <= 64 ? 6
                                  : 7;
          ++merge_stripd_run_hist_[b];
        }
        if (stripd_mergeable) {
          merge_stripd_run_active_ = true;
          merge_stripd_run_len_ = 1;
          merge_stripd_run_pipeline_ = current_guest_graphics_pipeline_;
          merge_stripd_run_layout_ = current_guest_graphics_pipeline_layout_;
          merge_stripd_run_vgt_offset_ = stripd_vgt_offset;
        } else {
          merge_stripd_run_active_ = false;
          merge_stripd_run_len_ = 0;
        }
      }
    }
    // Strip-coalescer TEXTURE-aware run histogram (stript_runlen): like the strip
    // run but a run extends only when the pixel texture descriptor signature is
    // ALSO unchanged - ISOLATES the texture run-breaker (parallel to stripd's
    // transform breaker) and predicts rank-5's standalone payoff (after a
    // pre-transform removes the transform breaker, only texture breaks remain).
    {
      const xenos::PrimitiveType stript_prim =
          primitive_processing_result.host_primitive_type;
      const int32_t stript_vgt_offset =
          regs.Get<int32_t>(XE_GPU_REG_VGT_INDX_OFFSET);
      const bool stript_mergeable =
          primitive_processing_result.index_buffer_type ==
              PrimitiveProcessor::ProcessedIndexBufferType::kGuestDMA &&
          stript_prim == xenos::PrimitiveType::kTriangleStrip &&
          memexport_extent_start >= memexport_extent_end;
      const bool stript_extend =
          merge_stript_run_active_ && stript_mergeable &&
          merge_draw_ptex_same_ &&
          current_guest_graphics_pipeline_ == merge_stript_run_pipeline_ &&
          current_guest_graphics_pipeline_layout_ == merge_stript_run_layout_ &&
          stript_vgt_offset == merge_stript_run_vgt_offset_;
      if (stript_extend) {
        ++merge_stript_run_len_;
      } else {
        if (merge_stript_run_len_) {
          uint32_t rl = merge_stript_run_len_;
          uint32_t b = rl <= 1    ? 0
                       : rl == 2  ? 1
                       : rl <= 4  ? 2
                       : rl <= 8  ? 3
                       : rl <= 16 ? 4
                       : rl <= 32 ? 5
                       : rl <= 64 ? 6
                                  : 7;
          ++merge_stript_run_hist_[b];
        }
        if (stript_mergeable) {
          merge_stript_run_active_ = true;
          merge_stript_run_len_ = 1;
          merge_stript_run_pipeline_ = current_guest_graphics_pipeline_;
          merge_stript_run_layout_ = current_guest_graphics_pipeline_layout_;
          merge_stript_run_vgt_offset_ = stript_vgt_offset;
        } else {
          merge_stript_run_active_ = false;
          merge_stript_run_len_ = 0;
        }
      }
    }
  }
  trace_last_draw_sequence_ = ++trace_draw_sequence_;
  trace_last_draw_vs_hash_ = vertex_shader_hash;
  trace_last_draw_ps_hash_ = pixel_shader_hash;

  // Invalidate textures in memexported memory and watch for changes.
  for (const draw_util::MemExportRange& memexport_range : memexport_ranges_) {
    shared_memory_->RangeWrittenByGpu(memexport_range.base_address_dwords << 2,
                                      memexport_range.size_bytes);
  }

  return true;
}

bool VulkanCommandProcessor::IssueCopy() {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  // Pass-collapse (BD-30): once the native renderer has rendered the field this
  // frame, the LLE EDRAM resolves that follow (frontbuffer/composite copies) are
  // REDUNDANT - we present the native RT, not the resolved LLE surface. Skip them
  // (23 resolve passes = the bulk of the 79-pass EDRAM overhead). Resolves BEFORE
  // the field (bd_native_field_rendered_ still false - textures/shadows feeding the
  // field) are kept. Gated default-off.
  if (cvars::gpu_bd_native_renderer && cvars::gpu_bd_native_skip_resolves &&
      bd_native_renderer_ && bd_native_renderer_->initialized() &&
      bd_native_field_rendered_ &&
      register_file_->Get<reg::RB_SURFACE_INFO>().surface_pitch == 720) {
    // Only skip resolves whose SOURCE is the field frontbuffer (pitch 720) - that
    // is the one made redundant by presenting the native RT. Keep resolves of
    // other surfaces (shadow/texture render-targets the field SAMPLES) - skipping
    // those blacked out the textured geometry (the thin-strip break).
    return true;
  }

  // Lever 2 (vulkan_merge_draws): a resolve/copy depends on prior draws having
  // executed - realize any pending concatenation run first.
  FlushPendingMergeRun();

  // Predicated-tiling flatten diagnosis: at every resolve, log where in the
  // bin-pass sequence it runs + the window state the resolve rect will use.
  // This is the direct observation of BD's tile-resolve timing that all three
  // pixel-fix attempts lacked.
  if (cvars::gpu_trace_bin_select) {
    const RegisterFile& regs = *register_file_;
    XELOGI(
        "RESOLVE ctx: passes_seen={} select={:08X} win_off={:08X} "
        "offset_en={} scissor_tl={:08X} br={:08X} color={:08X} depth={:08X} "
        "surf={:08X} ignore_off={}",
        flatten_bin_passes_seen_, uint32_t(bin_select_ & 0xFFFFFFFFull),
        regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET],
        uint32_t(regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable),
        regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL],
        regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR],
        regs[XE_GPU_REG_RB_COLOR_INFO], regs[XE_GPU_REG_RB_DEPTH_INFO],
        regs[XE_GPU_REG_RB_SURFACE_INFO],
        draw_util::resolve_ignore_window_offset ? 1 : 0);
  }

  uint64_t copy_sequence = ++trace_copy_sequence_;
  bool trace_copy_state = ShouldTraceVulkanCopyState();
  if (trace_copy_state) {
    const RegisterFile& regs = *register_file_;
    auto rb_copy_control = regs.Get<reg::RB_COPY_CONTROL>();
    auto rb_copy_dest_info = regs.Get<reg::RB_COPY_DEST_INFO>();
    auto rb_copy_dest_pitch = regs.Get<reg::RB_COPY_DEST_PITCH>();
    auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
    auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
    auto rb_color_mask = regs.Get<reg::RB_COLOR_MASK>();
    auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
    auto color0 = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[0]);
    auto color1 = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[1]);
    auto color2 = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[2]);
    auto color3 = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[3]);
    XELOGI(
        "GPU copy trace: IssueCopy begin copy_seq={} last_draw_seq={} "
        "last_vs_hash={:016X} last_ps_hash={:016X} "
        "control={:08X} src={} sample={} command={} color_clear={} "
        "depth_clear={} raw_dest_base={:08X} "
        "dest_info={:08X} dest_format={} dest_pitch={} dest_height={} "
        "surface={:08X} surface_pitch={} msaa={} color={:08X},{:08X},{:08X},"
        "{:08X} depth={:08X} colorcontrol={:08X} color_mask={:04X}",
        copy_sequence, trace_last_draw_sequence_, trace_last_draw_vs_hash_,
        trace_last_draw_ps_hash_, rb_copy_control.value,
        uint32_t(rb_copy_control.copy_src_select),
        uint32_t(rb_copy_control.copy_sample_select),
        uint32_t(rb_copy_control.copy_command),
        uint32_t(rb_copy_control.color_clear_enable),
        uint32_t(rb_copy_control.depth_clear_enable),
        regs[XE_GPU_REG_RB_COPY_DEST_BASE], rb_copy_dest_info.value,
        uint32_t(rb_copy_dest_info.copy_dest_format),
        uint32_t(rb_copy_dest_pitch.copy_dest_pitch),
        uint32_t(rb_copy_dest_pitch.copy_dest_height),
        rb_surface_info.value, uint32_t(rb_surface_info.surface_pitch),
        uint32_t(rb_surface_info.msaa_samples), color0.value, color1.value,
        color2.value, color3.value, rb_depth_info.value,
        rb_colorcontrol.value, rb_color_mask.value & 0xFFFF);
    uint32_t copy_src = uint32_t(rb_copy_control.copy_src_select);
    if (copy_src < xenos::kMaxColorRenderTargets) {
      auto src_color = regs.Get<reg::RB_COLOR_INFO>(
          reg::RB_COLOR_INFO::rt_register_indices[copy_src]);
      uint32_t src_write_mask = (rb_color_mask.value >> (copy_src * 4)) & 0xF;
      XELOGI(
          "GPU copy trace: IssueCopy source color{} base_tiles={} bit11={} "
          "format={}({}) exp_bias={} write_mask={:X} "
          "approx_edram_byte_offset={:08X}",
          copy_src, src_color.color_base, src_color.color_base_bit_11,
          uint32_t(src_color.color_format),
          xenos::GetColorRenderTargetFormatName(src_color.color_format),
          src_color.color_exp_bias, src_write_mask,
          src_color.color_base * xenos::kEdramTileWidthSamples *
              xenos::kEdramTileHeightSamples * uint32_t(sizeof(uint32_t)));
    } else {
      XELOGI(
          "GPU copy trace: IssueCopy source depth-like src={} depth_base={} "
          "depth_base_bit11={} depth_format={} depth_info={:08X}",
          copy_src, rb_depth_info.depth_base, rb_depth_info.depth_base_bit_11,
          uint32_t(rb_depth_info.depth_format), rb_depth_info.value);
    }
  }

  if (!BeginSubmission(true)) {
    return false;
  }

  uint32_t written_address, written_length;
  bool resolved = render_target_cache_->Resolve(
      *memory_, *shared_memory_, *texture_cache_, written_address,
      written_length);
  if (trace_copy_state) {
    XELOGI(
        "GPU copy trace: IssueCopy resolve_result={} copy_seq={} "
        "last_draw_seq={} last_vs_hash={:016X} last_ps_hash={:016X} "
        "written={:08X}+{:08X}",
        resolved, copy_sequence, trace_last_draw_sequence_,
        trace_last_draw_vs_hash_, trace_last_draw_ps_hash_, written_address,
        written_length);
  }
  if (!resolved) {
    return false;
  }
  xenos::TextureFormat dest_format = xenos::TextureFormat::k_8_8_8_8;
  uint32_t dest_width = 0;
  uint32_t dest_pitch = 0;
  uint32_t dest_height = 0;
  if (written_length) {
    const RegisterFile& regs = *register_file_;
    auto rb_copy_dest_info = regs.Get<reg::RB_COPY_DEST_INFO>();
    auto rb_copy_dest_pitch = regs.Get<reg::RB_COPY_DEST_PITCH>();
    auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
    dest_format = static_cast<xenos::TextureFormat>(
        uint32_t(rb_copy_dest_info.copy_dest_format));
    dest_width = uint32_t(rb_surface_info.surface_pitch);
    dest_pitch = uint32_t(rb_copy_dest_pitch.copy_dest_pitch);
    if (!dest_width) {
      dest_width = dest_pitch;
    }
    dest_height = uint32_t(rb_copy_dest_pitch.copy_dest_height);
    constexpr uint32_t kMinDebugPresentWidth = 1280;
    constexpr uint32_t kMinDebugPresentHeight = 720;
    constexpr uint32_t kMinDebugPresentBytes =
        kMinDebugPresentWidth * kMinDebugPresentHeight * 4;
    bool candidate_is_fullscreen =
        dest_width >= kMinDebugPresentWidth &&
        dest_height >= kMinDebugPresentHeight &&
        written_length >= kMinDebugPresentBytes;
    if (candidate_is_fullscreen &&
        IsDebugPresentResolveCandidateFormat(dest_format)) {
      bool candidate_is_preferred =
          IsPreferredDebugPresentResolveCandidateFormat(dest_format);
      bool previous_is_preferred =
          IsPreferredDebugPresentResolveCandidateFormat(
              recent_present_resolve_candidate_.format);
      if (candidate_is_preferred || !previous_is_preferred ||
          !recent_present_resolve_candidate_.address) {
        recent_present_resolve_candidate_.address = written_address;
        recent_present_resolve_candidate_.length = written_length;
        recent_present_resolve_candidate_.width = dest_width;
        recent_present_resolve_candidate_.height = dest_height;
        recent_present_resolve_candidate_.pitch = dest_pitch;
        recent_present_resolve_candidate_.format = dest_format;
        recent_present_resolve_candidate_.sequence =
            ++recent_present_resolve_sequence_;
        if (cvars::vulkan_present_recent_resolve_on_swap &&
            trace_copy_state) {
          XELOGI(
              "GPU copy trace: recent present resolve candidate "
              "source={:08X}+{:08X} size={}x{} pitch={} format={} "
              "preferred={} sequence={}",
              written_address, written_length, dest_width, dest_height,
              dest_pitch, static_cast<uint32_t>(dest_format),
              candidate_is_preferred,
              recent_present_resolve_candidate_.sequence);
        }
      }
    }
  }

  bool trace_checksum = ShouldTraceVulkanResolveChecksum();
  bool scored_candidate = false;
  int32_t scored_budget = cvars::vulkan_present_scored_resolve_budget;
  int32_t required_scored_format =
      cvars::vulkan_present_scored_resolve_required_format;
  bool scored_format_allowed =
      required_scored_format < 0 ||
      int32_t(dest_format) == required_scored_format;
  if (cvars::vulkan_present_scored_resolve_on_swap && written_length &&
      IsDebugPresentResolveCandidateFormat(dest_format) &&
      scored_format_allowed &&
      dest_width >= uint32_t(std::max<int32_t>(
                        1, cvars::vulkan_present_scored_resolve_min_width)) &&
      dest_height >= uint32_t(std::max<int32_t>(
                         1, cvars::vulkan_present_scored_resolve_min_height)) &&
      (scored_budget < 0 ||
       scored_present_resolve_readback_count_ < scored_budget)) {
    scored_candidate = true;
    ++scored_present_resolve_readback_count_;
  }
  bool readback_resolve =
      cvars::vulkan_readback_resolve &&
      !texture_cache_->IsDrawResolutionScaled() && written_length;
  if ((trace_checksum || readback_resolve || scored_candidate) &&
      written_length) {
    SharedMemoryReadbackStats scored_stats;
    bool readback_ok = ReadbackSharedMemoryRange(
        written_address, written_length, "resolve",
        trace_checksum || scored_candidate, readback_resolve,
        scored_candidate ? &scored_stats : nullptr);
    if (scored_candidate && readback_ok) {
      constexpr uint32_t kScoredPresentResolveMinScore = 64;
      bool useful_scored_candidate =
          scored_stats.score >= kScoredPresentResolveMinScore &&
          scored_stats.varying_samples &&
          (!cvars::vulkan_present_scored_resolve_reject_clear_like ||
           !scored_stats.clear_like);
      if (!useful_scored_candidate &&
          scored_present_resolve_candidate_.address == written_address) {
        PresentResolveCandidate restored_candidate =
            scored_present_resolve_fallback_candidate_;
        if (cvars::gpu_trace_swap || cvars::vulkan_trace_copy_state) {
          XELOGI(
              "GPU copy trace: clearing stale scored present resolve "
              "candidate source={:08X}+{:08X} score={} nonzero={} "
              "varying={} first_sample={:08X} first_sample_matches={} "
              "low_variation={} clear_like={} checksum={:016X} "
              "restoring={:08X}",
              written_address, written_length, scored_stats.score,
              scored_stats.nonzero_samples, scored_stats.varying_samples,
              scored_stats.first_sample_value,
              scored_stats.first_sample_matches, scored_stats.low_variation,
              scored_stats.clear_like,
              scored_stats.checksum, restored_candidate.address);
        }
        scored_present_resolve_candidate_ = restored_candidate;
        scored_present_resolve_fallback_candidate_ = PresentResolveCandidate();
      }
      if (!useful_scored_candidate) {
        return true;
      }
      if (scored_present_resolve_candidate_.address &&
          scored_present_resolve_candidate_.address != written_address) {
        scored_present_resolve_fallback_candidate_ =
            scored_present_resolve_candidate_;
      }
      scored_present_resolve_candidate_.address = written_address;
      scored_present_resolve_candidate_.length = written_length;
      scored_present_resolve_candidate_.width = dest_width;
      scored_present_resolve_candidate_.height = dest_height;
      scored_present_resolve_candidate_.pitch = dest_pitch;
      scored_present_resolve_candidate_.format = dest_format;
      scored_present_resolve_candidate_.sequence =
          ++scored_present_resolve_sequence_;
      scored_present_resolve_candidate_.score = scored_stats.score;
      scored_present_resolve_candidate_.nonzero_samples =
          scored_stats.nonzero_samples;
      scored_present_resolve_candidate_.varying_samples =
          scored_stats.varying_samples;
      scored_present_resolve_candidate_.first_sample_value =
          scored_stats.first_sample_value;
      scored_present_resolve_candidate_.first_sample_matches =
          scored_stats.first_sample_matches;
      scored_present_resolve_candidate_.first_nonzero_value =
          scored_stats.first_nonzero_value;
      scored_present_resolve_candidate_.low_variation =
          scored_stats.low_variation;
      scored_present_resolve_candidate_.clear_like = scored_stats.clear_like;
      scored_present_resolve_candidate_.checksum = scored_stats.checksum;
      if (cvars::gpu_trace_swap || cvars::vulkan_trace_copy_state) {
        XELOGI(
            "GPU copy trace: scored present resolve candidate "
            "source={:08X}+{:08X} size={}x{} pitch={} format={} sequence={} "
            "score={} samples={} nonzero={} varying={} first_sample={:08X} "
            "first_sample_matches={} first_nonzero={:08X} low_variation={} "
            "clear_like={} checksum={:016X}",
            written_address, written_length, dest_width, dest_height,
            dest_pitch, static_cast<uint32_t>(dest_format),
            scored_present_resolve_candidate_.sequence, scored_stats.score,
            scored_stats.samples, scored_stats.nonzero_samples,
            scored_stats.varying_samples, scored_stats.first_sample_value,
            scored_stats.first_sample_matches, scored_stats.first_nonzero_value,
            scored_stats.low_variation, scored_stats.clear_like,
            scored_stats.checksum);
      }
    }
  }

  return true;
}

void VulkanCommandProcessor::InitializeTrace() {
  CommandProcessor::InitializeTrace();

  if (!BeginSubmission(true)) {
    return;
  }
  // TODO(Triang3l): Write the EDRAM.
  bool shared_memory_submitted =
      shared_memory_->InitializeTraceSubmitDownloads();
  if (!shared_memory_submitted) {
    return;
  }
  AwaitAllQueueOperationsCompletion();
  if (shared_memory_submitted) {
    shared_memory_->InitializeTraceCompleteDownloads();
  }
}

void VulkanCommandProcessor::CheckSubmissionCompletionAndDeviceLoss(
    uint64_t await_submission) {
  // Only report once, no need to retry a wait that won't succeed anyway.
  if (device_lost_) {
    return;
  }

  if (await_submission >= GetCurrentSubmission()) {
    if (submission_open_) {
      EndSubmission(false);
    }
    // A submission won't be ended if it hasn't been started, or if ending
    // has failed - clamp the index.
    await_submission = GetCurrentSubmission() - 1;
  }

  // fopen[fence_us=]: time the PURE fence await/poll apart from the
  // completion-side reclamation below (shared memory / primitive processor /
  // render target cache / texture cache CompletedSubmissionUpdated). The
  // frame-open wait_us minus this = reclamation CPU - splits "GPU genuinely
  // late" from "completion processing is expensive".
  if (cvars::vulkan_trace_draw_outcomes_per_frame) {
    const std::chrono::steady_clock::time_point fence_t0 =
        std::chrono::steady_clock::now();
    completion_timeline_.AwaitSubmissionAndUpdateCompleted(await_submission);
    completion_fence_await_ns_ +=
        uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::steady_clock::now() - fence_t0)
                     .count());
  } else {
    completion_timeline_.AwaitSubmissionAndUpdateCompleted(await_submission);
  }

  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();

  if (vulkan_device->IsLost()) {
    device_lost_ = true;
    graphics_system_->OnHostGpuLossFromAnyThread(true);
    return;
  }

  const uint64_t completed_submission = GetCompletedSubmission();

  // Reclaim semaphores.
  while (!submissions_in_flight_semaphores_.empty()) {
    const auto& semaphore_submission =
        submissions_in_flight_semaphores_.front();
    if (semaphore_submission.first > completed_submission) {
      break;
    }
    semaphores_free_.push_back(semaphore_submission.second);
    submissions_in_flight_semaphores_.pop_front();
  }

  // Reclaim command pools.
  while (!command_buffers_submitted_.empty()) {
    const auto& command_buffer_pair = command_buffers_submitted_.front();
    if (command_buffer_pair.first > completed_submission) {
      break;
    }
    command_buffers_writable_.push_back(command_buffer_pair.second);
    command_buffers_submitted_.pop_front();
  }

  shared_memory_->CompletedSubmissionUpdated();

  primitive_processor_->CompletedSubmissionUpdated();

  render_target_cache_->CompletedSubmissionUpdated();

  texture_cache_->CompletedSubmissionUpdated(completed_submission);

  // Destroy objects scheduled for destruction.
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  while (!destroy_framebuffers_.empty()) {
    const auto& destroy_pair = destroy_framebuffers_.front();
    if (destroy_pair.first > completed_submission) {
      break;
    }
    dfn.vkDestroyFramebuffer(device, destroy_pair.second, nullptr);
    destroy_framebuffers_.pop_front();
  }
  while (!destroy_buffers_.empty()) {
    const auto& destroy_pair = destroy_buffers_.front();
    if (destroy_pair.first > completed_submission) {
      break;
    }
    dfn.vkDestroyBuffer(device, destroy_pair.second, nullptr);
    destroy_buffers_.pop_front();
  }
  while (!destroy_memory_.empty()) {
    const auto& destroy_pair = destroy_memory_.front();
    if (destroy_pair.first > completed_submission) {
      break;
    }
    dfn.vkFreeMemory(device, destroy_pair.second, nullptr);
    destroy_memory_.pop_front();
  }
}

bool VulkanCommandProcessor::BeginSubmission(bool is_guest_command) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  if (device_lost_) {
    return false;
  }

  bool is_opening_frame = is_guest_command && !frame_open_;
  if (submission_open_ && !is_opening_frame) {
    return true;
  }

  // Check the fence - needed for all kinds of submissions (to reclaim transient
  // resources early) and specifically for frames (not to queue too many), and
  // await the availability of the current frame. Also check whether the device
  // is still available, and whether the await was successful.
  uint64_t await_submission =
      is_opening_frame
          ? closed_frame_submissions_[frame_current_ % kMaxFramesInFlight]
          : 0;
  // Frame-serialization probe (fopen[] in the outcomes line): time the ONE
  // frame-open await and record how many frames were not yet known-complete
  // when it started. inflight ~1 = the producer (guest swap pacing) is
  // serializing; inflight ~3 with a long wait = the await/bookkeeping is.
  const bool time_frame_open =
      is_opening_frame && cvars::vulkan_trace_draw_outcomes_per_frame;
  std::chrono::steady_clock::time_point frame_open_t0;
  if (time_frame_open) {
    draw_frame_open_in_flight_ = uint32_t(frame_current_ - frame_completed_);
    // Submission backlog before/after the await: post ~= pre - 1 means the GPU
    // is genuinely executing ~that many submissions behind (real GPU span per
    // frame is longer than gpu_frame_us measures); post collapsing to ~0 means
    // completions arrived in a BURST during the wait (the GPU was blocked on
    // something the CPU/present released - signaling/pacing, not execution).
    draw_frame_open_sub_pre_ =
        uint32_t(GetCurrentSubmission() - GetCompletedSubmission());
    draw_frame_open_await_idx_ = await_submission;
    draw_frame_open_upcoming_ = GetCurrentSubmission();
    draw_frame_open_completed_ = GetCompletedSubmission();
    frame_open_t0 = std::chrono::steady_clock::now();
  }
  CheckSubmissionCompletionAndDeviceLoss(await_submission);
  if (time_frame_open) {
    draw_cpu_frame_open_wait_ns_ =
        uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::steady_clock::now() - frame_open_t0)
                     .count());
    draw_frame_open_sub_post_ =
        uint32_t(GetCurrentSubmission() - GetCompletedSubmission());
  }
  const uint64_t completed_submission = GetCompletedSubmission();
  if (device_lost_ || completed_submission < await_submission) {
    return false;
  }

  if (is_opening_frame) {
    // Update the completed frame index, also obtaining the actual completed
    // frame number (since the CPU may be actually less than 3 frames behind)
    // before reclaiming resources tracked with the frame number.
    frame_completed_ = std::max(frame_current_, uint64_t(kMaxFramesInFlight)) -
                       kMaxFramesInFlight;
    for (uint64_t frame = frame_completed_ + 1; frame < frame_current_;
         ++frame) {
      if (closed_frame_submissions_[frame % kMaxFramesInFlight] >
          completed_submission) {
        break;
      }
      frame_completed_ = frame;
    }
  }

  if (!submission_open_) {
    submission_open_ = true;

    // Start a new deferred command buffer - will submit it to the real one in
    // the end of the submission (when async pipeline object creation requests
    // are fulfilled).
    deferred_command_buffer_.Reset();

    // Reset cached state of the command buffer.
    // BD input-attachment merge: invalidate the captured producer position (a
    // command_stream_ index) with the stream.
    feedback_producer_begin_pos_ = SIZE_MAX;
    feedback_merge_active_ = false;
    dynamic_viewport_update_needed_ = true;
    dynamic_scissor_update_needed_ = true;
    dynamic_depth_bias_update_needed_ = true;
    dynamic_blend_constants_update_needed_ = true;
    dynamic_stencil_compare_mask_front_update_needed_ = true;
    dynamic_stencil_compare_mask_back_update_needed_ = true;
    dynamic_stencil_write_mask_front_update_needed_ = true;
    dynamic_stencil_write_mask_back_update_needed_ = true;
    dynamic_stencil_reference_front_update_needed_ = true;
    dynamic_stencil_reference_back_update_needed_ = true;
    dynamic_cull_mode_update_needed_ = true;
    dynamic_front_face_update_needed_ = true;
    dynamic_primitive_topology_update_needed_ = true;
    dynamic_primitive_restart_enable_update_needed_ = true;
    dynamic_depth_test_enable_update_needed_ = true;
    dynamic_depth_write_enable_update_needed_ = true;
    dynamic_depth_compare_op_update_needed_ = true;
    dynamic_stencil_test_enable_update_needed_ = true;
    dynamic_stencil_op_update_needed_ = true;
    current_render_pass_ = VK_NULL_HANDLE;
    current_framebuffer_ = nullptr;
    current_guest_graphics_pipeline_ = VK_NULL_HANDLE;
    current_external_graphics_pipeline_ = VK_NULL_HANDLE;
    current_external_compute_pipeline_ = VK_NULL_HANDLE;
    current_guest_graphics_pipeline_layout_ = nullptr;
    current_graphics_descriptor_sets_bound_up_to_date_ = 0;
    // Lever 2: a pending draw-concatenation run never spans a submission. Reset
    // regardless of the cvar so a mid-run toggle cannot carry stale state. (A
    // run pending at submission end is flushed by the EndSubmission flush point
    // before teardown - added in a later increment - so this only clears state.)
    merge_pending_active_ = false;
    merge_pending_index_count_ = 0;

    primitive_processor_->BeginSubmission();

    texture_cache_->BeginSubmission(GetCurrentSubmission());

    // Double-buffer shared memory: perform the version switch + stale-range sync
    // copy here, at the submission boundary - no render pass is open (just reset
    // above) and the deferred command buffer is fresh, so recording the transfer
    // + barriers is legal, and it happens before any of this submission's
    // uploads/draws so they target the version the GPU will read. No-op unless
    // gpu_shared_memory_double_buffer is active.
    shared_memory_->BeginSubmission();
  }

  if (is_opening_frame) {
    frame_open_ = true;

    // Reset bindings that depend on transient data.
    std::memset(current_float_constant_map_vertex_, 0,
                sizeof(current_float_constant_map_vertex_));
    std::memset(current_float_constant_map_pixel_, 0,
                sizeof(current_float_constant_map_pixel_));
    std::memset(current_graphics_descriptor_sets_, 0,
                sizeof(current_graphics_descriptor_sets_));
    current_constant_buffers_up_to_date_ = 0;
    // gpu_shared_memory_double_buffer: bind the descriptor set for the version
    // the GPU currently reads. When off this is exactly
    // shared_memory_and_edram_descriptor_set_ (version 0).
    {
      const uint32_t shared_memory_version =
          shared_memory_->double_buffer_active()
              ? shared_memory_->current_version()
              : 0u;
      current_graphics_descriptor_sets_
          [SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram] =
              (shared_memory_version == 1 &&
               shared_memory_and_edram_descriptor_set_v1_ != VK_NULL_HANDLE)
                  ? shared_memory_and_edram_descriptor_set_v1_
                  : shared_memory_and_edram_descriptor_set_;
      shared_memory_descriptor_set_bound_version_ = shared_memory_version;
    }
    current_graphics_descriptor_set_values_up_to_date_ =
        UINT32_C(1)
        << SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram;
    // The transient texture/sampler descriptor sets are reclaimed across
    // submissions, so any previously written set the signature referred to is
    // gone - invalidate the reuse signature so the next draw rewrites.
    texture_descriptor_signature_vertex_valid_ = false;
    texture_descriptor_signature_pixel_valid_ = false;

    // Reclaim pool pages - no need to do this every small submission since some
    // may be reused.
    // FIXME(Triang3l): This will result in a memory leak if the guest is not
    // presenting.
    uniform_buffer_pool_->Reclaim(frame_completed_);
    if (indirect_buffer_pool_) {
      indirect_buffer_pool_->Reclaim(frame_completed_);
    }
    if (cull_index_buffer_pool_) {
      cull_index_buffer_pool_->Reclaim(frame_completed_);
    }
    // R2: rotate the constant arena rings to this frame's segment. frame_current_
    // advances every kMaxFramesInFlight (== the ring's kFramesInFlight) frames, so
    // a segment is only reused once its frame has completed - no in-flight stomp.
    // (current_constant_buffers_up_to_date_ was just cleared above, so every
    // constant re-allocates into the fresh segment this frame.)
    if (constants_dynamic_descriptor_set_ != VK_NULL_HANDLE) {
      for (auto& ring : dynamic_constants_rings_) {
        ring.FrameAdvance(frame_current_);
      }
    }
    // G1-lite: rotate the compact position ring to this frame's segment and
    // drop the intra-frame gather cache (its entries point into the previous
    // segment).
    if (compact_pos_ring_.is_valid()) {
      compact_pos_ring_.FrameAdvance(frame_current_);
      compact_pos_cache_.clear();
      compact_pos_cache_frame_ = frame_current_;
    }
    while (!single_transient_descriptors_used_.empty()) {
      const UsedSingleTransientDescriptor& used_transient_descriptor =
          single_transient_descriptors_used_.front();
      if (used_transient_descriptor.frame > frame_completed_) {
        break;
      }
      single_transient_descriptors_free_[size_t(
                                             used_transient_descriptor.layout)]
          .push_back(used_transient_descriptor.set);
      single_transient_descriptors_used_.pop_front();
    }
    while (!constants_transient_descriptors_used_.empty()) {
      const std::pair<uint64_t, VkDescriptorSet>& used_transient_descriptor =
          constants_transient_descriptors_used_.front();
      if (used_transient_descriptor.first > frame_completed_) {
        break;
      }
      constants_transient_descriptors_free_.push_back(
          used_transient_descriptor.second);
      constants_transient_descriptors_used_.pop_front();
    }
    while (!texture_transient_descriptor_sets_used_.empty()) {
      const UsedTextureTransientDescriptorSet& used_transient_descriptor_set =
          texture_transient_descriptor_sets_used_.front();
      if (used_transient_descriptor_set.frame > frame_completed_) {
        break;
      }
      auto it = texture_transient_descriptor_sets_free_.find(
          used_transient_descriptor_set.layout);
      if (it == texture_transient_descriptor_sets_free_.end()) {
        it =
            texture_transient_descriptor_sets_free_
                .emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(used_transient_descriptor_set.layout),
                    std::forward_as_tuple())
                .first;
      }
      it->second.push_back(used_transient_descriptor_set.set);
      texture_transient_descriptor_sets_used_.pop_front();
    }

    primitive_processor_->BeginFrame();

    texture_cache_->BeginFrame();
  }

  return true;
}

bool VulkanCommandProcessor::EndSubmission(bool is_swap) {
  ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // Make sure everything needed for submitting exist.
  if (submission_open_) {
    // Lever 2 (vulkan_merge_draws): a run must never span a submission - realize
    // it before any teardown (the EndRenderPass below also flushes, but flush
    // explicitly here in case any pre-teardown work depends on prior draws).
    FlushPendingMergeRun();
    if (!sparse_memory_binds_.empty() && semaphores_free_.empty()) {
      VkSemaphoreCreateInfo semaphore_create_info;
      semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
      semaphore_create_info.pNext = nullptr;
      semaphore_create_info.flags = 0;
      VkSemaphore semaphore;
      if (dfn.vkCreateSemaphore(device, &semaphore_create_info, nullptr,
                                &semaphore) != VK_SUCCESS) {
        XELOGE("Failed to create a Vulkan semaphore");
        return false;
      }
      semaphores_free_.push_back(semaphore);
    }
    if (command_buffers_writable_.empty()) {
      CommandBuffer command_buffer;
      VkCommandPoolCreateInfo command_pool_create_info;
      command_pool_create_info.sType =
          VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
      command_pool_create_info.pNext = nullptr;
      command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
      command_pool_create_info.queueFamilyIndex =
          vulkan_device->queue_family_graphics_compute();
      if (dfn.vkCreateCommandPool(device, &command_pool_create_info, nullptr,
                                  &command_buffer.pool) != VK_SUCCESS) {
        XELOGE("Failed to create a Vulkan command pool");
        return false;
      }
      VkCommandBufferAllocateInfo command_buffer_allocate_info;
      command_buffer_allocate_info.sType =
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      command_buffer_allocate_info.pNext = nullptr;
      command_buffer_allocate_info.commandPool = command_buffer.pool;
      command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      command_buffer_allocate_info.commandBufferCount = 1;
      if (dfn.vkAllocateCommandBuffers(device, &command_buffer_allocate_info,
                                       &command_buffer.buffer) != VK_SUCCESS) {
        XELOGE("Failed to allocate a Vulkan command buffer");
        dfn.vkDestroyCommandPool(device, command_buffer.pool, nullptr);
        return false;
      }
      command_buffers_writable_.push_back(command_buffer);
    }
  }

  bool is_closing_frame = is_swap && frame_open_;

  if (is_closing_frame) {
    primitive_processor_->EndFrame();
  }

  if (submission_open_) {
    assert_false(scratch_buffer_used_);

    EndRenderPass();

    render_target_cache_->EndSubmission();

    primitive_processor_->EndSubmission();

    shared_memory_->EndSubmission();

    uniform_buffer_pool_->FlushWrites();

    // Submit sparse binds earlier, before executing the deferred command
    // buffer, to reduce latency.
    if (!sparse_memory_binds_.empty()) {
      sparse_buffer_bind_infos_temp_.clear();
      sparse_buffer_bind_infos_temp_.reserve(sparse_buffer_binds_.size());
      for (const SparseBufferBind& sparse_buffer_bind : sparse_buffer_binds_) {
        VkSparseBufferMemoryBindInfo& sparse_buffer_bind_info =
            sparse_buffer_bind_infos_temp_.emplace_back();
        sparse_buffer_bind_info.buffer = sparse_buffer_bind.buffer;
        sparse_buffer_bind_info.bindCount = sparse_buffer_bind.bind_count;
        sparse_buffer_bind_info.pBinds =
            sparse_memory_binds_.data() + sparse_buffer_bind.bind_offset;
      }
      assert_false(semaphores_free_.empty());
      VkSemaphore bind_sparse_semaphore = semaphores_free_.back();
      VkBindSparseInfo bind_sparse_info;
      bind_sparse_info.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
      bind_sparse_info.pNext = nullptr;
      bind_sparse_info.waitSemaphoreCount = 0;
      bind_sparse_info.pWaitSemaphores = nullptr;
      bind_sparse_info.bufferBindCount =
          uint32_t(sparse_buffer_bind_infos_temp_.size());
      bind_sparse_info.pBufferBinds =
          !sparse_buffer_bind_infos_temp_.empty()
              ? sparse_buffer_bind_infos_temp_.data()
              : nullptr;
      bind_sparse_info.imageOpaqueBindCount = 0;
      bind_sparse_info.pImageOpaqueBinds = nullptr;
      bind_sparse_info.imageBindCount = 0;
      bind_sparse_info.pImageBinds = 0;
      bind_sparse_info.signalSemaphoreCount = 1;
      bind_sparse_info.pSignalSemaphores = &bind_sparse_semaphore;
      VkResult bind_sparse_result;
      {
        ui::vulkan::VulkanDevice::Queue::Acquisition queue_acquisition =
            vulkan_device->AcquireQueue(
                vulkan_device->queue_family_sparse_binding(), 0);
        bind_sparse_result = dfn.vkQueueBindSparse(
            queue_acquisition.queue(), 1, &bind_sparse_info, VK_NULL_HANDLE);
      }
      if (bind_sparse_result != VK_SUCCESS) {
        XELOGE("Failed to submit Vulkan sparse binds");
        return false;
      }
      current_submission_wait_semaphores_.push_back(bind_sparse_semaphore);
      semaphores_free_.pop_back();
      current_submission_wait_stage_masks_.push_back(
          sparse_bind_wait_stage_mask_);
      sparse_bind_wait_stage_mask_ = 0;
      sparse_buffer_binds_.clear();
      sparse_memory_binds_.clear();
    }

    SubmitBarriers(true);

    assert_false(command_buffers_writable_.empty());
    CommandBuffer command_buffer = command_buffers_writable_.back();
    if (dfn.vkResetCommandPool(device, command_buffer.pool, 0) != VK_SUCCESS) {
      XELOGE("Failed to reset a Vulkan command pool");
      return false;
    }
    VkCommandBufferBeginInfo command_buffer_begin_info;
    command_buffer_begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_buffer_begin_info.pNext = nullptr;
    command_buffer_begin_info.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    command_buffer_begin_info.pInheritanceInfo = nullptr;
    if (dfn.vkBeginCommandBuffer(command_buffer.buffer,
                                 &command_buffer_begin_info) != VK_SUCCESS) {
      XELOGE("Failed to begin a Vulkan command buffer");
      return false;
    }
    // GPU frame-time bracket: TOP-of-pipe timestamp before the frame's work,
    // BOTTOM-of-pipe after. Reset precedes the writes (required). Read back
    // deferred at swap. For >1 submission/frame the last submission's span wins
    // (Blue Dragon measured 1 submit/frame).
    uint32_t gpu_ts_base = 0;
    if (gpu_timestamp_pool_ != VK_NULL_HANDLE) {
      gpu_ts_base = 2u * uint32_t(frame_current_ % kMaxFramesInFlight);
      dfn.vkCmdResetQueryPool(command_buffer.buffer, gpu_timestamp_pool_,
                              gpu_ts_base, 2);
      dfn.vkCmdWriteTimestamp(command_buffer.buffer,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              gpu_timestamp_pool_, gpu_ts_base);
      gpu_timestamp_frame_written_[frame_current_ % kMaxFramesInFlight] =
          frame_current_;
    }
    // Route A: reset this frame's pass-timestamp range BEFORE Execute replays
    // the per-pass timestamp writes, and record how many bracket pairs were
    // recorded this frame for the deferred readback. (1 submission/frame on BD;
    // multi-submit frames keep only the last submission's spans.)
    if (cvars::vulkan_trace_pass_timestamps &&
        gpu_pass_timestamp_pool_ != VK_NULL_HANDLE) {
      uint32_t pass_slot = uint32_t(frame_current_ % kMaxFramesInFlight);
      dfn.vkCmdResetQueryPool(command_buffer.buffer, gpu_pass_timestamp_pool_,
                              pass_slot * 2u * kMaxPassBrackets,
                              2u * kMaxPassBrackets);
      gpu_pass_count_written_[pass_slot] = gpu_pass_bracket_count_;
      gpu_pass_bracket_count_ = 0;
    }
    // Fold this submission's recorded draw count into the host_draws=
    // accumulator before the next BeginSubmission resets the stat.
    host_draws_recorded_accum_ += deferred_command_buffer_.record_stats().draws;
    deferred_command_buffer_.Execute(command_buffer.buffer);
    if (gpu_timestamp_pool_ != VK_NULL_HANDLE) {
      dfn.vkCmdWriteTimestamp(command_buffer.buffer,
                              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                              gpu_timestamp_pool_, gpu_ts_base + 1);
    }
    if (dfn.vkEndCommandBuffer(command_buffer.buffer) != VK_SUCCESS) {
      XELOGE("Failed to end a Vulkan command buffer");
      return false;
    }

    const uint64_t submission_index = GetCurrentSubmission();

    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    if (!current_submission_wait_semaphores_.empty()) {
      submit_info.waitSemaphoreCount =
          uint32_t(current_submission_wait_semaphores_.size());
      submit_info.pWaitSemaphores = current_submission_wait_semaphores_.data();
      submit_info.pWaitDstStageMask =
          current_submission_wait_stage_masks_.data();
    }
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer.buffer;
    const uint64_t submit_start =
        ui::vulkan::VulkanPerfCountersEnabled()
            ? ui::vulkan::VulkanPerfCountersNow()
            : 0;
    const VkResult submit_result = completion_timeline_.AcquireFenceAndSubmit(
        vulkan_device->queue_family_graphics_compute(), 0, 1, &submit_info);
    ui::vulkan::VulkanPerfCountersRecordQueueSubmit(
        submit_start, 1, submit_info.commandBufferCount,
        submit_info.waitSemaphoreCount, int32_t(submit_result));
    if (submit_result != VK_SUCCESS) {
      XELOGE("Failed to submit a GPU emulation Vulkan command buffer: {}",
             vk::to_string(vk::Result(submit_result)));
      if (vulkan_device->IsLost() && !device_lost_) {
        device_lost_ = true;
        graphics_system_->OnHostGpuLossFromAnyThread(true);
      }
      return false;
    }
    current_submission_wait_stage_masks_.clear();
    for (VkSemaphore semaphore : current_submission_wait_semaphores_) {
      submissions_in_flight_semaphores_.emplace_back(submission_index,
                                                     semaphore);
    }
    current_submission_wait_semaphores_.clear();
    command_buffers_submitted_.emplace_back(submission_index, command_buffer);
    command_buffers_writable_.pop_back();

    submission_open_ = false;
  }

  if (is_closing_frame) {
    if (cvars::clear_memory_page_state) {
      shared_memory_->SetSystemPageBlocksValidWithGpuDataWritten();
    }

    frame_open_ = false;
    // Submission already closed now, so minus 1.
    closed_frame_submissions_[(frame_current_++) % kMaxFramesInFlight] =
        GetCurrentSubmission() - 1;

    if (cache_clear_requested_ && AwaitAllQueueOperationsCompletion()) {
      cache_clear_requested_ = false;

      DestroyScratchBuffer();

      for (SwapFramebuffer& swap_framebuffer : swap_framebuffers_) {
        ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyFramebuffer, device,
                                               swap_framebuffer.framebuffer);
      }

      assert_true(command_buffers_submitted_.empty());
      for (const CommandBuffer& command_buffer : command_buffers_writable_) {
        dfn.vkDestroyCommandPool(device, command_buffer.pool, nullptr);
      }
      command_buffers_writable_.clear();

      ClearTransientDescriptorPools();

      uniform_buffer_pool_->ClearCache();

      texture_cache_->ClearCache();

      render_target_cache_->ClearCache();

      // Not clearing the pipeline layouts and the descriptor set layouts as
      // they're referenced by pipelines, which are not destroyed.

      primitive_processor_->ClearCache();

      shared_memory_->ClearCache();
    }
  }

  return true;
}

void VulkanCommandProcessor::ClearTransientDescriptorPools() {
  texture_transient_descriptor_sets_free_.clear();
  texture_transient_descriptor_sets_used_.clear();
  transient_descriptor_allocator_textures_.Reset();

  constants_transient_descriptors_free_.clear();
  constants_transient_descriptors_used_.clear();
  for (std::vector<VkDescriptorSet>& transient_descriptors_free :
       single_transient_descriptors_free_) {
    transient_descriptors_free.clear();
  }
  single_transient_descriptors_used_.clear();
  transient_descriptor_allocator_storage_buffer_.Reset();
  transient_descriptor_allocator_uniform_buffer_.Reset();
  transient_descriptor_allocator_constants_mixed_.Reset();
}

void VulkanCommandProcessor::SplitPendingBarrier() {
  size_t pending_buffer_memory_barrier_count =
      pending_barriers_buffer_memory_barriers_.size();
  size_t pending_image_memory_barrier_count =
      pending_barriers_image_memory_barriers_.size();
  if (!current_pending_barrier_.src_stage_mask &&
      !current_pending_barrier_.dst_stage_mask &&
      current_pending_barrier_.buffer_memory_barriers_offset >=
          pending_buffer_memory_barrier_count &&
      current_pending_barrier_.image_memory_barriers_offset >=
          pending_image_memory_barrier_count) {
    return;
  }
  pending_barriers_.emplace_back(current_pending_barrier_);
  current_pending_barrier_.src_stage_mask = 0;
  current_pending_barrier_.dst_stage_mask = 0;
  current_pending_barrier_.buffer_memory_barriers_offset =
      pending_buffer_memory_barrier_count;
  current_pending_barrier_.image_memory_barriers_offset =
      pending_image_memory_barrier_count;
}

void VulkanCommandProcessor::DestroyScratchBuffer() {
  assert_false(scratch_buffer_used_);

  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  scratch_buffer_last_usage_submission_ = 0;
  scratch_buffer_last_access_mask_ = 0;
  scratch_buffer_last_stage_mask_ = 0;
  scratch_buffer_size_ = 0;
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                         scratch_buffer_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                         scratch_buffer_memory_);
}

void VulkanCommandProcessor::FlushPendingMergeRun() {
  // Lever 2b (vulkan_merge_draws_indirect) is mutually exclusive with Lever 2
  // concatenation, so at most one run is ever active; flushing the MDI run here
  // means every concatenation flush point also bounds an MDI run.
  if (merge_mdi_active_) {
    FlushPendingMergeRunIndirect();
  }
  // Lever 2 (vulkan_merge_draws): close the accumulated draw-concatenation
  // run. With head-emit, the run's single CmdVkBindIndexBuffer +
  // CmdVkDrawIndexed were recorded when the run STARTED (in the correct
  // position relative to the head draw's own state), and extensions patched
  // the recorded index count in place - so nothing is emitted here; the run
  // just stops being extendable. (The old flush-time emission recorded the
  // run draw AFTER subsequent draws' state setup, drawing the run with the
  // wrong state.)
  merge_pending_active_ = false;
  merge_pending_index_count_ = 0;
  merge_pending_rewrite_mapping_ = nullptr;
  merge_pending_rewrite_used_bytes_ = 0;
}

void VulkanCommandProcessor::FlushPendingMergeRunIndirect() {
  // Lever 2b (vulkan_merge_draws_indirect): close the active MDI run. With
  // head-emit, the run's single vkCmdDrawIndexedIndirectCount was already recorded
  // at the run head, and continuation draws filled slots of its retained command
  // array + bumped the live count buffer directly - so there is nothing to emit
  // here; just stop the run so the next mergeable draw opens a fresh head. Called
  // from every merge flush point (FlushPendingMergeRun) and before any standalone
  // draw.
  merge_mdi_active_ = false;
  merge_mdi_mapping_ = nullptr;
  merge_mdi_count_ptr_ = nullptr;
  merge_mdi_count_ = 0;
}

uint32_t VulkanCommandProcessor::CountCullableTriangles(
    const Shader& vertex_shader) {
  // Front B (gpu_trace_cullable_tris), READ-ONLY decision instrument. Replays
  // the guest VS positions on the (idle) CPU to size a potential triangle cull,
  // which would reduce the GPU binning front-end cost - the proven heavy-scene
  // bottleneck. NEVER mutates the index buffer; only counts. The geometry-
  // altering cull itself stays held for a device A/B. The estimator is
  // constructed lazily, so the default (cvar-off) path pays nothing.
  if (!cull_extent_estimator_) {
    cull_extent_estimator_ = std::make_unique<DrawExtentEstimator>(
        *register_file_, *memory_, nullptr);
  }
  return cull_extent_estimator_->CountCullableTriangles(vertex_shader);
}

void VulkanCommandProcessor::EmitOpaquePrepassDraw(VkBuffer index_buffer,
                                                   VkDeviceSize index_offset,
                                                   VkIndexType index_type,
                                                   uint32_t index_count) {
  // Opaque depth pre-pass (gpu_opaque_depth_prepass), Unit 3: emit a SELF-
  // CONTAINED copy of the current opaque draw into prepass_command_buffer_,
  // which EndRenderPass splices to the FRONT of the render pass. Reuses the
  // EXACT current pipeline (color-write baked in - this is a reorder, not a
  // depth-only pass) + the dynamic-state member values already populated for
  // this draw by UpdateDynamicState. v1 is gated by the caller to EDS-off +
  // host-render-target path, so depth/cull/topology/stencil are baked into the
  // pipeline (correct for the reused pipeline) and only the always-dynamic
  // viewport/scissor (+ host-RT depth-bias/blend/stencil masks) need re-emitting.
  DeferredCommandBuffer& pb = prepass_command_buffer_;
  if (current_guest_graphics_pipeline_ == VK_NULL_HANDLE ||
      current_guest_graphics_pipeline_layout_ == nullptr) {
    return;
  }
  pb.CmdVkBindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                       current_guest_graphics_pipeline_);
  // All descriptor sets the layout expects, with the constants set's per-draw
  // dynamic offsets (the constants set is the ONLY set with dynamic descriptors).
  const bool constants_present =
      constants_dynamic_descriptor_set_ != VK_NULL_HANDLE;
  pb.CmdVkBindDescriptorSets(
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      current_guest_graphics_pipeline_layout_->GetPipelineLayout(), 0,
      uint32_t(SpirvShaderTranslator::kDescriptorSetCount),
      current_graphics_descriptor_sets_,
      constants_present ? uint32_t(SpirvShaderTranslator::kConstantBufferCount)
                        : 0,
      constants_present ? current_constant_dynamic_offsets_ : nullptr);
  // Always-dynamic state (values already current from this draw's
  // UpdateDynamicState). Viewport/scissor are always dynamic; depth-bias /
  // blend-constants / stencil masks+refs are dynamic on the host-RT path.
  pb.CmdVkSetViewport(0, 1, &dynamic_viewport_);
  pb.CmdVkSetScissor(0, 1, &dynamic_scissor_);
  if (render_target_cache_->GetPath() ==
      RenderTargetCache::Path::kHostRenderTargets) {
    pb.CmdVkSetDepthBias(dynamic_depth_bias_constant_factor_, 0.0f,
                         dynamic_depth_bias_slope_factor_);
    pb.CmdVkSetBlendConstants(dynamic_blend_constants_);
    pb.CmdVkSetStencilCompareMask(VK_STENCIL_FACE_FRONT_BIT,
                                  dynamic_stencil_compare_mask_front_);
    pb.CmdVkSetStencilCompareMask(VK_STENCIL_FACE_BACK_BIT,
                                  dynamic_stencil_compare_mask_back_);
    pb.CmdVkSetStencilWriteMask(VK_STENCIL_FACE_FRONT_BIT,
                                dynamic_stencil_write_mask_front_);
    pb.CmdVkSetStencilWriteMask(VK_STENCIL_FACE_BACK_BIT,
                                dynamic_stencil_write_mask_back_);
    pb.CmdVkSetStencilReference(VK_STENCIL_FACE_FRONT_BIT,
                                dynamic_stencil_reference_front_);
    pb.CmdVkSetStencilReference(VK_STENCIL_FACE_BACK_BIT,
                                dynamic_stencil_reference_back_);
  }
  pb.CmdVkBindIndexBuffer(index_buffer, index_offset, index_type);
  pb.CmdVkDrawIndexed(index_count, 1, 0, 0, 0);
}

void VulkanCommandProcessor::UpdateDynamicState(
    const draw_util::ViewportInfo& viewport_info, bool primitive_polygonal,
    reg::RB_DEPTHCONTROL normalized_depth_control,
    xenos::PrimitiveType host_primitive_type, bool host_primitive_reset_enabled,
    uint32_t normalized_color_mask) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  const RegisterFile& regs = *register_file_;

  // Window parameters.
  // http://ftp.tku.edu.tw/NetBSD/NetBSD-current/xsrc/external/mit/xf86-video-ati/dist/src/r600_reg_auto_r6xx.h
  // See r200UpdateWindow:
  // https://github.com/freedreno/mesa/blob/master/src/mesa/drivers/dri/r200/r200_state.c
  auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();

  // Viewport.
  VkViewport viewport;
  if (viewport_info.xy_extent[0] && viewport_info.xy_extent[1]) {
    viewport.x = float(viewport_info.xy_offset[0]);
    viewport.y = float(viewport_info.xy_offset[1]);
    viewport.width = float(viewport_info.xy_extent[0]);
    viewport.height = float(viewport_info.xy_extent[1]);
  } else {
    // Vulkan viewport width must be greater than 0.0f, but the Xenia  viewport
    // may be empty for various reasons - set the viewport to outside the
    // framebuffer.
    viewport.x = -1.0f;
    viewport.y = -1.0f;
    viewport.width = 1.0f;
    viewport.height = 1.0f;
  }
  viewport.minDepth = viewport_info.z_min;
  viewport.maxDepth = viewport_info.z_max;
  // gpu_resolution_downscale_pct: scale the draw viewport (fewer rasterized
  // fragments, same geometry). gpu_diag_raster_ab: RIGOROUS fill-vs-geometry
  // isolation - when the alternator (gpu_freeze_ab_alternate_vrs) is active,
  // apply the viewport shrink ONLY in the phase-off blocks, so one run measures
  // the RASTER/FILL cost on the SAME scene (viewport scale keeps vertex/geometry
  // work identical, only fragment coverage changes). A frame-time delta => the
  // scene is fill/raster-bound; flat => geometry/vertex-bound. Definitive.
  bool apply_raster_scale = cvars::gpu_resolution_downscale_pct > 0 &&
                            cvars::gpu_resolution_downscale_pct < 100;
  if (apply_raster_scale && cvars::gpu_diag_raster_ab && gpu_ab_alt_active_) {
    apply_raster_scale = !gpu_freeze_vrs_phase_on_;
  }
  if (apply_raster_scale) {
    float f = float(cvars::gpu_resolution_downscale_pct) / 100.0f;
    viewport.x *= f;
    viewport.y *= f;
    viewport.width *= f;
    viewport.height *= f;
  }
  SetViewport(viewport);

  // Scissor.
  draw_util::Scissor scissor;
  draw_util::GetScissor(regs, scissor);
  VkRect2D scissor_rect;
  scissor_rect.offset.x = int32_t(scissor.offset[0]);
  scissor_rect.offset.y = int32_t(scissor.offset[1]);
  scissor_rect.extent.width = scissor.extent[0];
  scissor_rect.extent.height = scissor.extent[1];
  if (apply_raster_scale) {
    uint32_t pct = uint32_t(cvars::gpu_resolution_downscale_pct);
    scissor_rect.offset.x = scissor_rect.offset.x * int32_t(pct) / 100;
    scissor_rect.offset.y = scissor_rect.offset.y * int32_t(pct) / 100;
    scissor_rect.extent.width = scissor_rect.extent.width * pct / 100u;
    scissor_rect.extent.height = scissor_rect.extent.height * pct / 100u;
  }
  // gpu_flatten_predicated_tiling stage 2: during the frame's FIRST bin pass
  // (the one the flatten force-passes all predicated draws into), the guest's
  // scissor covers only tile 1's rect - widen it so the single surviving pass
  // rasterizes the whole surface. Vulkan clips the scissor to the framebuffer,
  // so a huge extent is safe. Only while a tiled sequence is active (a
  // SET_BIN_SELECT was seen this frame); passes before/after tiling keep the
  // guest scissor.
  if (cvars::gpu_flatten_predicated_tiling &&
      cvars::gpu_flatten_predicated_tiling_widen &&
      flatten_bin_passes_seen_ == 2) {
    scissor_rect.offset.x = 0;
    scissor_rect.offset.y = 0;
    scissor_rect.extent.width = 16384;
    scissor_rect.extent.height = 16384;
  }
  SetScissor(scissor_rect);

  if (render_target_cache_->GetPath() ==
      RenderTargetCache::Path::kHostRenderTargets) {
    // Depth bias.
    float depth_bias_constant_factor, depth_bias_slope_factor;
    draw_util::GetPreferredFacePolygonOffset(regs, primitive_polygonal,
                                             depth_bias_slope_factor,
                                             depth_bias_constant_factor);
    depth_bias_constant_factor *=
        regs.Get<reg::RB_DEPTH_INFO>().depth_format ==
                xenos::DepthRenderTargetFormat::kD24S8
            ? draw_util::kD3D10PolygonOffsetFactorUnorm24
            : draw_util::kD3D10PolygonOffsetFactorFloat24;
    // With non-square resolution scaling, make sure the worst-case impact is
    // reverted (slope only along the scaled axis), thus max. More bias is
    // better than less bias, because less bias means Z fighting with the
    // background is more likely.
    depth_bias_slope_factor *=
        xenos::kPolygonOffsetScaleSubpixelUnit *
        float(std::max(render_target_cache_->draw_resolution_scale_x(),
                       render_target_cache_->draw_resolution_scale_y()));
    // std::memcmp instead of != so in case of NaN, every draw won't be
    // invalidating it.
    dynamic_depth_bias_update_needed_ |=
        std::memcmp(&dynamic_depth_bias_constant_factor_,
                    &depth_bias_constant_factor, sizeof(float)) != 0;
    dynamic_depth_bias_update_needed_ |=
        std::memcmp(&dynamic_depth_bias_slope_factor_, &depth_bias_slope_factor,
                    sizeof(float)) != 0;
    if (dynamic_depth_bias_update_needed_) {
      dynamic_depth_bias_constant_factor_ = depth_bias_constant_factor;
      dynamic_depth_bias_slope_factor_ = depth_bias_slope_factor;
      deferred_command_buffer_.CmdVkSetDepthBias(
          dynamic_depth_bias_constant_factor_, 0.0f,
          dynamic_depth_bias_slope_factor_);
      dynamic_depth_bias_update_needed_ = false;
    }

    // Blend constants.
    float blend_constants[] = {
        regs.Get<float>(XE_GPU_REG_RB_BLEND_RED),
        regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN),
        regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE),
        regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA),
    };
    dynamic_blend_constants_update_needed_ |=
        std::memcmp(dynamic_blend_constants_, blend_constants,
                    sizeof(float) * 4) != 0;
    if (dynamic_blend_constants_update_needed_) {
      std::memcpy(dynamic_blend_constants_, blend_constants, sizeof(float) * 4);
      deferred_command_buffer_.CmdVkSetBlendConstants(dynamic_blend_constants_);
      dynamic_blend_constants_update_needed_ = false;
    }

    // Stencil masks and references.
    // Due to pretty complex conditions involving registers not directly related
    // to stencil (primitive type, culling), changing the values only when
    // stencil is actually needed. However, due to the way dynamic state needs
    // to be set in Vulkan, which doesn't take into account whether the state
    // actually has effect on drawing, and because the masks and the references
    // are always dynamic in Xenia guest pipelines, they must be set in the
    // command buffer before any draw.
    if (normalized_depth_control.stencil_enable) {
      Register stencil_ref_mask_front_reg, stencil_ref_mask_back_reg;
      if (primitive_polygonal && normalized_depth_control.backface_enable) {
        if (GetVulkanDevice()->properties().separateStencilMaskRef) {
          stencil_ref_mask_front_reg = XE_GPU_REG_RB_STENCILREFMASK;
          stencil_ref_mask_back_reg = XE_GPU_REG_RB_STENCILREFMASK_BF;
        } else {
          // Choose the back face values only if drawing only back faces.
          stencil_ref_mask_front_reg =
              regs.Get<reg::PA_SU_SC_MODE_CNTL>().cull_front
                  ? XE_GPU_REG_RB_STENCILREFMASK_BF
                  : XE_GPU_REG_RB_STENCILREFMASK;
          stencil_ref_mask_back_reg = stencil_ref_mask_front_reg;
        }
      } else {
        stencil_ref_mask_front_reg = XE_GPU_REG_RB_STENCILREFMASK;
        stencil_ref_mask_back_reg = XE_GPU_REG_RB_STENCILREFMASK;
      }
      auto stencil_ref_mask_front =
          regs.Get<reg::RB_STENCILREFMASK>(stencil_ref_mask_front_reg);
      auto stencil_ref_mask_back =
          regs.Get<reg::RB_STENCILREFMASK>(stencil_ref_mask_back_reg);
      // Compare mask.
      dynamic_stencil_compare_mask_front_update_needed_ |=
          dynamic_stencil_compare_mask_front_ !=
          stencil_ref_mask_front.stencilmask;
      dynamic_stencil_compare_mask_front_ = stencil_ref_mask_front.stencilmask;
      dynamic_stencil_compare_mask_back_update_needed_ |=
          dynamic_stencil_compare_mask_back_ !=
          stencil_ref_mask_back.stencilmask;
      dynamic_stencil_compare_mask_back_ = stencil_ref_mask_back.stencilmask;
      // Write mask.
      dynamic_stencil_write_mask_front_update_needed_ |=
          dynamic_stencil_write_mask_front_ !=
          stencil_ref_mask_front.stencilwritemask;
      dynamic_stencil_write_mask_front_ =
          stencil_ref_mask_front.stencilwritemask;
      dynamic_stencil_write_mask_back_update_needed_ |=
          dynamic_stencil_write_mask_back_ !=
          stencil_ref_mask_back.stencilwritemask;
      dynamic_stencil_write_mask_back_ = stencil_ref_mask_back.stencilwritemask;
      // Reference.
      dynamic_stencil_reference_front_update_needed_ |=
          dynamic_stencil_reference_front_ != stencil_ref_mask_front.stencilref;
      dynamic_stencil_reference_front_ = stencil_ref_mask_front.stencilref;
      dynamic_stencil_reference_back_update_needed_ |=
          dynamic_stencil_reference_back_ != stencil_ref_mask_back.stencilref;
      dynamic_stencil_reference_back_ = stencil_ref_mask_back.stencilref;
    }
    // Using VK_STENCIL_FACE_FRONT_AND_BACK for higher safety when running on
    // the Vulkan portability subset without separateStencilMaskRef.
    if (dynamic_stencil_compare_mask_front_update_needed_ ||
        dynamic_stencil_compare_mask_back_update_needed_) {
      if (dynamic_stencil_compare_mask_front_ ==
          dynamic_stencil_compare_mask_back_) {
        deferred_command_buffer_.CmdVkSetStencilCompareMask(
            VK_STENCIL_FACE_FRONT_AND_BACK,
            dynamic_stencil_compare_mask_front_);
      } else {
        if (dynamic_stencil_compare_mask_front_update_needed_) {
          deferred_command_buffer_.CmdVkSetStencilCompareMask(
              VK_STENCIL_FACE_FRONT_BIT, dynamic_stencil_compare_mask_front_);
        }
        if (dynamic_stencil_compare_mask_back_update_needed_) {
          deferred_command_buffer_.CmdVkSetStencilCompareMask(
              VK_STENCIL_FACE_BACK_BIT, dynamic_stencil_compare_mask_back_);
        }
      }
      dynamic_stencil_compare_mask_front_update_needed_ = false;
      dynamic_stencil_compare_mask_back_update_needed_ = false;
    }
    if (dynamic_stencil_write_mask_front_update_needed_ ||
        dynamic_stencil_write_mask_back_update_needed_) {
      if (dynamic_stencil_write_mask_front_ ==
          dynamic_stencil_write_mask_back_) {
        deferred_command_buffer_.CmdVkSetStencilWriteMask(
            VK_STENCIL_FACE_FRONT_AND_BACK, dynamic_stencil_write_mask_front_);
      } else {
        if (dynamic_stencil_write_mask_front_update_needed_) {
          deferred_command_buffer_.CmdVkSetStencilWriteMask(
              VK_STENCIL_FACE_FRONT_BIT, dynamic_stencil_write_mask_front_);
        }
        if (dynamic_stencil_write_mask_back_update_needed_) {
          deferred_command_buffer_.CmdVkSetStencilWriteMask(
              VK_STENCIL_FACE_BACK_BIT, dynamic_stencil_write_mask_back_);
        }
      }
      dynamic_stencil_write_mask_front_update_needed_ = false;
      dynamic_stencil_write_mask_back_update_needed_ = false;
    }
    if (dynamic_stencil_reference_front_update_needed_ ||
        dynamic_stencil_reference_back_update_needed_) {
      if (dynamic_stencil_reference_front_ == dynamic_stencil_reference_back_) {
        deferred_command_buffer_.CmdVkSetStencilReference(
            VK_STENCIL_FACE_FRONT_AND_BACK, dynamic_stencil_reference_front_);
      } else {
        if (dynamic_stencil_reference_front_update_needed_) {
          deferred_command_buffer_.CmdVkSetStencilReference(
              VK_STENCIL_FACE_FRONT_BIT, dynamic_stencil_reference_front_);
        }
        if (dynamic_stencil_reference_back_update_needed_) {
          deferred_command_buffer_.CmdVkSetStencilReference(
              VK_STENCIL_FACE_BACK_BIT, dynamic_stencil_reference_back_);
        }
      }
      dynamic_stencil_reference_front_update_needed_ = false;
      dynamic_stencil_reference_back_update_needed_ = false;
    }
  }

  // EDS (Lever 1b): cull mode + front face promoted to dynamic state. Reproduce
  // EXACTLY what VulkanPipelineCache::GetCurrentStateDescription baked into the
  // pipeline key (which is zeroed out there when promoted): cull/front-face only
  // apply to polygonal primitives; for non-polygonal draws the key keeps NONE /
  // counter-clockwise. The cvar short-circuits first, so this is zero-cost (no
  // device query, no emission) on the default path.
  if (cvars::vulkan_dynamic_state_cull_front &&
      GetVulkanDevice()->properties().apiVersion >=
          VK_MAKE_API_VERSION(0, 1, 3, 0)) {
    VkCullModeFlags cull_mode = VK_CULL_MODE_NONE;
    VkFrontFace front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    if (primitive_polygonal) {
      auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
      if (pa_su_sc_mode_cntl.cull_front) {
        cull_mode |= VK_CULL_MODE_FRONT_BIT;
      }
      if (pa_su_sc_mode_cntl.cull_back) {
        cull_mode |= VK_CULL_MODE_BACK_BIT;
      }
      front_face = pa_su_sc_mode_cntl.face != 0
                       ? VK_FRONT_FACE_CLOCKWISE
                       : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
    if (dynamic_cull_mode_update_needed_ || cull_mode != dynamic_cull_mode_) {
      deferred_command_buffer_.CmdVkSetCullMode(cull_mode);
      dynamic_cull_mode_ = cull_mode;
      dynamic_cull_mode_update_needed_ = false;
    }
    if (dynamic_front_face_update_needed_ ||
        front_face != dynamic_front_face_) {
      deferred_command_buffer_.CmdVkSetFrontFace(front_face);
      dynamic_front_face_ = front_face;
      dynamic_front_face_update_needed_ = false;
    }
  }

  // EDS topology (Lever 1): emit the real primitive topology for promoted
  // triangle LIST/STRIP draws. MUST match the dynamic-state array + key
  // normalization in the pipeline cache - only host triangle list/strip get
  // VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY (non-GS, triangle class); everything
  // else keeps a static topology and must NOT be emitted here.
  if ((cvars::vulkan_dynamic_state_topology || cvars::gpu_cull_compaction) &&
      GetVulkanDevice()->properties().apiVersion >=
          VK_MAKE_API_VERSION(0, 1, 3, 0) &&
      (host_primitive_type == xenos::PrimitiveType::kTriangleList ||
       host_primitive_type == xenos::PrimitiveType::kTriangleStrip)) {
    VkPrimitiveTopology topology =
        host_primitive_type == xenos::PrimitiveType::kTriangleStrip
            ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
            : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    if (dynamic_primitive_topology_update_needed_ ||
        topology != dynamic_primitive_topology_) {
      deferred_command_buffer_.CmdVkSetPrimitiveTopology(topology);
      dynamic_primitive_topology_ = topology;
      dynamic_primitive_topology_update_needed_ = false;
    }
    // primitive_restart is promoted together with topology (the key zeroes it
    // when normalizing strip->list, so it MUST be applied dynamically here).
    VkBool32 restart_enable =
        host_primitive_reset_enabled ? VK_TRUE : VK_FALSE;
    if (dynamic_primitive_restart_enable_update_needed_ ||
        restart_enable != dynamic_primitive_restart_enable_) {
      deferred_command_buffer_.CmdVkSetPrimitiveRestartEnable(restart_enable);
      dynamic_primitive_restart_enable_ = restart_enable;
      dynamic_primitive_restart_enable_update_needed_ = false;
    }
  }

  // EDS depth (Lever 1): depth test enable + write enable + compare op promoted
  // to dynamic state. Reproduce EXACTLY what GetCurrentStateDescription baked +
  // EnsurePipelineCreated derived: depthTestEnable = (write || compare!=ALWAYS),
  // depthWriteEnable = write, depthCompareOp = NEVER + compare. Only the host-
  // render-target path uses depth/stencil state (gated like the dynamic-state
  // array). normalized_depth_control is the same source the key used, so the
  // values match. The cvar short-circuits first (zero-cost on the default path).
  if (cvars::vulkan_dynamic_state_depth &&
      GetVulkanDevice()->properties().apiVersion >=
          VK_MAKE_API_VERSION(0, 1, 3, 0) &&
      render_target_cache_->GetPath() !=
          RenderTargetCache::Path::kPixelShaderInterlock &&
      !hybrid_current_draw_composite_) {
    uint32_t key_depth_write = 0;
    xenos::CompareFunction key_compare = xenos::CompareFunction::kAlways;
    if (normalized_depth_control.z_enable) {
      key_depth_write = normalized_depth_control.z_write_enable;
      key_compare = normalized_depth_control.zfunc;
    }
    VkBool32 depth_test_enable = VK_FALSE;
    VkBool32 depth_write_enable = VK_FALSE;
    VkCompareOp depth_compare_op = VK_COMPARE_OP_NEVER;
    if (key_depth_write || key_compare != xenos::CompareFunction::kAlways) {
      depth_test_enable = VK_TRUE;
      depth_write_enable = key_depth_write ? VK_TRUE : VK_FALSE;
      depth_compare_op = VkCompareOp(uint32_t(VK_COMPARE_OP_NEVER) +
                                     uint32_t(key_compare));
    }
    if (dynamic_depth_test_enable_update_needed_ ||
        depth_test_enable != dynamic_depth_test_enable_) {
      deferred_command_buffer_.CmdVkSetDepthTestEnable(depth_test_enable);
      dynamic_depth_test_enable_ = depth_test_enable;
      dynamic_depth_test_enable_update_needed_ = false;
    }
    if (dynamic_depth_write_enable_update_needed_ ||
        depth_write_enable != dynamic_depth_write_enable_) {
      deferred_command_buffer_.CmdVkSetDepthWriteEnable(depth_write_enable);
      dynamic_depth_write_enable_ = depth_write_enable;
      dynamic_depth_write_enable_update_needed_ = false;
    }
    if (dynamic_depth_compare_op_update_needed_ ||
        depth_compare_op != dynamic_depth_compare_op_) {
      deferred_command_buffer_.CmdVkSetDepthCompareOp(depth_compare_op);
      dynamic_depth_compare_op_ = depth_compare_op;
      dynamic_depth_compare_op_update_needed_ = false;
    }
  }

  // EDS stencil (Lever 1): stencil test enable + ops promoted to dynamic state.
  // Reproduce EXACTLY what GetCurrentStateDescription baked (incl. the back=front
  // fallback when !backface_enable) + EnsurePipelineCreated mapped (KEEP + op,
  // NEVER + compare), from the same normalized_depth_control. Gated like depth
  // (host-render-target path only). cvar short-circuits first (zero-cost off).
  if (cvars::vulkan_dynamic_state_stencil &&
      GetVulkanDevice()->properties().apiVersion >=
          VK_MAKE_API_VERSION(0, 1, 3, 0) &&
      render_target_cache_->GetPath() !=
          RenderTargetCache::Path::kPixelShaderInterlock &&
      !hybrid_current_draw_composite_) {
    VkBool32 stencil_test_enable =
        normalized_depth_control.stencil_enable ? VK_TRUE : VK_FALSE;
    VkStencilOp front_fail = VK_STENCIL_OP_KEEP, front_pass = VK_STENCIL_OP_KEEP,
                front_depth_fail = VK_STENCIL_OP_KEEP;
    VkCompareOp front_compare = VK_COMPARE_OP_NEVER;
    VkStencilOp back_fail = VK_STENCIL_OP_KEEP, back_pass = VK_STENCIL_OP_KEEP,
                back_depth_fail = VK_STENCIL_OP_KEEP;
    VkCompareOp back_compare = VK_COMPARE_OP_NEVER;
    if (normalized_depth_control.stencil_enable) {
      front_fail = VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                               uint32_t(normalized_depth_control.stencilfail));
      front_pass = VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                               uint32_t(normalized_depth_control.stencilzpass));
      front_depth_fail =
          VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                      uint32_t(normalized_depth_control.stencilzfail));
      front_compare =
          VkCompareOp(uint32_t(VK_COMPARE_OP_NEVER) +
                      uint32_t(normalized_depth_control.stencilfunc));
      if (primitive_polygonal && normalized_depth_control.backface_enable) {
        back_fail =
            VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                        uint32_t(normalized_depth_control.stencilfail_bf));
        back_pass =
            VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                        uint32_t(normalized_depth_control.stencilzpass_bf));
        back_depth_fail =
            VkStencilOp(uint32_t(VK_STENCIL_OP_KEEP) +
                        uint32_t(normalized_depth_control.stencilzfail_bf));
        back_compare =
            VkCompareOp(uint32_t(VK_COMPARE_OP_NEVER) +
                        uint32_t(normalized_depth_control.stencilfunc_bf));
      } else {
        back_fail = front_fail;
        back_pass = front_pass;
        back_depth_fail = front_depth_fail;
        back_compare = front_compare;
      }
    }
    if (dynamic_stencil_test_enable_update_needed_ ||
        stencil_test_enable != dynamic_stencil_test_enable_) {
      deferred_command_buffer_.CmdVkSetStencilTestEnable(stencil_test_enable);
      dynamic_stencil_test_enable_ = stencil_test_enable;
      dynamic_stencil_test_enable_update_needed_ = false;
    }
    if (dynamic_stencil_op_update_needed_ ||
        front_fail != dynamic_stencil_front_fail_op_ ||
        front_pass != dynamic_stencil_front_pass_op_ ||
        front_depth_fail != dynamic_stencil_front_depth_fail_op_ ||
        front_compare != dynamic_stencil_front_compare_op_ ||
        back_fail != dynamic_stencil_back_fail_op_ ||
        back_pass != dynamic_stencil_back_pass_op_ ||
        back_depth_fail != dynamic_stencil_back_depth_fail_op_ ||
        back_compare != dynamic_stencil_back_compare_op_) {
      deferred_command_buffer_.CmdVkSetStencilOp(VK_STENCIL_FACE_FRONT_BIT,
                                                 front_fail, front_pass,
                                                 front_depth_fail, front_compare);
      deferred_command_buffer_.CmdVkSetStencilOp(VK_STENCIL_FACE_BACK_BIT,
                                                 back_fail, back_pass,
                                                 back_depth_fail, back_compare);
      dynamic_stencil_front_fail_op_ = front_fail;
      dynamic_stencil_front_pass_op_ = front_pass;
      dynamic_stencil_front_depth_fail_op_ = front_depth_fail;
      dynamic_stencil_front_compare_op_ = front_compare;
      dynamic_stencil_back_fail_op_ = back_fail;
      dynamic_stencil_back_pass_op_ = back_pass;
      dynamic_stencil_back_depth_fail_op_ = back_depth_fail;
      dynamic_stencil_back_compare_op_ = back_compare;
      dynamic_stencil_op_update_needed_ = false;
    }
  }

  // gpu_dynamic_blend_state (EDS3): emit the per-attachment color blend enable /
  // equation / write mask promoted to dynamic state. Reproduce EXACTLY the static
  // color blend attachment the pipeline cache would have baked
  // (GetCurrentColorBlendDynamicState mirrors WritePipelineRenderTargetDescription
  // + EnsurePipelineCreated's derivation), gated identically to the pipeline-key
  // zeroing + the dynamic-state array (host-render-target path only, cvar + all 3
  // EDS3 blend sub-features, color attachments present) so the bound pipeline that
  // has these dynamic states is exactly the one for which we emit. The cvar
  // short-circuits first (zero-cost on the default path). Emitted every draw (no
  // caching) - the deferred stream records 3 tiny commands; correctness over
  // micro-optimization, and every draw's pipeline in this path has them dynamic.
  {
    const auto& device_extensions = GetVulkanDevice()->extensions();
    if (cvars::gpu_dynamic_blend_state &&
        device_extensions.ext_EXT_extended_dynamic_state3 &&
        device_extensions.eds3_dynamic_blend_enable &&
        device_extensions.eds3_dynamic_blend_equation &&
        device_extensions.eds3_dynamic_write_mask &&
        render_target_cache_->GetPath() !=
            RenderTargetCache::Path::kPixelShaderInterlock &&
        !hybrid_current_draw_composite_) {
      VulkanRenderTargetCache::RenderPassKey render_pass_key =
          render_target_cache_->last_update_render_pass_key();
      if ((render_pass_key.depth_and_color_used >> 1) != 0) {
        uint32_t attachment_count = 0;
        VkBool32 blend_enables[xenos::kMaxColorRenderTargets];
        VkColorBlendEquationEXT blend_equations[xenos::kMaxColorRenderTargets];
        VkColorComponentFlags write_masks[xenos::kMaxColorRenderTargets];
        pipeline_cache_->GetCurrentColorBlendDynamicState(
            normalized_color_mask, render_pass_key, attachment_count,
            blend_enables, blend_equations, write_masks);
        if (attachment_count) {
          deferred_command_buffer_.CmdVkSetColorBlendEnable(0, attachment_count,
                                                            blend_enables);
          deferred_command_buffer_.CmdVkSetColorBlendEquation(
              0, attachment_count, blend_equations);
          deferred_command_buffer_.CmdVkSetColorWriteMask(0, attachment_count,
                                                          write_masks);
        }
      }
    }
  }

  // TODO(Triang3l): more VK_EXT_extended_dynamic_state2 fields (primitive
  // topology / restart).
}

void VulkanCommandProcessor::UpdateSystemConstantValues(
    bool primitive_polygonal,
    const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
    bool shader_32bit_index_dma, const draw_util::ViewportInfo& viewport_info,
    uint32_t used_texture_mask, reg::RB_DEPTHCONTROL normalized_depth_control,
    uint32_t normalized_color_mask) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  const RegisterFile& regs = *register_file_;
  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  auto rb_alpha_ref = regs.Get<float>(XE_GPU_REG_RB_ALPHA_REF);
  auto rb_colorcontrol = regs.Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = regs.Get<reg::RB_DEPTH_INFO>();
  auto rb_stencilrefmask = regs.Get<reg::RB_STENCILREFMASK>();
  auto rb_stencilrefmask_bf =
      regs.Get<reg::RB_STENCILREFMASK>(XE_GPU_REG_RB_STENCILREFMASK_BF);
  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  auto vgt_indx_offset = regs.Get<int32_t>(XE_GPU_REG_VGT_INDX_OFFSET);

  bool edram_fragment_shader_interlock =
      render_target_cache_->GetPath() ==
          RenderTargetCache::Path::kPixelShaderInterlock ||
      hybrid_current_draw_composite_;
  uint32_t draw_resolution_scale_x = texture_cache_->draw_resolution_scale_x();
  uint32_t draw_resolution_scale_y = texture_cache_->draw_resolution_scale_y();

  // Get the color info register values for each render target. Also, for FSI,
  // exclude components that don't exist in the format from the write mask.
  // Don't exclude fully overlapping render targets, however - two render
  // targets with the same base address are used in the lighting pass of
  // 4D5307E6, for example, with the needed one picked with dynamic control
  // flow.
  reg::RB_COLOR_INFO color_infos[xenos::kMaxColorRenderTargets];
  float rt_clamp[4][4];
  // Two UINT32_MAX if no components actually existing in the RT are written.
  uint32_t rt_keep_masks[4][2];
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    auto color_info = regs.Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[i]);
    color_infos[i] = color_info;
    if (edram_fragment_shader_interlock) {
      RenderTargetCache::GetPSIColorFormatInfo(
          color_info.color_format, (normalized_color_mask >> (i * 4)) & 0b1111,
          rt_clamp[i][0], rt_clamp[i][1], rt_clamp[i][2], rt_clamp[i][3],
          rt_keep_masks[i][0], rt_keep_masks[i][1]);
    }
  }

  // Disable depth and stencil if it aliases a color render target (for
  // instance, during the XBLA logo in 58410954, though depth writing is already
  // disabled there).
  bool depth_stencil_enabled = normalized_depth_control.stencil_enable ||
                               normalized_depth_control.z_enable;
  if (edram_fragment_shader_interlock && depth_stencil_enabled) {
    for (uint32_t i = 0; i < 4; ++i) {
      if (rb_depth_info.depth_base == color_infos[i].color_base &&
          (rt_keep_masks[i][0] != UINT32_MAX ||
           rt_keep_masks[i][1] != UINT32_MAX)) {
        depth_stencil_enabled = false;
        break;
      }
    }
  }

  bool dirty = false;

  // Flags.
  uint32_t flags = 0;
  // Vertex index shader loading.
  if (shader_32bit_index_dma) {
    flags |= SpirvShaderTranslator::kSysFlag_VertexIndexLoad;
  }
  if (primitive_processing_result.index_buffer_type ==
      PrimitiveProcessor::ProcessedIndexBufferType::kHostBuiltinForDMA) {
    flags |= SpirvShaderTranslator::kSysFlag_ComputeOrPrimitiveVertexIndexLoad;
    if (vgt_draw_initiator.index_size == xenos::IndexFormat::kInt32) {
      flags |= SpirvShaderTranslator ::
          kSysFlag_ComputeOrPrimitiveVertexIndexLoad32Bit;
    }
  }
  // W0 division control.
  // http://www.x.org/docs/AMD/old/evergreen_3D_registers_v2.pdf
  // 8: VTX_XY_FMT = true: the incoming XY have already been multiplied by 1/W0.
  //               = false: multiply the X, Y coordinates by 1/W0.
  // 9: VTX_Z_FMT = true: the incoming Z has already been multiplied by 1/W0.
  //              = false: multiply the Z coordinate by 1/W0.
  // 10: VTX_W0_FMT = true: the incoming W0 is not 1/W0. Perform the reciprocal
  //                        to get 1/W0.
  if (pa_cl_vte_cntl.vtx_xy_fmt) {
    flags |= SpirvShaderTranslator::kSysFlag_XYDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_z_fmt) {
    flags |= SpirvShaderTranslator::kSysFlag_ZDividedByW;
  }
  if (pa_cl_vte_cntl.vtx_w0_fmt) {
    flags |= SpirvShaderTranslator::kSysFlag_WNotReciprocal;
  }
  // Whether the primitive is polygonal, and gl_FrontFacing matters.
  if (primitive_polygonal) {
    flags |= SpirvShaderTranslator::kSysFlag_PrimitivePolygonal;
  }
  // Primitive type.
  if (draw_util::IsPrimitiveLine(regs)) {
    flags |= SpirvShaderTranslator::kSysFlag_PrimitiveLine;
  }
  // MSAA sample count.
  // BD-30 foliage ROP lever: clamp consistently so the pixel shader's sample-count
  // view matches the (lowered) host RT / render pass (gpu_force_max_msaa_samples).
  flags |= uint32_t(draw_util::ClampForcedMsaaSamples(rb_surface_info.msaa_samples))
           << SpirvShaderTranslator::kSysFlag_MsaaSamples_Shift;
  // Depth format.
  if (rb_depth_info.depth_format == xenos::DepthRenderTargetFormat::kD24FS8) {
    flags |= SpirvShaderTranslator::kSysFlag_DepthFloat24;
  }
  // Alpha test.
  xenos::CompareFunction alpha_test_function =
      rb_colorcontrol.alpha_test_enable ? rb_colorcontrol.alpha_func
                                        : xenos::CompareFunction::kAlways;
  flags |= uint32_t(alpha_test_function)
           << SpirvShaderTranslator::kSysFlag_AlphaPassIfLess_Shift;
  // Gamma writing.
  // TODO(Triang3l): Gamma as unorm8 check.
  if (!edram_fragment_shader_interlock) {
    for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
      if (color_infos[i].color_format ==
          xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {
        flags |= SpirvShaderTranslator::kSysFlag_ConvertColor0ToGamma << i;
      }
    }
  }
  // THE EDRAM SOLVE, hybrid form: a full-screen post-process composite overwrites
  // the whole pixel, so it doesn't need the FSI depth/stencil ROP (which on the
  // interlock path is the slow, in-shader, no-early-Z per-sample work the research
  // flags as the ROP tax). Skip it for composites to cut the composite software-ROP
  // overhead. Gated on hybrid_current_draw_composite_ (default off); no effect on
  // composites that already run depth-off. Measure the delta next session.
  if (edram_fragment_shader_interlock && depth_stencil_enabled &&
      !hybrid_current_draw_composite_) {
    flags |= SpirvShaderTranslator::kSysFlag_FSIDepthStencil;
    if (normalized_depth_control.z_enable) {
      flags |= uint32_t(normalized_depth_control.zfunc)
               << SpirvShaderTranslator::kSysFlag_FSIDepthPassIfLess_Shift;
      if (normalized_depth_control.z_write_enable) {
        flags |= SpirvShaderTranslator::kSysFlag_FSIDepthWrite;
      }
    } else {
      // In case stencil is used without depth testing - always pass, and
      // don't modify the stored depth.
      flags |= SpirvShaderTranslator::kSysFlag_FSIDepthPassIfLess |
               SpirvShaderTranslator::kSysFlag_FSIDepthPassIfEqual |
               SpirvShaderTranslator::kSysFlag_FSIDepthPassIfGreater;
    }
    if (normalized_depth_control.stencil_enable) {
      flags |= SpirvShaderTranslator::kSysFlag_FSIStencilTest;
    }
    // Hint - if not applicable to the shader, will not have effect.
    if (alpha_test_function == xenos::CompareFunction::kAlways &&
        !rb_colorcontrol.alpha_to_mask_enable) {
      flags |= SpirvShaderTranslator::kSysFlag_FSIDepthStencilEarlyWrite;
    }
  }
  // G1-lite: this draw's position vfetch reads the compact de-interleaved
  // stream gathered in IssueDraw (UINT32_MAX = no gather - verbatim draw).
  uint32_t compact_pos_base_dwords = 0;
  if (compact_pos_current_base_dwords_ != UINT32_MAX) {
    flags |= SpirvShaderTranslator::kSysFlag_PosFetchRedirect;
    compact_pos_base_dwords = compact_pos_current_base_dwords_;
  }
  dirty |= system_constants_.compact_pos_base_dwords != compact_pos_base_dwords;
  system_constants_.compact_pos_base_dwords = compact_pos_base_dwords;

  // gpu_hw_vertex_fetch: redirect eligible attributes to the fixed-function
  // vertex-input variables only when the unit's element selection matches the
  // guest's. The guest computes EndianSwap(gl_VertexIndex) + vertex_base_index;
  // the fixed-function unit indexes by raw gl_VertexIndex - so the redirect is
  // valid iff the index needs no endian swap and the base index is zero. Only
  // the plain host-type-kVertex shader is translated with the input variables.
  // Any other draw keeps the flag clear and runs the unchanged SSBO arm
  // (byte-identical). The vertex buffers are bound in IssueDraw when the cvar is
  // on, independent of this flag, so the declared inputs always have a buffer.
  if (cvars::gpu_hw_vertex_fetch &&
      primitive_processing_result.host_vertex_shader_type ==
          Shader::HostVertexShaderType::kVertex) {
    ++draw_outcomes_hwvtx_elig_draws_;
    if (cvars::gpu_hw_vertex_fetch_force ||
        (primitive_processing_result.host_shader_index_endian ==
             xenos::Endian::kNone &&
         vgt_indx_offset == 0)) {
      flags |= SpirvShaderTranslator::kSysFlag_HwVertexFetch;
      ++draw_outcomes_hwvtx_redir_draws_;
    }
  }

  dirty |= system_constants_.flags != flags;
  system_constants_.flags = flags;

  // Index buffer address for loading in the shaders.
  if (flags &
      (SpirvShaderTranslator::kSysFlag_VertexIndexLoad |
       SpirvShaderTranslator::kSysFlag_ComputeOrPrimitiveVertexIndexLoad)) {
    dirty |= system_constants_.vertex_index_load_address !=
             primitive_processing_result.guest_index_base;
    system_constants_.vertex_index_load_address =
        primitive_processing_result.guest_index_base;
  }

  // Index or tessellation edge factor buffer endianness.
  dirty |= system_constants_.vertex_index_endian !=
           primitive_processing_result.host_shader_index_endian;
  system_constants_.vertex_index_endian =
      primitive_processing_result.host_shader_index_endian;

  // Vertex index offset.
  dirty |= system_constants_.vertex_base_index != vgt_indx_offset;
  system_constants_.vertex_base_index = vgt_indx_offset;

  // Conversion to host normalized device coordinates.
  for (uint32_t i = 0; i < 3; ++i) {
    dirty |= system_constants_.ndc_scale[i] != viewport_info.ndc_scale[i];
    dirty |= system_constants_.ndc_offset[i] != viewport_info.ndc_offset[i];
    system_constants_.ndc_scale[i] = viewport_info.ndc_scale[i];
    system_constants_.ndc_offset[i] = viewport_info.ndc_offset[i];
  }

  // Point size.
  if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
    auto pa_su_point_minmax = regs.Get<reg::PA_SU_POINT_MINMAX>();
    auto pa_su_point_size = regs.Get<reg::PA_SU_POINT_SIZE>();
    float point_vertex_diameter_min =
        float(pa_su_point_minmax.min_size) * (2.0f / 16.0f);
    float point_vertex_diameter_max =
        float(pa_su_point_minmax.max_size) * (2.0f / 16.0f);
    float point_constant_diameter_x =
        float(pa_su_point_size.width) * (2.0f / 16.0f);
    float point_constant_diameter_y =
        float(pa_su_point_size.height) * (2.0f / 16.0f);
    dirty |= system_constants_.point_vertex_diameter_min !=
             point_vertex_diameter_min;
    dirty |= system_constants_.point_vertex_diameter_max !=
             point_vertex_diameter_max;
    dirty |= system_constants_.point_constant_diameter[0] !=
             point_constant_diameter_x;
    dirty |= system_constants_.point_constant_diameter[1] !=
             point_constant_diameter_y;
    system_constants_.point_vertex_diameter_min = point_vertex_diameter_min;
    system_constants_.point_vertex_diameter_max = point_vertex_diameter_max;
    system_constants_.point_constant_diameter[0] = point_constant_diameter_x;
    system_constants_.point_constant_diameter[1] = point_constant_diameter_y;
    // 2 because 1 in the NDC is half of the viewport's axis, 0.5 for diameter
    // to radius conversion to avoid multiplying the per-vertex diameter by an
    // additional constant in the shader.
    float point_screen_diameter_to_ndc_radius_x =
        (/* 0.5f * 2.0f * */ float(draw_resolution_scale_x)) /
        std::max(viewport_info.xy_extent[0], uint32_t(1));
    float point_screen_diameter_to_ndc_radius_y =
        (/* 0.5f * 2.0f * */ float(draw_resolution_scale_y)) /
        std::max(viewport_info.xy_extent[1], uint32_t(1));
    dirty |= system_constants_.point_screen_diameter_to_ndc_radius[0] !=
             point_screen_diameter_to_ndc_radius_x;
    dirty |= system_constants_.point_screen_diameter_to_ndc_radius[1] !=
             point_screen_diameter_to_ndc_radius_y;
    system_constants_.point_screen_diameter_to_ndc_radius[0] =
        point_screen_diameter_to_ndc_radius_x;
    system_constants_.point_screen_diameter_to_ndc_radius[1] =
        point_screen_diameter_to_ndc_radius_y;
  }

  // Texture signedness / gamma.
  {
    uint32_t textures_remaining = used_texture_mask;
    uint32_t texture_index;
    while (xe::bit_scan_forward(textures_remaining, &texture_index)) {
      textures_remaining &= ~(UINT32_C(1) << texture_index);
      uint32_t& texture_signs_uint =
          system_constants_.texture_swizzled_signs[texture_index >> 2];
      uint32_t texture_signs_shift = 8 * (texture_index & 3);
      uint8_t texture_signs =
          texture_cache_->GetActiveTextureSwizzledSigns(texture_index);
      uint32_t texture_signs_shifted = uint32_t(texture_signs)
                                       << texture_signs_shift;
      uint32_t texture_signs_mask = ((UINT32_C(1) << 8) - 1)
                                    << texture_signs_shift;
      dirty |=
          (texture_signs_uint & texture_signs_mask) != texture_signs_shifted;
      texture_signs_uint =
          (texture_signs_uint & ~texture_signs_mask) | texture_signs_shifted;
    }
  }

  // Texture host swizzle in the shader.
  if (!GetVulkanDevice()->properties().imageViewFormatSwizzle) {
    uint32_t textures_remaining = used_texture_mask;
    uint32_t texture_index;
    while (xe::bit_scan_forward(textures_remaining, &texture_index)) {
      textures_remaining &= ~(UINT32_C(1) << texture_index);
      uint32_t& texture_swizzles_uint =
          system_constants_.texture_swizzles[texture_index >> 1];
      uint32_t texture_swizzle_shift = 12 * (texture_index & 1);
      uint32_t texture_swizzle =
          texture_cache_->GetActiveTextureHostSwizzle(texture_index);
      uint32_t texture_swizzle_shifted = uint32_t(texture_swizzle)
                                         << texture_swizzle_shift;
      uint32_t texture_swizzle_mask = ((UINT32_C(1) << 12) - 1)
                                      << texture_swizzle_shift;
      dirty |= (texture_swizzles_uint & texture_swizzle_mask) !=
               texture_swizzle_shifted;
      texture_swizzles_uint = (texture_swizzles_uint & ~texture_swizzle_mask) |
                              texture_swizzle_shifted;
    }
  }

  // Alpha test.
  dirty |= system_constants_.alpha_test_reference != rb_alpha_ref;
  system_constants_.alpha_test_reference = rb_alpha_ref;

  uint32_t edram_tile_dwords_scaled =
      xenos::kEdramTileWidthSamples * xenos::kEdramTileHeightSamples *
      (draw_resolution_scale_x * draw_resolution_scale_y);

  // EDRAM pitch for FSI render target writing.
  if (edram_fragment_shader_interlock) {
    // Align, then multiply by 32bpp tile size in dwords.
    uint32_t edram_32bpp_tile_pitch_dwords_scaled =
        ((rb_surface_info.surface_pitch *
          (rb_surface_info.msaa_samples >= xenos::MsaaSamples::k4X ? 2 : 1)) +
         (xenos::kEdramTileWidthSamples - 1)) /
        xenos::kEdramTileWidthSamples * edram_tile_dwords_scaled;
    dirty |= system_constants_.edram_32bpp_tile_pitch_dwords_scaled !=
             edram_32bpp_tile_pitch_dwords_scaled;
    system_constants_.edram_32bpp_tile_pitch_dwords_scaled =
        edram_32bpp_tile_pitch_dwords_scaled;
  }

  // Color exponent bias and FSI render target writing.
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    reg::RB_COLOR_INFO color_info = color_infos[i];
    // Exponent bias is in bits 20:25 of RB_COLOR_INFO.
    int32_t color_exp_bias = color_info.color_exp_bias;
    if (render_target_cache_->GetPath() ==
            RenderTargetCache::Path::kHostRenderTargets &&
        (color_info.color_format == xenos::ColorRenderTargetFormat::k_16_16 &&
             !render_target_cache_->IsFixedRG16TruncatedToMinus1To1() ||
         color_info.color_format ==
                 xenos::ColorRenderTargetFormat::k_16_16_16_16 &&
             !render_target_cache_->IsFixedRGBA16TruncatedToMinus1To1())) {
      // Remap from -32...32 to -1...1 by dividing the output values by 32,
      // losing blending correctness, but getting the full range.
      color_exp_bias -= 5;
    }
    float color_exp_bias_scale;
    *reinterpret_cast<int32_t*>(&color_exp_bias_scale) =
        UINT32_C(0x3F800000) + (color_exp_bias << 23);
    dirty |= system_constants_.color_exp_bias[i] != color_exp_bias_scale;
    system_constants_.color_exp_bias[i] = color_exp_bias_scale;
    if (edram_fragment_shader_interlock) {
      dirty |=
          system_constants_.edram_rt_keep_mask[i][0] != rt_keep_masks[i][0];
      system_constants_.edram_rt_keep_mask[i][0] = rt_keep_masks[i][0];
      dirty |=
          system_constants_.edram_rt_keep_mask[i][1] != rt_keep_masks[i][1];
      system_constants_.edram_rt_keep_mask[i][1] = rt_keep_masks[i][1];
      if (rt_keep_masks[i][0] != UINT32_MAX ||
          rt_keep_masks[i][1] != UINT32_MAX) {
        uint32_t rt_base_dwords_scaled =
            color_info.color_base * edram_tile_dwords_scaled;
        dirty |= system_constants_.edram_rt_base_dwords_scaled[i] !=
                 rt_base_dwords_scaled;
        system_constants_.edram_rt_base_dwords_scaled[i] =
            rt_base_dwords_scaled;
        uint32_t format_flags =
            RenderTargetCache::AddPSIColorFormatFlags(color_info.color_format);
        dirty |= system_constants_.edram_rt_format_flags[i] != format_flags;
        system_constants_.edram_rt_format_flags[i] = format_flags;
        uint32_t blend_factors_ops =
            regs[reg::RB_BLENDCONTROL::rt_register_indices[i]] & 0x1FFF1FFF;
        dirty |= system_constants_.edram_rt_blend_factors_ops[i] !=
                 blend_factors_ops;
        system_constants_.edram_rt_blend_factors_ops[i] = blend_factors_ops;
        // Can't do float comparisons here because NaNs would result in always
        // setting the dirty flag.
        dirty |= std::memcmp(system_constants_.edram_rt_clamp[i], rt_clamp[i],
                             4 * sizeof(float)) != 0;
        std::memcpy(system_constants_.edram_rt_clamp[i], rt_clamp[i],
                    4 * sizeof(float));
      }
    }
  }

  if (edram_fragment_shader_interlock) {
    uint32_t depth_base_dwords_scaled =
        rb_depth_info.depth_base * edram_tile_dwords_scaled;
    dirty |= system_constants_.edram_depth_base_dwords_scaled !=
             depth_base_dwords_scaled;
    system_constants_.edram_depth_base_dwords_scaled = depth_base_dwords_scaled;

    // For non-polygons, front polygon offset is used, and it's enabled if
    // POLY_OFFSET_PARA_ENABLED is set, for polygons, separate front and back
    // are used.
    float poly_offset_front_scale = 0.0f, poly_offset_front_offset = 0.0f;
    float poly_offset_back_scale = 0.0f, poly_offset_back_offset = 0.0f;
    if (primitive_polygonal) {
      if (pa_su_sc_mode_cntl.poly_offset_front_enable) {
        poly_offset_front_scale =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
        poly_offset_front_offset =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
      }
      if (pa_su_sc_mode_cntl.poly_offset_back_enable) {
        poly_offset_back_scale =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_SCALE);
        poly_offset_back_offset =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_BACK_OFFSET);
      }
    } else {
      if (pa_su_sc_mode_cntl.poly_offset_para_enable) {
        poly_offset_front_scale =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE);
        poly_offset_front_offset =
            regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET);
        poly_offset_back_scale = poly_offset_front_scale;
        poly_offset_back_offset = poly_offset_front_offset;
      }
    }
    // With non-square resolution scaling, make sure the worst-case impact is
    // reverted (slope only along the scaled axis), thus max. More bias is
    // better than less bias, because less bias means Z fighting with the
    // background is more likely.
    float poly_offset_scale_factor =
        xenos::kPolygonOffsetScaleSubpixelUnit *
        std::max(draw_resolution_scale_x, draw_resolution_scale_y);
    poly_offset_front_scale *= poly_offset_scale_factor;
    poly_offset_back_scale *= poly_offset_scale_factor;
    dirty |= system_constants_.edram_poly_offset_front_scale !=
             poly_offset_front_scale;
    system_constants_.edram_poly_offset_front_scale = poly_offset_front_scale;
    dirty |= system_constants_.edram_poly_offset_front_offset !=
             poly_offset_front_offset;
    system_constants_.edram_poly_offset_front_offset = poly_offset_front_offset;
    dirty |= system_constants_.edram_poly_offset_back_scale !=
             poly_offset_back_scale;
    system_constants_.edram_poly_offset_back_scale = poly_offset_back_scale;
    dirty |= system_constants_.edram_poly_offset_back_offset !=
             poly_offset_back_offset;
    system_constants_.edram_poly_offset_back_offset = poly_offset_back_offset;

    if (depth_stencil_enabled && normalized_depth_control.stencil_enable) {
      uint32_t stencil_front_reference_masks =
          rb_stencilrefmask.value & 0xFFFFFF;
      dirty |= system_constants_.edram_stencil_front_reference_masks !=
               stencil_front_reference_masks;
      system_constants_.edram_stencil_front_reference_masks =
          stencil_front_reference_masks;
      uint32_t stencil_func_ops =
          (normalized_depth_control.value >> 8) & ((1 << 12) - 1);
      dirty |=
          system_constants_.edram_stencil_front_func_ops != stencil_func_ops;
      system_constants_.edram_stencil_front_func_ops = stencil_func_ops;

      if (primitive_polygonal && normalized_depth_control.backface_enable) {
        uint32_t stencil_back_reference_masks =
            rb_stencilrefmask_bf.value & 0xFFFFFF;
        dirty |= system_constants_.edram_stencil_back_reference_masks !=
                 stencil_back_reference_masks;
        system_constants_.edram_stencil_back_reference_masks =
            stencil_back_reference_masks;
        uint32_t stencil_func_ops_bf =
            (normalized_depth_control.value >> 20) & ((1 << 12) - 1);
        dirty |= system_constants_.edram_stencil_back_func_ops !=
                 stencil_func_ops_bf;
        system_constants_.edram_stencil_back_func_ops = stencil_func_ops_bf;
      } else {
        dirty |= std::memcmp(system_constants_.edram_stencil_back,
                             system_constants_.edram_stencil_front,
                             2 * sizeof(uint32_t)) != 0;
        std::memcpy(system_constants_.edram_stencil_back,
                    system_constants_.edram_stencil_front,
                    2 * sizeof(uint32_t));
      }
    }

    dirty |= system_constants_.edram_blend_constant[0] !=
             regs.Get<float>(XE_GPU_REG_RB_BLEND_RED);
    system_constants_.edram_blend_constant[0] =
        regs.Get<float>(XE_GPU_REG_RB_BLEND_RED);
    dirty |= system_constants_.edram_blend_constant[1] !=
             regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN);
    system_constants_.edram_blend_constant[1] =
        regs.Get<float>(XE_GPU_REG_RB_BLEND_GREEN);
    dirty |= system_constants_.edram_blend_constant[2] !=
             regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE);
    system_constants_.edram_blend_constant[2] =
        regs.Get<float>(XE_GPU_REG_RB_BLEND_BLUE);
    dirty |= system_constants_.edram_blend_constant[3] !=
             regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA);
    system_constants_.edram_blend_constant[3] =
        regs.Get<float>(XE_GPU_REG_RB_BLEND_ALPHA);
  }

  if (dirty) {
    current_constant_buffers_up_to_date_ &=
        ~(UINT32_C(1) << SpirvShaderTranslator::kConstantBufferSystem);
  }
}

void VulkanCommandProcessor::UpdateSharedMemoryDescriptorSetForCurrentVersion() {
  // gpu_shared_memory_double_buffer: no-op (and byte-identical) when off.
  if (!shared_memory_->double_buffer_active()) {
    return;
  }
  const uint32_t version = shared_memory_->current_version();
  if (version == shared_memory_descriptor_set_bound_version_) {
    return;
  }
  // The shared memory switched versions - point the cached set at the version
  // the GPU now reads (fall back to version 0's set if the v1 set could not be
  // allocated) and invalidate it so the next bind re-records it.
  VkDescriptorSet set =
      (version == 1 &&
       shared_memory_and_edram_descriptor_set_v1_ != VK_NULL_HANDLE)
          ? shared_memory_and_edram_descriptor_set_v1_
          : shared_memory_and_edram_descriptor_set_;
  current_graphics_descriptor_sets_
      [SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram] = set;
  current_graphics_descriptor_set_values_up_to_date_ |=
      UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram;
  current_graphics_descriptor_sets_bound_up_to_date_ &=
      ~(UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram);
  shared_memory_descriptor_set_bound_version_ = version;
}

bool VulkanCommandProcessor::UpdateBindings(const VulkanShader* vertex_shader,
                                            const VulkanShader* pixel_shader) {
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES

  const RegisterFile& regs = *register_file_;

  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // Merge-eligibility attribution: at this point current_guest_graphics_pipeline_
  // is this draw's pipeline (set in IssueDraw before this call) and
  // current_constant_buffers_up_to_date_ still reflects what the guest
  // invalidated since the previous draw. Count consecutive same-pipeline draws
  // and whether their vertex float constants (per-mesh transforms) changed.
  if (cvars::vulkan_trace_draw_outcomes_per_frame) {
    if (current_guest_graphics_pipeline_ != VK_NULL_HANDLE &&
        current_guest_graphics_pipeline_ == merge_last_pipeline_) {
      ++merge_pipe_same_;
      if (current_constant_buffers_up_to_date_ &
          (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatVertex)) {
        ++merge_consts_same_;
      } else {
        ++merge_consts_changed_;
      }
    }
    merge_last_pipeline_ = current_guest_graphics_pipeline_;
  }

  // Capture - BEFORE the per-draw invalidation below re-sets the up-to-date bit -
  // whether this draw's vertex float constants (the per-mesh transform) are
  // UNCHANGED from the previous draw. This is the strip-coalescer's true (rank-3)
  // mergeability gate: consecutive strips can be stitched into one physical draw
  // only if they share the per-object transform. Consumed by the stripd_runlen
  // histogram in the draw-outcomes instrumentation (which runs later, after the
  // bit has already been re-set by the upload below).
  merge_draw_vfetch_consts_same_ =
      (current_constant_buffers_up_to_date_ &
       (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatVertex)) != 0;

  // Invalidate constant buffers and descriptors for changed data.

  // Float constants.
  // These are the constant base addresses/ranges for shaders.
  // We have these hardcoded right now cause nothing seems to differ on the Xbox
  // 360 (however, OpenGL ES on Adreno 200 on Android has different ranges).
  assert_true(regs[XE_GPU_REG_SQ_VS_CONST] == 0x000FF000 ||
              regs[XE_GPU_REG_SQ_VS_CONST] == 0x00000000);
  assert_true(regs[XE_GPU_REG_SQ_PS_CONST] == 0x000FF100 ||
              regs[XE_GPU_REG_SQ_PS_CONST] == 0x00000000);
  // Check if the float constant layout is still the same and get the counts.
  const Shader::ConstantRegisterMap& float_constant_map_vertex =
      vertex_shader->constant_register_map();
  uint32_t float_constant_count_vertex = float_constant_map_vertex.float_count;
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_float_constant_map_vertex_[i] !=
        float_constant_map_vertex.float_bitmap[i]) {
      current_float_constant_map_vertex_[i] =
          float_constant_map_vertex.float_bitmap[i];
      // If no float constants at all, any buffer can be reused for them, so not
      // invalidating.
      if (float_constant_count_vertex) {
        current_constant_buffers_up_to_date_ &=
            ~(UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatVertex);
      }
    }
  }
  uint32_t float_constant_count_pixel = 0;
  if (pixel_shader != nullptr) {
    const Shader::ConstantRegisterMap& float_constant_map_pixel =
        pixel_shader->constant_register_map();
    float_constant_count_pixel = float_constant_map_pixel.float_count;
    for (uint32_t i = 0; i < 4; ++i) {
      if (current_float_constant_map_pixel_[i] !=
          float_constant_map_pixel.float_bitmap[i]) {
        current_float_constant_map_pixel_[i] =
            float_constant_map_pixel.float_bitmap[i];
        if (float_constant_count_pixel) {
          current_constant_buffers_up_to_date_ &= ~(
              UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatPixel);
        }
      }
    }
  } else {
    std::memset(current_float_constant_map_pixel_, 0,
                sizeof(current_float_constant_map_pixel_));
  }

  // Write the new constant buffers.
  constexpr uint32_t kAllConstantBuffersMask =
      (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferCount) - 1;
  assert_zero(current_constant_buffers_up_to_date_ & ~kAllConstantBuffersMask);
  if ((current_constant_buffers_up_to_date_ & kAllConstantBuffersMask) !=
      kAllConstantBuffersMask) {
    current_graphics_descriptor_set_values_up_to_date_ &=
        ~(UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetConstants);
    size_t uniform_buffer_alignment =
        size_t(vulkan_device->properties().minUniformBufferOffsetAlignment);
    // R2 (vulkan_dynamic_constants_arena): when armed, write each constant buffer
    // into its persistent per-frame-segmented dynamic ring and record the per-draw
    // dynamic offset, instead of allocating a fresh transient pool slice (and,
    // below, a transient descriptor set) per draw. Returns the host pointer to
    // write into, or nullptr on ring overflow. !arena keeps the exact pool path
    // (including updating buffer_info for the transient descriptor write).
    const bool arena = constants_dynamic_descriptor_set_ != VK_NULL_HANDLE;
    auto request_constant = [&](uint32_t index, size_t size,
                                VkDescriptorBufferInfo& buffer_info,
                                VkDeviceSize& out_offset) -> uint8_t* {
      if (index < 5u) {
        ++mrw_cb_upload_[index];
      }
      if (arena) {
        bool ok = false;
        VkDeviceSize off =
            dynamic_constants_rings_[index].Allocate(VkDeviceSize(size), &ok);
        if (!ok) {
          return nullptr;
        }
        current_constant_dynamic_offsets_[index] = uint32_t(off);
        out_offset = off;
        return dynamic_constants_rings_[index].host_mapping() + off;
      }
      uint8_t* pool_mapping = uniform_buffer_pool_->Request(
          frame_current_, size, uniform_buffer_alignment, buffer_info.buffer,
          buffer_info.offset);
      if (pool_mapping) {
        buffer_info.range = VkDeviceSize(size);
      }
      return pool_mapping;
    };
    // System constants.
    if (!(current_constant_buffers_up_to_date_ &
          (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferSystem))) {
      VkDescriptorBufferInfo& buffer_info = current_constant_buffer_infos_
          [SpirvShaderTranslator::kConstantBufferSystem];
      VkDeviceSize ring_off = 0;
      uint8_t* mapping = request_constant(
          SpirvShaderTranslator::kConstantBufferSystem,
          sizeof(SpirvShaderTranslator::SystemConstants), buffer_info, ring_off);
      if (!mapping) {
        return false;
      }
      std::memcpy(mapping, &system_constants_,
                  sizeof(SpirvShaderTranslator::SystemConstants));
      if (arena) {
        dynamic_constants_rings_[SpirvShaderTranslator::kConstantBufferSystem]
            .FlushRange(ring_off,
                        sizeof(SpirvShaderTranslator::SystemConstants));
      }
      current_constant_buffers_up_to_date_ |=
          UINT32_C(1) << SpirvShaderTranslator::kConstantBufferSystem;
    }
    // Vertex shader float constants.
    if (!(current_constant_buffers_up_to_date_ &
          (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatVertex))) {
      VkDescriptorBufferInfo& buffer_info = current_constant_buffer_infos_
          [SpirvShaderTranslator::kConstantBufferFloatVertex];
      // Even if the shader doesn't need any float constants, a valid binding
      // must still be provided (the pipeline layout always has float constants,
      // for both the vertex shader and the pixel shader), so if the first draw
      // in the frame doesn't have float constants at all, still allocate a
      // dummy buffer.
      size_t float_constants_size =
          sizeof(float) * 4 *
          std::max(float_constant_count_vertex, UINT32_C(1));
      VkDeviceSize ring_off = 0;
      uint8_t* mapping = request_constant(
          SpirvShaderTranslator::kConstantBufferFloatVertex,
          float_constants_size, buffer_info, ring_off);
      if (!mapping) {
        return false;
      }
      for (uint32_t i = 0; i < 4; ++i) {
        uint64_t float_constant_map_entry =
            current_float_constant_map_vertex_[i];
        uint32_t float_constant_index;
        while (xe::bit_scan_forward(float_constant_map_entry,
                                    &float_constant_index)) {
          float_constant_map_entry &= ~(1ull << float_constant_index);
          std::memcpy(mapping,
                      &regs[XE_GPU_REG_SHADER_CONSTANT_000_X + (i << 8) +
                            (float_constant_index << 2)],
                      sizeof(float) * 4);
          mapping += sizeof(float) * 4;
        }
      }
      if (arena) {
        dynamic_constants_rings_
            [SpirvShaderTranslator::kConstantBufferFloatVertex]
                .FlushRange(ring_off, VkDeviceSize(float_constants_size));
      }
      current_constant_buffers_up_to_date_ |=
          UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatVertex;
    }
    // Pixel shader float constants.
    if (!(current_constant_buffers_up_to_date_ &
          (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatPixel))) {
      VkDescriptorBufferInfo& buffer_info = current_constant_buffer_infos_
          [SpirvShaderTranslator::kConstantBufferFloatPixel];
      size_t float_constants_size =
          sizeof(float) * 4 * std::max(float_constant_count_pixel, UINT32_C(1));
      VkDeviceSize ring_off = 0;
      uint8_t* mapping = request_constant(
          SpirvShaderTranslator::kConstantBufferFloatPixel, float_constants_size,
          buffer_info, ring_off);
      if (!mapping) {
        return false;
      }
      for (uint32_t i = 0; i < 4; ++i) {
        uint64_t float_constant_map_entry =
            current_float_constant_map_pixel_[i];
        uint32_t float_constant_index;
        while (xe::bit_scan_forward(float_constant_map_entry,
                                    &float_constant_index)) {
          float_constant_map_entry &= ~(1ull << float_constant_index);
          std::memcpy(mapping,
                      &regs[XE_GPU_REG_SHADER_CONSTANT_256_X + (i << 8) +
                            (float_constant_index << 2)],
                      sizeof(float) * 4);
          mapping += sizeof(float) * 4;
        }
      }
      if (arena) {
        dynamic_constants_rings_
            [SpirvShaderTranslator::kConstantBufferFloatPixel]
                .FlushRange(ring_off, VkDeviceSize(float_constants_size));
      }
      current_constant_buffers_up_to_date_ |=
          UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFloatPixel;
    }
    // Bool and loop constants.
    if (!(current_constant_buffers_up_to_date_ &
          (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferBoolLoop))) {
      VkDescriptorBufferInfo& buffer_info = current_constant_buffer_infos_
          [SpirvShaderTranslator::kConstantBufferBoolLoop];
      constexpr size_t kBoolLoopConstantsSize = sizeof(uint32_t) * (8 + 32);
      VkDeviceSize ring_off = 0;
      uint8_t* mapping = request_constant(
          SpirvShaderTranslator::kConstantBufferBoolLoop, kBoolLoopConstantsSize,
          buffer_info, ring_off);
      if (!mapping) {
        return false;
      }
      std::memcpy(mapping, &regs[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031],
                  kBoolLoopConstantsSize);
      if (arena) {
        dynamic_constants_rings_[SpirvShaderTranslator::kConstantBufferBoolLoop]
            .FlushRange(ring_off, VkDeviceSize(kBoolLoopConstantsSize));
      }
      current_constant_buffers_up_to_date_ |=
          UINT32_C(1) << SpirvShaderTranslator::kConstantBufferBoolLoop;
    }
    // Fetch constants.
    if (!(current_constant_buffers_up_to_date_ &
          (UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch))) {
      VkDescriptorBufferInfo& buffer_info = current_constant_buffer_infos_
          [SpirvShaderTranslator::kConstantBufferFetch];
      constexpr size_t kFetchConstantsSize = sizeof(uint32_t) * 6 * 32;
      VkDeviceSize ring_off = 0;
      uint8_t* mapping = request_constant(
          SpirvShaderTranslator::kConstantBufferFetch, kFetchConstantsSize,
          buffer_info, ring_off);
      if (!mapping) {
        return false;
      }
      std::memcpy(mapping, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],
                  kFetchConstantsSize);
      if (arena) {
        dynamic_constants_rings_[SpirvShaderTranslator::kConstantBufferFetch]
            .FlushRange(ring_off, VkDeviceSize(kFetchConstantsSize));
      }
      current_constant_buffers_up_to_date_ |=
          UINT32_C(1) << SpirvShaderTranslator::kConstantBufferFetch;
    }
  }

  // Textures and samplers.
  const std::vector<VulkanShader::SamplerBinding>& samplers_vertex =
      vertex_shader->GetSamplerBindingsAfterTranslation();
  const std::vector<VulkanShader::TextureBinding>& textures_vertex =
      vertex_shader->GetTextureBindingsAfterTranslation();
  uint32_t sampler_count_vertex = uint32_t(samplers_vertex.size());
  uint32_t texture_count_vertex = uint32_t(textures_vertex.size());
  const std::vector<VulkanShader::SamplerBinding>* samplers_pixel;
  const std::vector<VulkanShader::TextureBinding>* textures_pixel;
  uint32_t sampler_count_pixel, texture_count_pixel;
  if (pixel_shader) {
    samplers_pixel = &pixel_shader->GetSamplerBindingsAfterTranslation();
    textures_pixel = &pixel_shader->GetTextureBindingsAfterTranslation();
    sampler_count_pixel = uint32_t(samplers_pixel->size());
    texture_count_pixel = uint32_t(textures_pixel->size());
  } else {
    samplers_pixel = nullptr;
    textures_pixel = nullptr;
    sampler_count_pixel = 0;
    texture_count_pixel = 0;
  }

  // BRICK 1 native bindless render path: instead of allocating + writing +
  // binding a transient per-draw texture descriptor set, push this draw's
  // texture/sampler GLOBAL SLOT indices as push constants (the one global
  // bindless set is bound once, by the bind loop below) and skip the entire
  // legacy texture-descriptor path. Slots come from the SAME per-draw views /
  // samplers the legacy path would bind (GetActiveBindingOrNullImageView /
  // current_samplers_*), so the sampled data is identical.
  if (native_render_path_active_) {
    struct BindlessPushConstants {
      uint32_t vertex_texture_slots
          [SpirvShaderTranslator::kBindlessPushVertexTextureCount];
      uint32_t vertex_sampler_slots
          [SpirvShaderTranslator::kBindlessPushVertexSamplerCount];
      uint32_t pixel_texture_slots
          [SpirvShaderTranslator::kBindlessPushPixelTextureCount];
      uint32_t pixel_sampler_slots
          [SpirvShaderTranslator::kBindlessPushPixelSamplerCount];
    } bindless_push{};
    static_assert(
        sizeof(BindlessPushConstants) ==
            SpirvShaderTranslator::kBindlessPushConstantsSize,
        "Bindless push-constant struct must match the SPIR-V block size");
    auto image_binding_for_dimension =
        [](xenos::FetchOpDimension dimension) -> uint32_t {
      switch (dimension) {
        case xenos::FetchOpDimension::k3DOrStacked:
          return SpirvShaderTranslator::kBindlessBindingTexture3D;
        case xenos::FetchOpDimension::kCube:
          return SpirvShaderTranslator::kBindlessBindingTextureCube;
        default:
          return SpirvShaderTranslator::kBindlessBindingTexture2DArray;
      }
    };
    for (uint32_t i = 0;
         i < texture_count_vertex &&
         i < SpirvShaderTranslator::kBindlessPushVertexTextureCount;
         ++i) {
      const VulkanShader::TextureBinding& tb = textures_vertex[i];
      VkImageView view = texture_cache_->GetActiveBindingOrNullImageView(
          tb.fetch_constant, tb.dimension, bool(tb.is_signed));
      bindless_push.vertex_texture_slots[i] =
          UseBindlessImageSlot(view, image_binding_for_dimension(tb.dimension));
    }
    for (uint32_t i = 0;
         i < sampler_count_vertex &&
         i < SpirvShaderTranslator::kBindlessPushVertexSamplerCount;
         ++i) {
      bindless_push.vertex_sampler_slots[i] =
          UseBindlessSamplerSlot(current_samplers_vertex_[i].second);
    }
    if (pixel_shader) {
      for (uint32_t i = 0;
           i < texture_count_pixel &&
           i < SpirvShaderTranslator::kBindlessPushPixelTextureCount;
           ++i) {
        const VulkanShader::TextureBinding& tb = (*textures_pixel)[i];
        VkImageView view = texture_cache_->GetActiveBindingOrNullImageView(
            tb.fetch_constant, tb.dimension, bool(tb.is_signed));
        bindless_push.pixel_texture_slots[i] = UseBindlessImageSlot(
            view, image_binding_for_dimension(tb.dimension));
      }
      for (uint32_t i = 0;
           i < sampler_count_pixel &&
           i < SpirvShaderTranslator::kBindlessPushPixelSamplerCount;
           ++i) {
        bindless_push.pixel_sampler_slots[i] =
            UseBindlessSamplerSlot(current_samplers_pixel_[i].second);
      }
    }
    deferred_command_buffer_.CmdVkPushConstants(
        current_guest_graphics_pipeline_layout_->GetPipelineLayout(),
        guest_shader_vertex_stages_ | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
        sizeof(bindless_push), &bindless_push);
    // Point set 2 at the global bindless set (bound once by the loop below);
    // set 3 is unused. Mark both value-up-to-date so the legacy write path is
    // fully skipped for them.
    current_graphics_descriptor_sets_
        [SpirvShaderTranslator::kDescriptorSetTexturesVertex] =
            bindless_descriptor_set_;
    current_graphics_descriptor_set_values_up_to_date_ |=
        (UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex) |
        (UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel);
  }

  // EDRAM-recompiler RT-as-texture (increment 1, gpu_rt_as_texture, default-off):
  // for each pixel texture fetch that samples data a resolve wrote to shared
  // memory this frame, if the SOURCE render target is still resident and a 1:1
  // host-format match, override the descriptor to sample that render target's
  // image directly (skipping the resolve->shared-memory->reload round-trip).
  // Computed once here so BOTH the descriptor signature and the descriptor write
  // loop below bind the same view; if they disagreed, the descriptor cache
  // (vulkan_cache_texture_descriptors) would keep a stale view. Filled every draw
  // (cheap, 32 pointers) so the unconditional override reads below stay valid even
  // with the cvar off (then all VK_NULL_HANDLE = no behavior change).
  rt_as_texture_views_pixel_.fill(VK_NULL_HANDLE);
  if (cvars::gpu_rt_as_texture && pixel_shader && texture_count_pixel &&
      !feedback_merge_active_ && !native_render_path_active_) {
    for (const VulkanShader::TextureBinding& texture_binding : *textures_pixel) {
      uint32_t fetch_constant = texture_binding.fetch_constant;
      // Only non-signed, identity-RGBA-swizzle fetches can sample the native RT
      // image directly; the texture reload path is what applies sign/swizzle
      // remaps, which a direct RT bind would skip.
      if (texture_binding.is_signed ||
          texture_cache_->GetActiveTextureHostSwizzle(fetch_constant) !=
              xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA) {
        continue;
      }
      uint32_t texture_base_address = 0;
      VkFormat texture_host_format_unsigned = VK_FORMAT_UNDEFINED;
      if (!texture_cache_->GetActiveTextureGuestInfo(
              fetch_constant, &texture_base_address,
              &texture_host_format_unsigned)) {
        continue;
      }
      const ResolveEdge* resolve_edge = ResolveEdgeForBase(texture_base_address);
      if (!resolve_edge) {
        continue;
      }
      VkImageView rt_view =
          render_target_cache_->GetResolveSourceRenderTargetViewForSampling(
              resolve_edge->src_edram_base_tiles, resolve_edge->src_pitch_tiles,
              resolve_edge->src_format, resolve_edge->src_msaa,
              resolve_edge->src_is_depth, texture_host_format_unsigned);
      if (rt_view != VK_NULL_HANDLE) {
        rt_as_texture_views_pixel_[fetch_constant] = rt_view;
        ++rt_served_textures_;
      }
    }
  }

  // Reuse texture and sampler bindings if not changed since the last draw
  // (vulkan_cache_texture_descriptors). Allocating a transient descriptor set
  // and calling vkUpdateDescriptorSets for the textures/samplers on every draw
  // is the dominant CPU cost on the Adreno command thread; most consecutive
  // draws bind the exact same image views and samplers, so a fresh write is
  // wasted work. Build a signature of the would-be contents (image views and
  // samplers, plus counts which encode the descriptor set layout) and only mark
  // the descriptor set out of date - forcing the existing rewrite path below -
  // when the signature actually changes. The signature stores exact handles
  // (not a hash), so it is precise. When caching is disabled, fall back to the
  // original unconditional rewrite.
  if (native_render_path_active_) {
    // BRICK 1: texture/sampler slots were pushed as push constants above and the
    // global bindless set is bound once - nothing to sign, allocate, or write
    // here, and the set-2/set-3 value bits stay up-to-date (do NOT fall into the
    // else that clears them).
  } else if (cvars::vulkan_cache_texture_descriptors) {
    auto build_texture_signature =
        [this](bool is_vertex, uint32_t texture_count, uint32_t sampler_count,
               const std::vector<VulkanShader::TextureBinding>* textures,
               const std::vector<
                   std::pair<VulkanTextureCache::SamplerParameters, VkSampler>>&
                   samplers,
               std::vector<uint64_t>& signature_out) {
          signature_out.clear();
          signature_out.push_back((uint64_t(texture_count) << 32) |
                                  uint64_t(sampler_count));
          if (texture_count && textures) {
            for (const VulkanShader::TextureBinding& texture_binding :
                 *textures) {
              // RT-as-texture override (pixel only) takes precedence and MUST
              // match the descriptor write loop below exactly, or the descriptor
              // cache keeps a stale view. Vertex textures are never overridden.
              VkImageView view =
                  is_vertex
                      ? VK_NULL_HANDLE
                      : rt_as_texture_views_pixel_[texture_binding
                                                       .fetch_constant];
              if (view == VK_NULL_HANDLE) {
                view = (!is_vertex && feedback_merge_active_)
                           ? feedback_merge_producer_view_
                           : texture_cache_->GetActiveBindingOrNullImageView(
                                 texture_binding.fetch_constant,
                                 texture_binding.dimension,
                                 bool(texture_binding.is_signed));
              }
              signature_out.push_back(reinterpret_cast<uint64_t>(view));
            }
          }
          if (sampler_count) {
            for (const std::pair<VulkanTextureCache::SamplerParameters,
                                 VkSampler>& sampler_pair : samplers) {
              signature_out.push_back(
                  reinterpret_cast<uint64_t>(sampler_pair.second));
            }
          }
        };
    // Vertex/pixel textures/samplers. Build the signature into a REUSED scratch
    // buffer (build_texture_signature clears it but keeps its capacity) instead
    // of a fresh per-draw heap vector, and swap it into the stored signature on
    // a mismatch so the old buffer's capacity is recycled. Device-profiled
    // 2026-06-24: the per-draw std::vector alloc/realloc/free here (push_back ->
    // __push_back_slow_path -> __split_buffer) was a hot cost on the GPU
    // command-processor draw path. Behaviorally identical (the stored signatures
    // are the same); only the per-draw allocation is removed.
    if (texture_count_vertex || sampler_count_vertex) {
      build_texture_signature(true, texture_count_vertex, sampler_count_vertex,
                              &textures_vertex, current_samplers_vertex_,
                              texture_signature_scratch_);
      if (!texture_descriptor_signature_vertex_valid_ ||
          texture_signature_scratch_ != texture_descriptor_signature_vertex_) {
        current_graphics_descriptor_set_values_up_to_date_ &= ~(
            UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex);
        std::swap(texture_descriptor_signature_vertex_,
                  texture_signature_scratch_);
        texture_descriptor_signature_vertex_valid_ = true;
      }
    }
    if (texture_count_pixel || sampler_count_pixel) {
      build_texture_signature(false, texture_count_pixel, sampler_count_pixel,
                              textures_pixel, current_samplers_pixel_,
                              texture_signature_scratch_);
      if (!texture_descriptor_signature_pixel_valid_ ||
          texture_signature_scratch_ != texture_descriptor_signature_pixel_) {
        current_graphics_descriptor_set_values_up_to_date_ &= ~(
            UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel);
        std::swap(texture_descriptor_signature_pixel_,
                  texture_signature_scratch_);
        texture_descriptor_signature_pixel_valid_ = true;
      }
    }
  } else {
    current_graphics_descriptor_set_values_up_to_date_ &=
        ~((UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex) |
          (UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel));
  }

  // Capture - while the bit still reflects THIS draw's change - whether the pixel
  // texture descriptor set is UNCHANGED from the previous draw (only meaningful
  // with vulkan_cache_texture_descriptors on; otherwise the bit was just cleared
  // unconditionally above). Consumed by the stript_runlen histogram to isolate the
  // texture run-breaker and predict rank-5's standalone merge payoff.
  merge_draw_ptex_same_ =
      (current_graphics_descriptor_set_values_up_to_date_ &
       (UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel)) != 0;

  // Make sure new descriptor sets are bound to the command buffer.

  current_graphics_descriptor_sets_bound_up_to_date_ &=
      current_graphics_descriptor_set_values_up_to_date_;

  // Fill the texture and sampler write image infos.

  bool write_vertex_textures =
      (texture_count_vertex || sampler_count_vertex) &&
      !(current_graphics_descriptor_set_values_up_to_date_ &
        (UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex));
  bool write_pixel_textures =
      (texture_count_pixel || sampler_count_pixel) &&
      !(current_graphics_descriptor_set_values_up_to_date_ &
        (UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel));
  descriptor_write_image_info_.clear();
  descriptor_write_image_info_.reserve(
      (write_vertex_textures ? texture_count_vertex + sampler_count_vertex
                             : 0) +
      (write_pixel_textures ? texture_count_pixel + sampler_count_pixel : 0));
  size_t vertex_texture_image_info_offset = descriptor_write_image_info_.size();
  if (write_vertex_textures && texture_count_vertex) {
    for (const VulkanShader::TextureBinding& texture_binding :
         textures_vertex) {
      VkDescriptorImageInfo& descriptor_image_info =
          descriptor_write_image_info_.emplace_back();
      descriptor_image_info.imageView =
          texture_cache_->GetActiveBindingOrNullImageView(
              texture_binding.fetch_constant, texture_binding.dimension,
              bool(texture_binding.is_signed));
      descriptor_image_info.imageLayout =
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
  }
  size_t vertex_sampler_image_info_offset = descriptor_write_image_info_.size();
  if (write_vertex_textures && sampler_count_vertex) {
    for (const std::pair<VulkanTextureCache::SamplerParameters, VkSampler>&
             sampler_pair : current_samplers_vertex_) {
      VkDescriptorImageInfo& descriptor_image_info =
          descriptor_write_image_info_.emplace_back();
      descriptor_image_info.sampler = sampler_pair.second;
    }
  }
  size_t pixel_texture_image_info_offset = descriptor_write_image_info_.size();
  if (write_pixel_textures && texture_count_pixel) {
    for (const VulkanShader::TextureBinding& texture_binding :
         *textures_pixel) {
      VkDescriptorImageInfo& descriptor_image_info =
          descriptor_write_image_info_.emplace_back();
      // RT-as-texture override (gpu_rt_as_texture) takes precedence: sample the
      // resident resolve-source render target directly. Must match the signature
      // builder above exactly. Otherwise -
      // BD input-attachment merge: the variant reads the producer RT as an input
      // attachment, so bind the producer view (not the sampled guest texture).
      VkImageView view =
          rt_as_texture_views_pixel_[texture_binding.fetch_constant];
      if (view == VK_NULL_HANDLE) {
        view = feedback_merge_active_
                   ? feedback_merge_producer_view_
                   : texture_cache_->GetActiveBindingOrNullImageView(
                         texture_binding.fetch_constant,
                         texture_binding.dimension,
                         bool(texture_binding.is_signed));
      }
      descriptor_image_info.imageView = view;
      descriptor_image_info.imageLayout =
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
  }
  size_t pixel_sampler_image_info_offset = descriptor_write_image_info_.size();
  if (write_pixel_textures && sampler_count_pixel) {
    for (const std::pair<VulkanTextureCache::SamplerParameters, VkSampler>&
             sampler_pair : current_samplers_pixel_) {
      VkDescriptorImageInfo& descriptor_image_info =
          descriptor_write_image_info_.emplace_back();
      descriptor_image_info.sampler = sampler_pair.second;
    }
  }

  // Write the new descriptor sets.

  // Consecutive bindings updated via a single VkWriteDescriptorSet must have
  // identical stage flags, but for the constants they vary. Plus vertex and
  // pixel texture images and samplers.
  std::array<VkWriteDescriptorSet,
             SpirvShaderTranslator::kConstantBufferCount + 2 * 2>
      write_descriptor_sets;
  uint32_t write_descriptor_set_count = 0;
  uint32_t write_descriptor_set_bits = 0;
  assert_not_zero(
      current_graphics_descriptor_set_values_up_to_date_ &
      (UINT32_C(1)
       << SpirvShaderTranslator::kDescriptorSetSharedMemoryAndEdram));
  // Constant buffers.
  if (!(current_graphics_descriptor_set_values_up_to_date_ &
        (UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetConstants))) {
    if (constants_dynamic_descriptor_set_ != VK_NULL_HANDLE) {
      // R2 (arena): the persistent dynamic set already points at the constant
      // rings; this draw's data was written into the rings above and is selected
      // via pDynamicOffsets at bind time. No transient set alloc, no per-draw
      // vkUpdateDescriptorSets - just mark the constants value up to date.
      current_graphics_descriptor_sets_
          [SpirvShaderTranslator::kDescriptorSetConstants] =
              constants_dynamic_descriptor_set_;
      write_descriptor_set_bits |=
          UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetConstants;
    } else {
      VkDescriptorSet constants_descriptor_set;
      if (!constants_transient_descriptors_free_.empty()) {
        constants_descriptor_set = constants_transient_descriptors_free_.back();
        constants_transient_descriptors_free_.pop_back();
      } else {
        if (cvars::gpu_vulkan_float_constants_ssbo) {
          // Mixed set: 3 UNIFORM_BUFFER (system/bool/fetch) + 2 STORAGE_BUFFER
          // (float vertex/pixel) - matches the layout + write + SPIR-V.
          VkDescriptorPoolSize constants_counts[2] = {
              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
               SpirvShaderTranslator::kConstantBufferCount - 2},
              {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}};
          constants_descriptor_set =
              transient_descriptor_allocator_constants_mixed_.Allocate(
                  descriptor_set_layout_constants_, constants_counts, 2);
        } else {
          VkDescriptorPoolSize constants_descriptor_count;
          constants_descriptor_count.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
          constants_descriptor_count.descriptorCount =
              SpirvShaderTranslator::kConstantBufferCount;
          constants_descriptor_set =
              transient_descriptor_allocator_uniform_buffer_.Allocate(
                  descriptor_set_layout_constants_, &constants_descriptor_count,
                  1);
        }
        if (constants_descriptor_set == VK_NULL_HANDLE) {
          return false;
        }
      }
      constants_transient_descriptors_used_.emplace_back(
          frame_current_, constants_descriptor_set);
      // Consecutive bindings updated via a single VkWriteDescriptorSet must have
      // identical stage flags, but for the constants they vary.
      for (uint32_t i = 0; i < SpirvShaderTranslator::kConstantBufferCount;
           ++i) {
        VkWriteDescriptorSet& write_constants =
            write_descriptor_sets[write_descriptor_set_count++];
        write_constants.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_constants.pNext = nullptr;
        write_constants.dstSet = constants_descriptor_set;
        write_constants.dstBinding = i;
        write_constants.dstArrayElement = 0;
        write_constants.descriptorCount = 1;
        write_constants.descriptorType =
            (cvars::gpu_vulkan_float_constants_ssbo &&
             (i == SpirvShaderTranslator::kConstantBufferFloatVertex ||
              i == SpirvShaderTranslator::kConstantBufferFloatPixel))
                ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write_constants.pImageInfo = nullptr;
        write_constants.pBufferInfo = &current_constant_buffer_infos_[i];
        write_constants.pTexelBufferView = nullptr;
      }
      write_descriptor_set_bits |=
          UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetConstants;
      current_graphics_descriptor_sets_
          [SpirvShaderTranslator::kDescriptorSetConstants] =
              constants_descriptor_set;
    }
  }
  // Vertex shader textures and samplers.
  if (write_vertex_textures) {
    if (push_descriptors_active_) {
      // Push the texture/sampler descriptors inline - no transient set alloc, no
      // separate write+bind. The push is recorded into the deferred command
      // buffer before the draw; mark the set both value- and bound-up-to-date so
      // the transient write/bind paths below skip it.
      std::array<VkWriteDescriptorSet, 2> push_writes;
      uint32_t push_write_count = WritePushTextureBindings(
          texture_count_vertex, sampler_count_vertex,
          descriptor_write_image_info_.data() +
              vertex_texture_image_info_offset,
          descriptor_write_image_info_.data() +
              vertex_sampler_image_info_offset,
          push_writes.data());
      deferred_command_buffer_.CmdVkPushDescriptorSetKHR(
          VK_PIPELINE_BIND_POINT_GRAPHICS,
          current_guest_graphics_pipeline_layout_->GetPipelineLayout(),
          SpirvShaderTranslator::kDescriptorSetTexturesVertex, push_write_count,
          push_writes.data());
      current_graphics_descriptor_set_values_up_to_date_ |=
          UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex;
      current_graphics_descriptor_sets_bound_up_to_date_ |=
          UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex;
    } else {
      VkWriteDescriptorSet* write_textures =
          write_descriptor_sets.data() + write_descriptor_set_count;
      uint32_t texture_descriptor_set_write_count =
          WriteTransientTextureBindings(
              true, texture_count_vertex, sampler_count_vertex,
              current_guest_graphics_pipeline_layout_
                  ->descriptor_set_layout_textures_vertex_ref(),
              descriptor_write_image_info_.data() +
                  vertex_texture_image_info_offset,
              descriptor_write_image_info_.data() +
                  vertex_sampler_image_info_offset,
              write_textures);
      if (!texture_descriptor_set_write_count) {
        return false;
      }
      write_descriptor_set_count += texture_descriptor_set_write_count;
      write_descriptor_set_bits |=
          UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex;
      current_graphics_descriptor_sets_
          [SpirvShaderTranslator::kDescriptorSetTexturesVertex] =
              write_textures[0].dstSet;
    }
  }
  // Pixel shader textures and samplers.
  if (write_pixel_textures) {
    if (push_descriptors_active_) {
      std::array<VkWriteDescriptorSet, 2> push_writes;
      uint32_t push_write_count = WritePushTextureBindings(
          texture_count_pixel, sampler_count_pixel,
          descriptor_write_image_info_.data() + pixel_texture_image_info_offset,
          descriptor_write_image_info_.data() +
              pixel_sampler_image_info_offset,
          push_writes.data(), feedback_merge_active_);
      deferred_command_buffer_.CmdVkPushDescriptorSetKHR(
          VK_PIPELINE_BIND_POINT_GRAPHICS,
          current_guest_graphics_pipeline_layout_->GetPipelineLayout(),
          SpirvShaderTranslator::kDescriptorSetTexturesPixel, push_write_count,
          push_writes.data());
      current_graphics_descriptor_set_values_up_to_date_ |=
          UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel;
      current_graphics_descriptor_sets_bound_up_to_date_ |=
          UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel;
    } else {
      VkWriteDescriptorSet* write_textures =
          write_descriptor_sets.data() + write_descriptor_set_count;
      uint32_t texture_descriptor_set_write_count =
          WriteTransientTextureBindings(
              false, texture_count_pixel, sampler_count_pixel,
              current_guest_graphics_pipeline_layout_
                  ->descriptor_set_layout_textures_pixel_ref(),
              descriptor_write_image_info_.data() +
                  pixel_texture_image_info_offset,
              descriptor_write_image_info_.data() +
                  pixel_sampler_image_info_offset,
              write_textures);
      if (!texture_descriptor_set_write_count) {
        return false;
      }
      write_descriptor_set_count += texture_descriptor_set_write_count;
      write_descriptor_set_bits |=
          UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel;
      current_graphics_descriptor_sets_
          [SpirvShaderTranslator::kDescriptorSetTexturesPixel] =
              write_textures[0].dstSet;
    }
  }
  // Write.
  if (write_descriptor_set_count) {
    dfn.vkUpdateDescriptorSets(device, write_descriptor_set_count,
                               write_descriptor_sets.data(), 0, nullptr);
  }
  // Only make valid if all descriptor sets have been allocated and written
  // successfully.
  current_graphics_descriptor_set_values_up_to_date_ |=
      write_descriptor_set_bits;

  // Bind the new descriptor sets.
  uint32_t descriptor_sets_needed =
      (UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetCount) - 1;
  if (native_render_path_active_) {
    // BRICK 1: the single global bindless set occupies set 2 (bound once per
    // command buffer - kept out of bound-up-to-date at submission start, so the
    // loop below binds it on the first draw and skips it thereafter); set 3 is
    // unused.
    descriptor_sets_needed |=
        UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex;
    descriptor_sets_needed &=
        ~(UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel);
  } else {
    if (!texture_count_vertex && !sampler_count_vertex) {
      descriptor_sets_needed &=
          ~(UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesVertex);
    }
    if (!texture_count_pixel && !sampler_count_pixel) {
      descriptor_sets_needed &=
          ~(UINT32_C(1) << SpirvShaderTranslator::kDescriptorSetTexturesPixel);
    }
  }
  uint32_t descriptor_sets_remaining =
      descriptor_sets_needed &
      ~current_graphics_descriptor_sets_bound_up_to_date_;
  uint32_t descriptor_set_index;
  while (
      xe::bit_scan_forward(descriptor_sets_remaining, &descriptor_set_index)) {
    uint32_t descriptor_set_mask_tzcnt =
        xe::tzcnt(~(descriptor_sets_remaining |
                    ((UINT32_C(1) << descriptor_set_index) - 1)));
    // TODO(Triang3l): Bind to compute for memexport emulation without vertex
    // shader memory stores.
    // R2 (arena): when this contiguous range includes the constants set, pass its
    // per-draw dynamic offsets. The constants set is the ONLY set with dynamic
    // descriptors, so its kConstantBufferCount bindings consume all the offsets in
    // order, regardless of the range's other (non-dynamic) sets.
    const bool arena_constants_in_range =
        constants_dynamic_descriptor_set_ != VK_NULL_HANDLE &&
        descriptor_set_index <=
            uint32_t(SpirvShaderTranslator::kDescriptorSetConstants) &&
        uint32_t(SpirvShaderTranslator::kDescriptorSetConstants) <
            descriptor_set_mask_tzcnt;
    deferred_command_buffer_.CmdVkBindDescriptorSets(
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        current_guest_graphics_pipeline_layout_->GetPipelineLayout(),
        descriptor_set_index, descriptor_set_mask_tzcnt - descriptor_set_index,
        current_graphics_descriptor_sets_ + descriptor_set_index,
        arena_constants_in_range ? SpirvShaderTranslator::kConstantBufferCount
                                 : 0,
        arena_constants_in_range ? current_constant_dynamic_offsets_ : nullptr);
    ++draw_outcomes_descriptor_binds_;
    for (uint32_t set_i = descriptor_set_index;
         set_i < descriptor_set_mask_tzcnt && set_i < 4u; ++set_i) {
      ++mrw_ds_rebind_[set_i];
    }
    if (descriptor_set_mask_tzcnt >= 32) {
      break;
    }
    descriptor_sets_remaining &=
        ~((UINT32_C(1) << descriptor_set_mask_tzcnt) - 1);
  }
  current_graphics_descriptor_sets_bound_up_to_date_ |= descriptor_sets_needed;

  return true;
}

uint32_t VulkanCommandProcessor::WriteTransientTextureBindings(
    bool is_vertex, uint32_t texture_count, uint32_t sampler_count,
    VkDescriptorSetLayout descriptor_set_layout,
    const VkDescriptorImageInfo* texture_image_info,
    const VkDescriptorImageInfo* sampler_image_info,
    VkWriteDescriptorSet* descriptor_set_writes_out) {
  assert_true(frame_open_);
  if (!texture_count && !sampler_count) {
    return 0;
  }
  TextureDescriptorSetLayoutKey texture_descriptor_set_layout_key;
  texture_descriptor_set_layout_key.texture_count = texture_count;
  texture_descriptor_set_layout_key.sampler_count = sampler_count;
  texture_descriptor_set_layout_key.is_vertex = uint32_t(is_vertex);
  VkDescriptorSet texture_descriptor_set;
  auto textures_free_it = texture_transient_descriptor_sets_free_.find(
      texture_descriptor_set_layout_key);
  if (textures_free_it != texture_transient_descriptor_sets_free_.end() &&
      !textures_free_it->second.empty()) {
    texture_descriptor_set = textures_free_it->second.back();
    textures_free_it->second.pop_back();
  } else {
    std::array<VkDescriptorPoolSize, 2> texture_descriptor_counts;
    uint32_t texture_descriptor_counts_count = 0;
    if (texture_count) {
      VkDescriptorPoolSize& texture_descriptor_count =
          texture_descriptor_counts[texture_descriptor_counts_count++];
      texture_descriptor_count.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      texture_descriptor_count.descriptorCount = texture_count;
    }
    if (sampler_count) {
      VkDescriptorPoolSize& texture_descriptor_count =
          texture_descriptor_counts[texture_descriptor_counts_count++];
      texture_descriptor_count.type = VK_DESCRIPTOR_TYPE_SAMPLER;
      texture_descriptor_count.descriptorCount = sampler_count;
    }
    assert_not_zero(texture_descriptor_counts_count);
    texture_descriptor_set = transient_descriptor_allocator_textures_.Allocate(
        descriptor_set_layout, texture_descriptor_counts.data(),
        texture_descriptor_counts_count);
    if (texture_descriptor_set == VK_NULL_HANDLE) {
      return 0;
    }
  }
  UsedTextureTransientDescriptorSet& used_texture_descriptor_set =
      texture_transient_descriptor_sets_used_.emplace_back();
  used_texture_descriptor_set.frame = frame_current_;
  used_texture_descriptor_set.layout = texture_descriptor_set_layout_key;
  used_texture_descriptor_set.set = texture_descriptor_set;
  uint32_t descriptor_set_write_count = 0;
  if (texture_count) {
    VkWriteDescriptorSet& descriptor_set_write =
        descriptor_set_writes_out[descriptor_set_write_count++];
    descriptor_set_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_set_write.pNext = nullptr;
    descriptor_set_write.dstSet = texture_descriptor_set;
    descriptor_set_write.dstBinding = 0;
    descriptor_set_write.dstArrayElement = 0;
    descriptor_set_write.descriptorCount = texture_count;
    // BD input-attachment merge: pixel textures of the feedback consumer are
    // input attachments (read via subpassLoad), not sampled images.
    descriptor_set_write.descriptorType =
        (!is_vertex && feedback_merge_active_)
            ? VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
            : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptor_set_write.pImageInfo = texture_image_info;
    descriptor_set_write.pBufferInfo = nullptr;
    descriptor_set_write.pTexelBufferView = nullptr;
  }
  if (sampler_count) {
    VkWriteDescriptorSet& descriptor_set_write =
        descriptor_set_writes_out[descriptor_set_write_count++];
    descriptor_set_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_set_write.pNext = nullptr;
    descriptor_set_write.dstSet = texture_descriptor_set;
    descriptor_set_write.dstBinding = texture_count;
    descriptor_set_write.dstArrayElement = 0;
    descriptor_set_write.descriptorCount = sampler_count;
    descriptor_set_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptor_set_write.pImageInfo = sampler_image_info;
    descriptor_set_write.pBufferInfo = nullptr;
    descriptor_set_write.pTexelBufferView = nullptr;
  }
  assert_not_zero(descriptor_set_write_count);
  return descriptor_set_write_count;
}

uint32_t VulkanCommandProcessor::WritePushTextureBindings(
    uint32_t texture_count, uint32_t sampler_count,
    const VkDescriptorImageInfo* texture_image_info,
    const VkDescriptorImageInfo* sampler_image_info,
    VkWriteDescriptorSet* descriptor_set_writes_out, bool input_attachment) {
  // Builds VkWriteDescriptorSet entries for vkCmdPushDescriptorSetKHR: no
  // descriptor set is allocated, dstSet is left null (ignored by push).
  uint32_t descriptor_set_write_count = 0;
  if (texture_count) {
    VkWriteDescriptorSet& descriptor_set_write =
        descriptor_set_writes_out[descriptor_set_write_count++];
    descriptor_set_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_set_write.pNext = nullptr;
    descriptor_set_write.dstSet = VK_NULL_HANDLE;
    descriptor_set_write.dstBinding = 0;
    descriptor_set_write.dstArrayElement = 0;
    descriptor_set_write.descriptorCount = texture_count;
    // BD input-attachment merge: feedback consumer's pixel textures are input
    // attachments (subpassLoad), not sampled images.
    descriptor_set_write.descriptorType =
        input_attachment ? VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT
                         : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    descriptor_set_write.pImageInfo = texture_image_info;
    descriptor_set_write.pBufferInfo = nullptr;
    descriptor_set_write.pTexelBufferView = nullptr;
  }
  if (sampler_count) {
    VkWriteDescriptorSet& descriptor_set_write =
        descriptor_set_writes_out[descriptor_set_write_count++];
    descriptor_set_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_set_write.pNext = nullptr;
    descriptor_set_write.dstSet = VK_NULL_HANDLE;
    descriptor_set_write.dstBinding = texture_count;
    descriptor_set_write.dstArrayElement = 0;
    descriptor_set_write.descriptorCount = sampler_count;
    descriptor_set_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    descriptor_set_write.pImageInfo = sampler_image_info;
    descriptor_set_write.pBufferInfo = nullptr;
    descriptor_set_write.pTexelBufferView = nullptr;
  }
  return descriptor_set_write_count;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
