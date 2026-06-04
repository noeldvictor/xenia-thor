/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_DYNAMIC_BUFFER_RING_H_
#define XENIA_UI_VULKAN_VULKAN_DYNAMIC_BUFFER_RING_H_

#include <cstdint>

#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace ui {
namespace vulkan {

// A persistent (device-lifetime) GPU buffer with a bump-allocating, per-frame-
// segmented ring on top - shared infra for the Thor hyperopt refactors:
//   R2: HOST_VISIBLE|DEVICE_LOCAL on UMA, CPU writes constants into host_mapping()
//       in place and binds via a UNIFORM_BUFFER_DYNAMIC pDynamicOffset.
//   R4: DEVICE_LOCAL, a GPU-resident cache populated by staged copies.
// Split into kFramesInFlight segments so frame N+1 never overwrites the data of
// in-flight frame N. NOT thread-safe; one instance per use site, driven from the
// command-processor thread.
class VulkanDynamicBufferRing {
 public:
  static constexpr uint32_t kFramesInFlight = 3;

  VulkanDynamicBufferRing() = default;
  ~VulkanDynamicBufferRing();
  VulkanDynamicBufferRing(const VulkanDynamicBufferRing&) = delete;
  VulkanDynamicBufferRing& operator=(const VulkanDynamicBufferRing&) = delete;

  // capacity = TOTAL bytes (split into kFramesInFlight aligned segments). usage =
  // e.g. VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT (R2) or STORAGE/TRANSFER (R4).
  // memory_props = e.g. HOST_VISIBLE|DEVICE_LOCAL (R2) or DEVICE_LOCAL (R4).
  // Returns false (and leaves is_valid()==false) if creation fails - the caller
  // must fall back to its existing path (e.g. uniform_buffer_pool_).
  // tail_padding: extra bytes appended to the backing buffer BEYOND the
  // allocatable capacity (segments). Lets a UNIFORM_BUFFER_DYNAMIC binding use a
  // fixed descriptor `range` R larger than a variable-size allocation: even the
  // last slot in the last segment can read [offset, offset+R) without running
  // past the buffer, as long as tail_padding >= R. Allocation still only uses
  // `capacity`.
  bool Initialize(const VulkanDevice* device, VkDeviceSize capacity,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_props,
                  VkDeviceSize alignment, VkDeviceSize tail_padding = 0);
  void Shutdown();

  bool is_valid() const { return buffer_ != VK_NULL_HANDLE; }
  VkBuffer buffer() const { return buffer_; }
  // Host pointer to the whole buffer (offset into it with Allocate's return), or
  // nullptr if the chosen memory type is not HOST_VISIBLE.
  uint8_t* host_mapping() const { return mapping_; }
  bool host_coherent() const { return host_coherent_; }
  VkDeviceSize capacity() const { return capacity_; }

  // Bump-allocate `size` aligned bytes within the current frame segment. Returns
  // the absolute byte offset into the buffer; sets *out_ok=false if the segment
  // is full (caller falls back for this draw).
  VkDeviceSize Allocate(VkDeviceSize size, bool* out_ok);

  // Flush a written range for non-HOST_COHERENT memory (no-op when coherent or
  // not host-visible). Call after memcpy-ing into host_mapping().
  void FlushRange(VkDeviceSize offset, VkDeviceSize size);

  // Rotate to the segment for (frame_index % kFramesInFlight) and reset its
  // write cursor. Call once per frame.
  void FrameAdvance(uint64_t frame_index);

 private:
  uint32_t ChooseMemoryType(uint32_t type_bits,
                            VkMemoryPropertyFlags props) const;

  const VulkanDevice* device_ = nullptr;
  VkBuffer buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory memory_ = VK_NULL_HANDLE;
  uint8_t* mapping_ = nullptr;
  uint32_t memory_type_ = UINT32_MAX;
  bool host_coherent_ = false;
  VkDeviceSize capacity_ = 0;
  VkDeviceSize tail_padding_ = 0;
  VkDeviceSize segment_size_ = 0;
  VkDeviceSize alignment_ = 1;
  uint32_t current_segment_ = 0;
  VkDeviceSize segment_write_ = 0;  // byte cursor within the current segment
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_DYNAMIC_BUFFER_RING_H_
