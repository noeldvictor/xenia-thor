/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_
#define XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_

#include <array>
#include <climits>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/hash.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/draw_extent_estimator.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/gpu/vulkan/vulkan_pipeline_cache.h"
#include "xenia/gpu/vulkan/vulkan_primitive_processor.h"
#include "xenia/gpu/vulkan/vulkan_render_target_cache.h"
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/vulkan/vulkan_shared_memory.h"
#include "xenia/gpu/vulkan/vulkan_texture_cache.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/ui/vulkan/linked_type_descriptor_set_allocator.h"
#include "xenia/ui/vulkan/vulkan_gpu_completion_timeline.h"
#include "xenia/ui/vulkan/vulkan_presenter.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_dynamic_buffer_ring.h"
#include "xenia/ui/vulkan/vulkan_upload_buffer_pool.h"

namespace xe {
namespace gpu {
namespace vulkan {

class BdNativeRenderer;

class VulkanCommandProcessor : public CommandProcessor {
 public:
  // Single-descriptor layouts for use within a single frame.
  enum class SingleTransientDescriptorLayout {
    kStorageBufferCompute,
    kCount,
  };

  class ScratchBufferAcquisition {
   public:
    explicit ScratchBufferAcquisition() = default;
    explicit ScratchBufferAcquisition(VulkanCommandProcessor& command_processor,
                                      VkBuffer buffer,
                                      VkPipelineStageFlags stage_mask,
                                      VkAccessFlags access_mask)
        : command_processor_(&command_processor),
          buffer_(buffer),
          stage_mask_(stage_mask),
          access_mask_(access_mask) {}

    ScratchBufferAcquisition(const ScratchBufferAcquisition& acquisition) =
        delete;
    ScratchBufferAcquisition& operator=(
        const ScratchBufferAcquisition& acquisition) = delete;

    ScratchBufferAcquisition(ScratchBufferAcquisition&& acquisition) {
      command_processor_ = acquisition.command_processor_;
      buffer_ = acquisition.buffer_;
      stage_mask_ = acquisition.stage_mask_;
      access_mask_ = acquisition.access_mask_;
      acquisition.command_processor_ = nullptr;
      acquisition.buffer_ = VK_NULL_HANDLE;
      acquisition.stage_mask_ = 0;
      acquisition.access_mask_ = 0;
    }
    ScratchBufferAcquisition& operator=(
        ScratchBufferAcquisition&& acquisition) {
      if (this == &acquisition) {
        return *this;
      }
      command_processor_ = acquisition.command_processor_;
      buffer_ = acquisition.buffer_;
      stage_mask_ = acquisition.stage_mask_;
      access_mask_ = acquisition.access_mask_;
      acquisition.command_processor_ = nullptr;
      acquisition.buffer_ = VK_NULL_HANDLE;
      acquisition.stage_mask_ = 0;
      acquisition.access_mask_ = 0;
      return *this;
    }

    ~ScratchBufferAcquisition() {
      if (buffer_ != VK_NULL_HANDLE) {
        assert_true(command_processor_->scratch_buffer_used_);
        assert_true(command_processor_->scratch_buffer_ == buffer_);
        command_processor_->scratch_buffer_last_stage_mask_ = stage_mask_;
        command_processor_->scratch_buffer_last_access_mask_ = access_mask_;
        command_processor_->scratch_buffer_last_usage_submission_ =
            command_processor_->GetCurrentSubmission();
        command_processor_->scratch_buffer_used_ = false;
      }
    }

    // VK_NULL_HANDLE if failed to acquire or if moved.
    VkBuffer buffer() const { return buffer_; }

    VkPipelineStageFlags GetStageMask() const { return stage_mask_; }
    VkPipelineStageFlags SetStageMask(VkPipelineStageFlags new_stage_mask) {
      VkPipelineStageFlags old_stage_mask = stage_mask_;
      stage_mask_ = new_stage_mask;
      return old_stage_mask;
    }
    VkAccessFlags GetAccessMask() const { return access_mask_; }
    VkAccessFlags SetAccessMask(VkAccessFlags new_access_mask) {
      VkAccessFlags old_access_mask = access_mask_;
      access_mask_ = new_access_mask;
      return old_access_mask;
    }

   private:
    VulkanCommandProcessor* command_processor_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkPipelineStageFlags stage_mask_ = 0;
    VkAccessFlags access_mask_ = 0;
  };

  VulkanCommandProcessor(VulkanGraphicsSystem* graphics_system,
                         kernel::KernelState* kernel_state);
  ~VulkanCommandProcessor();

  void ClearCaches() override;

  void TracePlaybackWroteMemory(uint32_t base_ptr, uint32_t length) override;

  void RestoreEdramSnapshot(const void* snapshot) override;

  ui::vulkan::VulkanDevice* GetVulkanDevice() const {
    return static_cast<const ui::vulkan::VulkanProvider*>(
               graphics_system_->provider())
        ->vulkan_device();
  }

  // Returns the deferred drawing command list for the currently open
  // submission.
  DeferredCommandBuffer& deferred_command_buffer() {
    assert_true(submission_open_);
    return deferred_command_buffer_;
  }

  bool submission_open() const { return submission_open_; }
  uint64_t GetCurrentSubmission() const {
    return completion_timeline_.GetUpcomingSubmission();
  }
  uint64_t GetCompletedSubmission() const {
    return completion_timeline_.GetCompletedSubmissionFromLastUpdate();
  }
  // Blocks (polling fences) until the given submission has completed on the GPU.
  // Safe to call with any submission index: it clamps to a submission that can
  // actually complete (ending the open submission if asked to wait on the
  // current/upcoming one). Used by the UMA direct-write shared-memory path to
  // avoid overwriting pages a still-in-flight submission is reading.
  void AwaitSubmissionCompletion(uint64_t await_submission) {
    CheckSubmissionCompletionAndDeviceLoss(await_submission);
  }

  // Sparse binds are:
  // - In a single submission, all submitted in one vkQueueBindSparse.
  // - Sent to the queue without waiting for a semaphore.
  // Thus, multiple sparse binds between the completed and the current
  // submission, and within one submission, must not touch any overlapping
  // memory regions.
  void SparseBindBuffer(VkBuffer buffer, uint32_t bind_count,
                        const VkSparseMemoryBind* binds,
                        VkPipelineStageFlags wait_stage_mask);

  uint64_t GetCurrentFrame() const { return frame_current_; }
  uint64_t GetCompletedFrame() const { return frame_completed_; }

  // Submission must be open to insert barriers. If no pipeline stages access
  // the resource in a synchronization scope, the stage masks should be 0 (top /
  // bottom of pipe should be specified only if explicitly needed). Returning
  // true if the barrier has actually been inserted and not dropped.
  bool PushBufferMemoryBarrier(
      VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
      VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
      VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
      uint32_t src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
      uint32_t dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
      bool skip_if_equal = true);
  bool PushImageMemoryBarrier(
      VkImage image, const VkImageSubresourceRange& subresource_range,
      VkPipelineStageFlags src_stage_mask, VkPipelineStageFlags dst_stage_mask,
      VkAccessFlags src_access_mask, VkAccessFlags dst_access_mask,
      VkImageLayout old_layout, VkImageLayout new_layout,
      uint32_t src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
      uint32_t dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
      bool skip_if_equal = true);
  // Returns whether any barriers have been submitted - if true is returned, the
  // render pass will also be closed.
  bool SubmitBarriers(bool force_end_render_pass);

  // gpu_trace_resolve_timing: classifies each per-pass GPU-timestamp bracket so
  // the readback can split the frame's GPU time into guest-geometry vs deferred
  // EDRAM (transfer / resolve / store) work. kGuest..kResolveClear are render
  // passes; kResolveCopyDispatch..kHostDepthStoreDispatch are compute dispatches
  // bracketed in the inter-pass gaps.
  enum class GpuPassKind : uint8_t {
    kGuest = 0,
    kEdramTransfer = 1,
    kResolveClear = 2,
    kResolveCopyDispatch = 3,
    kResolveClearDispatch = 4,
    kHostDepthStoreDispatch = 5,
    // Full-screen post-process COMPOSITE consumer guest passes (rect/quad whose
    // pixel shader samples a producer RT) - split out of kGuest so
    // gpu_trace_resolve_timing measures exactly the tile-I/O brick 2 (composite
    // ->compute) would remove. Timing classification only; renders identically.
    kGuestComposite = 6,
    kCount
  };
  // Brackets an EDRAM resolve / clear / host-depth-store COMPUTE dispatch with a
  // GPU timestamp pair (reusing the per-pass pool) so its time is attributed out
  // of the inter-pass gap. No-op unless gpu_trace_resolve_timing AND the per-pass
  // timestamp machinery are both on -> byte-identical when off. Call begin before
  // the dispatch, end after.
  void RecordResolveTimingBracket(bool is_begin, GpuPassKind kind);

  // If not started yet, begins a render pass from the render target cache.
  // Submission must be open.
  void SubmitBarriersAndEnterRenderTargetCacheRenderPass(
      VkRenderPass render_pass,
      const VulkanRenderTargetCache::Framebuffer* framebuffer,
      GpuPassKind pass_kind = GpuPassKind::kGuest);
  // Must be called before doing anything outside the render pass scope,
  // including adding pipeline barriers that are not a part of the render pass
  // scope. Submission must be open.
  void EndRenderPass();
  // gpu_vulkan_retro_depth_none: hindsight-patch the ending pass's recorded
  // begin to the depth DONT_CARE/STORE_OP_NONE variant if no draw used depth.
  // Called from EndRenderPass AND the direct CmdVkEndRenderPass (RT-change) path.
  void RetroPatchDepthNoneAtPassEnd();

  VkDescriptorSetLayout GetSingleTransientDescriptorLayout(
      SingleTransientDescriptorLayout transient_descriptor_layout) const {
    return descriptor_set_layouts_single_transient_[size_t(
        transient_descriptor_layout)];
  }
  // A frame must be open.
  VkDescriptorSet AllocateSingleTransientDescriptor(
      SingleTransientDescriptorLayout transient_descriptor_layout);

  // The returned reference is valid until a cache clear.
  VkDescriptorSetLayout GetTextureDescriptorSetLayout(
      bool is_vertex, size_t texture_count, size_t sampler_count,
      bool input_attachment = false);
  // The returned reference is valid until a cache clear.
  const VulkanPipelineCache::PipelineLayoutProvider* GetPipelineLayout(
      size_t texture_count_pixel, size_t sampler_count_pixel,
      size_t texture_count_vertex, size_t sampler_count_vertex,
      bool pixel_textures_input_attachment = false);

  // BRICK 1 native bindless render path (gpu_native_render_path). True when the
  // cvar is set AND the device enabled the descriptor-indexing features + the
  // global bindless descriptor set was created successfully. Kept in sync with
  // SpirvShaderTranslator::Features::bindless_textures.
  bool native_render_path_active() const { return native_render_path_active_; }
  // Called by the texture cache when it destroys an image view / sampler so the
  // owning global bindless slot can be freed for reuse. Post-await (the texture
  // cache only destroys after the referencing submission completed), so no
  // in-flight command still references the freed slot. No-ops when the native
  // path is inactive.
  void ReleaseBindlessImageView(VkImageView image_view);
  void ReleaseBindlessSampler(VkSampler sampler);

  // BD input-attachment merge: whether the active merge consumer uses the
  // 1-attachment in-place feedback render pass (read by the pipeline cache to
  // pick the matching feedback render pass for the consumer's subpass-1 pipeline).
  bool feedback_merge_in_place() const { return feedback_merge_in_place_; }

  // Returns a single temporary GPU-side buffer within a submission for tasks
  // like texture untiling and resolving. May push a buffer memory barrier into
  // the initial usage. Submission must be open.
  ScratchBufferAcquisition AcquireScratchGpuBuffer(
      VkDeviceSize size, VkPipelineStageFlags initial_stage_mask,
      VkAccessFlags initial_access_mask);

  // Binds a graphics pipeline for host-specific purposes, invalidating the
  // affected state. keep_dynamic_* must be false (to invalidate the dynamic
  // state after binding the pipeline with the same state being static, or if
  // the caller changes the dynamic state bypassing the VulkanCommandProcessor)
  // unless the caller has these state variables as dynamic and uses the
  // tracking in VulkanCommandProcessor to modify them.
  void BindExternalGraphicsPipeline(VkPipeline pipeline,
                                    bool keep_dynamic_depth_bias = false,
                                    bool keep_dynamic_blend_constants = false,
                                    bool keep_dynamic_stencil_mask_ref = false);
  void BindExternalComputePipeline(VkPipeline pipeline);
  void SetViewport(const VkViewport& viewport);
  void SetScissor(const VkRect2D& scissor);

  // Returns the text to display in the GPU backend name in the window title.
  std::string GetWindowTitleText() const;

 protected:
  bool SetupContext() override;
  void ShutdownContext() override;

  void WriteRegister(uint32_t index, uint32_t value) override;

  void OnGammaRamp256EntryTableValueWritten() override;
  void OnGammaRampPWLValueWritten() override;

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width,
                 uint32_t frontbuffer_height, uint32_t display_width,
                 uint32_t display_height) override;

  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address,
                     const uint32_t* host_address,
                     uint32_t dword_count) override;

  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count,
                 IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override;
  bool IssueCopy() override;

  // Blue Dragon native-draw HLE: mirror of the D3D12 IssueDraw-entry counter so
  // gpu_bd_native_hle's issuedraw_delta is a REAL signal on the Vulkan/Thor
  // backend. The base CommandProcessor returns a constant 0 here, which made
  // EVERY synthetic emit on the Thor read as issuedraw_delta=0 even when the
  // draw genuinely reached IssueDraw (a pure measurement gap, not the draw
  // failing to execute). Incremented at the top of IssueDraw (entry count).
  uint32_t BdDebugIssueDrawCount() const override { return bd_issuedraw_count_; }

  // Blue Dragon native-draw HLE (Half B / decoupled present): armed by the base
  // CommandProcessor around the synthetic native draw whose RB_COLOR_INFO was
  // redirected to a non-aliasing EDRAM base. While armed, the next IssueDraw
  // remembers the dedicated full-surface color host RT it renders into, so
  // IssueSwap can present it (mirrors D3D12CommandProcessor::BdArmDecoupled
  // Capture, but here the RT is presented, not just read back to a PNG).
  void BdArmDecoupledCapture(bool armed) override;

  void InitializeTrace() override;

 private:
  friend class VulkanRenderTargetCache;

  // Blue Dragon native-draw HLE: IssueDraw-entry counter backing
  // BdDebugIssueDrawCount() (see the public override). Bracketed around the
  // synthetic-PM4 emit in the base CommandProcessor to prove the native draw
  // reaches IssueDraw on the Thor.
  uint32_t bd_issuedraw_count_ = 0;

  // Blue Dragon native-draw HLE decoupled present (gpu_bd_hle_present_decoupled).
  // bd_capture_armed_ is set by BdArmDecoupledCapture around the synthetic draw;
  // while armed, the covered IssueDraw asks the RT cache to latch the bound
  // decoupled color host RT (the RT cache owns the RenderTarget* - it is a
  // protected nested type the command processor cannot name). IssueSwap presents
  // that RT and the RT cache clears the latch each frame.
  bool bd_capture_armed_ = false;

  struct SharedMemoryReadbackStats {
    uint32_t samples = 0;
    uint32_t nonzero_samples = 0;
    uint32_t varying_samples = 0;
    uint32_t first_sample_value = 0;
    uint32_t first_sample_matches = 0;
    uint32_t first_nonzero_value = 0;
    bool low_variation = false;
    bool clear_like = false;
    uint64_t checksum = 0;
    uint32_t score = 0;
  };

  bool ReadbackSharedMemoryRange(uint32_t address, uint32_t length,
                                 const char* label, bool log_checksum,
                                 bool copy_to_guest,
                                 SharedMemoryReadbackStats* stats = nullptr);
  bool TraceTextureSourceChecksums(uint32_t used_texture_mask,
                                   const char* stage_label,
                                   uint64_t shader_hash);
  void TraceShaderConstants(const VulkanShader& shader, const char* stage_label,
                            bool is_pixel_shader);
  void TraceVertexFetchSources(const VulkanShader& shader,
                               uint32_t host_draw_vertex_count);

  struct PresentResolveCandidate {
    uint32_t address = 0;
    uint32_t length = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t pitch = 0;
    xenos::TextureFormat format = xenos::TextureFormat::k_8_8_8_8;
    uint64_t sequence = 0;
    uint32_t score = 0;
    uint32_t nonzero_samples = 0;
    uint32_t varying_samples = 0;
    uint32_t first_sample_value = 0;
    uint32_t first_sample_matches = 0;
    uint32_t first_nonzero_value = 0;
    bool low_variation = false;
    bool clear_like = false;
    uint64_t checksum = 0;
  };

  uint64_t trace_draw_sequence_ = 0;
  uint64_t trace_copy_sequence_ = 0;
  uint64_t trace_last_draw_sequence_ = 0;
  uint64_t trace_last_draw_vs_hash_ = 0;
  uint64_t trace_last_draw_ps_hash_ = 0;

  struct CommandBuffer {
    VkCommandPool pool;
    VkCommandBuffer buffer;
  };

  struct SparseBufferBind {
    VkBuffer buffer;
    size_t bind_offset;
    uint32_t bind_count;
  };

  union TextureDescriptorSetLayoutKey {
    uint32_t key;
    struct {
      // If texture and sampler counts are both 0, use
      // descriptor_set_layout_empty_ instead as these are owning references.
      uint32_t texture_count : 16;
      uint32_t sampler_count : 14;
      // BD input-attachment merge: the texture bindings are VK_DESCRIPTOR_TYPE_
      // INPUT_ATTACHMENT (the feedback variant reads the producer RT via
      // subpassLoad) instead of SAMPLED_IMAGE. Pixel sets only.
      uint32_t pixel_textures_input_attachment : 1;
      uint32_t is_vertex : 1;
    };

    TextureDescriptorSetLayoutKey() : key(0) {
      static_assert_size(*this, sizeof(key));
    }

    struct Hasher {
      size_t operator()(const TextureDescriptorSetLayoutKey& key) const {
        return std::hash<decltype(key.key)>{}(key.key);
      }
    };
    bool operator==(const TextureDescriptorSetLayoutKey& other_key) const {
      return key == other_key.key;
    }
    bool operator!=(const TextureDescriptorSetLayoutKey& other_key) const {
      return !(*this == other_key);
    }
  };

  union PipelineLayoutKey {
    uint64_t key;
    struct {
      // Pixel textures in the low bits since those are varied much more
      // commonly.
      uint16_t texture_count_pixel;
      uint16_t sampler_count_pixel;
      uint16_t texture_count_vertex;
      uint16_t sampler_count_vertex;
    };

    PipelineLayoutKey() : key(0) { static_assert_size(*this, sizeof(key)); }

    struct Hasher {
      size_t operator()(const PipelineLayoutKey& key) const {
        return std::hash<decltype(key.key)>{}(key.key);
      }
    };
    bool operator==(const PipelineLayoutKey& other_key) const {
      return key == other_key.key;
    }
    bool operator!=(const PipelineLayoutKey& other_key) const {
      return !(*this == other_key);
    }
  };

  class PipelineLayout : public VulkanPipelineCache::PipelineLayoutProvider {
   public:
    explicit PipelineLayout(
        VkPipelineLayout pipeline_layout,
        VkDescriptorSetLayout descriptor_set_layout_textures_vertex_ref,
        VkDescriptorSetLayout descriptor_set_layout_textures_pixel_ref)
        : pipeline_layout_(pipeline_layout),
          descriptor_set_layout_textures_vertex_ref_(
              descriptor_set_layout_textures_vertex_ref),
          descriptor_set_layout_textures_pixel_ref_(
              descriptor_set_layout_textures_pixel_ref) {}
    VkPipelineLayout GetPipelineLayout() const override {
      return pipeline_layout_;
    }
    VkDescriptorSetLayout descriptor_set_layout_textures_vertex_ref() const {
      return descriptor_set_layout_textures_vertex_ref_;
    }
    VkDescriptorSetLayout descriptor_set_layout_textures_pixel_ref() const {
      return descriptor_set_layout_textures_pixel_ref_;
    }

   private:
    VkPipelineLayout pipeline_layout_;
    VkDescriptorSetLayout descriptor_set_layout_textures_vertex_ref_;
    VkDescriptorSetLayout descriptor_set_layout_textures_pixel_ref_;
  };

  struct UsedSingleTransientDescriptor {
    uint64_t frame;
    SingleTransientDescriptorLayout layout;
    VkDescriptorSet set;
  };

  struct UsedTextureTransientDescriptorSet {
    uint64_t frame;
    TextureDescriptorSetLayoutKey layout;
    VkDescriptorSet set;
  };

  enum SwapApplyGammaDescriptorSet : uint32_t {
    kSwapApplyGammaDescriptorSetRamp,
    kSwapApplyGammaDescriptorSetSource,

    kSwapApplyGammaDescriptorSetCount,
  };

  // Framebuffer for the current presenter's guest output image revision, and
  // its usage tracking.
  struct SwapFramebuffer {
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    uint64_t version = UINT64_MAX;
    uint64_t last_submission = 0;
  };

  // BeginSubmission and EndSubmission may be called at any time. If there's an
  // open non-frame submission, BeginSubmission(true) will promote it to a
  // frame. EndSubmission(true) will close the frame no matter whether the
  // submission has already been closed.
  // Unlike on Direct3D 12, submission boundaries do not imply any memory
  // barriers aside from an incoming host write (but not outgoing host read)
  // dependency.

  // Rechecks submission number and reclaims per-submission resources. Pass 0 as
  // the submission to await to simply check status, or pass
  // GetCurrentSubmission() to wait for all queue operations to be completed.
  void CheckSubmissionCompletionAndDeviceLoss(uint64_t await_submission);
  // If is_guest_command is true, a new full frame - with full cleanup of
  // resources and, if needed, starting capturing - is opened if pending (as
  // opposed to simply resuming after mid-frame synchronization). Returns
  // whether a submission is open currently and the device is not lost.
  bool BeginSubmission(bool is_guest_command);
  // If is_swap is true, a full frame is closed - with, if needed, cache
  // clearing and stopping capturing. Returns whether the submission was done
  // successfully, if it has failed, leaves it open.
  bool EndSubmission(bool is_swap);
  bool AwaitAllQueueOperationsCompletion() {
    CheckSubmissionCompletionAndDeviceLoss(GetCurrentSubmission());
    return !submission_open_ &&
           GetCompletedSubmission() + 1u >= GetCurrentSubmission();
  }

  void ClearTransientDescriptorPools();

  void SplitPendingBarrier();

  void DestroyScratchBuffer();

  void UpdateDynamicState(const draw_util::ViewportInfo& viewport_info,
                          bool primitive_polygonal,
                          reg::RB_DEPTHCONTROL normalized_depth_control,
                          xenos::PrimitiveType host_primitive_type,
                          bool host_primitive_reset_enabled,
                          uint32_t normalized_color_mask);

  // Opaque depth pre-pass (gpu_opaque_depth_prepass), Unit 3: emit a
  // self-contained copy of the current opaque draw into prepass_command_buffer_
  // (reuses the current pipeline + the already-current dynamic-state members).
  void EmitOpaquePrepassDraw(VkBuffer index_buffer, VkDeviceSize index_offset,
                             VkIndexType index_type, uint32_t index_count);

  // Lever 2 (vulkan_merge_draws): emit the accumulated draw-concatenation run as
  // one CmdVkBindIndexBuffer + one CmdVkDrawIndexed, then clear the pending run.
  // No-op when no run is pending. Must be called at every flush point before any
  // command that depends on prior draws having executed.
  void FlushPendingMergeRun();

  // Lever 2b (vulkan_merge_draws_indirect): emit the accumulated MDI run as one
  // CmdVkBindIndexBuffer (at offset 0) + one CmdVkDrawIndexedIndirect over the
  // per-frame indirect buffer (or a single CmdVkDrawIndexed for a 1-draw run),
  // then clear it. No-op when no run is pending. Called from FlushPendingMergeRun()
  // so every existing flush point covers both levers.
  void FlushPendingMergeRunIndirect();

  // Front B (gpu_trace_cullable_tris, READ-ONLY): count how many triangles of this
  // draw a CPU-side cull WOULD drop. Mutates nothing. C1 returns 0 (scaffolding);
  // C2/C3 add the ShaderInterpreter VS-position replay + exact backface/frustum.
  uint32_t CountCullableTriangles(const Shader& vertex_shader);
  void UpdateSystemConstantValues(
      bool primitive_polygonal,
      const PrimitiveProcessor::ProcessingResult& primitive_processing_result,
      bool shader_32bit_index_dma, const draw_util::ViewportInfo& viewport_info,
      uint32_t used_texture_mask, reg::RB_DEPTHCONTROL normalized_depth_control,
      uint32_t normalized_color_mask);
  bool UpdateBindings(const VulkanShader* vertex_shader,
                      const VulkanShader* pixel_shader);
  // Allocates a descriptor set and fills one or two VkWriteDescriptorSet
  // structure instances (for images and samplers).
  // The descriptor set layout must be the one for the given is_vertex,
  // texture_count, sampler_count (from GetTextureDescriptorSetLayout - may be
  // already available at the moment of the call, no need to locate it again).
  // Returns how many VkWriteDescriptorSet structure instances have been
  // written, or 0 if there was a failure to allocate the descriptor set or no
  // bindings were requested.
  uint32_t WriteTransientTextureBindings(
      bool is_vertex, uint32_t texture_count, uint32_t sampler_count,
      VkDescriptorSetLayout descriptor_set_layout,
      const VkDescriptorImageInfo* texture_image_info,
      const VkDescriptorImageInfo* sampler_image_info,
      VkWriteDescriptorSet* descriptor_set_writes_out);

  // Like WriteTransientTextureBindings but for push descriptors: builds the
  // write array only (no descriptor set allocation, dstSet null).
  uint32_t WritePushTextureBindings(
      uint32_t texture_count, uint32_t sampler_count,
      const VkDescriptorImageInfo* texture_image_info,
      const VkDescriptorImageInfo* sampler_image_info,
      VkWriteDescriptorSet* descriptor_set_writes_out,
      bool input_attachment = false);

  bool device_lost_ = false;

  bool cache_clear_requested_ = false;

  // Host shader types that guest shaders can be translated into - they can
  // access the shared memory (via vertex fetch, memory export, or manual index
  // buffer reading) and textures.
  VkPipelineStageFlags guest_shader_pipeline_stages_ = 0;
  VkShaderStageFlags guest_shader_vertex_stages_ = 0;

  std::vector<VkSemaphore> semaphores_free_;

  ui::vulkan::VulkanGPUCompletionTimeline completion_timeline_;
  bool submission_open_ = false;
  // In case vkQueueSubmit fails after something like a successful
  // vkQueueBindSparse, to wait correctly on the next attempt.
  std::vector<VkSemaphore> current_submission_wait_semaphores_;
  std::vector<VkPipelineStageFlags> current_submission_wait_stage_masks_;
  std::deque<std::pair<uint64_t, VkSemaphore>>
      submissions_in_flight_semaphores_;

  static constexpr uint32_t kMaxFramesInFlight = 3;
  bool frame_open_ = false;
  // Guest frame index, since some transient resources can be reused across
  // submissions. Values updated in the beginning of a frame.
  uint64_t frame_current_ = 1;
  uint64_t frame_completed_ = 0;
  // Submission indices of frames that have already been submitted.
  uint64_t closed_frame_submissions_[kMaxFramesInFlight] = {};

  // <Submission where last used, resource>, sorted by the submission number.
  std::deque<std::pair<uint64_t, VkDeviceMemory>> destroy_memory_;
  std::deque<std::pair<uint64_t, VkBuffer>> destroy_buffers_;
  std::deque<std::pair<uint64_t, VkFramebuffer>> destroy_framebuffers_;

  std::vector<CommandBuffer> command_buffers_writable_;
  std::deque<std::pair<uint64_t, CommandBuffer>> command_buffers_submitted_;
  DeferredCommandBuffer deferred_command_buffer_;

  // Opaque depth pre-pass (gpu_opaque_depth_prepass). During a render pass, the
  // opaque draws' depth-only commands are recorded here; at EndRenderPass this
  // stream is spliced into the main deferred buffer right after BeginRenderPass
  // (at prepass_insert_pos_), so the color stream early-Z-rejects against the
  // primed opaque depth. prepass_active_ is set between Begin/EndRenderPass when
  // the cvar is on. Unit 3 (the opaque-draw capture) fills this; until then it
  // stays empty and the splice is a no-op.
  DeferredCommandBuffer prepass_command_buffer_;
  size_t prepass_insert_pos_ = 0;
  bool prepass_active_ = false;

  std::vector<VkSparseMemoryBind> sparse_memory_binds_;
  std::vector<SparseBufferBind> sparse_buffer_binds_;
  // SparseBufferBind converted to VkSparseBufferMemoryBindInfo to this buffer
  // on submission (because pBinds should point to a place in std::vector, but
  // it may be reallocated).
  std::vector<VkSparseBufferMemoryBindInfo> sparse_buffer_bind_infos_temp_;
  VkPipelineStageFlags sparse_bind_wait_stage_mask_ = 0;

  // Temporary storage with reusable memory for creating descriptor set layouts.
  std::vector<VkDescriptorSetLayoutBinding> descriptor_set_layout_bindings_;
  // Temporary storage with reusable memory for writing image and sampler
  // descriptors.
  std::vector<VkDescriptorImageInfo> descriptor_write_image_info_;

  std::unique_ptr<ui::vulkan::VulkanUploadBufferPool> uniform_buffer_pool_;
  // Lever 2b (vulkan_merge_draws_indirect): per-frame ring holding the
  // VkDrawIndexedIndirectCommand[] arrays consumed by vkCmdDrawIndexedIndirect.
  // Only allocated when the lever is enabled and multiDrawIndirect is supported.
  std::unique_ptr<ui::vulkan::VulkanUploadBufferPool> indirect_buffer_pool_;
  // Lever 2 cull (gpu_cull_compaction): per-frame ring of transient INDEX buffers
  // that hold the per-draw guest indices copied out of shared memory (Step 1 copies
  // them verbatim; the eventual cull writes a compacted subset). Created at init so
  // the cvar is live-toggleable.
  std::unique_ptr<ui::vulkan::VulkanUploadBufferPool> cull_index_buffer_pool_;

  // R2 (vulkan_dynamic_constants_arena): persistent, per-frame-segmented UMA ring
  // arena for the kConstantBufferCount guest draw constant buffers. The eventual
  // bind path writes constants in place and binds them once via a
  // UNIFORM_BUFFER_DYNAMIC descriptor set with per-draw pDynamicOffsets, instead
  // of allocating a transient descriptor set per draw. Only Initialize()d when the
  // cvar is on; default-off leaves these invalid and keeps uniform_buffer_pool_
  // (byte-identical to today). is_valid() gates the whole arena path.
  std::array<ui::vulkan::VulkanDynamicBufferRing,
             size_t(SpirvShaderTranslator::kConstantBufferCount)>
      dynamic_constants_rings_;
  // R2: a UNIFORM_BUFFER_DYNAMIC variant of descriptor_set_layout_constants_ plus
  // one persistent descriptor set bound once to dynamic_constants_rings_ (the
  // per-draw dynamic offset selects each draw's constant slot). All VK_NULL_HANDLE
  // unless the arena is active. The set is freed implicitly with its pool.
  VkDescriptorSetLayout descriptor_set_layout_constants_dynamic_ = VK_NULL_HANDLE;
  VkDescriptorPool constants_dynamic_descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet constants_dynamic_descriptor_set_ = VK_NULL_HANDLE;
  // R2: the per-draw dynamic offset into each ring for the current draw's
  // constant slot, passed as pDynamicOffsets when binding the dynamic set.
  uint32_t current_constant_dynamic_offsets_
      [SpirvShaderTranslator::kConstantBufferCount] = {};

  // G1-lite (gpu_binning_deinterleave_pos): persistent per-frame-segmented
  // ring holding the per-draw compact de-interleaved position streams (raw
  // guest dwords, ascending word order, popcount(needed_words) per element).
  // Bound once at the set-0 compact binding; the shader offsets into it with
  // SystemConstants compact_pos_base_dwords. Only Initialize()d when the cvar
  // is on; is_valid() gates the whole gather path.
  ui::vulkan::VulkanDynamicBufferRing compact_pos_ring_;
  // Intra-frame gather cache: draws sharing a fetch constant + tag layout
  // reuse one gathered stream within a frame (ring segments are frame-local).
  struct CompactPosCacheEntry {
    uint32_t fc_dword_0;
    uint32_t fc_dword_1;
    uint32_t stride_words;
    uint32_t offset_words;
    uint32_t needed_words;
    uint32_t base_dwords;
  };
  std::vector<CompactPosCacheEntry> compact_pos_cache_;
  uint64_t compact_pos_cache_frame_ = 0;
  // Dword offset of the current draw's gathered stream into compact_pos_ring_,
  // or UINT32_MAX when this draw is not redirected (the flag stays clear and
  // the draw runs verbatim off the interleaved stream).
  uint32_t compact_pos_current_base_dwords_ = UINT32_MAX;

  // Descriptor set layouts used by different shaders.
  VkDescriptorSetLayout descriptor_set_layout_empty_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout_constants_ = VK_NULL_HANDLE;
  std::array<VkDescriptorSetLayout,
             size_t(SingleTransientDescriptorLayout::kCount)>
      descriptor_set_layouts_single_transient_{};
  VkDescriptorSetLayout descriptor_set_layout_shared_memory_and_edram_ =
      VK_NULL_HANDLE;

  // Descriptor set layouts are referenced by pipeline_layouts_.
  std::unordered_map<TextureDescriptorSetLayoutKey, VkDescriptorSetLayout,
                     TextureDescriptorSetLayoutKey::Hasher>
      descriptor_set_layouts_textures_;
  // Pipeline layouts are referenced by VulkanPipelineCache.
  std::unordered_map<PipelineLayoutKey, PipelineLayout,
                     PipelineLayoutKey::Hasher>
      pipeline_layouts_;

  // No specific reason for 32768, just the "too much" descriptor count from
  // Direct3D 12 PIX warnings.
  static constexpr uint32_t kLinkedTypeDescriptorPoolSetCount = 32768;
  static const VkDescriptorPoolSize kDescriptorPoolSizeUniformBuffer;
  static const VkDescriptorPoolSize kDescriptorPoolSizeStorageBuffer;
  static const VkDescriptorPoolSize kDescriptorPoolSizeTextures[2];
  // gpu_vulkan_float_constants_ssbo: the guest-constants set becomes MIXED (the
  // system/bool/fetch bindings stay UNIFORM_BUFFER, the two float bindings
  // become STORAGE_BUFFER), so it needs a pool providing both types.
  static const VkDescriptorPoolSize kDescriptorPoolSizeConstantsMixed[2];
  ui::vulkan::LinkedTypeDescriptorSetAllocator
      transient_descriptor_allocator_uniform_buffer_;
  ui::vulkan::LinkedTypeDescriptorSetAllocator
      transient_descriptor_allocator_storage_buffer_;
  // Allocator for the MIXED guest-constants set when gpu_vulkan_float_constants_
  // ssbo is on (UNIFORM_BUFFER + STORAGE_BUFFER bindings in one set). Lazily
  // allocates pages, so costs nothing when the cvar is off.
  ui::vulkan::LinkedTypeDescriptorSetAllocator
      transient_descriptor_allocator_constants_mixed_;
  std::deque<UsedSingleTransientDescriptor> single_transient_descriptors_used_;
  std::array<std::vector<VkDescriptorSet>,
             size_t(SingleTransientDescriptorLayout::kCount)>
      single_transient_descriptors_free_;
  // <Usage frame, set>.
  std::deque<std::pair<uint64_t, VkDescriptorSet>>
      constants_transient_descriptors_used_;
  std::vector<VkDescriptorSet> constants_transient_descriptors_free_;

  ui::vulkan::LinkedTypeDescriptorSetAllocator
      transient_descriptor_allocator_textures_;
  std::deque<UsedTextureTransientDescriptorSet>
      texture_transient_descriptor_sets_used_;
  std::unordered_map<TextureDescriptorSetLayoutKey,
                     std::vector<VkDescriptorSet>,
                     TextureDescriptorSetLayoutKey::Hasher>
      texture_transient_descriptor_sets_free_;

  std::unique_ptr<VulkanSharedMemory> shared_memory_;

  std::unique_ptr<VulkanPrimitiveProcessor> primitive_processor_;

  std::unique_ptr<VulkanRenderTargetCache> render_target_cache_;
  // Blue Dragon FULL native D3D9->Vulkan HLE renderer (gpu_bd_native_renderer).
  std::unique_ptr<BdNativeRenderer> bd_native_renderer_;
  // True if BD field draws were redirected into the native RT THIS frame (only
  // then present it; else the native RT is empty). Reset each frame at swap.
  bool bd_native_field_rendered_ = false;
  // Persistent totals (logged periodically at swap) to DEFINITIVELY confirm the
  // native path fires - the last log survives logcat rotation.
  uint64_t bd_redirect_total_ = 0;
  uint64_t bd_present_native_total_ = 0;
  uint64_t bd_swap_total_ = 0;
  uint64_t bd_native_begins_total_ = 0;
  // REAL-HLE aux: C2 render-redirects into native surfaces, and Brick-B texture
  // fetches SERVED from a native surface (the consume side firing = the proof the
  // field samples native content, so EDRAM transfers for it are dead).
  uint64_t bd_native_aux_redirects_ = 0;
  uint64_t bd_native_tex_served_ = 0;
  // EDRAM resolves surgically dropped because a native surface serves the dest =
  // the count of EDRAM->RAM copies DELETED by the real HLE (the payoff metric).
  uint64_t bd_native_resolves_dropped_ = 0;

  // COLOR-ONLY native HLE (gpu_bd_native_color_lifetime_hle, Codex 7-step plan):
  // per-resolve-dest consumer tracking = the safe-drop gate substrate. Which
  // consumer classes read each native-covered color surface THIS frame; a surface
  // is drop-safe only when the prior stable frame saw NO non-native consumer.
  enum BdColorConsumer : uint32_t {
    kBdConsumerPixelTexture = 1u << 0,  // pixel-shader texture fetch (native-served)
    kBdConsumerComposite = 1u << 1,     // composite-consumer draw (native-served)
    kBdConsumerPresent = 1u << 2,       // frontbuffer present/gamma pass
    kBdConsumerNonNative = 1u << 3,     // a reader that was NOT native-redirected
  };
  // dest_base -> OR of BdColorConsumer bits seen this frame.
  std::unordered_map<uint32_t, uint32_t> bd_color_consumer_bits_;
  // dest_base -> bits from the PRIOR completed frame (the stable snapshot the drop
  // gate reads: a surface is ColorDropSafe only if its prior-frame bits had no
  // kBdConsumerNonNative and included at least one native consumer).
  std::unordered_map<uint32_t, uint32_t> bd_color_consumer_bits_prev_;
  // Record that guest address `dest_base`'s native color surface had a consumer of
  // `consumer` class this frame (no-op unless gpu_bd_native_color_lifetime_hle>0).
  void BdNoteColorConsumer(uint32_t dest_base, uint32_t consumer);

  std::unique_ptr<VulkanPipelineCache> pipeline_cache_;

  std::unique_ptr<VulkanTextureCache> texture_cache_;

  VkDescriptorPool shared_memory_and_edram_descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet shared_memory_and_edram_descriptor_set_;
  // gpu_shared_memory_double_buffer: a SECOND shared-memory-and-EDRAM descriptor
  // set whose binding 0 points at shared-memory version 1 (the set above points
  // at version 0). Only allocated + written when the shared memory reports
  // double-buffering active; VK_NULL_HANDLE (and unused) otherwise. The current
  // version's set is selected per frame / on version switch.
  VkDescriptorSet shared_memory_and_edram_descriptor_set_v1_ = VK_NULL_HANDLE;
  // Which shared-memory version's descriptor set is currently cached in
  // current_graphics_descriptor_sets_[kDescriptorSetSharedMemoryAndEdram]. Used
  // to detect a mid-frame version switch and re-point + re-bind the set.
  uint32_t shared_memory_descriptor_set_bound_version_ = 0;
  // Returns the cached descriptor set for the shared memory version the GPU
  // currently reads (version 0's set if double-buffering is off or the v1 set is
  // unavailable). When the version differs from
  // shared_memory_descriptor_set_bound_version_, re-points
  // current_graphics_descriptor_sets_[kDescriptorSetSharedMemoryAndEdram] and
  // clears its bound/value up-to-date bits so the next draw rebinds it.
  void UpdateSharedMemoryDescriptorSetForCurrentVersion();

  // BRICK 1 native bindless render path (gpu_native_render_path). One global
  // descriptor set (created once) with runtime arrays of sampled images (2D
  // array / 3D / cube at bindings 0/1/2) and samplers (binding 3), bound once
  // per command buffer; guest shaders index it by a per-draw push constant. See
  // docs/research/native-render-path-rearch.md. All of this is inert unless the
  // native path is active.
  static constexpr uint32_t kBindlessImageCapacity = 8192;
  static constexpr uint32_t kBindlessSamplerCapacity = 2048;
  bool native_render_path_active_ = false;
  VkDescriptorSetLayout bindless_descriptor_set_layout_ = VK_NULL_HANDLE;
  // Empty layout occupying kDescriptorSetTexturesPixel in native mode (that set
  // is unused - all textures go through the single bindless set at
  // kDescriptorSetTexturesVertex).
  VkDescriptorSetLayout bindless_empty_descriptor_set_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool bindless_descriptor_pool_ = VK_NULL_HANDLE;
  VkDescriptorSet bindless_descriptor_set_ = VK_NULL_HANDLE;
  // The single shared pipeline layout used for every guest draw in native mode
  // (set 0 shared memory, set 1 constants, set 2 bindless, set 3 empty, + the
  // bindless push-constant range). Built once, cached in pipeline_layouts_ under
  // a fixed all-zero key.
  const VulkanPipelineCache::PipelineLayoutProvider* native_pipeline_layout_ =
      nullptr;
  // Monotonic slot allocators (image-view / sampler handle -> global array
  // index; slot 0 reserved as "unused"). Freed slots are recycled via the free
  // lists once the owning resource is destroyed (post-await, so safe).
  std::unordered_map<VkImageView, uint32_t> bindless_image_view_slots_;
  std::vector<uint32_t> bindless_image_free_slots_;
  uint32_t bindless_image_next_slot_ = 1;
  std::unordered_map<VkSampler, uint32_t> bindless_sampler_slots_;
  std::vector<uint32_t> bindless_sampler_free_slots_;
  uint32_t bindless_sampler_next_slot_ = 1;
  // Creates the bindless descriptor set layout(s), pool, and set. Returns false
  // (leaving the native path inactive) on any failure.
  bool InitializeBindlessDescriptors();
  // Returns (allocating + writing the descriptor on first sight) the global slot
  // for an image view in the given bindless binding (0=2D array, 1=3D, 2=cube),
  // or 0 on capacity overflow / null view.
  uint32_t UseBindlessImageSlot(VkImageView image_view, uint32_t binding);
  // Returns (allocating + writing on first sight) the global slot for a sampler.
  uint32_t UseBindlessSamplerSlot(VkSampler sampler);

  // Bytes 0x0...0x3FF - 256-entry gamma ramp table with B10G10R10X2 data (read
  // as R10G10B10X2 with swizzle).
  // Bytes 0x400...0x9FF - 128-entry PWL R16G16 gamma ramp (R - base, G - delta,
  // low 6 bits of each are zero, 3 elements per entry).
  // kMaxFramesInFlight pairs of gamma ramps if in host-visible memory and
  // uploaded directly, one otherwise.
  VkDeviceMemory gamma_ramp_buffer_memory_ = VK_NULL_HANDLE;
  VkBuffer gamma_ramp_buffer_ = VK_NULL_HANDLE;
  // kMaxFramesInFlight pairs, only when the gamma ramp buffer is not
  // host-visible.
  VkDeviceMemory gamma_ramp_upload_buffer_memory_ = VK_NULL_HANDLE;
  VkBuffer gamma_ramp_upload_buffer_ = VK_NULL_HANDLE;
  VkDeviceSize gamma_ramp_upload_memory_size_;
  uint32_t gamma_ramp_upload_memory_type_;
  // Mapping of either gamma_ramp_buffer_memory_ (if it's host-visible) or
  // gamma_ramp_upload_buffer_memory_ (otherwise).
  void* gamma_ramp_upload_mapping_;
  std::array<VkBufferView, 2 * kMaxFramesInFlight> gamma_ramp_buffer_views_{};
  // UINT32_MAX if outdated.
  uint32_t gamma_ramp_256_entry_table_current_frame_ = UINT32_MAX;
  uint32_t gamma_ramp_pwl_current_frame_ = UINT32_MAX;

  VkDescriptorSetLayout swap_descriptor_set_layout_sampled_image_ =
      VK_NULL_HANDLE;
  VkDescriptorSetLayout swap_descriptor_set_layout_uniform_texel_buffer_ =
      VK_NULL_HANDLE;

  // Descriptor pool for allocating descriptors needed for presentation, such as
  // the destination images and the gamma ramps.
  VkDescriptorPool swap_descriptor_pool_ = VK_NULL_HANDLE;
  // Interleaved 256-entry table and PWL texel buffer descriptors.
  // kMaxFramesInFlight pairs of gamma ramps if in host-visible memory and
  // uploaded directly, one otherwise.
  std::array<VkDescriptorSet, 2 * kMaxFramesInFlight>
      swap_descriptors_gamma_ramp_;
  // Sampled images.
  std::array<VkDescriptorSet, kMaxFramesInFlight> swap_descriptors_source_;

  VkPipelineLayout swap_apply_gamma_pipeline_layout_ = VK_NULL_HANDLE;
  // Has no dependencies on specific pipeline stages on both ends to simplify
  // use in different scenarios with different pipelines - use explicit barriers
  // for synchronization.
  VkRenderPass swap_apply_gamma_render_pass_ = VK_NULL_HANDLE;
  VkPipeline swap_apply_gamma_256_entry_table_pipeline_ = VK_NULL_HANDLE;
  VkPipeline swap_apply_gamma_pwl_pipeline_ = VK_NULL_HANDLE;

  std::array<SwapFramebuffer,
             ui::vulkan::VulkanPresenter::kMaxActiveGuestOutputImageVersions>
      swap_framebuffers_;

  // Pending pipeline barriers.
  std::vector<VkBufferMemoryBarrier> pending_barriers_buffer_memory_barriers_;
  std::vector<VkImageMemoryBarrier> pending_barriers_image_memory_barriers_;
  struct PendingBarrier {
    VkPipelineStageFlags src_stage_mask = 0;
    VkPipelineStageFlags dst_stage_mask = 0;
    size_t buffer_memory_barriers_offset = 0;
    size_t image_memory_barriers_offset = 0;
  };
  std::vector<PendingBarrier> pending_barriers_;
  PendingBarrier current_pending_barrier_;

  // GPU-local scratch buffer.
  static constexpr VkDeviceSize kScratchBufferSizeIncrement = 16 * 1024 * 1024;
  VkDeviceMemory scratch_buffer_memory_ = VK_NULL_HANDLE;
  VkBuffer scratch_buffer_ = VK_NULL_HANDLE;
  VkDeviceSize scratch_buffer_size_ = 0;
  VkPipelineStageFlags scratch_buffer_last_stage_mask_ = 0;
  VkAccessFlags scratch_buffer_last_access_mask_ = 0;
  uint64_t scratch_buffer_last_usage_submission_ = 0;
  bool scratch_buffer_used_ = false;

  // The current dynamic state of the graphics pipeline bind point. Note that
  // binding any pipeline to the bind point with static state (even if it's
  // unused, like depth bias being disabled, but the values themselves still not
  // declared as dynamic in the pipeline) invalidates such dynamic state.
  VkViewport dynamic_viewport_;
  VkRect2D dynamic_scissor_;
  // Dynamic fixed-function depth bias, blend constants, stencil state are
  // applicable only to the render target implementations where they are
  // actually involved.
  float dynamic_depth_bias_constant_factor_;
  float dynamic_depth_bias_slope_factor_;
  float dynamic_blend_constants_[4];
  // The stencil values are pre-initialized (to D3D11_DEFAULT_STENCIL_*, and the
  // initial values for front and back are the same for portability subset
  // safety) because they're updated conditionally to avoid changing the back
  // face values when stencil is disabled and the primitive type is changed
  // between polygonal and non-polygonal.
  uint32_t dynamic_stencil_compare_mask_front_ = UINT8_MAX;
  uint32_t dynamic_stencil_compare_mask_back_ = UINT8_MAX;
  uint32_t dynamic_stencil_write_mask_front_ = UINT8_MAX;
  uint32_t dynamic_stencil_write_mask_back_ = UINT8_MAX;
  uint32_t dynamic_stencil_reference_front_ = 0;
  uint32_t dynamic_stencil_reference_back_ = 0;
  // EDS (Lever 1b, vulkan_dynamic_state_cull_front): cull mode + front face
  // promoted to dynamic state. The per-command-buffer reset forces the first
  // emission regardless of these initial values.
  VkCullModeFlags dynamic_cull_mode_ = VK_CULL_MODE_NONE;
  VkFrontFace dynamic_front_face_ = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  // EDS primitive topology (Lever 1, vulkan_dynamic_state_topology) - only
  // triangle list/strip are promoted.
  VkPrimitiveTopology dynamic_primitive_topology_ =
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkBool32 dynamic_primitive_restart_enable_ = VK_FALSE;
  // EDS depth (Lever 1, vulkan_dynamic_state_depth).
  VkBool32 dynamic_depth_test_enable_ = VK_FALSE;
  VkBool32 dynamic_depth_write_enable_ = VK_FALSE;
  VkCompareOp dynamic_depth_compare_op_ = VK_COMPARE_OP_NEVER;
  // EDS stencil (Lever 1, vulkan_dynamic_state_stencil). Distinct from the
  // already-dynamic stencil compare/write masks + reference below.
  VkBool32 dynamic_stencil_test_enable_ = VK_FALSE;
  VkStencilOp dynamic_stencil_front_fail_op_ = VK_STENCIL_OP_KEEP;
  VkStencilOp dynamic_stencil_front_pass_op_ = VK_STENCIL_OP_KEEP;
  VkStencilOp dynamic_stencil_front_depth_fail_op_ = VK_STENCIL_OP_KEEP;
  VkCompareOp dynamic_stencil_front_compare_op_ = VK_COMPARE_OP_NEVER;
  VkStencilOp dynamic_stencil_back_fail_op_ = VK_STENCIL_OP_KEEP;
  VkStencilOp dynamic_stencil_back_pass_op_ = VK_STENCIL_OP_KEEP;
  VkStencilOp dynamic_stencil_back_depth_fail_op_ = VK_STENCIL_OP_KEEP;
  VkCompareOp dynamic_stencil_back_compare_op_ = VK_COMPARE_OP_NEVER;
  bool dynamic_viewport_update_needed_;
  bool dynamic_scissor_update_needed_;
  bool dynamic_depth_bias_update_needed_;
  bool dynamic_blend_constants_update_needed_;
  bool dynamic_stencil_compare_mask_front_update_needed_;
  bool dynamic_stencil_compare_mask_back_update_needed_;
  bool dynamic_stencil_write_mask_front_update_needed_;
  bool dynamic_stencil_write_mask_back_update_needed_;
  bool dynamic_stencil_reference_front_update_needed_;
  bool dynamic_stencil_reference_back_update_needed_;
  bool dynamic_cull_mode_update_needed_ = false;
  bool dynamic_front_face_update_needed_ = false;
  bool dynamic_primitive_topology_update_needed_ = false;
  bool dynamic_primitive_restart_enable_update_needed_ = false;
  bool dynamic_depth_test_enable_update_needed_ = false;
  bool dynamic_depth_write_enable_update_needed_ = false;
  bool dynamic_depth_compare_op_update_needed_ = false;
  bool dynamic_stencil_test_enable_update_needed_ = false;
  bool dynamic_stencil_op_update_needed_ = false;

  // Whether VK_KHR_push_descriptor is enabled + active (cvar on, extension
  // supported, function loaded). Decided in SetupContext before any descriptor
  // set layout is created, since push-descriptor layouts need a creation flag.
  bool push_descriptors_active_ = false;

  // Currently used samplers.
  std::vector<std::pair<VulkanTextureCache::SamplerParameters, VkSampler>>
      current_samplers_vertex_;
  std::vector<std::pair<VulkanTextureCache::SamplerParameters, VkSampler>>
      current_samplers_pixel_;

  // Texture/sampler descriptor reuse (vulkan_cache_texture_descriptors): a
  // signature of the image views and samplers bound for the vertex/pixel
  // texture descriptor sets on the last draw that wrote them. If the next draw
  // produces an identical signature, the previously written descriptor set is
  // reused instead of allocating + vkUpdateDescriptorSets again (per-draw
  // descriptor churn dominates the GPU command thread on Adreno). Reset
  // whenever the descriptor set bit goes out of date for another reason (frame
  // start, pipeline layout change). A hash collision would only ever cause a
  // skipped-but-needed rewrite; the contents are tiny, so the signature stores
  // the exact handles, not a hash, to be exact.
  std::vector<uint64_t> texture_descriptor_signature_vertex_;
  std::vector<uint64_t> texture_descriptor_signature_pixel_;
  bool texture_descriptor_signature_vertex_valid_ = false;
  bool texture_descriptor_signature_pixel_valid_ = false;
  // Reused per-draw scratch for building the above signatures without a fresh
  // heap allocation each draw (UpdateBindings, CP worker thread only).
  std::vector<uint64_t> texture_signature_scratch_;
  // EDRAM-recompiler RT-as-texture (increment 1, gpu_rt_as_texture): per-draw
  // override of the pixel texture descriptor image views, indexed by texture
  // fetch constant. VK_NULL_HANDLE = no override (use the texture cache). Filled
  // once per draw in UpdateBindings BEFORE both the descriptor signature and the
  // descriptor write loop, which must read it identically or the descriptor
  // cache would keep a stale view.
  std::array<VkImageView, xenos::kTextureFetchConstantCount>
      rt_as_texture_views_pixel_{};

  // Per-frame draw-outcome counters (vulkan_trace_draw_outcomes_per_frame).
  // Accumulated across a guest frame, logged and reset at the swap. Used to find
  // silently dropped geometry (black 3D) and per-frame work with no effect.
  uint32_t draw_outcomes_rendered_ = 0;
  uint32_t draw_outcomes_skipped_no_vs_ = 0;
  uint32_t draw_outcomes_skipped_no_rast_ = 0;
  uint32_t draw_outcomes_copy_ = 0;
  // gpu_foliage_decimate_pct: per-frame counter of alpha-test (foliage) draws,
  // used to evenly spread the decimation skips. Reset at swap.
  uint32_t foliage_draw_counter_ = 0;
  uint64_t draw_outcomes_total_vertices_ = 0;
  uint32_t draw_outcomes_max_vertices_ = 0;
  // Depth-prepass eligibility classification (Unit 1 of the opaque depth
  // pre-pass): per frame, how many rendered draws are OPAQUE (depth-write on,
  // no alpha-test/discard, no blend) - i.e. safe to render depth-only in a
  // pre-pass so the color pass early-Z-rejects their occluded fragments - vs
  // ALPHA-TEST (defeats LRZ, prepass doesn't help) vs BLENDED (order-locked,
  // cannot prepass). The opaque vertex sum estimates the pre-pass-recoverable
  // overdraw. Decides whether the (expensive) deferral/replay is worth building.
  uint32_t draw_outcomes_opaque_draws_ = 0;
  uint64_t draw_outcomes_opaque_verts_ = 0;
  uint32_t draw_outcomes_alphatest_draws_ = 0;
  uint32_t draw_outcomes_blended_draws_ = 0;
  // Front B (gpu_trace_cullable_tris): would-cull triangle count this frame - a
  // read-only DECISION instrument (no geometry mutated). C1 scaffolding counts 0;
  // C2/C3 wire the ShaderInterpreter VS-position replay + backface/frustum test.
  uint64_t draw_outcomes_cullable_tris_ = 0;
  // Whole-draw frustum-cull potential (gpu_trace_cullable_tris): draws entirely
  // off-screen (droppable before binning, saving the full per-draw cost), draws
  // that were eligible (position-replayed), and the verts those whole-cullable
  // draws would save from binning. Read-only sizing of the whole-draw lever.
  uint32_t draw_outcomes_wholecull_draws_ = 0;
  uint32_t draw_outcomes_wholecull_elig_ = 0;
  uint64_t draw_outcomes_wholecull_verts_ = 0;
  // Whole-draw skips actually performed in the gpu_cull_compaction path: draws
  // whose cull left 0 indices (fully off-screen) and were not submitted - the
  // real per-draw binning saving. + the vertices thereby spared from binning.
  uint32_t draw_outcomes_cull_whole_skip_ = 0;
  uint64_t draw_outcomes_cull_whole_skip_verts_ = 0;
  // Lever 2 Step 0 (vulkan_trace_draw_outcomes_per_frame): rendered draws/verts
  // whose position shader qualifies for the CPU/NEON affine-MVP cull transform
  // (read-only feasibility classifier, see Shader::is_affine_mvp_candidate). A
  // lower-bound signal for whether the cull can engage on a title.
  uint32_t draw_outcomes_affine_mvp_draws_ = 0;
  uint64_t draw_outcomes_affine_mvp_vertices_ = 0;
  // G1-lite coverage: draws/verts whose position vfetch is statically
  // redirectable (Shader::position_vfetch_tag) - the de-interleave ceiling.
  uint32_t draw_outcomes_deint_elig_draws_ = 0;
  uint64_t draw_outcomes_deint_elig_verts_ = 0;
  // G1-lite live telemetry: draws/verts actually redirected to the compact
  // stream, CPU time spent gathering, and bails (invalid fc / cap / ring
  // full). Counted unconditionally (cheap); logged with the outcomes line.
  uint32_t draw_outcomes_deint_redir_draws_ = 0;
  uint64_t draw_outcomes_deint_redir_verts_ = 0;
  uint64_t draw_outcomes_deint_gather_ns_ = 0;
  uint32_t draw_outcomes_deint_bails_ = 0;
  // gpu_hw_vertex_fetch engagement diagnostic (per frame): elig = draws that
  // reached the redirect gate (cvar on + host kVertex shader); redir = draws that
  // passed it (kNone index endian + base 0) and set kSysFlag_HwVertexFetch.
  uint32_t draw_outcomes_hwvtx_elig_draws_ = 0;
  uint32_t draw_outcomes_hwvtx_redir_draws_ = 0;
  // Safe DONT_CARE (gpu_edram_passes_dont_care_safe): the current draw's
  // provable full-cover overwrite state - attachment deadness mask (bit 0
  // depth, bits 1-4 color 0-3) and the covered pixel rect (x0,y0,x1,y1; x1/y1
  // exclusive). Consumed only if THIS draw opens a render pass; 0 = no proof.
  uint32_t dc_safe_pending_state_mask_ = 0;
  int32_t dc_safe_pending_rect_[4] = {};
  // Telemetry: passes begun with a load-DONT_CARE variant + attachments whose
  // tile load was elided.
  uint32_t draw_outcomes_dc_safe_passes_ = 0;
  uint32_t draw_outcomes_dc_safe_atts_ = 0;
  // gpu_vulkan_skip_unused_depth_store: whether THIS guest draw provably never
  // accesses depth/stencil (set per-draw in IssueDraw, consumed if it opens a
  // pass); whether the currently-open guest pass began with the depth-store-NONE
  // variant (so a later depth-using draw must break it first); + telemetry count
  // of passes begun in that mode.
  bool depth_store_none_pending_ = false;
  bool current_pass_depth_store_none_ = false;
  uint32_t draw_outcomes_depth_none_passes_ = 0;
  // gpu_vulkan_retro_depth_none (frame-graph recompiler increment A): the
  // currently-open guest pass's recorded BeginRenderPass position + the depth
  // DONT_CARE/STORE_OP_NONE render-pass variant + framebuffer captured at begin,
  // and whether any draw in the pass has used depth/stencil so far. At
  // EndRenderPass, if unused, the recorded begin is PATCHED to the variant
  // (hindsight elision - never breaks the pass). SIZE_MAX = not capturing.
  size_t retro_depth_begin_pos_ = SIZE_MAX;
  VkRenderPass retro_depth_variant_ = VK_NULL_HANDLE;
  VkFramebuffer retro_depth_framebuffer_ = VK_NULL_HANDLE;
  bool retro_pass_depth_used_ = false;
  uint32_t draw_outcomes_retro_depth_none_ = 0;
  // Throttled why-not-eligible diagnostic count (reset per frame with the rest).
  uint32_t draw_outcomes_retro_depth_none_diag_ = 0;
  // gpu_vulkan_retro_color_dontcare (frame-graph recompiler increment B(a)):
  // per-open-pass retro color-load elision state. The RenderPassKey + extent are
  // captured at begin (at the RT-change end path last_update has already moved
  // to the NEXT pass, so the end-time variant must be derived from the captured
  // key, never from ForLastUpdate). Per color attachment: the union of
  // full-height provable-replace draw coverage as disjoint X-intervals (the
  // guest strip-clear idiom), a poisoned flag (a non-replace write happened
  // while the union was still incomplete => pre-pass content may be read), and
  // a complete flag (union covers the whole render area => load is dead).
  VulkanRenderTargetCache::RenderPassKey retro_pass_key_;
  VkExtent2D retro_pass_extent_{};
  struct RetroCoverage {
    static constexpr uint32_t kMaxIntervals = 8;
    int32_t x0[kMaxIntervals];
    int32_t x1[kMaxIntervals];
    uint32_t interval_count = 0;
    bool poisoned = false;
    bool complete = false;
    void Reset() {
      interval_count = 0;
      poisoned = false;
      complete = false;
    }
    // Merge [a,b) into the disjoint set; returns false when the set overflows
    // (treated as poisoned by the caller - conservative).
    bool AddInterval(int32_t a, int32_t b);
    bool Covers(int32_t width) const {
      return interval_count == 1 && x0[0] <= 0 && x1[0] >= width;
    }
  };
  RetroCoverage retro_color_cov_[4];
  uint32_t draw_outcomes_retro_color_atts_ = 0;
  uint32_t draw_outcomes_retro_color_diag_ = 0;
  // host_draws= telemetry: draw stats of completed submissions are folded into
  // the accumulator at Execute time (the per-recording stat zeroes on Reset);
  // the print marker turns the monotone total into a per-frame delta.
  uint64_t host_draws_recorded_accum_ = 0;
  uint64_t host_draws_printed_marker_ = 0;
  // Step 0b: the precise position-export-slice classifier (counts draws/verts
  // whose POSITION slice is affine-MVP, ignoring the color/UV path) - the number
  // that actually sizes the CPU/NEON cull's reach. See
  // Shader::is_position_affine_mvp_candidate.
  uint32_t draw_outcomes_affine_mvp_pos_draws_ = 0;
  uint64_t draw_outcomes_affine_mvp_pos_vertices_ = 0;
  // Step 0c: for the NON-qualifying draws, why their position slice failed (verts
  // per reason) - decides whether the cull's unreachable bulk is skinning (a0,
  // liftable by a skinning-aware kernel) or genuinely non-affine. See
  // Shader::position_mvp_disqual_reason.
  uint64_t draw_outcomes_pos_disq_a0_verts_ = 0;
  uint64_t draw_outcomes_pos_disq_loop_verts_ = 0;
  uint64_t draw_outcomes_pos_disq_backjump_verts_ = 0;
  uint64_t draw_outcomes_pos_disq_call_verts_ = 0;
  uint64_t draw_outcomes_pos_disq_texfetch_verts_ = 0;
  uint64_t draw_outcomes_pos_disq_other_verts_ = 0;
  // Step 2a-ii diagnostic funnel (gpu_cull_compaction): where each kGuestDMA draw
  // that entered the cull branch fell off - skip_dyntop (no dynamic topology),
  // skip_qual (not position-affine-MVP), skip_build (BuildCulledIndexList bailed /
  // dropped nothing), or culled (cull_draws + cull_dropped_tris). Pinpoints why the
  // cull does/doesn't engage on device.
  uint32_t draw_outcomes_cull_branch_ = 0;
  uint32_t draw_outcomes_cull_skip_dyntop_ = 0;
  uint32_t draw_outcomes_cull_skip_qual_ = 0;
  uint32_t draw_outcomes_cull_draws_ = 0;
  uint64_t draw_outcomes_cull_dropped_tris_ = 0;
  // BuildCulledIndexList bail reasons, indexed by DrawExtentEstimator::CullBail
  // (sized generously; kCount is currently 11). Names the systematic bail.
  uint32_t draw_outcomes_cull_bail_[12] = {};
  // gpu_cull_max_per_frame validation throttle: per-frame count of draws that ran
  // the (slow) interpreter cull, reset when frame_current_ changes.
  uint64_t cull_budget_frame_ = UINT64_MAX;
  uint32_t cull_draws_this_frame_ = 0;
  // Step 2b-i sanity: over qualifying (position-affine-MVP) draws, the sum of
  // position-slice op counts and how many slices are replayable. Confirms the
  // extracted slices are small + replayable before the replay kernel is built.
  uint64_t draw_outcomes_cull_slice_ops_sum_ = 0;
  uint32_t draw_outcomes_cull_slice_replayable_ = 0;
  // Step 2b-ii(a) affine-replay validation (gpu_cull_replay_validate): per-frame
  // counts of draws whose recovered affine matrix M reproduces the interpreter
  // (affine) / doesn't (nonaffine) / can't be set up (unsupported), and the max
  // relative error x1000. Confirms the matrix-recovery replay before it lands.
  uint32_t draw_outcomes_replay_affine_ = 0;
  uint32_t draw_outcomes_replay_nonaffine_ = 0;
  uint32_t draw_outcomes_replay_unsupported_ = 0;
  uint32_t draw_outcomes_replay_max_error_milli_ = 0;
  // Fast-replay engagement + fallback position-format histogram (keyed by
  // xenos::VertexFormat & 63): names which formats keep draws on the slow
  // interpreter so the right decodes get added.
  uint32_t draw_outcomes_cull_fast_engaged_ = 0;
  // Fast-replay setup fail histogram, indexed by DrawExtentEstimator::FastSetupFail
  // (noleaf/multileaf/novfetch/badfmt/recovery). Pinpoints the fallback cause.
  uint32_t draw_outcomes_cull_fail_[8] = {};
  // Multi-leaf fallbacks bucketed by leaf count (index = leaf count clamped to 7):
  // distinguishes fixable slice over-inclusion (2) from genuine skinning (3+).
  uint32_t draw_outcomes_cull_multi_lc_[8] = {};
  // Front B cullable-triangle counter: lazily-constructed CPU VS-position
  // replayer, used only when gpu_trace_cullable_tris is set. nullptr until first
  // use, so the default path pays nothing.
  std::unique_ptr<DrawExtentEstimator> cull_extent_estimator_;
  // Per-frame draw composition (what the ~2000 draws ARE): histogram of guest
  // PrimitiveType (index = uint32_t(prim_type) & 0xF) and host-vertex-count
  // size buckets (tiny<16, small<64, med<256, big>=256). Tells whether the
  // draws are tiny triangle-list spam, strips, rect/quad lists, etc., and how
  // batchable they are. Logged at swap under vulkan_trace_draw_outcomes_per_frame.
  uint32_t draw_prim_counts_[16] = {};
  uint32_t draw_vtx_bucket_[4] = {};
  // Merge-eligibility (per frame): of consecutive draws, how many reuse the
  // previous draw's pipeline, and of those, how many had vertex float constants
  // UNCHANGED since the previous draw (trivially mergeable) vs CHANGED (need
  // instancing, since each tiny draw has its own transform). Decides
  // merge-vs-instance for the tiny-draw batching work. Updated at UpdateBindings
  // entry under vulkan_trace_draw_outcomes_per_frame.
  VkPipeline merge_last_pipeline_ = VK_NULL_HANDLE;
  uint32_t merge_pipe_same_ = 0;
  uint32_t merge_consts_same_ = 0;
  uint32_t merge_consts_changed_ = 0;
  // vfetch contiguity (per frame): for consecutive same-pipeline draws, is the
  // primary vertex-fetch source the SAME address (identical geometry ->
  // instanceable), CONTIGUOUS with the previous (addr==prev_addr+prev_size ->
  // concatenable into one draw), or SCATTERED (neither). Decides merge strategy.
  uint32_t merge_vf_last_addr_ = 0;
  uint32_t merge_vf_last_end_ = 0;
  uint32_t merge_vf_same_ = 0;
  uint32_t merge_vf_contig_ = 0;
  uint32_t merge_vf_scattered_ = 0;
  // Run-length histogram (per frame): length of maximal runs of consecutive
  // rendered draws sharing the same guest graphics pipeline. The GO/NO-GO data
  // for whether ANY draw-batching is even possible (runs of length 1 => nothing
  // to merge). Buckets: [1,2,3-4,5-8,9-16,17-32,33-64,65+]. run_len_ is the
  // in-progress run length, flushed into a bucket on a pipeline change.
  uint32_t merge_run_len_ = 0;
  VkPipeline merge_run_pipeline_ = VK_NULL_HANDLE;
  uint32_t merge_run_hist_[8] = {};
  // True-eligible-run histogram (read-only, vulkan_trace_draw_outcomes_per_frame):
  // like merge_run_hist_ but a run only extends when the DOMINANT merge gates also
  // hold - byte-contiguous guest index range + list-mergeable topology
  // (kTriangleList/kLineList/kPointList, kGuestDMA, no memexport, no primitive
  // restart) - not just a shared pipeline. The real draw-concatenation potential,
  // measurable at BASELINE (independent of vulkan_merge_draws). An upper bound: it
  // omits the rarely-differing layout/index_type/vertex_base/endian checks and the
  // intervening-command break. The baseline-vs-+EDS delta shows how much EDS
  // variant-collapse raises real mergeability. Logged as elig_runlen[...].
  uint32_t merge_elig_run_len_ = 0;
  bool merge_elig_run_active_ = false;
  VkPipeline merge_elig_run_pipeline_ = VK_NULL_HANDLE;
  xenos::PrimitiveType merge_elig_run_prim_type_ =
      xenos::PrimitiveType::kPointList;
  uint32_t merge_elig_run_next_byte_ = 0;
  uint32_t merge_elig_run_hist_[8] = {};
  // Strip-coalescer-eligible run histogram (read-only): admits kTriangleStrip and
  // DROPS the byte-contiguity gate (the coalescer stitches non-contiguous strips
  // via a merged restart-separated index buffer), keyed on
  // pipeline+pipeline_layout+VGT_INDX_OFFSET. Sizes BD's strip draw-merge factor;
  // logged as strip_runlen[...].
  uint32_t merge_strip_run_len_ = 0;
  bool merge_strip_run_active_ = false;
  VkPipeline merge_strip_run_pipeline_ = VK_NULL_HANDLE;
  const PipelineLayout* merge_strip_run_layout_ = nullptr;
  int32_t merge_strip_run_vgt_offset_ = 0;
  uint32_t merge_strip_run_hist_[8] = {};
  // Strip-coalescer DESCRIPTOR-aware run histogram (logged as stripd_runlen[...]):
  // like merge_strip_run_* but a run additionally requires the vertex float
  // constants (the per-mesh transform) to be UNCHANGED - the TRUE rank-3 merge
  // factor, since strips can be stitched into ONE physical draw only if they share
  // the per-object transform. The gap between strip_runlen and stripd_runlen sizes
  // how much the rank-4 (bindless) / rank-5 (CPU pre-transform) wideners recover.
  // merge_draw_vfetch_consts_same_ is captured per draw in UpdateConstantBuffers
  // (before the per-draw invalidation re-sets the up-to-date bit) and consumed by
  // the stripd run logic in the draw-outcomes instrumentation.
  bool merge_draw_vfetch_consts_same_ = false;
  uint32_t merge_stripd_run_len_ = 0;
  bool merge_stripd_run_active_ = false;
  VkPipeline merge_stripd_run_pipeline_ = VK_NULL_HANDLE;
  const PipelineLayout* merge_stripd_run_layout_ = nullptr;
  int32_t merge_stripd_run_vgt_offset_ = 0;
  uint32_t merge_stripd_run_hist_[8] = {};
  // Strip-coalescer TEXTURE-aware run histogram (logged as stript_runlen[...]):
  // like merge_strip_run_* but a run additionally requires the pixel texture
  // descriptor signature to be UNCHANGED. This ISOLATES the texture breaker (vs
  // stripd's transform breaker) and directly predicts rank-5's standalone payoff:
  // after rank-5 pre-transform removes the transform as a run-breaker, only
  // texture changes remain, so stript_runlen == the post-rank-5 merge factor. If
  // stript stays high, rank-5 alone suffices; if it collapses like stripd, rank-4
  // (bindless) is ALSO required. Valid only with vulkan_cache_texture_descriptors
  // on (else the texture descriptor bit is cleared unconditionally every draw).
  bool merge_draw_ptex_same_ = false;
  uint32_t merge_stript_run_len_ = 0;
  bool merge_stript_run_active_ = false;
  VkPipeline merge_stript_run_pipeline_ = VK_NULL_HANDLE;
  const PipelineLayout* merge_stript_run_layout_ = nullptr;
  int32_t merge_stript_run_vgt_offset_ = 0;
  uint32_t merge_stript_run_hist_[8] = {};
  // Merge-miss attribution (read-only): when a true-eligible run breaks, why did
  // THIS draw fail to extend it (first failing gate)? non_dma = not kGuestDMA;
  // topo = non-list/line/point topology; other = memexport / primitive-restart;
  // state = pipeline or topology changed; noncontig = index range not contiguous.
  // The dominant bucket points to the next binning lever (state-dominated -> EDS
  // variant-collapse helps; noncontig-dominated -> index layout is the ceiling).
  uint32_t merge_miss_non_dma_ = 0;
  uint32_t merge_miss_topology_ = 0;
  uint32_t merge_miss_other_ = 0;
  uint32_t merge_miss_state_ = 0;
  uint32_t merge_miss_noncontig_ = 0;
  // LIVE merge-rewrite path attribution (mrw[] in the outcomes line): what the
  // interceptor actually did per draw this frame. ext = extended a run; head =
  // started a run; auto = bypassed via the non-indexed/shader-32bit-DMA path;
  // ndma = indexed but non-kGuestDMA (converted/builtin); nomrg = in-branch
  // but non-mergeable (topology/memexport/list-restart); for in-branch
  // mergeable draws with an active run that could NOT extend, the FIRST
  // failing can_extend gate: cant (a command was recorded since IssueDraw
  // entry - constants/texture/state churn), pipe (pipeline/layout), itype
  // (index type), cap (rewrite block capacity / zero-copy contiguity), vgt
  // (VGT_INDX_OFFSET), end (index endian), prim (primitive type), rst (reset
  // state). Answers "why didn't it merge" directly on-device.
  uint32_t mrw_ext_ = 0;
  uint32_t mrw_head_ = 0;
  uint32_t mrw_auto_ = 0;
  uint32_t mrw_ndma_ = 0;
  uint32_t mrw_nomrg_ = 0;
  uint32_t mrw_cant_ = 0;
  uint32_t mrw_pipe_ = 0;
  uint32_t mrw_itype_ = 0;
  uint32_t mrw_cap_ = 0;
  uint32_t mrw_vgt_ = 0;
  uint32_t mrw_end_ = 0;
  uint32_t mrw_prim_ = 0;
  uint32_t mrw_rst_ = 0;
  // mrw_cant attribution helpers: per frame, how many times each CONSTANT
  // BUFFER class was re-uploaded (its up-to-date bit was clear at request
  // time; indices = SpirvShaderTranslator::ConstantBuffer order sys/fv/fp/bl/
  // fetch) and how many times each DESCRIPTOR SET index was re-bound (0=shared
  // memory+EDRAM, 1=constants, 2=vertex textures, 3=pixel textures). The
  // dominant per-draw counter = the state churn that records commands between
  // draws and breaks every merge run (mrw[cant=]).
  uint32_t mrw_cb_upload_[5] = {};
  uint32_t mrw_ds_rebind_[4] = {};
  // Rebase-feasibility classifier (mrwf[] in the outcomes line): for each
  // merge-class (kGuestDMA, merge cvars on) draw, its vertex fetch constants
  // vs the PREVIOUS such draw: same = identical (fetch isn't the breaker);
  // b16/b32 = identical except ONE uniform stride-aligned base shift across
  // all bindings (the index-bias magnitude fits/exceeds 16-bit indices) -
  // the population the fetch-aware index-REBASING widener could merge; shape
  // = field/stride/binding change (unmergeable, real state change). Sizes
  // the rebasing build before it is attempted.
  struct MrwFetchSnap {
    uint32_t fetch_constant;
    uint32_t address_bytes;
    uint32_t dword_1;
    uint32_t type;
    uint32_t stride_bytes;
  };
  MrwFetchSnap mrw_prev_fetch_[8] = {};
  uint32_t mrw_prev_fetch_count_ = 0;
  bool mrw_prev_fetch_valid_ = false;
  uint32_t mrw_fetch_same_ = 0;
  uint32_t mrw_fetch_bias16_ = 0;
  uint32_t mrw_fetch_bias32_ = 0;
  uint32_t mrw_fetch_shape_ = 0;
  // Lever 2 (vulkan_merge_draws): zero-copy draw concatenation. A pending run of
  // consecutive same-state kGuestDMA draws indexing a contiguous byte range is
  // accumulated here and flushed (one CmdVkBindIndexBuffer + one CmdVkDrawIndexed)
  // by FlushPendingMergeRun(). Step 1 (scaffolding) flushes immediately so a run
  // is always length 1 (identical command stream); coalescing is enabled later.
  // All inert / reset when the cvar is off, so the off-path is bit-identical.
  bool merge_pending_active_ = false;
  // Set per draw when ANY command was recorded into deferred_command_buffer_
  // between IssueDraw entry and the draw-emit block (descriptor binds/pushes,
  // dynamic state, render-pass begin/end, barriers, pipeline bind) - i.e. state
  // changed, so a pending run cannot be extended by this draw. Computed via the
  // command-stream cursor; consumed by the merge interceptor (Step 4).
  bool merge_cannot_extend_this_draw_ = false;
  VkBuffer merge_pending_index_buffer_ = VK_NULL_HANDLE;
  VkDeviceSize merge_pending_index_base_ = 0;  // run head guest_index_base = bind offset
  VkIndexType merge_pending_index_type_ = VK_INDEX_TYPE_UINT16;
  uint32_t merge_pending_index_count_ = 0;  // running sum of host_draw_vertex_count
  // Head-emit: element offset of the run head's recorded ArgsVkDrawIndexed in
  // the deferred command stream - extensions patch its index_count in place.
  size_t merge_pending_draw_args_offset_ = 0;
  // Rewrite mode (vulkan_merge_draws_rewrite): the fixed-cap transient index
  // block the run's scattered guest index ranges are appended into (mapping
  // into cull_index_buffer_pool_; null when the run is zero-copy/inactive).
  static constexpr size_t kMergeRewriteBlockBytes = 64 * 1024;
  uint8_t* merge_pending_rewrite_mapping_ = nullptr;
  size_t merge_pending_rewrite_used_bytes_ = 0;
  // Predicate state captured from the run head, compared to extend (Step 4).
  uint32_t merge_pending_next_byte_ = 0;  // base + count*stride (contiguity test)
  VkPipeline merge_pending_pipeline_ = VK_NULL_HANDLE;
  const PipelineLayout* merge_pending_pipeline_layout_ = nullptr;
  uint32_t merge_pending_vertex_base_index_ = 0;  // VGT_INDX_OFFSET
  xenos::Endian merge_pending_vertex_index_endian_ = xenos::Endian::kNone;
  xenos::PrimitiveType merge_pending_prim_type_ =
      xenos::PrimitiveType::kTriangleList;
  // The run head's host_primitive_reset_enabled. Only ever true for strip runs
  // (vulkan_merge_draws_rewrite_strips); selects the strip JOIN strategy on
  // extend (restart marker vs degenerate triangles) and must match to extend.
  bool merge_pending_reset_enabled_ = false;
  // Lever 2b (vulkan_merge_draws_indirect): MDI draw batching. A run of
  // consecutive same-state kGuestDMA draws (LIST or STRIP, contiguous or not) is
  // batched into ONE vkCmdDrawIndexedIndirect. Each indirect command is an
  // independent draw (firstIndex = guest_index_base/stride, vertexOffset 0,
  // instanceCount 1, firstInstance 0), so strips do not stitch across draws and
  // index ranges need not be contiguous.
  // HEAD-EMIT ordering: Xenia records per-draw state (pipeline/descriptors) eagerly
  // BEFORE the draw-emit block, so a batched draw flushed at the breaker would land
  // after the breaker's state binds (wrong pipeline). Instead the indirect draw is
  // emitted at the run HEAD - correctly ordered right after the head's state - over
  // a buffer pre-sized to mdi_max_draw_count_ commands, ZEROED (indexCount=0 = a
  // no-op the GPU command processor skips with no binning), and slots are filled as
  // the run extends via the retained mapping. All inert when the cvar is off / the
  // feature is unsupported, so the off-path is bit-identical.
  bool mdi_supported_ = false;  // multiDrawIndirect feature + maxDrawIndirectCount>=2
  uint32_t mdi_max_draw_count_ = 1;  // min(maxDrawIndirectCount, cap): commands/run
  bool merge_mdi_active_ = false;
  VkDrawIndexedIndirectCommand* merge_mdi_mapping_ = nullptr;  // retained run array
  // Live draw count for vkCmdDrawIndexedIndirectCount: the GPU reads the count from
  // this buffer slot at submit time, so growing the run just bumps the value here -
  // NO zero-padded slots (the flaw that made the fixed-count padded MDI a net loss).
  uint32_t* merge_mdi_count_ptr_ = nullptr;
  uint32_t merge_mdi_count_ = 0;       // filled slots in the current run
  VkBuffer merge_mdi_index_buffer_ = VK_NULL_HANDLE;
  VkIndexType merge_mdi_index_type_ = VK_INDEX_TYPE_UINT16;
  VkPipeline merge_mdi_pipeline_ = VK_NULL_HANDLE;
  const PipelineLayout* merge_mdi_pipeline_layout_ = nullptr;
  uint32_t merge_mdi_vertex_base_index_ = 0;  // VGT_INDX_OFFSET (must match in run)
  xenos::Endian merge_mdi_index_endian_ = xenos::Endian::kNone;
  // Batchability signals (per frame): how often the expensive per-draw state
  // actually changes. If these are << rendered draw count, consecutive draws
  // share state and can be merged into far fewer host draws.
  uint32_t draw_outcomes_pipeline_binds_ = 0;
  uint32_t draw_outcomes_descriptor_binds_ = 0;
  // Per-frame CPU time (ns) spent on the GPU-Commands thread inside IssueDraw,
  // and the PrimitiveProcessor::Process sub-step, to localize where the
  // per-draw command-processor throughput cost goes (the structural gate for
  // draw-heavy guests like Blue Dragon, ~10k draws/frame). Gated by
  // vulkan_trace_draw_outcomes_per_frame; logged+reset at swap.
  uint64_t draw_cpu_total_ns_ = 0;
  uint64_t draw_cpu_process_ns_ = 0;
  // Finer IssueDraw sub-phase buckets (ns/frame) to pinpoint the dominant part
  // of the per-draw cost: texture RequestTextures, render-target cache Update,
  // pipeline ConfigurePipeline, and descriptor UpdateBindings. "Other" = total
  // minus these + process.
  uint64_t draw_cpu_textures_ns_ = 0;
  uint64_t draw_cpu_rt_ns_ = 0;
  uint64_t draw_cpu_pipeline_ns_ = 0;
  uint64_t draw_cpu_bindings_ns_ = 0;
  // Bisect the "other" bucket: setup_ = IssueDraw entry -> Process (shader
  // analysis + early state); emit_ = after UpdateBindings -> end of IssueDraw
  // (the actual draw-command recording + any GPU/resource wait). Tells us
  // whether the dominant per-draw cost is CPU setup or the emit/wait path.
  uint64_t draw_cpu_setup_ns_ = 0;
  uint64_t draw_cpu_emit_ns_ = 0;
  // BeginSubmission time (a SUBSET of setup_, which spans entry->Process and
  // includes this call). BeginSubmission contains the frame-await throttle-wait
  // that blocks until the GPU catches up - so this separates the GPU-paced WAIT
  // from the real per-draw CPU setup: on a GPU-bound title most of setup_ is
  // beginsubmit_ (wait, not a CPU lever); on a CPU-bound title beginsubmit_ is
  // small and the rest of setup_ is real work (the lever).
  uint64_t draw_cpu_beginsubmit_ns_ = 0;
  // Frame-serialization probe (fopen[]): the single frame-open await duration
  // (NOT accumulated - one frame open per frame) and the frames not yet
  // known-complete when it started (frame_current_ - frame_completed_).
  uint64_t draw_cpu_frame_open_wait_ns_ = 0;
  uint32_t draw_frame_open_in_flight_ = 0;
  // Submission backlog (current - completed) sampled before and after the
  // frame-open await - splits real-GPU-execution-lag from burst-completion
  // (blocked-then-released) from bookkeeping causes.
  uint32_t draw_frame_open_sub_pre_ = 0;
  uint32_t draw_frame_open_sub_post_ = 0;
  // The raw indices of the frame-open await: the awaited submission
  // (closed_frame_submissions_ slot), the upcoming submission, and the
  // last-known completed submission at await entry. sub_pre=2 with a
  // GPU-frame-length wait implies awaited == upcoming-1 (the JUST-closed
  // submission - bookkeeping degeneration) OR a wait on an already-passed
  // value; these three numbers decide which without inference.
  uint64_t draw_frame_open_await_idx_ = 0;
  uint64_t draw_frame_open_upcoming_ = 0;
  uint64_t draw_frame_open_completed_ = 0;
  // Pure fence await/poll time inside CheckSubmissionCompletionAndDeviceLoss
  // this frame (all call sites); fopen wait_us minus this = the completion-
  // side reclamation CPU. The (A)-GPU-late vs (B)-reclaim-cost discriminator.
  uint64_t completion_fence_await_ns_ = 0;
  // Subset of the untimed cpu_gap: the per-draw "ensure vertex buffers resident"
  // RequestRange loop. Its residency bitmask is rebuilt PER DRAW, so RequestRange
  // is called for every vertex binding of every draw (~1.5/draw) - a documented
  // TODO to cache like texture validity. On a high-draw CPU-bound title (Burnout
  // ~3442 draws) this can be a large share of cpu_gap; this attributes it before
  // building a (correctness-sensitive) frame-scoped residency cache.
  uint64_t draw_cpu_vfresidency_ns_ = 0;

  // Optional frame-scoped vertex-buffer residency cache (cvar
  // vulkan_cache_vertex_residency, default off): set of (address|size) vertex
  // ranges already RequestRange'd this frame, so the per-draw residency loop can
  // skip ranges seen earlier in the SAME frame. Frame-stamped: cleared when
  // frame_current_ changes (each frame re-requests once, so guest writes upload
  // next frame). Only touched when the cvar is on.
  std::unordered_set<uint64_t> vertex_residency_cache_;
  uint64_t vertex_residency_cache_frame_ = 0;

  // Optional RenderTargetCache::Update gate (cvar vulkan_gate_rt_update, default
  // off): snapshot of the RT config from the last real Update, to skip the
  // redundant per-draw Update when the config is unchanged AND the render pass
  // is still open (current_render_pass_ != null). Only touched when the cvar is
  // on. rt_gate_valid_ guards the first draw; the current_render_pass_ check
  // handles pass breaks / transfers / frame boundaries.
  bool rt_gate_valid_ = false;
  bool rt_gate_is_raster_done_ = false;
  bool rt_gate_last_ok_ = false;
  uint32_t rt_gate_surface_info_ = 0;
  uint32_t rt_gate_depth_control_ = 0;
  uint32_t rt_gate_color_mask_ = 0;
  uint32_t rt_gate_depth_info_ = 0;
  uint32_t rt_gate_color_info_[4] = {};
  uint64_t rt_gate_vs_hash_ = 0;

  // GPU-side frame time via Vulkan timestamp queries (Thor/Adreno bring-up).
  // Writes a TOP/BOTTOM timestamp pair around each frame's submitted command
  // buffer; the result is read back deferred (a completed frame) and logged at
  // swap. This is the decisive measure of whether the frame is GPU-bound (the
  // ~98 tile-flushes/frame hypothesis). 2 timestamps per kMaxFramesInFlight.
  VkQueryPool gpu_timestamp_pool_ = VK_NULL_HANDLE;
  float gpu_timestamp_period_ns_ = 0.0f;
  // Per-frame bookkeeping: which frame index last wrote each slot pair, so the
  // readback only trusts a slot it actually wrote.
  uint64_t gpu_timestamp_frame_written_[kMaxFramesInFlight] = {};
  // Last successfully read GPU frame time (microseconds), logged at swap.
  uint64_t gpu_frame_us_ = 0;

  // Route A per-pass GPU timing (Thor binning diagnostic; cvar
  // vulkan_trace_pass_timestamps, default OFF -> default path byte-identical).
  // Separate query pool so the working frame-time path is untouched. Brackets
  // each GAME render-pass span (begin->end) with deferred timestamps; readback
  // sums the spans -> gpu_pass_us_ = time spent INSIDE render passes, vs
  // gpu_frame_us_ total (difference = EDRAM transfers / compute / barriers /
  // stalls BETWEEN passes). kMaxPassBrackets span-pairs per frame.
  static constexpr uint32_t kMaxPassBrackets = 128;
  VkQueryPool gpu_pass_timestamp_pool_ = VK_NULL_HANDLE;
  uint32_t gpu_pass_bracket_count_ = 0;  // bracket pairs recorded this frame
  uint32_t gpu_pass_count_written_[kMaxFramesInFlight] = {};  // per frame slot
  uint64_t gpu_pass_us_ = 0;  // last read in-render-pass GPU time (us)
  // Records a GPU timestamp (begin or end of a render-pass span) into the pass
  // pool via the deferred command buffer. No-op unless the cvar is on. kind tags
  // the begin bracket for gpu_trace_resolve_timing's per-kind GPU-time split.
  void RecordPassTimestamp(bool is_begin,
                           GpuPassKind kind = GpuPassKind::kGuest);
  // gpu_trace_resolve_timing: at a guest render-pass end, log the pixel-shader
  // hash / blend / ROP / depth / RT-format of a small-draw pass over an
  // EDRAM-tile-oversized RT (the fb 6c57 720x1824-class 1-draw pass). Recording
  // time only; no GPU readback. No-op unless gpu_trace_resolve_timing is on.
  void MaybeLogSmallGuestPass();

  // Recording-time composition snapshots taken at each pass-timestamp bracket
  // (cumulative DeferredCommandBuffer::RecordStats at pass begin/end). The
  // readback diffs begin[i+1] - end[i] to attribute what GPU commands live in
  // the inter-pass gap i (compute dispatches / copies / barriers) - an empty
  // composition under a large measured gap means GPU idle/starvation instead.
  struct PassBoundarySnap {
    uint32_t dispatches;
    uint32_t buffer_copies;
    uint32_t buffer_image_copies;
    uint32_t barriers;
    uint32_t draws;
    // Low framebuffer-pointer bits at the bracket - a cheap per-pass identity
    // to tell which render passes flank the dominant gap.
    uint32_t framebuffer_id;
    uint64_t buffer_copy_bytes;
    // gpu_trace_resolve_timing: GpuPassKind of this bracket (set at begin) so the
    // readback can sum span ticks per kind. 0 (kGuest) for untagged brackets.
    uint8_t kind;
  };
  PassBoundarySnap gap_snap_begin_[kMaxFramesInFlight][kMaxPassBrackets] = {};
  PassBoundarySnap gap_snap_end_[kMaxFramesInFlight][kMaxPassBrackets] = {};
  // Bracket count at IssueSwap time per frame slot: which inter-pass gap the
  // guest swap/present teardown lands in (if the dominant empty gap sits right
  // here, the hole is present/pacing interaction, not GPU work).
  uint32_t gpu_swap_bracket_[kMaxFramesInFlight] = {};

  // gpu_trace_resolve_timing diagnostic state. current_pass_kind_ = kind of the
  // currently open render pass (set when it is entered); pass_begin_draws_ =
  // cumulative recorded draw count at that pass's begin (the pass's own draw
  // count is record_stats().draws - pass_begin_draws_ at end). Threshold for
  // "small-draw pass" worth logging.
  static constexpr uint32_t kResolveTimingSmallPassDraws = 4;
  GpuPassKind current_pass_kind_ = GpuPassKind::kGuest;
  uint32_t pass_begin_draws_ = 0;
  // Descriptor of the most recent guest draw issued (captured in IssueDraw under
  // gpu_trace_resolve_timing); logged at pass end for small-draw passes to
  // identify what the anomalous 1-draw 52ms pass actually renders.
  struct GuestDrawDesc {
    uint64_t ps_hash;
    uint64_t vs_hash;
    uint32_t blendcontrol0;
    uint32_t colorcontrol;
    uint32_t color_mask;
    uint32_t depthcontrol;
    uint32_t color0_info;
    uint32_t depth_info;
    uint32_t host_vertex_count;
    uint32_t index_count;
    uint32_t prim_type;
    uint32_t ps_writes_depth;
    uint32_t ps_kills;
  };
  GuestDrawDesc last_guest_draw_desc_ = {};

  // THE EDRAM SOLVE, hybrid form (gpu_vulkan_hybrid_postprocess): set per-draw in
  // IssueDraw when this draw is a full-screen 1x-coverage post-process composite
  // being routed through the EDRAM-buffer/SSBO ROP path (edram_fragment_shader_
  // interlock machinery) while the frame's path stays kHostRenderTargets. The
  // draw helpers OR this into their GetPath()==kPixelShaderInterlock checks so the
  // composite takes the FSI (no-attachment, EDRAM-buffer-write) draw path,
  // collapsing its tile-I/O pass-break, while the overdraw main scene keeps host-
  // RT GMEM ROP. Always false unless the hybrid cvar is on (default byte-identical).
  bool hybrid_current_draw_composite_ = false;

  // gpu_trace_resolve_timing measurement (brick 2 ceiling): the current draw is a
  // full-screen post-process composite consumer (rect/quad whose pixel shader
  // samples a texture) - computed UNGATED (independent of the hybrid cvar) so a
  // guest pass opened by such a draw is timed under GpuPassKind::kGuestComposite.
  // Diagnostic classification only; does not change any rendering.
  bool current_draw_is_composite_consumer_ = false;

  // EDRAM render-target transfer counters (per frame), the suspected source of
  // the per-draw render-pass breaks / Adreno tile flushes. Incremented by the
  // render target cache; logged + reset at swap. transfer_calls = times
  // PerformTransfersAndResolveClears ran with >=1 transfer; transfers = total
  // Transfer entries processed; resolve_clears = resolve-clear invocations.
 public:
  // True once BD's field has been redirected into the native RT this frame. Used
  // by the render target cache to surgically skip EDRAM ownership-transfers that
  // run DURING/AFTER the field (we present the native RT, so field-surface EDRAM
  // re-aliasing is redundant) while keeping pre-field shadow/texture transfers.
  bool bd_native_field_rendered() const { return bd_native_field_rendered_; }
  // REAL-HLE (EDRAM deletion): true when a live native surface holds rendered
  // content for guest address `dest_base` (i.e. the field now SAMPLES that native
  // image via Brick B, so the EDRAM resolve/transfer that used to carry it is
  // dead weight and can be surgically dropped). Defined in the .cc (BdNativeRenderer
  // is incomplete here). Returns false unless gpu_bd_native_aux_rt is active.
  bool BdNativeSurfaceServes(uint32_t dest_base);
  void AddBdNativeResolveDropped() { ++bd_native_resolves_dropped_; }
  void AddRenderTargetTransferStats(uint32_t transfer_count,
                                    bool resolve_clear) {
    if (transfer_count) {
      ++rt_transfer_calls_;
      rt_transfers_ += transfer_count;
    }
    if (resolve_clear) {
      ++rt_resolve_clears_;
    }
  }
  // RT-as-texture rearch detector (increment 1): each guest EDRAM->shared-memory
  // resolve COPY and its dest byte length. The GPU trace pinned ~13% of BD's GPU to
  // these ResolveCopy compute dispatches + their RAM round-trip; this sizes how much
  // data BD round-trips through shared memory per frame (the volume the RT-as-texture
  // bridge would eliminate by sampling resident RTs directly). dest_base recorded so a
  // later increment can match it against texture fetch bases (the re-sampled subset).
  // EDRAM-recompiler FIRST BRICK - the resolve->sample dependency EDGE. Each
  // EDRAM->shared-memory resolve records not just its dest range but the SOURCE
  // RT identity (EDRAM base/pitch/format/msaa/depth), so a later texture fetch
  // landing in the dest range can be routed to the resident source RT directly
  // (RT-as-texture) or have its resolve deferred (lazy). Promotes the increment-1
  // detector from "counts the round-trip" to "knows the graph edge".
  struct ResolveEdge {
    uint32_t dest_start;  // copy_dest_extent_start (matched vs a fetch base)
    uint32_t dest_length;
    uint32_t dest_base;   // copy_dest_base (texture origin)
    uint32_t src_edram_base_tiles;
    uint32_t src_pitch_tiles;
    uint32_t src_format;  // xenos Color/DepthRenderTargetFormat value, as uint
    uint8_t src_msaa;     // xenos::MsaaSamples
    bool src_is_depth;
    // Packed RenderTargetKey of the source RT (base+pitch+format+msaa+depth) — the
    // FULL identity. Needed because BD renders every RT at EDRAM base 0 (base alone
    // aliases); this disambiguates, keying the native render-redirect unambiguously.
    uint32_t src_rt_key = 0;
  };
  void AddResolveCopyStats(const ResolveEdge& edge) {
    ++rt_resolve_copies_;
    rt_resolve_copy_bytes_ += edge.dest_length;
    if (frame_resolve_edges_.size() < 256 && edge.dest_length) {
      frame_resolve_edges_.push_back(edge);
    }
    // REAL-HLE resolve graph (persistent across frames): record src RT identity ->
    // dest guest address so the native render-redirect knows, BEFORE this frame's
    // resolve, which native surface an EDRAM-bound draw should render into. BD's
    // addresses are stable frame-to-frame, so the prior edge is authoritative.
    // Keyed by the FULL packed RenderTargetKey (base+pitch+format+msaa) — NOT base
    // alone, which aliases (BD renders every RT at EDRAM base 0).
    if (edge.dest_length && edge.dest_base && edge.src_rt_key) {
      persistent_resolve_edges_[edge.src_rt_key] = edge;
    }
  }
  // The persistent (cross-frame) resolve edge for a src RT identity (packed
  // RenderTargetKey), or nullptr if that RT has never been resolved. Used by the
  // native render-redirect to map a bound EDRAM RT -> its resolve-dest guest
  // address (= native surface key).
  const ResolveEdge* PersistentResolveEdgeForSrc(uint32_t src_rt_key) const {
    auto it = persistent_resolve_edges_.find(src_rt_key);
    return it == persistent_resolve_edges_.end() ? nullptr : &it->second;
  }
  // The resolve edge whose dest range contains this texture-fetch base (the graph
  // edge: fetch <- source RT), or nullptr if the fetch is not RT-fed this frame.
  const ResolveEdge* ResolveEdgeForBase(uint32_t base) const {
    for (const ResolveEdge& e : frame_resolve_edges_) {
      if (base >= e.dest_start && base < e.dest_start + e.dest_length) {
        return &e;
      }
    }
    return nullptr;
  }
  bool IsResolveFedTextureBase(uint32_t base) const {
    return ResolveEdgeForBase(base) != nullptr;
  }
  void AddRtFedTextureStat() { ++rt_fed_textures_; }
  // Per dest-RT transfer pass: would it be format-compatible with the guest
  // draw pass (eligible for render-pass reuse to avoid a tile flush)?
  void AddTransferFormatStats(bool same_format_as_guest_pass) {
    if (same_format_as_guest_pass) {
      ++rt_transfer_same_format_;
    } else {
      ++rt_transfer_diff_format_;
    }
  }
  // In-pass EDRAM transfers (gpu_vulkan_inpass_edram_transfers): destinations
  // transferred inside the guest render pass this call, and why others were
  // skipped (integer-reinterpret format vs feedback/cross-copy/bounds).
  void AddInpassTransferStats(uint32_t inpass_dests, uint32_t skipped_format,
                              uint32_t skipped_other) {
    rt_inpass_transfer_dests_ += inpass_dests;
    rt_inpass_skipped_format_ += skipped_format;
    rt_inpass_skipped_other_ += skipped_other;
  }
  // gpu_vulkan_classify_img_sr_breaks (input-attachment feasibility, BD-30):
  // tally a FEEDBACK EDRAM ownership transfer (source bound as a current
  // framebuffer attachment - the subset gpu_vulkan_inpass_edram_transfers always
  // leaves on the legacy store/load pass break, i.e. BD's ~42 brk_img_sr breaks
  // / ~79ms). same_pixel == the source->dest EDRAM coordinate transform is the
  // identity (same base, pitch, MSAA, bpp, color/depth class), the ONLY case a
  // Vulkan input attachment (subpassLoad, strictly the fragment's own position)
  // could replace the store/load; otherwise the transfer reads a REMAPPED texel.
  void AddFeedbackTransferStats(bool same_pixel) {
    ++rt_feedback_transfers_;
    if (same_pixel) {
      ++rt_feedback_samepix_;
    }
  }
  // Throttle (<=16/frame) for the per-feedback-transfer detail log in the render
  // target cache; returns true and consumes a slot while under the cap.
  bool ShouldLogFeedbackDetail() {
    if (rt_feedback_detail_logged_ >= 16) {
      return false;
    }
    ++rt_feedback_detail_logged_;
    return true;
  }

 private:
  uint32_t rt_transfer_calls_ = 0;
  uint32_t rt_transfers_ = 0;
  uint32_t rt_resolve_clears_ = 0;
  // RT-as-texture rearch detector (increment 1), reset at swap.
  uint32_t rt_resolve_copies_ = 0;
  uint32_t rt_resolve_copy_bytes_ = 0;
  uint32_t rt_fed_textures_ = 0;
  // RT-as-texture (increment 1): pixel texture fetches actually bound straight to
  // a resident render target this frame (the served subset of rt_fed_textures_).
  uint32_t rt_served_textures_ = 0;
  std::vector<ResolveEdge> frame_resolve_edges_;
  // Persistent (never cleared per-frame) src EDRAM RT -> resolve-dest edge map for
  // the native-HLE render-redirect (see AddResolveCopyStats / NativeSrcKey).
  std::unordered_map<uint32_t, ResolveEdge> persistent_resolve_edges_;
  // Per-frame attribution of render-pass breaks at the per-draw enter point:
  // _barrier = ended to flush a pending barrier; _rt_change = ended because the
  // render pass / framebuffer changed (RT reconfiguration).
  uint32_t rt_pass_break_barrier_ = 0;
  uint32_t rt_pass_break_rt_change_ = 0;
  uint32_t rt_transfer_same_format_ = 0;
  uint32_t rt_transfer_diff_format_ = 0;
  uint32_t rt_inpass_transfer_dests_ = 0;
  uint32_t rt_inpass_skipped_format_ = 0;
  uint32_t rt_inpass_skipped_other_ = 0;
  // gpu_vulkan_classify_img_sr_breaks: feedback EDRAM ownership transfers this
  // frame (source bound as a current attachment - the legacy-break subset) and
  // how many are same-pixel (input-attachment / subpassLoad eligible). _detail
  // throttles the per-transfer base/pitch/class log. All read-only; byte-
  // identical when the cvar is off.
  uint32_t rt_feedback_transfers_ = 0;
  uint32_t rt_feedback_samepix_ = 0;
  uint32_t rt_feedback_detail_logged_ = 0;
  // Per-frame attribution of WHAT barriers actually end an open render pass
  // (the tiler-killing breaks). Tallied in SubmitBarriers when it ends a live
  // pass: how many such break-flushes, and the composition of the flushed
  // barriers (buffer = shared-memory/upload hazards; image split by target
  // layout: shader-read = texture sampling hazards, vs other = RT/transfer).
  // This tells the rewrite which barrier source to coalesce/hoist first.
  uint32_t brk_open_breaks_ = 0;
  uint32_t brk_buffer_barriers_ = 0;
  uint32_t brk_img_shaderread_ = 0;
  uint32_t brk_img_other_ = 0;
  // gpu_vulkan_classify_img_sr_breaks: sub-classification of the brk_img_sr
  // (sampled-as-SHADER_READ) breaks, to scope the input-attachment / GMEM-
  // residency lever. sr_rtsrc_ = the SHADER_READ image was a render target
  // (oldLayout a color/depth ATTACHMENT layout): an EDRAM ownership-transfer /
  // resolve SOURCE read (already addressable via gpu_vulkan_inpass_edram_
  // transfers, except the feedback subset). sr_texsample_ = a non-attachment
  // image (a guest texture loaded from shared memory, e.g. a resolved-scene
  // composite source) - the class needing the full GMEM-residency rework.
  // sr_fscomposite_ = of ALL brk_img_sr breaks, those whose triggering guest
  // draw is a full-screen composite (rect / <=6-vert quad + pixel shader +
  // color write) - the same-pixel input-attachment CANDIDATE count (per-shader
  // same-pixel confirmation still required). All read-only; byte-identical when
  // off. brk_img_sr_detail_logged_ throttles the per-break detail log.
  uint32_t brk_img_sr_rtsrc_ = 0;
  uint32_t brk_img_sr_texsample_ = 0;
  uint32_t brk_img_sr_fscomposite_ = 0;
  // sr_rtsrc_fscomp_ = of the RENDER-TO-TEXTURE breaks (rtsrc: a just-rendered
  // RT transitions COLOR/DEPTH_ATTACHMENT -> SHADER_READ so a later pass can
  // sample it), those whose consumer draw is a full-screen composite. This is
  // the ACTUAL input-attachment-eligible set for BD's village field (0 ownership
  // transfers => all brk_img_sr are rtsrc), which sr_fscomposite_ structurally
  // CANNOT count (it gates on the texsample path, oldLayout=TRANSFER_DST, which
  // is empty for rtsrc breaks). This counter sizes the GMEM-residency win.
  uint32_t brk_img_sr_rtsrc_fscomp_ = 0;
  uint32_t brk_img_sr_detail_logged_ = 0;
  // BD input-attachment merge (gpu_vulkan_feedback_merge): deferred-stream
  // position of the CURRENT pass's BeginRenderPass, captured at pass begin so a
  // same-pixel composite consumer detected at the next break can retroactively
  // repoint it (PatchBeginRenderPassTargets) to the 2-subpass feedback render
  // pass (this pass = producer subpass 0). SIZE_MAX = none captured / cvar off.
  size_t feedback_producer_begin_pos_ = SIZE_MAX;
  // Set by the break-time detection when the CURRENT draw is a merge CONSUMER (a
  // same-pixel composite reading the producer RT as an input attachment). Drives
  // the shader modification (feedback_input_attachment), the feedback pipeline
  // (subpass 1), and the input-attachment descriptor write. Cleared after the
  // consumer draw / when not merging.
  bool feedback_merge_active_ = false;
  // Set at the merge redirect: true if the merge uses the 1-attachment in-place
  // (producer == consumer RT) feedback render pass, false for the 2-attachment
  // variant. Read by the pipeline cache to pick the matching feedback render pass.
  bool feedback_merge_in_place_ = false;
  uint32_t feedback_merge_producer_fetch_constant_ = 0;
  VkImageView feedback_merge_producer_view_ = VK_NULL_HANDLE;
  // Consumer pixel-shader texture count for the active composite. 1 = the merge
  // path; >1 = a multi-texture composite (logged for structure; the mixed-
  // descriptor merge gates on this).
  uint32_t feedback_merge_texcount_ = 0;
  // Count of feedback merges performed this frame (diagnostic, logged in the
  // draw-outcomes line; reset per frame).
  uint32_t rt_feedback_merges_ = 0;
  // Scene-lock: set once when gpu_freeze_at_guest_ms fires (guest near-frozen
  // for confound-free GPU A/B on an identical frame).
  bool gpu_scene_lock_frozen_ = false;
  // gpu_freeze_ab_alternate_vrs: once frozen, alternate VRS on/off in ~30-frame
  // blocks within the SAME run so it yields a matched VRS A/B (phase logged per
  // frame for gpu_frame_us bucketing). Sidesteps BD boot non-determinism + the
  // save-state path.
  bool gpu_freeze_vrs_phase_on_ = false;
  uint32_t gpu_freeze_ab_frame_counter_ = 0;
  // True when the VRS A/B alternation is live this frame (frozen OR free-running
  // past gpu_vrs_enable_after_guest_ms). Free-running mode lets the A/B start the
  // instant the field renders - robust to BD's variable field-reach wall-time +
  // the thermal watchdog (no precise freeze window needed).
  bool gpu_ab_alt_active_ = false;

  // Cache render pass currently started in the command buffer with the
  // framebuffer.
  VkRenderPass current_render_pass_;
  const VulkanRenderTargetCache::Framebuffer* current_framebuffer_;

  // Currently bound graphics pipeline, either from the pipeline cache (with
  // potentially deferred creation - current_external_graphics_pipeline_ is
  // VK_NULL_HANDLE in this case) or a non-Xenos one
  // (current_guest_graphics_pipeline_ is VK_NULL_HANDLE in this case).
  // TODO(Triang3l): Change to a deferred compilation handle.
  VkPipeline current_guest_graphics_pipeline_;
  VkPipeline current_external_graphics_pipeline_;
  VkPipeline current_external_compute_pipeline_;

  // Pipeline layout of the current guest graphics pipeline.
  const PipelineLayout* current_guest_graphics_pipeline_layout_;
  VkDescriptorBufferInfo current_constant_buffer_infos_
      [SpirvShaderTranslator::kConstantBufferCount];
  // Whether up-to-date data has been written to constant (uniform) buffers, and
  // the buffer infos in current_constant_buffer_infos_ point to them.
  uint32_t current_constant_buffers_up_to_date_;
  VkDescriptorSet current_graphics_descriptor_sets_
      [SpirvShaderTranslator::kDescriptorSetCount];
  // Whether descriptor sets in current_graphics_descriptor_sets_ point to
  // up-to-date data.
  uint32_t current_graphics_descriptor_set_values_up_to_date_;
  // Whether the descriptor sets currently bound to the command buffer - only
  // low bits for the descriptor set layouts that remained the same are kept
  // when changing the pipeline layout. May be out of sync with
  // current_graphics_descriptor_set_values_up_to_date_, but should be ensured
  // to be a subset of it at some point when it becomes important; bits for
  // non-existent descriptor set layouts may also be set, but need to be ignored
  // when they start to matter.
  uint32_t current_graphics_descriptor_sets_bound_up_to_date_;
  static_assert(
      SpirvShaderTranslator::kDescriptorSetCount <=
          sizeof(current_graphics_descriptor_set_values_up_to_date_) * CHAR_BIT,
      "Bit fields storing descriptor set validity must be large enough");
  static_assert(
      SpirvShaderTranslator::kDescriptorSetCount <=
          sizeof(current_graphics_descriptor_sets_bound_up_to_date_) * CHAR_BIT,
      "Bit fields storing descriptor set validity must be large enough");

  // Float constant usage masks of the last draw call.
  uint64_t current_float_constant_map_vertex_[4];
  uint64_t current_float_constant_map_pixel_[4];

  // System shader constants.
  SpirvShaderTranslator::SystemConstants system_constants_;

  // Temporary storage for memexport stream constants used in the draw.
  std::vector<draw_util::MemExportRange> memexport_ranges_;

  PresentResolveCandidate recent_present_resolve_candidate_;
  PresentResolveCandidate scored_present_resolve_candidate_;
  PresentResolveCandidate scored_present_resolve_fallback_candidate_;
  uint64_t recent_present_resolve_sequence_ = 0;
  uint64_t scored_present_resolve_sequence_ = 0;
  int32_t scored_present_resolve_readback_count_ = 0;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_
