/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GPU_FLAGS_H_
#define XENIA_GPU_GPU_FLAGS_H_
#include "xenia/base/cvar.h"

DECLARE_path(trace_gpu_prefix);
DECLARE_bool(trace_gpu_stream);

DECLARE_path(dump_shaders);

DECLARE_bool(vsync);

DECLARE_bool(gpu_allow_invalid_fetch_constants);
DECLARE_bool(clear_memory_page_state);
DECLARE_bool(gpu_interrupt_on_ring_idle);
DECLARE_bool(gpu_interrupt_on_swap);
DECLARE_bool(gpu_trace_interrupts);
DECLARE_int32(gpu_trace_interrupts_budget);
DECLARE_bool(gpu_blue_dragon_kick_wait_token);
DECLARE_int32(gpu_blue_dragon_kick_wait_token_budget);
DECLARE_bool(gpu_trace_swap);
DECLARE_int32(gpu_trace_packet_budget);
DECLARE_bool(gpu_trace_swap_frontbuffer_checksum);
DECLARE_int32(gpu_trace_swap_frontbuffer_checksum_budget);
DECLARE_bool(gpu_trace_swap_render_targets);
DECLARE_int32(gpu_trace_swap_render_targets_budget);
DECLARE_bool(vulkan_trace_resolve);
DECLARE_int32(vulkan_trace_resolve_budget);
DECLARE_bool(vulkan_trace_resolve_checksum);
DECLARE_int32(vulkan_trace_resolve_checksum_budget);
DECLARE_bool(vulkan_trace_edram_checksum);
DECLARE_int32(vulkan_trace_edram_checksum_budget);
DECLARE_bool(vulkan_readback_resolve);
DECLARE_bool(vulkan_trace_copy_state);
DECLARE_int32(vulkan_trace_copy_state_budget);
DECLARE_bool(vulkan_trace_draw_state);
DECLARE_int32(vulkan_trace_draw_state_budget);
DECLARE_bool(vulkan_trace_swap_shared_memory_checksum);
DECLARE_int32(vulkan_trace_swap_shared_memory_checksum_budget);
DECLARE_bool(vulkan_present_recent_resolve_on_swap);
DECLARE_bool(vulkan_present_scored_resolve_on_swap);
DECLARE_int32(vulkan_present_scored_resolve_min_width);
DECLARE_int32(vulkan_present_scored_resolve_min_height);
DECLARE_int32(vulkan_present_scored_resolve_budget);
DECLARE_int32(vulkan_present_scored_resolve_required_format);
DECLARE_bool(vulkan_present_scored_resolve_reject_clear_like);
DECLARE_bool(vulkan_present_forced_resolve_on_swap);
DECLARE_uint32(vulkan_present_forced_resolve_address);
DECLARE_uint32(vulkan_present_forced_resolve_length);
DECLARE_uint32(vulkan_present_forced_resolve_width);
DECLARE_uint32(vulkan_present_forced_resolve_height);
DECLARE_uint32(vulkan_present_forced_resolve_pitch);
DECLARE_uint32(vulkan_present_forced_resolve_format);
DECLARE_bool(vulkan_debug_solid_guest_output);
DECLARE_bool(vulkan_force_signed_2101010_unorm_fallback);
DECLARE_bool(gpu_early_primary_read_pointer_writeback);

DECLARE_bool(non_seamless_cube_map);

DECLARE_bool(half_pixel_offset);

DECLARE_int32(query_occlusion_fake_sample_count);

#define XE_GPU_FINE_GRAINED_DRAW_SCOPES 1

#endif  // XENIA_GPU_GPU_FLAGS_H_
