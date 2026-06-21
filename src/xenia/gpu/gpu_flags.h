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
DECLARE_bool(vsync_on_swap);
DECLARE_uint32(gpu_frame_limit_fps);
DECLARE_bool(gpu_adpf_performance_hints);
DECLARE_bool(gpu_adpf_thermal_throttle);
DECLARE_bool(gpu_vulkan_edram_roaa);
DECLARE_int32(gpu_vulkan_inpass_edram_transfers);
DECLARE_bool(gpu_binning_deinterleave_pos);
DECLARE_bool(gpu_edram_passes_dont_care_safe);

DECLARE_bool(gpu_allow_invalid_fetch_constants);
DECLARE_bool(clear_memory_page_state);
DECLARE_bool(gpu_interrupt_on_ring_idle);
DECLARE_bool(gpu_interrupt_on_swap);
DECLARE_bool(gpu_trace_interrupts);
DECLARE_int32(gpu_trace_interrupts_budget);
DECLARE_bool(gpu_log_interrupt_counts);
DECLARE_bool(gpu_watch_lo_render_gate);
DECLARE_bool(gpu_force_lo_render_latch);
DECLARE_int32(dump_guest_mem_at_ms);
DECLARE_uint32(dump_guest_mem_base);
DECLARE_int32(dump_guest_mem_size_mb);
DECLARE_string(dump_guest_mem_path);
DECLARE_bool(gpu_blue_dragon_kick_wait_token);
DECLARE_int32(gpu_blue_dragon_kick_wait_token_budget);
DECLARE_int32(gpu_vrs_foliage_rate);
DECLARE_uint32(gpu_vrs_enable_after_guest_ms);
DECLARE_bool(gpu_freeze_ab_alternate_vrs);
DECLARE_int32(thor_gpu_thread_affinity_cpu);
DECLARE_int32(gpu_cp_worker_nice);
DECLARE_bool(gpu_trace_swap);
DECLARE_bool(gpu_trace_vd_swap);
DECLARE_int32(gpu_trace_packet_budget);
DECLARE_bool(gpu_use_vd_scaler_output_for_swap);
DECLARE_bool(gpu_trace_texture_cache_actions);
DECLARE_int32(gpu_unknown_register_log_budget);
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
DECLARE_string(vulkan_trace_draw_shader_filter);
DECLARE_bool(vulkan_trace_pipeline_creation);
DECLARE_bool(vulkan_trace_shader_constants);
DECLARE_int32(vulkan_trace_shader_constants_budget);
DECLARE_string(vulkan_trace_shader_constants_shader_filter);
DECLARE_bool(vulkan_trace_texture_source_checksum);
DECLARE_int32(vulkan_trace_texture_source_checksum_budget);
DECLARE_string(vulkan_trace_texture_source_shader_filter);
DECLARE_bool(vulkan_trace_vertex_fetch_checksum);
DECLARE_int32(vulkan_trace_vertex_fetch_checksum_budget);
DECLARE_string(vulkan_trace_vertex_fetch_shader_filter);
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
DECLARE_string(vulkan_debug_pixel_shader_output_filter);
DECLARE_int32(vulkan_debug_pixel_shader_output_mode);
DECLARE_string(vulkan_debug_pixel_shader_output_secondary_filter);
DECLARE_int32(vulkan_debug_pixel_shader_output_secondary_mode);
DECLARE_bool(vulkan_force_2101010_rgba8_fallback);
DECLARE_bool(vulkan_force_signed_2101010_unorm_fallback);
DECLARE_bool(spirv_no_invariant_color_output);
DECLARE_bool(spirv_no_invariant_position_output);
DECLARE_bool(spirv_debug_force_fullscreen_position);
DECLARE_int32(spirv_debug_position_probe);
DECLARE_bool(gpu_vulkan_shared_memory_no_switch);
DECLARE_bool(gpu_vulkan_float_constants_ssbo);
DECLARE_bool(spirv_debug_identity_ndc);
DECLARE_bool(spirv_pos_binning_passthrough);
DECLARE_bool(spirv_pos_collapse_only);
DECLARE_bool(gpu_force_tiny_draws);
DECLARE_bool(gpu_collapse_alphatest_coverage);
DECLARE_bool(gpu_collapse_blended_coverage);
DECLARE_bool(gpu_collapse_opaque_coverage);
DECLARE_int32(gpu_blended_thin_factor);
DECLARE_int32(gpu_foliage_thin_factor);
DECLARE_bool(gpu_force_no_color_write);
DECLARE_bool(gpu_opaque_depth_prepass);
DECLARE_bool(gpu_foliage_lrz_force_depth);
DECLARE_bool(gpu_foliage_lrz_feedback);
DECLARE_bool(vulkan_debug_texture_fetch_disable_exp_adjust);
DECLARE_bool(vulkan_cache_texture_descriptors);
DECLARE_bool(vulkan_push_descriptors);
DECLARE_bool(vulkan_coalesce_edram_transfers);
DECLARE_bool(vulkan_trace_draw_outcomes_per_frame);
DECLARE_bool(gpu_early_primary_read_pointer_writeback);
DECLARE_bool(gpu_bulk_pm4_type0);
DECLARE_bool(gpu_edram_passes_dont_care);
DECLARE_bool(gpu_skip_edram_transfers);
DECLARE_uint32(gpu_freeze_at_guest_ms);
DECLARE_int32(gpu_skip_draws_below_verts);
DECLARE_int32(gpu_merge_vf_index_stride_fix);
DECLARE_bool(vulkan_dynamic_state_cull_front);
DECLARE_bool(vulkan_dynamic_state_depth);
DECLARE_bool(vulkan_dynamic_state_stencil);
DECLARE_bool(vulkan_dynamic_state_topology);
DECLARE_bool(vulkan_merge_draws);
DECLARE_bool(vulkan_merge_draws_rewrite);
DECLARE_bool(vulkan_merge_draws_rewrite_strips);
DECLARE_bool(gpu_vulkan_weak_external_subpass_deps);
DECLARE_bool(vulkan_merge_draws_indirect);
DECLARE_bool(vulkan_trace_pass_timestamps);
DECLARE_bool(vulkan_hoist_request_range_lock);
DECLARE_bool(vulkan_cache_vertex_residency);
DECLARE_bool(vulkan_gate_rt_update);
DECLARE_bool(gpu_trace_cullable_tris);
DECLARE_bool(gpu_cull_compaction);
DECLARE_bool(vulkan_dynamic_constants_arena);
DECLARE_uint32(gpu_cull_max_per_frame);
DECLARE_bool(gpu_cull_replay_validate);
DECLARE_bool(gpu_cull_fast_replay);
DECLARE_bool(gpu_cull_fast_only);
DECLARE_bool(gpu_whole_draw_only);

DECLARE_bool(non_seamless_cube_map);

DECLARE_bool(half_pixel_offset);

DECLARE_int32(query_occlusion_fake_sample_count);

#define XE_GPU_FINE_GRAINED_DRAW_SCOPES 1

#endif  // XENIA_GPU_GPU_FLAGS_H_
