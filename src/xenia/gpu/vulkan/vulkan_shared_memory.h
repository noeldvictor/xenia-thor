/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_SHARED_MEMORY_H_
#define XENIA_GPU_VULKAN_VULKAN_SHARED_MEMORY_H_

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "xenia/gpu/shared_memory.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/memory.h"
#include "xenia/ui/vulkan/vulkan_upload_buffer_pool.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;

class VulkanSharedMemory : public SharedMemory {
 public:
  VulkanSharedMemory(VulkanCommandProcessor& command_processor, Memory& memory,
                     TraceWriter& trace_writer,
                     VkPipelineStageFlags guest_shader_pipeline_stages);
  ~VulkanSharedMemory() override;

  bool Initialize();
  void Shutdown(bool from_destructor = false);

  void CompletedSubmissionUpdated();
  void EndSubmission();

  enum class Usage {
    // Index buffer, vfetch, compute read, transfer source.
    kRead,
    // Index buffer, vfetch, memexport.
    kGuestDrawReadWrite,
    kComputeWrite,
    kTransferDestination,
  };
  // Inserts a pipeline barrier for the target usage, also ensuring consecutive
  // read-write accesses are ordered with each other.
  void Use(Usage usage, std::pair<uint32_t, uint32_t> written_range = {});

  VkBuffer buffer() const {
    return (double_buffer_enabled_ && current_version_ == 1) ? buffer_version1_
                                                             : buffer_;
  }

  // Double-buffer (gpu_shared_memory_double_buffer) public accessors for the
  // command processor's per-version cached descriptor sets.
  bool double_buffer_active() const { return double_buffer_enabled_; }
  uint32_t current_version() const { return current_version_; }
  VkBuffer buffer_version(uint32_t v) const { return buffer_for_version(v); }

  // Returns true if any downloads were submitted to the command processor.
  bool InitializeTraceSubmitDownloads();
  void InitializeTraceCompleteDownloads();

 protected:
  bool AllocateSparseHostGpuMemoryRange(uint32_t offset_allocations,
                                        uint32_t length_allocations) override;

  bool UploadRanges(const std::vector<std::pair<uint32_t, uint32_t>>&
                        upload_page_ranges) override;

 private:
  void GetUsageMasks(Usage usage, VkPipelineStageFlags& stage_mask,
                     VkAccessFlags& access_mask) const;

  VulkanCommandProcessor& command_processor_;
  TraceWriter& trace_writer_;
  VkPipelineStageFlags guest_shader_pipeline_stages_;

  VkBuffer buffer_ = VK_NULL_HANDLE;
  uint32_t buffer_memory_type_;
  // Single for non-sparse, every allocation so far for sparse.
  std::vector<VkDeviceMemory> buffer_memory_;

  // Unified-memory direct-write path (gpu_uma_direct_shared_memory). On an
  // integrated GPU the buffer memory can be HOST_VISIBLE | DEVICE_LOCAL, so it
  // is persistently mapped and guest pages are written straight into the GPU
  // buffer with no staging buffer and no transfer copy. Only used on the
  // non-sparse buffer path.
  bool buffer_host_visible_ = false;
  bool buffer_host_coherent_ = false;
  void* buffer_host_mapping_ = nullptr;

  // Double-buffer (gpu_shared_memory_double_buffer): two host-visible versions
  // of the buffer. version 0 is the existing buffer_ / buffer_host_mapping_;
  // version 1 is below. current_version_ is the one the GPU reads + CP writes.
  static constexpr uint32_t kVersionCount = 2;
  bool double_buffer_enabled_ = false;
  uint32_t current_version_ = 0;
  VkBuffer buffer_version1_ = VK_NULL_HANDLE;
  VkDeviceMemory buffer_version1_memory_ = VK_NULL_HANDLE;
  void* buffer_version1_mapping_ = nullptr;
  // Per-version page staleness: bit set = that page was written to the OTHER
  // version since this version was last current, so this version is behind on it
  // and must be re-synced (GPU copy from the current version) before the GPU
  // reads it here. Indexed [version][page]. Sized to the page count.
  std::vector<uint64_t> version_stale_bits_[kVersionCount];
  // Submission in which each version was last READ by the GPU (for contention).
  uint64_t version_last_read_submission_[kVersionCount] = {0, 0};

  // The buffer the GPU currently reads / the CP currently writes (== buffer_ +
  // buffer_host_mapping_ for version 0). When double-buffering is off these
  // always return buffer_ / buffer_host_mapping_.
  VkBuffer current_buffer() const {
    return (double_buffer_enabled_ && current_version_ == 1) ? buffer_version1_
                                                             : buffer_;
  }
  void* current_buffer_mapping() const {
    return (double_buffer_enabled_ && current_version_ == 1)
               ? buffer_version1_mapping_
               : buffer_host_mapping_;
  }
  VkBuffer buffer_for_version(uint32_t v) const {
    return v == 1 ? buffer_version1_ : buffer_;
  }
  // Switches to a free version if the current one has an in-flight reader and we
  // are about to overwrite it; GPU-copies the new version's stale ranges from the
  // old current version first. Call at submission begin (from EndSubmission of
  // the prior submission is wrong; call it lazily before the first direct write
  // of a submission - see .cc). Returns the (possibly switched) current version.
  void MaybeSwitchVersionForWrite();
  void MarkVersionRead(uint64_t submission);
  void SetPageStaleInOtherVersions(uint32_t page_first, uint32_t page_count,
                                   uint32_t fresh_version);

  // Direct (host-visible) variant of UploadRanges.
  bool UploadRangesDirect(const std::vector<std::pair<uint32_t, uint32_t>>&
                              upload_page_ranges);

  // UMA in-flight read tracking (gpu_uma_direct_shared_memory TDR fix,
  // gpu_uma_smart_sync). The direct path overwrites guest pages IN PLACE on the
  // CPU timeline, but the Adreno tiler reads them deferred while binning/
  // rendering a PRIOR submission -> CPU-write-vs-deferred-read race -> torn
  // index/vfetch -> GPU MMU-fault TDR. The brute fix gpu_uma_serialize_before_
  // write drains ALL GPU work before every write (kills the UMA win). Correct +
  // cheap fix: track the single most-recent submission index in which the buffer
  // was consumed as a guest READ; before a direct write, if that submission is a
  // PRIOR (already-closed) one that has not completed, wait ONLY for it. Because
  // submissions complete in order on the one graphics queue, waiting for the
  // latest reader guarantees every earlier reader is also done - no full drain,
  // no deadlock (never waits on the still-open current submission).
  uint64_t uma_last_read_submission_ = 0;
  // Latest submission that WROTE guest data into the buffer (EDRAM->shared
  // resolve = kComputeWrite, memexport = kGuestDrawReadWrite, transfer dest).
  // gpu_uma_smart_sync_writes makes the direct path wait on this too, closing
  // the CPU-write-vs-in-flight-GPU-write WAW the GPU-only barrier can't order.
  uint64_t uma_last_write_submission_ = 0;

  Usage last_usage_;
  std::pair<uint32_t, uint32_t> last_written_range_;

  std::unique_ptr<ui::vulkan::VulkanUploadBufferPool> upload_buffer_pool_;
  std::vector<VkBufferCopy> upload_regions_;

  // Created temporarily, only for downloading.
  VkBuffer trace_download_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory trace_download_buffer_memory_ = VK_NULL_HANDLE;
  void ResetTraceDownload();
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_SHARED_MEMORY_H_
