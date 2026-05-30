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

  VkBuffer buffer() const { return buffer_; }

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

  // Direct (host-visible) variant of UploadRanges.
  bool UploadRangesDirect(const std::vector<std::pair<uint32_t, uint32_t>>&
                              upload_page_ranges);

  Usage last_usage_;
  std::pair<uint32_t, uint32_t> last_written_range_;

  // UMA direct-write race guard. The direct path memcpys guest pages straight
  // into the persistently-mapped GPU buffer at command-recording time. If a
  // prior, still-in-flight submission's draws are reading the buffer, that
  // memcpy would overwrite data mid-read -> corruption -> present wedges. The
  // staging path is safe because its pool buffers are submission-tagged and
  // reused only after the GPU finishes. We mirror that: record the submission
  // in which the buffer was last read, and before a direct write wait for any
  // such prior submission to complete. Set in Use() on read usages; consumed in
  // UploadRangesDirect. Only meaningful when buffer_host_visible_.
  uint64_t direct_last_read_submission_ = 0;

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
