/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_DEFERRED_COMMAND_BUFFER_H_
#define XENIA_GPU_VULKAN_DEFERRED_COMMAND_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_api.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanCommandProcessor;

class DeferredCommandBuffer {
 public:
  DeferredCommandBuffer(const VulkanCommandProcessor& command_processor,
                        size_t initial_size_bytes = 1024 * 1024);

  void Reset();
  void Execute(VkCommandBuffer command_buffer);

  // Monotonic recorded-command cursor (grows by one WriteCommand per recorded
  // command). Used by the draw-concatenation lever (vulkan_merge_draws): if this
  // is unchanged across a span, NO command was recorded in that span. Reset by
  // Reset().
  size_t command_stream_size_elements() const {
    return command_stream_.size();
  }

  // Opaque depth pre-pass (gpu_opaque_depth_prepass) splice primitive. The
  // command stream is POSITION-INDEPENDENT (Execute walks it header-by-header;
  // args hold absolute Vulkan handles + inline-by-value data whose offsets are
  // computed relative to each command at replay), so inserting another buffer's
  // entire recorded stream at an element position is a valid reordering. Used to
  // splice the recorded opaque depth-only draws in right after BeginRenderPass,
  // before the color draws, within one render pass. element_pos must be a
  // command-boundary cursor captured from command_stream_size_elements().
  void InsertStreamFrom(size_t element_pos, const DeferredCommandBuffer& other) {
    if (other.command_stream_.empty()) {
      return;
    }
    assert_true(element_pos <= command_stream_.size());
    command_stream_.insert(command_stream_.begin() + element_pos,
                           other.command_stream_.begin(),
                           other.command_stream_.end());
    record_stats_.draws += other.record_stats_.draws;
    record_stats_.barriers += other.record_stats_.barriers;
  }

  // Cheap recording-time composition counters for the between-render-pass GPU
  // gap attribution (snapshotted by the command processor at the pass
  // timestamp brackets; reset together with the buffer). Cumulative within one
  // recording.
  struct RecordStats {
    uint32_t dispatches = 0;
    uint32_t buffer_copies = 0;
    uint32_t buffer_image_copies = 0;
    uint32_t barriers = 0;
    uint32_t draws = 0;
    uint64_t buffer_copy_bytes = 0;
  };
  const RecordStats& record_stats() const { return record_stats_; }

  // render_pass_begin->pNext of all barriers must be null.
  void CmdVkBeginRenderPass(const VkRenderPassBeginInfo* render_pass_begin,
                            VkSubpassContents contents) {
    assert_null(render_pass_begin->pNext);
    size_t arguments_size = sizeof(ArgsVkBeginRenderPass);
    uint32_t clear_value_count = render_pass_begin->clearValueCount;
    size_t clear_values_offset = 0;
    if (clear_value_count) {
      arguments_size = xe::align(arguments_size, alignof(VkClearValue));
      clear_values_offset = arguments_size;
      arguments_size += sizeof(VkClearValue) * clear_value_count;
    }
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkBeginRenderPass, arguments_size));
    auto& args = *reinterpret_cast<ArgsVkBeginRenderPass*>(args_ptr);
    args.render_pass = render_pass_begin->renderPass;
    args.framebuffer = render_pass_begin->framebuffer;
    args.render_area = render_pass_begin->renderArea;
    args.clear_value_count = clear_value_count;
    args.contents = contents;
    if (clear_value_count) {
      std::memcpy(args_ptr + clear_values_offset,
                  render_pass_begin->pClearValues,
                  sizeof(VkClearValue) * clear_value_count);
    }
  }

  void CmdVkBindDescriptorSets(VkPipelineBindPoint pipeline_bind_point,
                               VkPipelineLayout layout, uint32_t first_set,
                               uint32_t descriptor_set_count,
                               const VkDescriptorSet* descriptor_sets,
                               uint32_t dynamic_offset_count,
                               const uint32_t* dynamic_offsets) {
    size_t arguments_size =
        xe::align(sizeof(ArgsVkBindDescriptorSets), alignof(VkDescriptorSet));
    size_t descriptor_sets_offset = arguments_size;
    arguments_size += sizeof(VkDescriptorSet) * descriptor_set_count;
    size_t dynamic_offsets_offset = 0;
    if (dynamic_offset_count) {
      arguments_size = xe::align(arguments_size, alignof(uint32_t));
      dynamic_offsets_offset = arguments_size;
      arguments_size += sizeof(uint32_t) * dynamic_offset_count;
    }
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkBindDescriptorSets, arguments_size));
    auto& args = *reinterpret_cast<ArgsVkBindDescriptorSets*>(args_ptr);
    args.pipeline_bind_point = pipeline_bind_point;
    args.layout = layout;
    args.first_set = first_set;
    args.descriptor_set_count = descriptor_set_count;
    args.dynamic_offset_count = dynamic_offset_count;
    std::memcpy(args_ptr + descriptor_sets_offset, descriptor_sets,
                sizeof(VkDescriptorSet) * descriptor_set_count);
    if (dynamic_offset_count) {
      std::memcpy(args_ptr + dynamic_offsets_offset, dynamic_offsets,
                  sizeof(uint32_t) * dynamic_offset_count);
    }
  }

  void CmdVkBindIndexBuffer(VkBuffer buffer, VkDeviceSize offset,
                            VkIndexType index_type) {
    auto& args = *reinterpret_cast<ArgsVkBindIndexBuffer*>(WriteCommand(
        Command::kVkBindIndexBuffer, sizeof(ArgsVkBindIndexBuffer)));
    args.buffer = buffer;
    args.offset = offset;
    args.index_type = index_type;
  }

  void CmdVkBindPipeline(VkPipelineBindPoint pipeline_bind_point,
                         VkPipeline pipeline) {
    auto& args = *reinterpret_cast<ArgsVkBindPipeline*>(
        WriteCommand(Command::kVkBindPipeline, sizeof(ArgsVkBindPipeline)));
    args.pipeline_bind_point = pipeline_bind_point;
    args.pipeline = pipeline;
  }

  void CmdVkBindVertexBuffers(uint32_t first_binding, uint32_t binding_count,
                              const VkBuffer* buffers,
                              const VkDeviceSize* offsets) {
    size_t arguments_size =
        xe::align(sizeof(ArgsVkBindVertexBuffers), alignof(VkBuffer));
    size_t buffers_offset = arguments_size;
    arguments_size =
        xe::align(arguments_size + sizeof(VkBuffer) * binding_count,
                  alignof(VkDeviceSize));
    size_t offsets_offset = arguments_size;
    arguments_size += sizeof(VkDeviceSize) * binding_count;
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkBindVertexBuffers, arguments_size));
    auto& args = *reinterpret_cast<ArgsVkBindVertexBuffers*>(args_ptr);
    args.first_binding = first_binding;
    args.binding_count = binding_count;
    std::memcpy(args_ptr + buffers_offset, buffers,
                sizeof(VkBuffer) * binding_count);
    std::memcpy(args_ptr + offsets_offset, offsets,
                sizeof(VkDeviceSize) * binding_count);
  }

  void CmdClearAttachmentsEmplace(uint32_t attachment_count,
                                  VkClearAttachment*& attachments_out,
                                  uint32_t rect_count,
                                  VkClearRect*& rects_out) {
    size_t arguments_size =
        xe::align(sizeof(ArgsVkClearAttachments), alignof(VkClearAttachment));
    size_t attachments_offset = arguments_size;
    arguments_size =
        xe::align(arguments_size + sizeof(VkClearAttachment) * attachment_count,
                  alignof(VkClearRect));
    size_t rects_offset = arguments_size;
    arguments_size += sizeof(VkClearRect) * rect_count;
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkClearAttachments, arguments_size));
    auto& args = *reinterpret_cast<ArgsVkClearAttachments*>(args_ptr);
    args.attachment_count = attachment_count;
    args.rect_count = rect_count;
    attachments_out =
        reinterpret_cast<VkClearAttachment*>(args_ptr + attachments_offset);
    rects_out = reinterpret_cast<VkClearRect*>(args_ptr + rects_offset);
  }
  void CmdVkClearAttachments(uint32_t attachment_count,
                             const VkClearAttachment* attachments,
                             uint32_t rect_count, const VkClearRect* rects) {
    VkClearAttachment* attachments_arg;
    VkClearRect* rects_arg;
    CmdClearAttachmentsEmplace(attachment_count, attachments_arg, rect_count,
                               rects_arg);
    std::memcpy(attachments_arg, attachments,
                sizeof(VkClearAttachment) * attachment_count);
    std::memcpy(rects_arg, rects, sizeof(VkClearRect) * rect_count);
  }

  VkImageSubresourceRange* CmdClearColorImageEmplace(
      VkImage image, VkImageLayout image_layout, const VkClearColorValue* color,
      uint32_t range_count) {
    const size_t header_size = xe::align(sizeof(ArgsVkClearColorImage),
                                         alignof(VkImageSubresourceRange));
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(WriteCommand(
        Command::kVkClearColorImage,
        header_size + sizeof(VkImageSubresourceRange) * range_count));
    auto& args = *reinterpret_cast<ArgsVkClearColorImage*>(args_ptr);
    args.image = image;
    args.image_layout = image_layout;
    args.color = *color;
    args.range_count = range_count;
    return reinterpret_cast<VkImageSubresourceRange*>(args_ptr + header_size);
  }
  void CmdVkClearColorImage(VkImage image, VkImageLayout image_layout,
                            const VkClearColorValue* color,
                            uint32_t range_count,
                            const VkImageSubresourceRange* ranges) {
    std::memcpy(
        CmdClearColorImageEmplace(image, image_layout, color, range_count),
        ranges, sizeof(VkImageSubresourceRange) * range_count);
  }

  VkBufferCopy* CmdCopyBufferEmplace(VkBuffer src_buffer, VkBuffer dst_buffer,
                                     uint32_t region_count) {
    const size_t header_size =
        xe::align(sizeof(ArgsVkCopyBuffer), alignof(VkBufferCopy));
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkCopyBuffer,
                     header_size + sizeof(VkBufferCopy) * region_count));
    ++record_stats_.buffer_copies;
    auto& args = *reinterpret_cast<ArgsVkCopyBuffer*>(args_ptr);
    args.src_buffer = src_buffer;
    args.dst_buffer = dst_buffer;
    args.region_count = region_count;
    return reinterpret_cast<VkBufferCopy*>(args_ptr + header_size);
  }
  void CmdVkCopyBuffer(VkBuffer src_buffer, VkBuffer dst_buffer,
                       uint32_t region_count, const VkBufferCopy* regions) {
    for (uint32_t i = 0; i < region_count; ++i) {
      record_stats_.buffer_copy_bytes += regions[i].size;
    }
    std::memcpy(CmdCopyBufferEmplace(src_buffer, dst_buffer, region_count),
                regions, sizeof(VkBufferCopy) * region_count);
  }

  VkBufferImageCopy* CmdCopyBufferToImageEmplace(VkBuffer src_buffer,
                                                 VkImage dst_image,
                                                 VkImageLayout dst_image_layout,
                                                 uint32_t region_count) {
    const size_t header_size =
        xe::align(sizeof(ArgsVkCopyBufferToImage), alignof(VkBufferImageCopy));
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkCopyBufferToImage,
                     header_size + sizeof(VkBufferImageCopy) * region_count));
    ++record_stats_.buffer_image_copies;
    auto& args = *reinterpret_cast<ArgsVkCopyBufferToImage*>(args_ptr);
    args.src_buffer = src_buffer;
    args.dst_image = dst_image;
    args.dst_image_layout = dst_image_layout;
    args.region_count = region_count;
    return reinterpret_cast<VkBufferImageCopy*>(args_ptr + header_size);
  }
  void CmdVkCopyBufferToImage(VkBuffer src_buffer, VkImage dst_image,
                              VkImageLayout dst_image_layout,
                              uint32_t region_count,
                              const VkBufferImageCopy* regions) {
    std::memcpy(CmdCopyBufferToImageEmplace(src_buffer, dst_image,
                                            dst_image_layout, region_count),
                regions, sizeof(VkBufferImageCopy) * region_count);
  }

  VkBufferImageCopy* CmdCopyImageToBufferEmplace(VkImage src_image,
                                                 VkImageLayout src_image_layout,
                                                 VkBuffer dst_buffer,
                                                 uint32_t region_count) {
    const size_t header_size =
        xe::align(sizeof(ArgsVkCopyImageToBuffer), alignof(VkBufferImageCopy));
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkCopyImageToBuffer,
                     header_size + sizeof(VkBufferImageCopy) * region_count));
    auto& args = *reinterpret_cast<ArgsVkCopyImageToBuffer*>(args_ptr);
    args.src_image = src_image;
    args.src_image_layout = src_image_layout;
    args.dst_buffer = dst_buffer;
    args.region_count = region_count;
    return reinterpret_cast<VkBufferImageCopy*>(args_ptr + header_size);
  }
  void CmdVkCopyImageToBuffer(VkImage src_image, VkImageLayout src_image_layout,
                              VkBuffer dst_buffer, uint32_t region_count,
                              const VkBufferImageCopy* regions) {
    std::memcpy(CmdCopyImageToBufferEmplace(src_image, src_image_layout,
                                            dst_buffer, region_count),
                regions, sizeof(VkBufferImageCopy) * region_count);
  }

  void CmdVkDispatch(uint32_t group_count_x, uint32_t group_count_y,
                     uint32_t group_count_z) {
    ++record_stats_.dispatches;
    auto& args = *reinterpret_cast<ArgsVkDispatch*>(
        WriteCommand(Command::kVkDispatch, sizeof(ArgsVkDispatch)));
    args.group_count_x = group_count_x;
    args.group_count_y = group_count_y;
    args.group_count_z = group_count_z;
  }

  void CmdVkDraw(uint32_t vertex_count, uint32_t instance_count,
                 uint32_t first_vertex, uint32_t first_instance) {
    ++record_stats_.draws;
    auto& args = *reinterpret_cast<ArgsVkDraw*>(
        WriteCommand(Command::kVkDraw, sizeof(ArgsVkDraw)));
    args.vertex_count = vertex_count;
    args.instance_count = instance_count;
    args.first_vertex = first_vertex;
    args.first_instance = first_instance;
  }

  void CmdVkDrawIndexed(uint32_t index_count, uint32_t instance_count,
                        uint32_t first_index, int32_t vertex_offset,
                        uint32_t first_instance) {
    ++record_stats_.draws;
    auto& args = *reinterpret_cast<ArgsVkDrawIndexed*>(
        WriteCommand(Command::kVkDrawIndexed, sizeof(ArgsVkDrawIndexed)));
    args.index_count = index_count;
    args.instance_count = instance_count;
    args.first_index = first_index;
    args.vertex_offset = vertex_offset;
    args.first_instance = first_instance;
  }

  // Head-emit draw concatenation (vulkan_merge_draws): like CmdVkDrawIndexed,
  // but returns the element offset of the recorded arguments so the run
  // head's index count can be grown in place while the run stays open
  // (recording the run draw at FLUSH time instead would place it AFTER later
  // draws' state setup commands - the head draw would then execute with the
  // wrong state). An offset is returned instead of a pointer because the
  // stream storage may reallocate.
  size_t CmdVkDrawIndexedRetained(uint32_t index_count,
                                  uint32_t instance_count,
                                  uint32_t first_index, int32_t vertex_offset,
                                  uint32_t first_instance) {
    size_t header_offset_elements = command_stream_.size();
    CmdVkDrawIndexed(index_count, instance_count, first_index, vertex_offset,
                     first_instance);
    return header_offset_elements + kCommandHeaderSizeElements;
  }
  void PatchVkDrawIndexedIndexCount(size_t args_offset_elements,
                                    uint32_t index_count) {
    reinterpret_cast<ArgsVkDrawIndexed*>(command_stream_.data() +
                                         args_offset_elements)
        ->index_count = index_count;
  }

  void CmdVkDrawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset,
                                uint32_t draw_count, uint32_t stride) {
    auto& args = *reinterpret_cast<ArgsVkDrawIndexedIndirect*>(WriteCommand(
        Command::kVkDrawIndexedIndirect, sizeof(ArgsVkDrawIndexedIndirect)));
    args.buffer = buffer;
    args.offset = offset;
    args.draw_count = draw_count;
    args.stride = stride;
  }

  void CmdVkDrawIndexedIndirectCount(VkBuffer buffer, VkDeviceSize offset,
                                     VkBuffer count_buffer,
                                     VkDeviceSize count_buffer_offset,
                                     uint32_t max_draw_count, uint32_t stride) {
    auto& args = *reinterpret_cast<ArgsVkDrawIndexedIndirectCount*>(
        WriteCommand(Command::kVkDrawIndexedIndirectCount,
                     sizeof(ArgsVkDrawIndexedIndirectCount)));
    args.buffer = buffer;
    args.offset = offset;
    args.count_buffer = count_buffer;
    args.count_buffer_offset = count_buffer_offset;
    args.max_draw_count = max_draw_count;
    args.stride = stride;
  }

  void CmdVkEndRenderPass() { WriteCommand(Command::kVkEndRenderPass, 0); }

  // BD input-attachment merge (Inc3): advance to the next subpass of a merged
  // 2-subpass feedback render pass (producer in subpass 0, the same-pixel
  // composite consumer reading it as an input attachment in subpass 1).
  void CmdVkNextSubpass(VkSubpassContents contents) {
    auto& args = *reinterpret_cast<ArgsVkNextSubpass*>(
        WriteCommand(Command::kVkNextSubpass, sizeof(ArgsVkNextSubpass)));
    args.contents = contents;
  }

  // pNext of all barriers must be null.
  void CmdVkPipelineBarrier(VkPipelineStageFlags src_stage_mask,
                            VkPipelineStageFlags dst_stage_mask,
                            VkDependencyFlags dependency_flags,
                            uint32_t memory_barrier_count,
                            const VkMemoryBarrier* memory_barriers,
                            uint32_t buffer_memory_barrier_count,
                            const VkBufferMemoryBarrier* buffer_memory_barriers,
                            uint32_t image_memory_barrier_count,
                            const VkImageMemoryBarrier* image_memory_barriers);

  void CmdVkPushConstants(VkPipelineLayout layout,
                          VkShaderStageFlags stage_flags, uint32_t offset,
                          uint32_t size, const void* values) {
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(WriteCommand(
        Command::kVkPushConstants, sizeof(ArgsVkPushConstants) + size));
    auto& args = *reinterpret_cast<ArgsVkPushConstants*>(args_ptr);
    args.layout = layout;
    args.stage_flags = stage_flags;
    args.offset = offset;
    args.size = size;
    std::memcpy(args_ptr + sizeof(ArgsVkPushConstants), values, size);
  }

  void CmdVkSetBlendConstants(const float* blend_constants) {
    auto& args = *reinterpret_cast<ArgsVkSetBlendConstants*>(WriteCommand(
        Command::kVkSetBlendConstants, sizeof(ArgsVkSetBlendConstants)));
    std::memcpy(args.blend_constants, blend_constants, sizeof(float) * 4);
  }

  // Records a push-descriptor set update. Only image-info writes are supported
  // (the texture/sampler sets): every write must reference VkDescriptorImageInfo
  // entries, which are flattened into one array and re-pointed at execution.
  void CmdVkPushDescriptorSetKHR(VkPipelineBindPoint pipeline_bind_point,
                                 VkPipelineLayout layout, uint32_t set,
                                 uint32_t descriptor_write_count,
                                 const VkWriteDescriptorSet* descriptor_writes) {
    uint32_t image_info_count = 0;
    for (uint32_t i = 0; i < descriptor_write_count; ++i) {
      image_info_count += descriptor_writes[i].descriptorCount;
    }
    size_t arguments_size = xe::align(sizeof(ArgsVkPushDescriptorSetKHR),
                                      alignof(VkWriteDescriptorSet));
    size_t writes_offset = arguments_size;
    arguments_size += sizeof(VkWriteDescriptorSet) * descriptor_write_count;
    arguments_size = xe::align(arguments_size, alignof(VkDescriptorImageInfo));
    size_t image_infos_offset = arguments_size;
    arguments_size += sizeof(VkDescriptorImageInfo) * image_info_count;
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkPushDescriptorSetKHR, arguments_size));
    auto& args = *reinterpret_cast<ArgsVkPushDescriptorSetKHR*>(args_ptr);
    args.pipeline_bind_point = pipeline_bind_point;
    args.layout = layout;
    args.set = set;
    args.descriptor_write_count = descriptor_write_count;
    args.image_info_count = image_info_count;
    auto writes_out =
        reinterpret_cast<VkWriteDescriptorSet*>(args_ptr + writes_offset);
    auto image_infos_out =
        reinterpret_cast<VkDescriptorImageInfo*>(args_ptr + image_infos_offset);
    uint32_t image_info_index = 0;
    for (uint32_t i = 0; i < descriptor_write_count; ++i) {
      VkWriteDescriptorSet& write_out = writes_out[i];
      write_out = descriptor_writes[i];
      // dstSet is ignored for push; null the pointers we don't serialize.
      write_out.dstSet = VK_NULL_HANDLE;
      write_out.pBufferInfo = nullptr;
      write_out.pTexelBufferView = nullptr;
      // Store the image-info array index in pImageInfo as an offset placeholder;
      // re-pointed to the replayed array in Execute (can't store the final
      // pointer here as the command stream may be reallocated).
      write_out.pImageInfo = reinterpret_cast<const VkDescriptorImageInfo*>(
          static_cast<uintptr_t>(image_info_index));
      if (descriptor_writes[i].descriptorCount && descriptor_writes[i].pImageInfo) {
        std::memcpy(image_infos_out + image_info_index,
                    descriptor_writes[i].pImageInfo,
                    sizeof(VkDescriptorImageInfo) *
                        descriptor_writes[i].descriptorCount);
      }
      image_info_index += descriptor_writes[i].descriptorCount;
    }
  }

  void CmdVkSetDepthBias(float depth_bias_constant_factor,
                         float depth_bias_clamp,
                         float depth_bias_slope_factor) {
    auto& args = *reinterpret_cast<ArgsVkSetDepthBias*>(
        WriteCommand(Command::kVkSetDepthBias, sizeof(ArgsVkSetDepthBias)));
    args.depth_bias_constant_factor = depth_bias_constant_factor;
    args.depth_bias_clamp = depth_bias_clamp;
    args.depth_bias_slope_factor = depth_bias_slope_factor;
  }

  void CmdVkSetCullMode(VkCullModeFlags cull_mode) {
    auto& args = *reinterpret_cast<ArgsVkSetCullMode*>(
        WriteCommand(Command::kVkSetCullMode, sizeof(ArgsVkSetCullMode)));
    args.cull_mode = cull_mode;
  }

  void CmdVkSetFrontFace(VkFrontFace front_face) {
    auto& args = *reinterpret_cast<ArgsVkSetFrontFace*>(
        WriteCommand(Command::kVkSetFrontFace, sizeof(ArgsVkSetFrontFace)));
    args.front_face = front_face;
  }

  void CmdVkSetDepthTestEnable(VkBool32 depth_test_enable) {
    auto& args = *reinterpret_cast<ArgsVkSetDepthTestEnable*>(WriteCommand(
        Command::kVkSetDepthTestEnable, sizeof(ArgsVkSetDepthTestEnable)));
    args.depth_test_enable = depth_test_enable;
  }

  void CmdVkSetDepthWriteEnable(VkBool32 depth_write_enable) {
    auto& args = *reinterpret_cast<ArgsVkSetDepthWriteEnable*>(WriteCommand(
        Command::kVkSetDepthWriteEnable, sizeof(ArgsVkSetDepthWriteEnable)));
    args.depth_write_enable = depth_write_enable;
  }

  void CmdVkSetDepthCompareOp(VkCompareOp depth_compare_op) {
    auto& args = *reinterpret_cast<ArgsVkSetDepthCompareOp*>(WriteCommand(
        Command::kVkSetDepthCompareOp, sizeof(ArgsVkSetDepthCompareOp)));
    args.depth_compare_op = depth_compare_op;
  }

  // VK_KHR_fragment_shading_rate (VRS) - coarse-shade per-draw (foliage lever).
  void CmdVkSetFragmentShadingRate(
      const VkExtent2D& fragment_size,
      VkFragmentShadingRateCombinerOpKHR combiner_op0,
      VkFragmentShadingRateCombinerOpKHR combiner_op1) {
    auto& args = *reinterpret_cast<ArgsVkSetFragmentShadingRate*>(
        WriteCommand(Command::kVkSetFragmentShadingRate,
                     sizeof(ArgsVkSetFragmentShadingRate)));
    args.fragment_size = fragment_size;
    args.combiner_ops[0] = combiner_op0;
    args.combiner_ops[1] = combiner_op1;
  }

  void CmdVkSetStencilTestEnable(VkBool32 stencil_test_enable) {
    auto& args = *reinterpret_cast<ArgsVkSetStencilTestEnable*>(WriteCommand(
        Command::kVkSetStencilTestEnable, sizeof(ArgsVkSetStencilTestEnable)));
    args.stencil_test_enable = stencil_test_enable;
  }

  void CmdVkSetStencilOp(VkStencilFaceFlags face_mask, VkStencilOp fail_op,
                         VkStencilOp pass_op, VkStencilOp depth_fail_op,
                         VkCompareOp compare_op) {
    auto& args = *reinterpret_cast<ArgsVkSetStencilOp*>(
        WriteCommand(Command::kVkSetStencilOp, sizeof(ArgsVkSetStencilOp)));
    args.face_mask = face_mask;
    args.fail_op = fail_op;
    args.pass_op = pass_op;
    args.depth_fail_op = depth_fail_op;
    args.compare_op = compare_op;
  }

  void CmdVkSetPrimitiveTopology(VkPrimitiveTopology primitive_topology) {
    auto& args = *reinterpret_cast<ArgsVkSetPrimitiveTopology*>(WriteCommand(
        Command::kVkSetPrimitiveTopology, sizeof(ArgsVkSetPrimitiveTopology)));
    args.primitive_topology = primitive_topology;
  }

  void CmdVkSetPrimitiveRestartEnable(VkBool32 primitive_restart_enable) {
    auto& args = *reinterpret_cast<ArgsVkSetPrimitiveRestartEnable*>(
        WriteCommand(Command::kVkSetPrimitiveRestartEnable,
                     sizeof(ArgsVkSetPrimitiveRestartEnable)));
    args.primitive_restart_enable = primitive_restart_enable;
  }

  void CmdVkSetScissor(uint32_t first_scissor, uint32_t scissor_count,
                       const VkRect2D* scissors) {
    const size_t header_size =
        xe::align(sizeof(ArgsVkSetScissor), alignof(VkRect2D));
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkSetScissor,
                     header_size + sizeof(VkRect2D) * scissor_count));
    auto& args = *reinterpret_cast<ArgsVkSetScissor*>(args_ptr);
    args.first_scissor = first_scissor;
    args.scissor_count = scissor_count;
    std::memcpy(args_ptr + header_size, scissors,
                sizeof(VkRect2D) * scissor_count);
  }

  void CmdVkSetStencilCompareMask(VkStencilFaceFlags face_mask,
                                  uint32_t compare_mask) {
    auto& args = *reinterpret_cast<ArgsSetStencilMaskReference*>(
        WriteCommand(Command::kVkSetStencilCompareMask,
                     sizeof(ArgsSetStencilMaskReference)));
    args.face_mask = face_mask;
    args.mask_reference = compare_mask;
  }

  void CmdVkSetStencilReference(VkStencilFaceFlags face_mask,
                                uint32_t reference) {
    auto& args = *reinterpret_cast<ArgsSetStencilMaskReference*>(WriteCommand(
        Command::kVkSetStencilReference, sizeof(ArgsSetStencilMaskReference)));
    args.face_mask = face_mask;
    args.mask_reference = reference;
  }

  void CmdVkSetStencilWriteMask(VkStencilFaceFlags face_mask,
                                uint32_t write_mask) {
    auto& args = *reinterpret_cast<ArgsSetStencilMaskReference*>(WriteCommand(
        Command::kVkSetStencilWriteMask, sizeof(ArgsSetStencilMaskReference)));
    args.face_mask = face_mask;
    args.mask_reference = write_mask;
  }

  void CmdVkSetViewport(uint32_t first_viewport, uint32_t viewport_count,
                        const VkViewport* viewports) {
    const size_t header_size =
        xe::align(sizeof(ArgsVkSetViewport), alignof(VkViewport));
    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(
        WriteCommand(Command::kVkSetViewport,
                     header_size + sizeof(VkViewport) * viewport_count));
    auto& args = *reinterpret_cast<ArgsVkSetViewport*>(args_ptr);
    args.first_viewport = first_viewport;
    args.viewport_count = viewport_count;
    std::memcpy(args_ptr + header_size, viewports,
                sizeof(VkViewport) * viewport_count);
  }

  // Route A per-pass GPU timing (Thor binning diagnostic): records a GPU
  // timestamp into query_pool at index `query` at the given pipeline stage,
  // in-order within the deferred stream so render-pass spans can be bracketed.
  // Only emitted when the caller chooses to (cvar-gated upstream); default path
  // never records this, so the command stream is unchanged when disabled.
  void CmdVkWriteTimestamp(VkPipelineStageFlagBits pipeline_stage,
                           VkQueryPool query_pool, uint32_t query) {
    auto& args = *reinterpret_cast<ArgsVkWriteTimestamp*>(WriteCommand(
        Command::kVkWriteTimestamp, sizeof(ArgsVkWriteTimestamp)));
    args.pipeline_stage = pipeline_stage;
    args.query_pool = query_pool;
    args.query = query;
  }

 private:
  RecordStats record_stats_;
  enum class Command {
    kVkBeginRenderPass,
    kVkBindDescriptorSets,
    kVkBindIndexBuffer,
    kVkBindPipeline,
    kVkBindVertexBuffers,
    kVkClearAttachments,
    kVkClearColorImage,
    kVkCopyBuffer,
    kVkCopyBufferToImage,
    kVkCopyImageToBuffer,
    kVkDispatch,
    kVkDraw,
    kVkDrawIndexed,
    kVkDrawIndexedIndirect,
    kVkDrawIndexedIndirectCount,
    kVkEndRenderPass,
    kVkNextSubpass,
    kVkPipelineBarrier,
    kVkPushConstants,
    kVkPushDescriptorSetKHR,
    kVkSetBlendConstants,
    kVkSetCullMode,
    kVkSetDepthBias,
    kVkSetDepthCompareOp,
    kVkSetDepthTestEnable,
    kVkSetDepthWriteEnable,
    kVkSetFragmentShadingRate,
    kVkSetFrontFace,
    kVkSetPrimitiveRestartEnable,
    kVkSetPrimitiveTopology,
    kVkSetScissor,
    kVkSetStencilCompareMask,
    kVkSetStencilOp,
    kVkSetStencilTestEnable,
    kVkSetStencilReference,
    kVkSetStencilWriteMask,
    kVkSetViewport,
    kVkWriteTimestamp,
  };

  struct CommandHeader {
    Command command;
    uint32_t arguments_size_elements;
  };
  static constexpr size_t kCommandHeaderSizeElements =
      (sizeof(CommandHeader) + sizeof(uintmax_t) - 1) / sizeof(uintmax_t);

  struct ArgsVkBeginRenderPass {
    VkRenderPass render_pass;
    VkFramebuffer framebuffer;
    VkRect2D render_area;
    uint32_t clear_value_count;
    VkSubpassContents contents;
    // Followed by aligned optional VkClearValue[].
    static_assert(alignof(VkClearValue) <= alignof(uintmax_t));
  };

  struct ArgsVkWriteTimestamp {
    VkQueryPool query_pool;
    uint32_t query;
    VkPipelineStageFlagBits pipeline_stage;
  };

  struct ArgsVkBindDescriptorSets {
    VkPipelineBindPoint pipeline_bind_point;
    VkPipelineLayout layout;
    uint32_t first_set;
    uint32_t descriptor_set_count;
    uint32_t dynamic_offset_count;
    // Followed by aligned VkDescriptorSet[], optional uint32_t[].
    static_assert(alignof(VkDescriptorSet) <= alignof(uintmax_t));
  };

  struct ArgsVkBindIndexBuffer {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkIndexType index_type;
  };

  struct ArgsVkBindPipeline {
    VkPipelineBindPoint pipeline_bind_point;
    VkPipeline pipeline;
  };

  struct ArgsVkBindVertexBuffers {
    uint32_t first_binding;
    uint32_t binding_count;
    // Followed by aligned VkBuffer[], VkDeviceSize[].
    static_assert(alignof(VkBuffer) <= alignof(uintmax_t));
    static_assert(alignof(VkDeviceSize) <= alignof(uintmax_t));
  };

  struct ArgsVkClearAttachments {
    uint32_t attachment_count;
    uint32_t rect_count;
    // Followed by aligned VkClearAttachment[], VkClearRect[].
    static_assert(alignof(VkClearAttachment) <= alignof(uintmax_t));
    static_assert(alignof(VkClearRect) <= alignof(uintmax_t));
  };

  struct ArgsVkClearColorImage {
    VkImage image;
    VkImageLayout image_layout;
    VkClearColorValue color;
    uint32_t range_count;
    // Followed by aligned VkImageSubresourceRange[].
    static_assert(alignof(VkImageSubresourceRange) <= alignof(uintmax_t));
  };

  struct ArgsVkCopyBuffer {
    VkBuffer src_buffer;
    VkBuffer dst_buffer;
    uint32_t region_count;
    // Followed by aligned VkBufferCopy[].
    static_assert(alignof(VkBufferCopy) <= alignof(uintmax_t));
  };

  struct ArgsVkCopyBufferToImage {
    VkBuffer src_buffer;
    VkImage dst_image;
    VkImageLayout dst_image_layout;
    uint32_t region_count;
    // Followed by aligned VkBufferImageCopy[].
    static_assert(alignof(VkBufferImageCopy) <= alignof(uintmax_t));
  };

  struct ArgsVkCopyImageToBuffer {
    VkImage src_image;
    VkImageLayout src_image_layout;
    VkBuffer dst_buffer;
    uint32_t region_count;
    // Followed by aligned VkBufferImageCopy[].
    static_assert(alignof(VkBufferImageCopy) <= alignof(uintmax_t));
  };

  struct ArgsVkDispatch {
    uint32_t group_count_x;
    uint32_t group_count_y;
    uint32_t group_count_z;
  };

  struct ArgsVkDraw {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
  };

  struct ArgsVkDrawIndexed {
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    int32_t vertex_offset;
    uint32_t first_instance;
  };

  struct ArgsVkDrawIndexedIndirect {
    VkBuffer buffer;
    VkDeviceSize offset;
    uint32_t draw_count;
    uint32_t stride;
  };

  struct ArgsVkDrawIndexedIndirectCount {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkBuffer count_buffer;
    VkDeviceSize count_buffer_offset;
    uint32_t max_draw_count;
    uint32_t stride;
  };

  struct ArgsVkPipelineBarrier {
    VkPipelineStageFlags src_stage_mask;
    VkPipelineStageFlags dst_stage_mask;
    VkDependencyFlags dependency_flags;
    uint32_t memory_barrier_count;
    uint32_t buffer_memory_barrier_count;
    uint32_t image_memory_barrier_count;
    // Followed by aligned optional VkMemoryBarrier[],
    // optional VkBufferMemoryBarrier[], optional VkImageMemoryBarrier[].
    static_assert(alignof(VkMemoryBarrier) <= alignof(uintmax_t));
    static_assert(alignof(VkBufferMemoryBarrier) <= alignof(uintmax_t));
    static_assert(alignof(VkImageMemoryBarrier) <= alignof(uintmax_t));
  };

  struct ArgsVkPushConstants {
    VkPipelineLayout layout;
    VkShaderStageFlags stage_flags;
    uint32_t offset;
    uint32_t size;
    // Followed by `size` bytes of values.
  };

  struct ArgsVkPushDescriptorSetKHR {
    VkPipelineBindPoint pipeline_bind_point;
    VkPipelineLayout layout;
    uint32_t set;
    uint32_t descriptor_write_count;
    uint32_t image_info_count;
    // Followed by aligned VkWriteDescriptorSet[descriptor_write_count], then
    // VkDescriptorImageInfo[image_info_count]. Only image-info writes (sampled
    // images / samplers) are supported - each write's pImageInfo is re-pointed
    // into the replayed image-info array at execution time, pBufferInfo and
    // pTexelBufferView are null. dstSet is ignored by vkCmdPushDescriptorSetKHR.
    static_assert(alignof(VkWriteDescriptorSet) <= alignof(uintmax_t));
    static_assert(alignof(VkDescriptorImageInfo) <= alignof(uintmax_t));
  };

  struct ArgsVkSetBlendConstants {
    float blend_constants[4];
  };

  struct ArgsVkNextSubpass {
    VkSubpassContents contents;
  };

  struct ArgsVkSetCullMode {
    VkCullModeFlags cull_mode;
  };

  struct ArgsVkSetFrontFace {
    VkFrontFace front_face;
  };

  struct ArgsVkSetDepthTestEnable {
    VkBool32 depth_test_enable;
  };

  struct ArgsVkSetDepthWriteEnable {
    VkBool32 depth_write_enable;
  };

  struct ArgsVkSetDepthCompareOp {
    VkCompareOp depth_compare_op;
  };

  struct ArgsVkSetFragmentShadingRate {
    VkExtent2D fragment_size;
    VkFragmentShadingRateCombinerOpKHR combiner_ops[2];
  };

  struct ArgsVkSetStencilTestEnable {
    VkBool32 stencil_test_enable;
  };

  struct ArgsVkSetStencilOp {
    VkStencilFaceFlags face_mask;
    VkStencilOp fail_op;
    VkStencilOp pass_op;
    VkStencilOp depth_fail_op;
    VkCompareOp compare_op;
  };

  struct ArgsVkSetPrimitiveTopology {
    VkPrimitiveTopology primitive_topology;
  };

  struct ArgsVkSetPrimitiveRestartEnable {
    VkBool32 primitive_restart_enable;
  };

  struct ArgsVkSetDepthBias {
    float depth_bias_constant_factor;
    float depth_bias_clamp;
    float depth_bias_slope_factor;
  };

  struct ArgsVkSetScissor {
    uint32_t first_scissor;
    uint32_t scissor_count;
    // Followed by aligned VkRect2D[].
    static_assert(alignof(VkRect2D) <= alignof(uintmax_t));
  };

  struct ArgsSetStencilMaskReference {
    VkStencilFaceFlags face_mask;
    uint32_t mask_reference;
  };

  struct ArgsVkSetViewport {
    uint32_t first_viewport;
    uint32_t viewport_count;
    // Followed by aligned VkViewport[].
    static_assert(alignof(VkViewport) <= alignof(uintmax_t));
  };

  void* WriteCommand(Command command, size_t arguments_size_bytes);

  const VulkanCommandProcessor& command_processor_;

  // uintmax_t to ensure uint64_t and pointer alignment of all structures.
  std::vector<uintmax_t> command_stream_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_DEFERRED_COMMAND_BUFFER_H_