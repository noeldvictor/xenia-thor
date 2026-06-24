/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_shared_memory.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_diagnostic_counters.h"
#include "xenia/ui/vulkan/vulkan_util.h"

DEFINE_bool(vulkan_sparse_shared_memory, true,
            "Enable sparse binding for shared memory emulation. Disabling it "
            "increases video memory usage - a 512 MB buffer is created - but "
            "allows graphics debuggers that don't support sparse binding to "
            "work.",
            "Vulkan");
DEFINE_bool(
    gpu_uma_direct_shared_memory, false,
    "Unified-memory optimization for integrated GPUs (e.g. mobile Adreno on "
    "AYN Thor). When the device exposes a HOST_VISIBLE | DEVICE_LOCAL memory "
    "type, back the shared-memory buffer with it and write guest pages "
    "directly into the GPU buffer, skipping the staging upload buffer and the "
    "vkCmdCopyBuffer transfer. Forces a non-sparse 512 MB buffer. "
    "Experimental - validate rendering per title on device before trusting it; "
    "a coherency bug here shows up as corrupted or black frames.",
    "Vulkan");
DEFINE_bool(
    gpu_uma_serialize_before_write, false,
    "DIAGNOSTIC for the Adreno UMA GPU-hang (TDR): before each direct in-place "
    "write of guest pages into the persistently-mapped shared-memory buffer, "
    "wait for ALL previously submitted GPU work to complete, so no in-flight "
    "(deferred tiler) draw can still be reading pages we are about to overwrite. "
    "Heavy-handed (serializes CPU and GPU); only for confirming the "
    "CPU-write-vs-deferred-GPU-read race hypothesis. Has no effect unless "
    "gpu_uma_direct_shared_memory is on.",
    "Vulkan");
DEFINE_bool(
    gpu_uma_smart_sync, true,
    "Adreno UMA TDR FIX (the shippable one): before a direct in-place write of "
    "guest pages into the persistently-mapped shared-memory buffer, wait ONLY "
    "for the last already-CLOSED submission that consumed the buffer as a guest "
    "read (if it hasn't completed), instead of draining ALL GPU work "
    "(gpu_uma_serialize_before_write). Submissions complete in order on the one "
    "graphics queue, so waiting for the latest prior reader guarantees all "
    "earlier readers are done - removing the CPU-write-vs-deferred-read race "
    "without serializing the whole GPU. Makes gpu_uma_direct_shared_memory "
    "safe. No effect unless gpu_uma_direct_shared_memory is on.",
    "Vulkan");
DEFINE_bool(
    gpu_uma_smart_sync_writes, true,
    "Extends gpu_uma_smart_sync to also guard the WRITE-after-write hazard: a "
    "direct in-place CPU upload of guest pages can race a PRIOR in-flight GPU "
    "write to the same range (EDRAM->shared-memory resolve / memexport). The "
    "shared-buffer memory barrier only orders GPU-vs-GPU accesses - it cannot "
    "order a CPU memcpy against a GPU write - so the read-only guard misses it. "
    "With this on, the direct path waits for the latest prior submission that "
    "either READ or WROTE the buffer (still a single-submission wait, not a "
    "drain). Off = read-only guard (the original smart-sync). No effect unless "
    "gpu_uma_direct_shared_memory + gpu_uma_smart_sync are on.",
    "Vulkan");
DEFINE_bool(
    gpu_uma_strong_coherency, false,
    "EXPERIMENT (b) for the Adreno UMA GPU-hang (TDR): when writing guest pages "
    "directly into the persistently-mapped HOST_VISIBLE|DEVICE_LOCAL shared "
    "buffer, apply maximal host->device coherency: (1) always "
    "vkFlushMappedMemoryRanges even if the heap reports HOST_COHERENT (covers "
    "write-combining-marked-coherent driver quirks), and (2) widen the "
    "host-write->guest-read barrier to the WHOLE buffer with "
    "ALL_COMMANDS/MEMORY_READ instead of the span-bounded HOST->read barrier. "
    "Tests whether the intermittent GPU MMU fault is a coherency/visibility gap "
    "the desktop path never hit. No effect unless gpu_uma_direct_shared_memory.",
    "Vulkan");
DEFINE_bool(
    gpu_shared_memory_double_buffer, false,
    "Adreno UMA double-buffer (race-free zero-staging shared memory). Keeps TWO "
    "HOST_VISIBLE|DEVICE_LOCAL persistently-mapped 512 MB versions of the shared "
    "buffer and writes guest pages into the version the GPU is NOT currently "
    "reading, switching versions on contention so a CPU upload never overwrites "
    "pages an in-flight deferred-tiler draw is still reading - fixing the "
    "gpu_uma_direct_shared_memory TDR hang without the serializing smart-sync "
    "wait. The off-version is brought current on switch by a GPU-side copy of the "
    "stale ranges (no extra CPU). Requires gpu_uma_direct_shared_memory; "
    "default-off, validate rendering per title on device. Doubles shared-memory "
    "VRAM (1 GB, trivial on the 16 GB UMA Thor).",
    "Vulkan");

namespace xe {
namespace gpu {
namespace vulkan {

VulkanSharedMemory::VulkanSharedMemory(
    VulkanCommandProcessor& command_processor, Memory& memory,
    TraceWriter& trace_writer,
    VkPipelineStageFlags guest_shader_pipeline_stages)
    : SharedMemory(memory),
      command_processor_(command_processor),
      trace_writer_(trace_writer),
      guest_shader_pipeline_stages_(guest_shader_pipeline_stages) {}

VulkanSharedMemory::~VulkanSharedMemory() { Shutdown(true); }

bool VulkanSharedMemory::Initialize() {
  InitializeCommon();

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  const VkBufferCreateFlags sparse_flags =
      VK_BUFFER_CREATE_SPARSE_BINDING_BIT |
      VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT;

  // Try to create a sparse buffer.
  VkBufferCreateInfo buffer_create_info;
  buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_create_info.pNext = nullptr;
  buffer_create_info.flags = sparse_flags;
  buffer_create_info.size = kBufferSize;
  buffer_create_info.usage =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_create_info.queueFamilyIndexCount = 0;
  buffer_create_info.pQueueFamilyIndices = nullptr;
  // The unified-memory direct-write path needs a single persistently mapped
  // allocation, so it always uses the non-sparse buffer below.
  //
  // The Mesa Turnip (Freedreno) driver advertises sparseResidencyBuffer, but its
  // sparse-buffer residency does not reliably back the 512 MB shared-memory pages
  // on Adreno: guest programmable-vertex-fetch reads from the storage buffer come
  // back as zero, so every gl_Position collapses to the origin and the whole
  // scene is black (Blue Dragon and others). Uniform buffers - which are not
  // sparse - read fine, which is how this was isolated. Other emulators
  // (Yuzu/Vita3K/etc.) run on Turnip because they don't depend on sparse
  // residency for a buffer this large. Force the plain, fully-backed buffer on
  // Turnip; a 512 MB device-local allocation is trivial on the unified-memory
  // Thor, and this is the spec-robust path that works on every driver.
  const bool driver_is_mesa_turnip =
      vulkan_device->properties().driverID == VK_DRIVER_ID_MESA_TURNIP;
  if (cvars::vulkan_sparse_shared_memory &&
      !cvars::gpu_uma_direct_shared_memory && !driver_is_mesa_turnip &&
      vulkan_device->properties().sparseResidencyBuffer) {
    if (dfn.vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer_) ==
        VK_SUCCESS) {
      VkMemoryRequirements buffer_memory_requirements;
      dfn.vkGetBufferMemoryRequirements(device, buffer_,
                                        &buffer_memory_requirements);
      if (xe::bit_scan_forward(buffer_memory_requirements.memoryTypeBits &
                                   vulkan_device->memory_types().device_local,
                               &buffer_memory_type_)) {
        uint32_t allocation_size_log2;
        xe::bit_scan_forward(
            std::max(uint64_t(buffer_memory_requirements.alignment),
                     uint64_t(1)),
            &allocation_size_log2);
        if (allocation_size_log2 < kBufferSizeLog2) {
          // Maximum of 1024 allocations in the worst case for all of the
          // buffer because of the overall 4096 allocation count limit on
          // Windows drivers.
          InitializeSparseHostGpuMemory(
              std::max(allocation_size_log2,
                       std::max(kHostGpuMemoryOptimalSparseAllocationLog2,
                                kBufferSizeLog2 - uint32_t(10))));
        } else {
          // Shouldn't happen on any real platform, but no point allocating the
          // buffer sparsely.
          dfn.vkDestroyBuffer(device, buffer_, nullptr);
          buffer_ = VK_NULL_HANDLE;
        }
      } else {
        XELOGE(
            "Shared memory: Failed to get a device-local Vulkan memory type "
            "for the sparse buffer");
        dfn.vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
      }
    } else {
      XELOGE("Shared memory: Failed to create the {} MB Vulkan sparse buffer",
             kBufferSize >> 20);
    }
  }

  // Create a non-sparse buffer if there were issues with the sparse buffer.
  if (buffer_ == VK_NULL_HANDLE) {
    XELOGGPU(
        "Vulkan sparse binding is not used for shared memory emulation - video "
        "memory usage may increase significantly because a full {} MB buffer "
        "will be created",
        kBufferSize >> 20);
    buffer_create_info.flags &= ~sparse_flags;
    if (dfn.vkCreateBuffer(device, &buffer_create_info, nullptr, &buffer_) !=
        VK_SUCCESS) {
      XELOGE("Shared memory: Failed to create the {} MB Vulkan buffer",
             kBufferSize >> 20);
      Shutdown();
      return false;
    }
    VkMemoryRequirements buffer_memory_requirements;
    dfn.vkGetBufferMemoryRequirements(device, buffer_,
                                      &buffer_memory_requirements);
    const ui::vulkan::VulkanDevice::MemoryTypes& memory_types =
        vulkan_device->memory_types();
    // Prefer HOST_VISIBLE | DEVICE_LOCAL memory for the unified-memory direct
    // path so guest pages can be written straight into the GPU buffer.
    const uint32_t host_visible_device_local =
        buffer_memory_requirements.memoryTypeBits & memory_types.device_local &
        memory_types.host_visible;
    buffer_host_visible_ = cvars::gpu_uma_direct_shared_memory &&
                           host_visible_device_local != 0;
    if (buffer_host_visible_) {
      xe::bit_scan_forward(host_visible_device_local, &buffer_memory_type_);
      buffer_host_coherent_ =
          (memory_types.host_coherent & (uint32_t(1) << buffer_memory_type_)) !=
          0;
      XELOGGPU(
          "Shared memory: using unified-memory direct-write path (memory type "
          "{}, {})",
          buffer_memory_type_,
          buffer_host_coherent_ ? "host-coherent" : "needs explicit flush");
    } else {
      if (cvars::gpu_uma_direct_shared_memory) {
        XELOGW(
            "Shared memory: gpu_uma_direct_shared_memory requested but no "
            "HOST_VISIBLE | DEVICE_LOCAL memory type is available; falling "
            "back to the staged device-local path");
      }
      if (!xe::bit_scan_forward(buffer_memory_requirements.memoryTypeBits &
                                    memory_types.device_local,
                                &buffer_memory_type_)) {
        XELOGE(
            "Shared memory: Failed to get a device-local Vulkan memory type "
            "for the buffer");
        Shutdown();
        return false;
      }
    }
    VkMemoryAllocateInfo buffer_memory_allocate_info;
    VkMemoryAllocateInfo* buffer_memory_allocate_info_last =
        &buffer_memory_allocate_info;
    buffer_memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buffer_memory_allocate_info.pNext = nullptr;
    buffer_memory_allocate_info.allocationSize =
        buffer_memory_requirements.size;
    buffer_memory_allocate_info.memoryTypeIndex = buffer_memory_type_;
    VkMemoryDedicatedAllocateInfo buffer_memory_dedicated_allocate_info;
    if (vulkan_device->extensions().ext_1_1_KHR_dedicated_allocation) {
      buffer_memory_allocate_info_last->pNext =
          &buffer_memory_dedicated_allocate_info;
      buffer_memory_allocate_info_last =
          reinterpret_cast<VkMemoryAllocateInfo*>(
              &buffer_memory_dedicated_allocate_info);
      buffer_memory_dedicated_allocate_info.sType =
          VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
      buffer_memory_dedicated_allocate_info.pNext = nullptr;
      buffer_memory_dedicated_allocate_info.image = VK_NULL_HANDLE;
      buffer_memory_dedicated_allocate_info.buffer = buffer_;
    }
    VkDeviceMemory buffer_memory;
    if (dfn.vkAllocateMemory(device, &buffer_memory_allocate_info, nullptr,
                             &buffer_memory) != VK_SUCCESS) {
      XELOGE(
          "Shared memory: Failed to allocate {} MB of memory for the Vulkan "
          "buffer",
          kBufferSize >> 20);
      Shutdown();
      return false;
    }
    buffer_memory_.push_back(buffer_memory);
    if (dfn.vkBindBufferMemory(device, buffer_, buffer_memory, 0) !=
        VK_SUCCESS) {
      XELOGE("Shared memory: Failed to bind memory to the Vulkan buffer");
      Shutdown();
      return false;
    }
    if (buffer_host_visible_) {
      if (dfn.vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0,
                          &buffer_host_mapping_) != VK_SUCCESS) {
        XELOGE(
            "Shared memory: Failed to map the unified-memory shared buffer; "
            "disable gpu_uma_direct_shared_memory");
        buffer_host_mapping_ = nullptr;
        Shutdown();
        return false;
      }
    }

    // Double-buffer (gpu_shared_memory_double_buffer): allocate a SECOND
    // host-visible|device-local persistently-mapped 512 MB buffer (version 1).
    // version 0 is buffer_ / buffer_host_mapping_ created above. On contention
    // the CP writes the version the GPU is NOT reading, so a direct CPU upload
    // never overwrites pages an in-flight deferred-tiler draw is still reading.
    // Correctness over the optimization: any failure here logs, frees the
    // partial version-1 resources, and leaves double_buffer_enabled_ = false
    // (the existing single-buffer path is then byte-identical).
    if (cvars::gpu_shared_memory_double_buffer && buffer_host_visible_ &&
        buffer_host_mapping_) {
      bool version1_ok = true;
      // Same buffer as version 0 - non-sparse (the sparse flags were already
      // cleared above for the non-sparse buffer), same usage.
      VkBufferCreateInfo version1_buffer_create_info = buffer_create_info;
      version1_buffer_create_info.flags &= ~sparse_flags;
      if (dfn.vkCreateBuffer(device, &version1_buffer_create_info, nullptr,
                             &buffer_version1_) != VK_SUCCESS) {
        XELOGW(
            "Shared memory: gpu_shared_memory_double_buffer: failed to create "
            "the version-1 buffer; falling back to single-buffer direct path");
        buffer_version1_ = VK_NULL_HANDLE;
        version1_ok = false;
      }
      if (version1_ok) {
        // Mirror version 0's memory type (the HOST_VISIBLE|DEVICE_LOCAL type
        // selected above). Allocate kBufferSize, not the version-1 buffer's
        // reported size, to match version 0's persistent mapping.
        VkMemoryAllocateInfo version1_allocate_info;
        VkMemoryAllocateInfo* version1_allocate_info_last =
            &version1_allocate_info;
        version1_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        version1_allocate_info.pNext = nullptr;
        version1_allocate_info.allocationSize = kBufferSize;
        version1_allocate_info.memoryTypeIndex = buffer_memory_type_;
        VkMemoryDedicatedAllocateInfo version1_dedicated_allocate_info;
        if (vulkan_device->extensions().ext_1_1_KHR_dedicated_allocation) {
          version1_allocate_info_last->pNext =
              &version1_dedicated_allocate_info;
          version1_allocate_info_last =
              reinterpret_cast<VkMemoryAllocateInfo*>(
                  &version1_dedicated_allocate_info);
          version1_dedicated_allocate_info.sType =
              VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
          version1_dedicated_allocate_info.pNext = nullptr;
          version1_dedicated_allocate_info.image = VK_NULL_HANDLE;
          version1_dedicated_allocate_info.buffer = buffer_version1_;
        }
        if (dfn.vkAllocateMemory(device, &version1_allocate_info, nullptr,
                                 &buffer_version1_memory_) != VK_SUCCESS) {
          XELOGW(
              "Shared memory: gpu_shared_memory_double_buffer: failed to "
              "allocate version-1 memory; falling back to single-buffer");
          buffer_version1_memory_ = VK_NULL_HANDLE;
          version1_ok = false;
        }
      }
      if (version1_ok &&
          dfn.vkBindBufferMemory(device, buffer_version1_,
                                 buffer_version1_memory_, 0) != VK_SUCCESS) {
        XELOGW(
            "Shared memory: gpu_shared_memory_double_buffer: failed to bind "
            "version-1 memory; falling back to single-buffer");
        version1_ok = false;
      }
      if (version1_ok &&
          dfn.vkMapMemory(device, buffer_version1_memory_, 0, VK_WHOLE_SIZE, 0,
                          &buffer_version1_mapping_) != VK_SUCCESS) {
        XELOGW(
            "Shared memory: gpu_shared_memory_double_buffer: failed to map "
            "version-1 memory; falling back to single-buffer");
        buffer_version1_mapping_ = nullptr;
        version1_ok = false;
      }
      if (version1_ok) {
        const uint32_t page_count = kBufferSize >> page_size_log2();
        const size_t stale_words = (page_count + 63) / 64;
        for (uint32_t v = 0; v < kVersionCount; ++v) {
          version_stale_bits_[v].assign(stale_words, uint64_t(0));
        }
        double_buffer_enabled_ = true;
        current_version_ = 0;
        XELOGGPU(
            "Shared memory: gpu_shared_memory_double_buffer active (two {} MB "
            "host-visible versions, memory type {})",
            kBufferSize >> 20, buffer_memory_type_);
      } else {
        // Free any partial version-1 resources; stay single-buffer.
        if (buffer_version1_mapping_) {
          // vkFreeMemory below implicitly unmaps.
          buffer_version1_mapping_ = nullptr;
        }
        if (buffer_version1_memory_ != VK_NULL_HANDLE) {
          dfn.vkFreeMemory(device, buffer_version1_memory_, nullptr);
          buffer_version1_memory_ = VK_NULL_HANDLE;
        }
        if (buffer_version1_ != VK_NULL_HANDLE) {
          dfn.vkDestroyBuffer(device, buffer_version1_, nullptr);
          buffer_version1_ = VK_NULL_HANDLE;
        }
        double_buffer_enabled_ = false;
      }
    }
  }

  // The first usage will likely be uploading.
  last_usage_ = Usage::kTransferDestination;
  last_written_range_ = std::make_pair<uint32_t, uint32_t>(0, 0);

  upload_buffer_pool_ = std::make_unique<ui::vulkan::VulkanUploadBufferPool>(
      vulkan_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      xe::align(ui::vulkan::VulkanUploadBufferPool::kDefaultPageSize,
                size_t(1) << page_size_log2()));

  return true;
}

void VulkanSharedMemory::Shutdown(bool from_destructor) {
  ResetTraceDownload();

  upload_buffer_pool_.reset();

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device, buffer_);
  // vkFreeMemory implicitly unmaps any persistent mapping.
  buffer_host_mapping_ = nullptr;
  buffer_host_visible_ = false;
  buffer_host_coherent_ = false;
  // Double-buffer version 1 (gpu_shared_memory_double_buffer).
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                         buffer_version1_);
  // vkFreeMemory implicitly unmaps the persistent mapping.
  buffer_version1_mapping_ = nullptr;
  if (buffer_version1_memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(device, buffer_version1_memory_, nullptr);
    buffer_version1_memory_ = VK_NULL_HANDLE;
  }
  double_buffer_enabled_ = false;
  current_version_ = 0;
  for (uint32_t v = 0; v < kVersionCount; ++v) {
    version_stale_bits_[v].clear();
    version_last_read_submission_[v] = 0;
  }
  for (VkDeviceMemory memory : buffer_memory_) {
    dfn.vkFreeMemory(device, memory, nullptr);
  }
  buffer_memory_.clear();

  // If calling from the destructor, the SharedMemory destructor will call
  // ShutdownCommon.
  if (!from_destructor) {
    ShutdownCommon();
  }
}

void VulkanSharedMemory::CompletedSubmissionUpdated() {
  upload_buffer_pool_->Reclaim(command_processor_.GetCompletedSubmission());
}

void VulkanSharedMemory::EndSubmission() { upload_buffer_pool_->FlushWrites(); }

void VulkanSharedMemory::BeginSubmission() {
  // Double-buffer version switch happens HERE, at the submission boundary (the
  // command processor calls this from its BeginSubmission, after resetting the
  // deferred command buffer and with no render pass open) - the only safe place
  // to record the stale-range sync copy + barriers, and BEFORE any of this
  // submission's uploads/draws, so every upload targets the version the GPU will
  // read for this submission. No-op when double-buffering is off.
  if (double_buffer_enabled_) {
    MaybeSwitchVersionForWrite();
  }
}

void VulkanSharedMemory::Use(Usage usage,
                             std::pair<uint32_t, uint32_t> written_range) {
  written_range.first = std::min(written_range.first, kBufferSize);
  written_range.second =
      std::min(written_range.second, kBufferSize - written_range.first);
  assert_true(usage != Usage::kRead || !written_range.second);
  // UMA smart-sync: remember the submission that consumes the buffer as a guest
  // read so a later direct in-place write can wait only for it (not a full GPU
  // drain). kRead / kGuestDrawReadWrite are the guest-read consumers.
  // Also remember the latest guest-data WRITER submission (EDRAM->shared-memory
  // resolve = kComputeWrite, memexport = kGuestDrawReadWrite, transfer dest):
  // a direct CPU upload to a range an in-flight GPU write also targets is a WAW
  // that the buffer barrier (GPU-vs-GPU only) cannot order against a CPU memcpy,
  // so the direct path must be able to wait for the latest writer too.
  if (buffer_host_visible_) {
    const uint64_t current_submission =
        command_processor_.GetCurrentSubmission();
    if (usage == Usage::kRead || usage == Usage::kGuestDrawReadWrite) {
      uma_last_read_submission_ = current_submission;
      // Double-buffer: record that the GPU reads the CURRENT version in this
      // submission, so MaybeSwitchVersionForWrite knows the version has an
      // in-flight reader and must not be overwritten in place.
      MarkVersionRead(current_submission);
    }
    if (usage == Usage::kComputeWrite ||
        usage == Usage::kTransferDestination ||
        usage == Usage::kGuestDrawReadWrite) {
      uma_last_write_submission_ = current_submission;
      // Double-buffer: a GPU write of guest data (EDRAM->shared resolve =
      // kComputeWrite, memexport = kGuestDrawReadWrite, transfer dest) lands ONLY
      // in the current version (its barrier/descriptor targets current_buffer()).
      // Mark those pages stale in the OTHER version so a later version switch
      // GPU-copies them current too - otherwise a switch would expose a version
      // missing the resolved/exported data. CPU uploads do this per-range in
      // UploadRangesDirect; this covers the GPU-write paths that bypass it.
      if (double_buffer_enabled_ && written_range.second) {
        const uint32_t page_size_log2_local = page_size_log2();
        const uint32_t page_first = written_range.first >> page_size_log2_local;
        const uint32_t page_end =
            (written_range.first + written_range.second +
             ((uint32_t(1) << page_size_log2_local) - 1)) >>
            page_size_log2_local;
        SetPageStaleInOtherVersions(page_first, page_end - page_first,
                                    current_version_);
      }
    }
  }
  if (last_usage_ != usage || last_written_range_.second) {
    VkPipelineStageFlags src_stage_mask, dst_stage_mask;
    VkAccessFlags src_access_mask, dst_access_mask;
    GetUsageMasks(last_usage_, src_stage_mask, src_access_mask);
    GetUsageMasks(usage, dst_stage_mask, dst_access_mask);
    VkDeviceSize offset, size;
    if (last_usage_ == usage) {
      // Committing the previous write, while not changing the access mask
      // (passing false as whether to skip the barrier if no masks are changed
      // for this reason).
      offset = VkDeviceSize(last_written_range_.first);
      size = VkDeviceSize(last_written_range_.second);
    } else {
      // Changing the stage and access mask - all preceding writes must be
      // available not only to the source stage, but to the destination as well.
      offset = 0;
      size = VK_WHOLE_SIZE;
      last_usage_ = usage;
    }
    // Double-buffer: barrier the version the GPU currently reads (current_buffer
    // == buffer_ when double-buffering is off, so this is unchanged then).
    command_processor_.PushBufferMemoryBarrier(
        current_buffer(), offset, size, src_stage_mask, dst_stage_mask,
        src_access_mask, dst_access_mask, VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED, false);
  }
  last_written_range_ = written_range;
}

bool VulkanSharedMemory::InitializeTraceSubmitDownloads() {
  ResetTraceDownload();
  PrepareForTraceDownload();
  uint32_t download_page_count = trace_download_page_count();
  if (!download_page_count) {
    return false;
  }

  if (!ui::vulkan::util::CreateDedicatedAllocationBuffer(
          command_processor_.GetVulkanDevice(),
          download_page_count << page_size_log2(),
          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          ui::vulkan::util::MemoryPurpose::kReadback, trace_download_buffer_,
          trace_download_buffer_memory_)) {
    XELOGE(
        "Shared memory: Failed to create a {} KB GPU-written memory download "
        "buffer for frame tracing",
        download_page_count << page_size_log2() >> 10);
    ResetTraceDownload();
    return false;
  }

  Use(Usage::kRead);
  command_processor_.SubmitBarriers(true);
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();

  size_t download_range_count = trace_download_ranges().size();
  VkBufferCopy* download_regions = command_buffer.CmdCopyBufferEmplace(
      buffer_, trace_download_buffer_, uint32_t(download_range_count));
  VkDeviceSize download_buffer_offset = 0;
  for (size_t i = 0; i < download_range_count; ++i) {
    VkBufferCopy& download_region = download_regions[i];
    const std::pair<uint32_t, uint32_t>& download_range =
        trace_download_ranges()[i];
    download_region.srcOffset = download_range.first;
    download_region.dstOffset = download_buffer_offset;
    download_region.size = download_range.second;
    download_buffer_offset += download_range.second;
  }

  command_processor_.PushBufferMemoryBarrier(
      trace_download_buffer_, 0, VK_WHOLE_SIZE, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_HOST_READ_BIT);

  return true;
}

void VulkanSharedMemory::InitializeTraceCompleteDownloads() {
  if (!trace_download_buffer_memory_) {
    return;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  void* download_mapping;
  if (dfn.vkMapMemory(device, trace_download_buffer_memory_, 0, VK_WHOLE_SIZE,
                      0, &download_mapping) == VK_SUCCESS) {
    uint32_t download_buffer_offset = 0;
    for (const auto& download_range : trace_download_ranges()) {
      trace_writer_.WriteMemoryRead(
          download_range.first, download_range.second,
          reinterpret_cast<const uint8_t*>(download_mapping) +
              download_buffer_offset);
    }
    dfn.vkUnmapMemory(device, trace_download_buffer_memory_);
  } else {
    XELOGE(
        "Shared memory: Failed to map the GPU-written memory download buffer "
        "for frame tracing");
  }
  ResetTraceDownload();
}

bool VulkanSharedMemory::AllocateSparseHostGpuMemoryRange(
    uint32_t offset_allocations, uint32_t length_allocations) {
  if (!length_allocations) {
    return true;
  }

  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  VkMemoryAllocateInfo memory_allocate_info;
  memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memory_allocate_info.pNext = nullptr;
  memory_allocate_info.allocationSize =
      length_allocations << host_gpu_memory_sparse_granularity_log2();
  memory_allocate_info.memoryTypeIndex = buffer_memory_type_;
  VkDeviceMemory memory;
  if (dfn.vkAllocateMemory(device, &memory_allocate_info, nullptr, &memory) !=
      VK_SUCCESS) {
    XELOGE("Shared memory: Failed to allocate sparse buffer memory");
    return false;
  }
  buffer_memory_.push_back(memory);

  VkSparseMemoryBind bind;
  bind.resourceOffset = offset_allocations
                        << host_gpu_memory_sparse_granularity_log2();
  bind.size = memory_allocate_info.allocationSize;
  bind.memory = memory;
  bind.memoryOffset = 0;
  bind.flags = 0;
  VkPipelineStageFlags bind_wait_stage_mask =
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
  if (vulkan_device->properties().tessellationShader) {
    bind_wait_stage_mask |=
        VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
  }
  command_processor_.SparseBindBuffer(buffer_, 1, &bind, bind_wait_stage_mask);

  return true;
}

bool VulkanSharedMemory::UploadRanges(
    const std::vector<std::pair<uint32_t, uint32_t>>& upload_page_ranges) {
  if (upload_page_ranges.empty()) {
    return true;
  }
  if (buffer_host_visible_) {
    return UploadRangesDirect(upload_page_ranges);
  }
  // upload_page_ranges are sorted, use them to determine the range for the
  // ordering barrier.
  Use(Usage::kTransferDestination,
      std::make_pair(
          upload_page_ranges.front().first << page_size_log2(),
          (upload_page_ranges.back().first + upload_page_ranges.back().second -
           upload_page_ranges.front().first)
              << page_size_log2()));
  command_processor_.SubmitBarriers(true);
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();
  uint64_t submission_current = command_processor_.GetCurrentSubmission();
  bool successful = true;
  upload_regions_.clear();
  VkBuffer upload_buffer_previous = VK_NULL_HANDLE;
  for (auto upload_range : upload_page_ranges) {
    uint32_t upload_range_start = upload_range.first;
    uint32_t upload_range_length = upload_range.second;
    trace_writer_.WriteMemoryRead(upload_range_start << page_size_log2(),
                                  upload_range_length << page_size_log2());
    while (upload_range_length) {
      VkBuffer upload_buffer;
      VkDeviceSize upload_buffer_offset, upload_buffer_size;
      uint8_t* upload_buffer_mapping = upload_buffer_pool_->RequestPartial(
          submission_current, upload_range_length << page_size_log2(),
          size_t(1) << page_size_log2(), upload_buffer, upload_buffer_offset,
          upload_buffer_size);
      if (upload_buffer_mapping == nullptr) {
        XELOGE("Shared memory: Failed to get a Vulkan upload buffer");
        successful = false;
        break;
      }
      MakeRangeValid(upload_range_start << page_size_log2(),
                     uint32_t(upload_buffer_size), false);
      std::memcpy(
          upload_buffer_mapping,
          memory().TranslatePhysical(upload_range_start << page_size_log2()),
          upload_buffer_size);
      if (upload_buffer_previous != upload_buffer && !upload_regions_.empty()) {
        assert_true(upload_buffer_previous != VK_NULL_HANDLE);
        uint64_t staging_copy_bytes = 0;
        for (const VkBufferCopy& region : upload_regions_) {
          staging_copy_bytes += uint64_t(region.size);
        }
        ui::vulkan::VulkanPerfCountersRecordSharedMemoryStagingCopy(
            staging_copy_bytes);
        command_buffer.CmdVkCopyBuffer(upload_buffer_previous, buffer_,
                                       uint32_t(upload_regions_.size()),
                                       upload_regions_.data());
        upload_regions_.clear();
      }
      upload_buffer_previous = upload_buffer;
      VkBufferCopy& upload_region = upload_regions_.emplace_back();
      upload_region.srcOffset = upload_buffer_offset;
      upload_region.dstOffset =
          VkDeviceSize(upload_range_start << page_size_log2());
      upload_region.size = upload_buffer_size;
      uint32_t upload_buffer_pages =
          uint32_t(upload_buffer_size >> page_size_log2());
      upload_range_start += upload_buffer_pages;
      upload_range_length -= upload_buffer_pages;
    }
    if (!successful) {
      break;
    }
  }
  if (!upload_regions_.empty()) {
    assert_true(upload_buffer_previous != VK_NULL_HANDLE);
    uint64_t staging_copy_bytes = 0;
    for (const VkBufferCopy& region : upload_regions_) {
      staging_copy_bytes += uint64_t(region.size);
    }
    ui::vulkan::VulkanPerfCountersRecordSharedMemoryStagingCopy(
        staging_copy_bytes);
    command_buffer.CmdVkCopyBuffer(upload_buffer_previous, buffer_,
                                   uint32_t(upload_regions_.size()),
                                   upload_regions_.data());
    upload_regions_.clear();
  }
  return successful;
}

bool VulkanSharedMemory::UploadRangesDirect(
    const std::vector<std::pair<uint32_t, uint32_t>>& upload_page_ranges) {
  // Unified-memory path: buffer_ memory is HOST_VISIBLE | DEVICE_LOCAL and
  // persistently mapped, so guest pages are copied straight into the GPU
  // buffer. No staging buffer, no vkCmdCopyBuffer, no transfer barrier.
  assert_not_null(buffer_host_mapping_);
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  const uint32_t page_size_log2_local = page_size_log2();

  // Double-buffer (gpu_shared_memory_double_buffer): the version switch + its
  // stale-range GPU copy is performed at SUBMISSION BEGIN (BeginSubmission()),
  // NOT here. Recording a transfer + barriers from inside UploadRangesDirect is
  // illegal (this runs mid-command-recording, possibly inside a render pass) and
  // hung the device. By switching at the submission boundary - before any of
  // this submission's uploads or draws - every CPU write below already targets
  // the version the GPU will read for this submission, so it never overwrites
  // pages an in-flight prior submission is still reading.
  // Write into the version the GPU currently reads (== buffer_host_mapping_ when
  // double-buffering is off, so this is byte-identical then).
  uint8_t* const buffer_mapping =
      reinterpret_cast<uint8_t*>(current_buffer_mapping());

  // Adreno is a tile-based DEFERRED renderer: vertex/index buffer reads happen
  // during binning + per-tile passes that execute long after a draw is recorded
  // (A7xx even bins concurrently with rendering). The direct path writes guest
  // pages IN PLACE into the single shared buffer on the CPU timeline, so an
  // in-flight prior submission can still be reading pages we overwrite -> torn
  // index -> out-of-bounds vertex fetch -> GPU MMU fault (the captured
  // "adreno-gen7-gmu: GPU hang detected" TDR). The staging path is immune
  // because every upload is an immutable per-submission copy.
  //
  // DIAGNOSTIC (gpu_uma_serialize_before_write): wait for ALL previously
  // submitted GPU work to finish before overwriting, so nothing in flight can be
  // reading these pages. This is the decisive test of the race hypothesis: if
  // the TDR disappears with this on, the hang IS the CPU-write-vs-deferred-read
  // race and the real fix is a correct per-page in-flight guard; if it still
  // TDRs, the fault is intra-frame and lives elsewhere.
  // (direct_last_read_submission_ was a single scalar clobbered to the open
  // submission by every Use(kRead), so the previous "< current" guard never
  // fired - it was effectively dead. Replaced by this explicit experiment.)
  //
  // Double-buffer: the version switch at BeginSubmission moves writes to a
  // version with no in-flight reader, so smart-sync usually finds nothing to
  // wait for - but it is KEPT ON as a PER-VERSION safety net. (Disabling the
  // guard entirely - trusting the switch alone - left the UMA race unguarded and
  // hung the device, because the switch is not airtight on every path.) When
  // double-buffering, the wait below keys off the PER-VERSION reader
  // (version_last_read_submission_[current_version_]) instead of the global
  // tracker, so a switch to a free version costs no wait while a still-contended
  // current version is still correctly guarded. The brute serialize-before-write
  // stays off for double-buffering (smart-sync is the guard); the single-buffer
  // paths are byte-identical.
  if (!double_buffer_enabled_ && cvars::gpu_uma_serialize_before_write) {
    const uint64_t current_submission =
        command_processor_.GetCurrentSubmission();
    if (current_submission > 1 &&
        command_processor_.GetCompletedSubmission() < current_submission - 1) {
      // Wait for everything submitted so far (current_submission - 1) to drain.
      command_processor_.AwaitSubmissionCompletion(current_submission - 1);
    }
  } else if (cvars::gpu_uma_smart_sync) {
    // TDR FIX: wait ONLY for the last submission that read this buffer, and only
    // if it is a PRIOR (already-closed) submission that has not yet completed.
    // GetCurrentSubmission() is the still-OPEN submission being recorded now -
    // never wait on it (its reads haven't been submitted, and waiting would
    // deadlock). Because queue submissions complete in order, awaiting the
    // latest prior reader guarantees all earlier readers are done too.
    const uint64_t current_submission =
        command_processor_.GetCurrentSubmission();
    // Wait for the latest prior submission that TOUCHED this buffer. Reads are
    // the documented TDR cause (deferred tiler index/vfetch); writes (resolve/
    // memexport) are a WAW the GPU-only barrier cannot order against the CPU
    // memcpy below. Submissions complete in order, so the single latest toucher
    // subsumes all earlier ones - still no full drain, no deadlock (never the
    // still-open current submission).
    // Per-version read tracker when double-buffering (so a switch to a free
    // version waits for nothing); global tracker on the single-buffer path.
    uint64_t wait_submission =
        double_buffer_enabled_
            ? version_last_read_submission_[current_version_]
            : uma_last_read_submission_;
    if (cvars::gpu_uma_smart_sync_writes &&
        uma_last_write_submission_ > wait_submission) {
      wait_submission = uma_last_write_submission_;
    }
    if (wait_submission != 0 && wait_submission < current_submission &&
        command_processor_.GetCompletedSubmission() < wait_submission) {
      command_processor_.AwaitSubmissionCompletion(wait_submission);
    }
  }

  // upload_page_ranges are sorted; bound the barrier to the touched span.
  const VkDeviceSize barrier_first_byte =
      VkDeviceSize(upload_page_ranges.front().first) << page_size_log2_local;
  const VkDeviceSize barrier_end_byte =
      VkDeviceSize(upload_page_ranges.back().first +
                   upload_page_ranges.back().second)
      << page_size_log2_local;

  for (auto upload_range : upload_page_ranges) {
    uint32_t start_byte = upload_range.first << page_size_log2_local;
    uint32_t length_bytes = upload_range.second << page_size_log2_local;
    trace_writer_.WriteMemoryRead(start_byte, length_bytes);
    MakeRangeValid(start_byte, length_bytes, false);
    std::memcpy(buffer_mapping + start_byte,
                memory().TranslatePhysical(start_byte), length_bytes);
    ui::vulkan::VulkanPerfCountersRecordSharedMemoryDirectWrite(
        uint64_t(length_bytes));
    // Double-buffer: these pages are now fresh ONLY in the current version; mark
    // them stale in the other version so a later switch GPU-copies them current.
    if (double_buffer_enabled_) {
      SetPageStaleInOtherVersions(upload_range.first, upload_range.second,
                                  current_version_);
    }
  }

  // Make the host writes visible to the device. Flushing the whole buffer
  // avoids nonCoherentAtomSize alignment handling on the touched subranges.
  // EXPERIMENT (b): with gpu_uma_strong_coherency, flush even when the heap
  // reports HOST_COHERENT - some Adreno host-visible-device-local heaps are
  // write-combining and the "coherent" flag does not fully cover the GPU-side
  // view, which is the leading suspect for the intermittent MMU-fault TDR.
  if (!buffer_host_coherent_ || cvars::gpu_uma_strong_coherency) {
    VkMappedMemoryRange flush_range;
    flush_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    flush_range.pNext = nullptr;
    // Double-buffer: flush the version we just wrote (version 1 has its own
    // memory; version 0 == buffer_memory_.front(), so this is unchanged off).
    flush_range.memory = (double_buffer_enabled_ && current_version_ == 1)
                             ? buffer_version1_memory_
                             : buffer_memory_.front();
    flush_range.offset = 0;
    flush_range.size = VK_WHOLE_SIZE;
    dfn.vkFlushMappedMemoryRanges(device, 1, &flush_range);
  }

  // Host-write -> guest-read availability/visibility barrier. The buffer is
  // consumed as index buffer / vfetch / shader storage by guest stages.
  // EXPERIMENT (b): gpu_uma_strong_coherency widens this to the WHOLE buffer
  // with ALL_COMMANDS / MEMORY_READ, in case the span-bounded HOST->read barrier
  // under-covers what the deferred tiler actually reads (and when).
  // Double-buffer: barrier the version we wrote (current_buffer() == buffer_
  // when double-buffering is off, so these are byte-identical then).
  if (cvars::gpu_uma_strong_coherency) {
    command_processor_.PushBufferMemoryBarrier(
        current_buffer(), 0, VK_WHOLE_SIZE, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_HOST_WRITE_BIT,
        VK_ACCESS_MEMORY_READ_BIT);
  } else {
    command_processor_.PushBufferMemoryBarrier(
        current_buffer(), barrier_first_byte,
        barrier_end_byte - barrier_first_byte, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | guest_shader_pipeline_stages_,
        VK_ACCESS_HOST_WRITE_BIT,
        VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
  }
  // The buffer now holds valid data visible to read stages.
  last_usage_ = Usage::kRead;
  last_written_range_ = std::make_pair(uint32_t(0), uint32_t(0));

  return true;
}

void VulkanSharedMemory::MarkVersionRead(uint64_t submission) {
  if (!double_buffer_enabled_) {
    return;
  }
  version_last_read_submission_[current_version_] = submission;
}

void VulkanSharedMemory::SetPageStaleInOtherVersions(uint32_t page_first,
                                                     uint32_t page_count,
                                                     uint32_t fresh_version) {
  for (uint32_t v = 0; v < kVersionCount; ++v) {
    if (v == fresh_version) {
      continue;
    }
    for (uint32_t p = page_first; p < page_first + page_count; ++p) {
      version_stale_bits_[v][p >> 6] |= (uint64_t(1) << (p & 63));
    }
  }
}

void VulkanSharedMemory::MaybeSwitchVersionForWrite() {
  if (!double_buffer_enabled_) {
    return;
  }
  const uint64_t completed = command_processor_.GetCompletedSubmission();
  const uint64_t current_open = command_processor_.GetCurrentSubmission();
  const uint32_t cur = current_version_;
  // Contended ONLY if a PRIOR, already-CLOSED submission is still reading the
  // current version (completed < reader < current_open). A read recorded in the
  // still-open current submission is NOT a race - our in-place write here is
  // ordered before that submission executes by the host->device barrier - so it
  // must not force a switch (that would thrash + self-stall every submission).
  // Mirrors gpu_uma_smart_sync's "wait_submission < current_submission" guard.
  auto version_has_closed_inflight_reader = [&](uint32_t v) {
    const uint64_t reader = version_last_read_submission_[v];
    return reader > completed && reader < current_open;
  };
  if (!version_has_closed_inflight_reader(cur)) {
    return;
  }
  // The current version is being read by a closed in-flight submission - switch
  // to the other version so we don't overwrite pages it is still reading.
  const uint32_t other = cur ^ 1u;
  // If the OTHER version is ALSO read by a closed in-flight submission, we can't
  // safely switch to it either - fall back to waiting for the current version's
  // reader (correctness; rare). After the wait the current version is safe to
  // overwrite in place; keep using it.
  if (version_has_closed_inflight_reader(other)) {
    command_processor_.AwaitSubmissionCompletion(
        version_last_read_submission_[cur]);
    return;
  }

  // Bring the OTHER version current by GPU-copying its stale ranges from the
  // current (up-to-date) version, then make it the new current version.
  const uint32_t page_size_log2_local = page_size_log2();
  const uint32_t page_count = kBufferSize >> page_size_log2_local;
  std::vector<uint64_t>& other_stale = version_stale_bits_[other];

  // Coalesce contiguous stale pages of `other` into byte ranges for the copy
  // (one VkBufferCopy per contiguous run, not one per page).
  std::vector<VkBufferCopy> copy_regions;
  uint32_t run_first_page = 0;
  bool in_run = false;
  for (uint32_t p = 0; p < page_count; ++p) {
    const bool stale =
        (other_stale[p >> 6] & (uint64_t(1) << (p & 63))) != 0;
    if (stale && !in_run) {
      run_first_page = p;
      in_run = true;
    } else if (!stale && in_run) {
      VkBufferCopy& region = copy_regions.emplace_back();
      region.srcOffset = VkDeviceSize(run_first_page) << page_size_log2_local;
      region.dstOffset = region.srcOffset;
      region.size = VkDeviceSize(p - run_first_page) << page_size_log2_local;
      in_run = false;
    }
  }
  if (in_run) {
    VkBufferCopy& region = copy_regions.emplace_back();
    region.srcOffset = VkDeviceSize(run_first_page) << page_size_log2_local;
    region.dstOffset = region.srcOffset;
    region.size = VkDeviceSize(page_count - run_first_page)
                  << page_size_log2_local;
  }

  if (!copy_regions.empty()) {
    // Make prior CPU (host) writes to the SOURCE (current) version visible to
    // the transfer read. The host-visible-device-local writes happened via the
    // persistent mapping with no command-buffer ordering, so without this the
    // copy could read stale source bytes.
    command_processor_.PushBufferMemoryBarrier(
        buffer_for_version(cur), 0, VK_WHOLE_SIZE, VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_HOST_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT);
    // Submit the pre-copy barrier so it is ordered before the copy commands.
    command_processor_.SubmitBarriers(false);
    command_processor_.deferred_command_buffer().CmdVkCopyBuffer(
        buffer_for_version(cur), buffer_for_version(other),
        uint32_t(copy_regions.size()), copy_regions.data());
    // Make the copy (TRANSFER_WRITE into `other`) available + visible to the
    // subsequent guest reads of `other` (index/vfetch/shader-storage). This is
    // queued and flushed by the draw flow's SubmitBarriers before the draw, like
    // the Use()/UploadRangesDirect host->read barriers.
    command_processor_.PushBufferMemoryBarrier(
        buffer_for_version(other), 0, VK_WHOLE_SIZE,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | guest_shader_pipeline_stages_,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
  }

  // `other` is now fully current; clear its staleness and switch to it.
  std::fill(other_stale.begin(), other_stale.end(), uint64_t(0));
  current_version_ = other;
}

void VulkanSharedMemory::GetUsageMasks(Usage usage,
                                       VkPipelineStageFlags& stage_mask,
                                       VkAccessFlags& access_mask) const {
  switch (usage) {
    case Usage::kComputeWrite:
      stage_mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      access_mask = VK_ACCESS_SHADER_WRITE_BIT;
      return;
    case Usage::kTransferDestination:
      stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;
      access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
      return;
    default:
      break;
  }
  stage_mask =
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | guest_shader_pipeline_stages_;
  access_mask = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
  switch (usage) {
    case Usage::kRead:
      stage_mask |=
          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
      access_mask |= VK_ACCESS_TRANSFER_READ_BIT;
      break;
    case Usage::kGuestDrawReadWrite:
      access_mask |= VK_ACCESS_SHADER_WRITE_BIT;
      break;
    default:
      assert_unhandled_case(usage);
  }
}

void VulkanSharedMemory::ResetTraceDownload() {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyBuffer, device,
                                         trace_download_buffer_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkFreeMemory, device,
                                         trace_download_buffer_memory_);
  ReleaseTraceDownloadRanges();
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
