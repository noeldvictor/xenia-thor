/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_DIAGNOSTIC_COUNTERS_H_
#define XENIA_UI_VULKAN_VULKAN_DIAGNOSTIC_COUNTERS_H_

#include <cstdint>

namespace xe {
namespace ui {
namespace vulkan {

bool VulkanPerfCountersEnabled();
uint64_t VulkanPerfCountersNow();

void VulkanPerfCountersRecordIssueSwap();
void VulkanPerfCountersRecordGraphicsPipelineCacheHit();
void VulkanPerfCountersRecordGraphicsPipelineCreate(uint64_t start_ticks,
                                                    int32_t result);
void VulkanPerfCountersRecordBufferBarrier();
void VulkanPerfCountersRecordImageBarrier();
void VulkanPerfCountersRecordBarrierSubmit(uint32_t barrier_group_count,
                                           uint32_t buffer_barrier_count,
                                           uint32_t image_barrier_count,
                                           bool force_end_render_pass);
void VulkanPerfCountersRecordRenderPassBegin(bool presenter);
void VulkanPerfCountersRecordQueueSubmit(uint64_t start_ticks,
                                         uint32_t submit_count,
                                         uint32_t command_buffer_count,
                                         uint32_t wait_semaphore_count,
                                         int32_t result);
void VulkanPerfCountersRecordPresentAcquire(uint64_t start_ticks,
                                            int32_t result);
void VulkanPerfCountersRecordPresentSubmit(uint64_t start_ticks,
                                           uint32_t command_buffer_count,
                                           int32_t result);
void VulkanPerfCountersRecordPresent(uint64_t start_ticks, int32_t result);
void VulkanPerfCountersLogSnapshot(const char* reason);

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_DIAGNOSTIC_COUNTERS_H_
