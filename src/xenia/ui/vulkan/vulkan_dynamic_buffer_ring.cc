/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_dynamic_buffer_ring.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace ui {
namespace vulkan {

VulkanDynamicBufferRing::~VulkanDynamicBufferRing() { Shutdown(); }

uint32_t VulkanDynamicBufferRing::ChooseMemoryType(
    uint32_t type_bits, VkMemoryPropertyFlags props) const {
  const VulkanDevice::MemoryTypes& mt = device_->memory_types();
  for (uint32_t i = 0; i < 32; ++i) {
    if (!(type_bits & (uint32_t(1) << i))) {
      continue;
    }
    bool ok = true;
    if (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
      ok = ok && (mt.device_local & (uint32_t(1) << i)) != 0;
    }
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      ok = ok && (mt.host_visible & (uint32_t(1) << i)) != 0;
    }
    if (props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
      ok = ok && (mt.host_coherent & (uint32_t(1) << i)) != 0;
    }
    if (ok) {
      return i;
    }
  }
  return UINT32_MAX;
}

bool VulkanDynamicBufferRing::Initialize(const VulkanDevice* device,
                                         VkDeviceSize capacity,
                                         VkBufferUsageFlags usage,
                                         VkMemoryPropertyFlags memory_props,
                                         VkDeviceSize alignment,
                                         VkDeviceSize tail_padding) {
  device_ = device;
  alignment_ = std::max<VkDeviceSize>(alignment, VkDeviceSize(1));
  tail_padding_ = xe::round_up(tail_padding, alignment_);
  // Each frame gets an aligned segment; total capacity is segment * frames.
  segment_size_ = xe::round_up(
      std::max<VkDeviceSize>(capacity / kFramesInFlight, alignment_), alignment_);
  capacity_ = segment_size_ * kFramesInFlight;

  const VulkanDevice::Functions& dfn = device_->functions();
  const VkDevice vkdev = device_->device();

  VkBufferCreateInfo buffer_create_info;
  buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_create_info.pNext = nullptr;
  buffer_create_info.flags = 0;
  buffer_create_info.size = capacity_ + tail_padding_;
  buffer_create_info.usage = usage;
  buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  buffer_create_info.queueFamilyIndexCount = 0;
  buffer_create_info.pQueueFamilyIndices = nullptr;
  if (dfn.vkCreateBuffer(vkdev, &buffer_create_info, nullptr, &buffer_) !=
      VK_SUCCESS) {
    XELOGE("VulkanDynamicBufferRing: failed to create a {} byte buffer",
           capacity_);
    buffer_ = VK_NULL_HANDLE;
    return false;
  }

  VkMemoryRequirements memory_requirements;
  dfn.vkGetBufferMemoryRequirements(vkdev, buffer_, &memory_requirements);
  memory_type_ =
      ChooseMemoryType(memory_requirements.memoryTypeBits, memory_props);
  if (memory_type_ == UINT32_MAX) {
    XELOGE(
        "VulkanDynamicBufferRing: no memory type satisfies property mask {:08X}",
        uint32_t(memory_props));
    Shutdown();
    return false;
  }
  host_coherent_ =
      (device_->memory_types().host_coherent & (uint32_t(1) << memory_type_)) !=
      0;

  VkMemoryAllocateInfo memory_allocate_info;
  memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memory_allocate_info.pNext = nullptr;
  memory_allocate_info.allocationSize = memory_requirements.size;
  memory_allocate_info.memoryTypeIndex = memory_type_;
  if (dfn.vkAllocateMemory(vkdev, &memory_allocate_info, nullptr, &memory_) !=
      VK_SUCCESS) {
    XELOGE("VulkanDynamicBufferRing: failed to allocate {} bytes",
           memory_requirements.size);
    Shutdown();
    return false;
  }
  if (dfn.vkBindBufferMemory(vkdev, buffer_, memory_, 0) != VK_SUCCESS) {
    XELOGE("VulkanDynamicBufferRing: failed to bind buffer memory");
    Shutdown();
    return false;
  }

  if (device_->memory_types().host_visible & (uint32_t(1) << memory_type_)) {
    void* mapping = nullptr;
    if (dfn.vkMapMemory(vkdev, memory_, 0, VK_WHOLE_SIZE, 0, &mapping) !=
        VK_SUCCESS) {
      XELOGE("VulkanDynamicBufferRing: failed to map host-visible memory");
      Shutdown();
      return false;
    }
    mapping_ = reinterpret_cast<uint8_t*>(mapping);
  }

  current_segment_ = 0;
  segment_write_ = 0;
  return true;
}

void VulkanDynamicBufferRing::Shutdown() {
  if (!device_) {
    return;
  }
  const VulkanDevice::Functions& dfn = device_->functions();
  const VkDevice vkdev = device_->device();
  // Unmapping is implicit on vkFreeMemory.
  if (buffer_ != VK_NULL_HANDLE) {
    dfn.vkDestroyBuffer(vkdev, buffer_, nullptr);
    buffer_ = VK_NULL_HANDLE;
  }
  if (memory_ != VK_NULL_HANDLE) {
    dfn.vkFreeMemory(vkdev, memory_, nullptr);
    memory_ = VK_NULL_HANDLE;
  }
  mapping_ = nullptr;
  memory_type_ = UINT32_MAX;
}

VkDeviceSize VulkanDynamicBufferRing::Allocate(VkDeviceSize size, bool* out_ok) {
  VkDeviceSize aligned = xe::round_up(segment_write_, alignment_);
  if (aligned + size > segment_size_) {
    if (out_ok) {
      *out_ok = false;
    }
    return 0;
  }
  segment_write_ = aligned + size;
  if (out_ok) {
    *out_ok = true;
  }
  return VkDeviceSize(current_segment_) * segment_size_ + aligned;
}

void VulkanDynamicBufferRing::FlushRange(VkDeviceSize offset, VkDeviceSize size) {
  if (host_coherent_ || !mapping_ || memory_ == VK_NULL_HANDLE) {
    return;
  }
  // util::FlushMappedMemoryRange no-ops for host-coherent memory and clamps to
  // nonCoherentAtomSize internally.
  util::FlushMappedMemoryRange(device_, memory_, memory_type_, offset,
                               capacity_ + tail_padding_, size);
}

void VulkanDynamicBufferRing::FrameAdvance(uint64_t frame_index) {
  current_segment_ = uint32_t(frame_index % kFramesInFlight);
  segment_write_ = 0;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
