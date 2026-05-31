/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gpu_flags.h"

DEFINE_path(trace_gpu_prefix, "scratch/gpu/",
            "Prefix path for GPU trace files.", "GPU");
DEFINE_bool(trace_gpu_stream, false, "Trace all GPU packets.", "GPU");

DEFINE_path(
    dump_shaders, "",
    "For shader debugging, path to dump GPU shaders to as they are compiled.",
    "GPU");

DEFINE_bool(vsync, true, "Enable VSYNC.", "GPU");

DEFINE_bool(
    gpu_allow_invalid_fetch_constants, false,
    "Allow texture and vertex fetch constants with invalid type - generally "
    "unsafe because the constant may contain completely invalid values, but "
    "may be used to bypass fetch constant type errors in certain games until "
    "the real reason why they're invalid is found.",
    "GPU");

DEFINE_bool(clear_memory_page_state, false,
            "Refresh valid GPU-written shared-memory page state on frame close.",
            "GPU");

DEFINE_bool(gpu_interrupt_on_ring_idle, false,
            "Experimental Android bring-up hack: dispatch a graphics interrupt "
            "after the primary ring drains.",
            "GPU");
DEFINE_bool(gpu_interrupt_on_swap, false,
            "Experimental Android bring-up hack: dispatch a source-1 graphics "
            "interrupt after PM4_XE_SWAP completes. Default-off Blue Dragon "
            "watchdog probe.",
            "GPU");
DEFINE_bool(gpu_trace_interrupts, false,
            "Trace GPU interrupt callback setup, PM4_INTERRUPT packets, "
            "VBlank interrupts, and source-1 dispatch chronology. "
            "Research-only Android watchdog probe.",
            "GPU");
DEFINE_int32(gpu_trace_interrupts_budget, 256,
             "Maximum GPU interrupt trace lines; negative means unlimited.",
             "GPU");
DEFINE_bool(gpu_blue_dragon_kick_wait_token, false,
            "Experimental Blue Dragon Android bring-up hack: increment the "
            "graphics wait token when a GPU interrupt is dispatched.",
            "GPU");
DEFINE_int32(gpu_blue_dragon_kick_wait_token_budget, 16,
             "Maximum Blue Dragon wait-token kick log lines.", "GPU");
DEFINE_bool(gpu_trace_swap, false,
            "Trace guest video swap setup, PM4 XE_SWAP packets, and host "
            "present/swap paths.",
            "GPU");
DEFINE_bool(gpu_trace_vd_swap, false,
            "Trace VdSwap enter/exit and compact presenter/GPU state snapshots "
            "for black-screen triage.",
            "GPU");
DEFINE_int32(gpu_trace_packet_budget, 128,
             "Maximum packet-level GPU swap trace lines.", "GPU");
DEFINE_bool(gpu_use_vd_scaler_output_for_swap, false,
            "Use the most recent VdInitializeScalerCommandBuffer output size "
            "as the guest output size for the next VdSwap when it matches the "
            "frontbuffer. Compatibility probe for games relying on the Xbox "
            "video scaler.",
            "GPU");
DEFINE_bool(gpu_trace_texture_cache_actions, false,
            "Trace texture cache create/load actions. Very noisy in games.",
            "GPU");
DEFINE_int32(gpu_unknown_register_log_budget, 128,
             "Maximum unknown GPU register read/write log lines per source; "
             "negative means unlimited, zero suppresses them.",
             "GPU");
DEFINE_bool(gpu_trace_swap_frontbuffer_checksum, false,
            "Trace a sparse CPU-side checksum of the physical frontbuffer "
            "passed to XE_SWAP. Research-only black-screen probe.",
            "GPU");
DEFINE_int32(gpu_trace_swap_frontbuffer_checksum_budget, 24,
             "Maximum frontbuffer checksum trace lines.", "GPU");
DEFINE_bool(gpu_trace_swap_render_targets, false,
            "Trace active guest render-target registers at PM4_XE_SWAP. "
            "Research-only black-screen probe.",
            "GPU");
DEFINE_int32(gpu_trace_swap_render_targets_budget, 24,
             "Maximum active render-target swap trace groups.", "GPU");
DEFINE_bool(vulkan_trace_resolve, false,
            "Trace Vulkan render target resolve/copy destinations. "
            "Research-only Android black-screen probe.",
            "GPU");
DEFINE_int32(vulkan_trace_resolve_budget, 128,
             "Maximum Vulkan resolve destination trace lines.", "GPU");
DEFINE_bool(vulkan_trace_resolve_checksum, false,
            "Read back Vulkan resolve destinations and log a sparse checksum. "
            "Research-only and very slow.",
            "GPU");
DEFINE_int32(vulkan_trace_resolve_checksum_budget, 24,
             "Maximum Vulkan resolve readback checksum trace lines.", "GPU");
DEFINE_bool(vulkan_trace_edram_checksum, false,
            "Read back the Vulkan EDRAM dump range before shared-memory "
            "resolve and log a sparse checksum. Research-only and very slow.",
            "GPU");
DEFINE_int32(vulkan_trace_edram_checksum_budget, 12,
             "Maximum Vulkan EDRAM readback checksum trace lines.", "GPU");
DEFINE_bool(vulkan_readback_resolve, false,
            "Read Vulkan render-to-texture resolve results back into CPU "
            "guest memory. Very slow, for bring-up parity with D3D12 only.",
            "GPU");
DEFINE_bool(vulkan_trace_copy_state, false,
            "Trace Vulkan copy-mode register state before render target "
            "resolve. Research-only Android black-screen probe.",
            "GPU");
DEFINE_int32(vulkan_trace_copy_state_budget, 64,
             "Maximum Vulkan copy-mode state trace lines.", "GPU");
DEFINE_bool(vulkan_trace_draw_state, false,
            "Trace Vulkan draw shader, render-target, and color-mask state for "
            "Android GPU bring-up.",
            "GPU");
DEFINE_int32(vulkan_trace_draw_state_budget, 128,
             "Maximum Vulkan draw-state trace lines.", "GPU");
DEFINE_string(vulkan_trace_draw_shader_filter, "",
              "Comma-separated vertex or pixel shader ucode hashes to include "
              "in vulkan_trace_draw_state; empty traces all.",
              "GPU");
DEFINE_bool(vulkan_trace_pipeline_creation, false,
            "Trace Vulkan graphics pipeline creation. Noisy during bring-up.",
            "GPU");
DEFINE_bool(vulkan_trace_shader_constants, false,
            "Trace active pixel shader constant maps and values around Vulkan "
            "draws. Research-only Android GPU bring-up probe.",
            "GPU");
DEFINE_int32(vulkan_trace_shader_constants_budget, 32,
             "Maximum active pixel shader constant trace groups.", "GPU");
DEFINE_string(vulkan_trace_shader_constants_shader_filter, "",
              "Comma-separated shader ucode hashes to include in "
              "vulkan_trace_shader_constants; empty traces all.",
              "GPU");
DEFINE_bool(vulkan_trace_texture_source_checksum, false,
            "Read back active texture source ranges from Vulkan shared memory "
            "and log sparse checksums. Research-only and very slow.",
            "GPU");
DEFINE_int32(vulkan_trace_texture_source_checksum_budget, 24,
             "Maximum active texture source checksum readbacks.", "GPU");
DEFINE_string(vulkan_trace_texture_source_shader_filter, "",
              "Comma-separated shader ucode hashes to include in "
              "vulkan_trace_texture_source_checksum; empty traces all.",
              "GPU");
DEFINE_bool(vulkan_trace_vertex_fetch_checksum, false,
            "Trace active vertex fetch buffers and sparse CPU-side checksums. "
            "Research-only Android black-screen probe.",
            "GPU");
DEFINE_int32(vulkan_trace_vertex_fetch_checksum_budget, 24,
             "Maximum active vertex fetch trace groups.", "GPU");
DEFINE_string(vulkan_trace_vertex_fetch_shader_filter, "",
              "Comma-separated vertex shader ucode hashes to include in "
              "vulkan_trace_vertex_fetch_checksum; empty traces all.",
              "GPU");
DEFINE_bool(vulkan_trace_swap_shared_memory_checksum, false,
            "Read back the Vulkan shared-memory frontbuffer used by swap and "
            "log a sparse checksum. Research-only and very slow.",
            "GPU");
DEFINE_int32(vulkan_trace_swap_shared_memory_checksum_budget, 12,
             "Maximum Vulkan swap shared-memory checksum trace lines.", "GPU");
DEFINE_bool(vulkan_present_recent_resolve_on_swap, false,
            "Present the most recent full-size Vulkan resolve instead of the "
            "VdSwap frontbuffer. Research-only Blue Dragon black-screen "
            "probe; incorrect for general compatibility.",
            "GPU");
DEFINE_bool(vulkan_present_scored_resolve_on_swap, false,
            "Present the latest useful scored Vulkan resolve candidate instead "
            "of the VdSwap frontbuffer. Uses slow readback stats to avoid "
            "clear or black candidates. Research-only Android black-screen "
            "probe.",
            "GPU");
DEFINE_int32(vulkan_present_scored_resolve_min_width, 320,
             "Minimum resolve width considered by the scored-present probe.",
             "GPU");
DEFINE_int32(vulkan_present_scored_resolve_min_height, 180,
             "Minimum resolve height considered by the scored-present probe.",
             "GPU");
DEFINE_int32(vulkan_present_scored_resolve_budget, 64,
             "Maximum resolve readbacks for the scored-present probe; "
             "negative means unlimited.",
             "GPU");
DEFINE_int32(vulkan_present_scored_resolve_required_format, -1,
             "Optional Xenos texture format enum required by the "
             "scored-present probe; negative accepts all supported formats.",
             "GPU");
DEFINE_bool(vulkan_present_scored_resolve_reject_clear_like, false,
            "Reject near-uniform sampled resolve candidates in the "
            "scored-present probe. Research-only Blue Dragon source-content "
            "triage knob.",
            "GPU");
DEFINE_bool(vulkan_present_forced_resolve_on_swap, false,
            "Present a manually specified Vulkan resolve source instead of "
            "the VdSwap frontbuffer. Research-only Android black-screen "
            "probe; incorrect for general compatibility.",
            "GPU");
DEFINE_uint32(vulkan_present_forced_resolve_address, 0,
              "Guest physical address to force-present when "
              "vulkan_present_forced_resolve_on_swap is enabled.",
              "GPU");
DEFINE_uint32(vulkan_present_forced_resolve_length, 0,
              "Optional byte length of the forced present resolve source.",
              "GPU");
DEFINE_uint32(vulkan_present_forced_resolve_width, 0,
              "Width of the forced present resolve source.", "GPU");
DEFINE_uint32(vulkan_present_forced_resolve_height, 0,
              "Height of the forced present resolve source.", "GPU");
DEFINE_uint32(vulkan_present_forced_resolve_pitch, 0,
              "Pitch in pixels of the forced present resolve source.", "GPU");
DEFINE_uint32(vulkan_present_forced_resolve_format, 0,
              "Xenos texture format enum of the forced present resolve "
              "source.",
              "GPU");
DEFINE_bool(vulkan_debug_solid_guest_output, false,
            "Fill the Vulkan guest output image with a solid debug color on "
            "swap. Research-only presenter proof; incorrect for gameplay.",
            "GPU");
DEFINE_string(vulkan_debug_pixel_shader_output_filter, "",
              "Comma-separated pixel shader ucode hashes whose color exports "
              "may be overridden by vulkan_debug_pixel_shader_output_mode. "
              "Research-only shader-output triage.",
              "GPU");
DEFINE_int32(vulkan_debug_pixel_shader_output_mode, 0,
             "Research-only shader-output override for hashes in "
             "vulkan_debug_pixel_shader_output_filter: 0 disables, 1 forces "
             "magenta RGBA, 2 keeps RGB and forces alpha to 1, 10 exports "
             "the last post-processed texture fetch observed by the shader, "
             "11-14 smear individual XYZW fetch components, 15 exports a "
             "fetch nonzero predicate, 20 exports final fetch coordinates, 30 "
             "exports raw unsigned sample, 31 exports raw signed sample.",
             "GPU");
DEFINE_string(
    vulkan_debug_pixel_shader_output_secondary_filter, "",
    "Second comma-separated pixel shader ucode hash filter for "
    "vulkan_debug_pixel_shader_output_secondary_mode. Lets one shader be "
    "forced while another shader is probed in the same research run.",
    "GPU");
DEFINE_int32(vulkan_debug_pixel_shader_output_secondary_mode, 0,
             "Research-only secondary shader-output override: same modes as "
             "vulkan_debug_pixel_shader_output_mode.",
             "GPU");
DEFINE_bool(vulkan_force_2101010_rgba8_fallback, false,
            "Research-only Android/Adreno probe: load 2_10_10_10 textures as "
            "raw R8G8B8A8 host textures. This is color-incorrect and only for "
            "isolating A2B10G10R10 sampling/upload failures.",
            "GPU");
DEFINE_bool(
    vulkan_force_signed_2101010_unorm_fallback, false,
    "Research-only Android/Adreno probe: when signed A2B10G10R10 texture "
    "sampling is unavailable, load signed 2_10_10_10 textures through the "
    "unsigned UNORM host format. This is color-incorrect and only for "
    "black-screen triage.",
    "GPU");
DEFINE_bool(vulkan_debug_texture_fetch_disable_exp_adjust, false,
            "Research-only shader probe: force texture fetch result exponent "
            "adjustment to 1.0. This is incorrect for gameplay and isolates "
            "2_10_10_10_FLOAT resolve/fetch fallback darkening.",
            "GPU");
DEFINE_bool(
    vulkan_cache_texture_descriptors, true,
    "Reuse the texture/sampler descriptor sets across consecutive draws when "
    "the bound image views and samplers have not changed, instead of "
    "allocating and writing a fresh transient descriptor set every draw. Big "
    "CPU win on mobile drivers (Adreno) where vkUpdateDescriptorSets per draw "
    "dominates the GPU command thread. Set false to restore the per-draw "
    "rewrite (e.g. to isolate a descriptor-caching regression).",
    "GPU");
DEFINE_bool(
    vulkan_coalesce_edram_transfers, false,
    "EXPERIMENTAL (default off, EDRAM core): coalesce EDRAM render-target ownership "
    "transfers so format-compatible transfers share one render pass instead of one "
    "per destination RT, to cut Adreno tile flushes (render-pass begin/end). High "
    "risk - validate rendering per title before enabling. Off = the proven "
    "per-dest-RT transfer path.",
    "GPU");
DEFINE_bool(
    vulkan_push_descriptors, true,
    "Use VK_KHR_push_descriptor (when supported) to push the per-draw texture and "
    "sampler descriptors inline into the command buffer, instead of allocating a "
    "transient descriptor set, writing it with vkUpdateDescriptorSets, and binding "
    "it every draw. Large per-draw CPU win on mobile drivers (Adreno) where "
    "descriptor binds dominate the GPU command thread. Set false to use the "
    "transient-descriptor-set path (e.g. to isolate a push-descriptor regression).",
    "GPU");
DEFINE_bool(
    vulkan_trace_draw_outcomes_per_frame, false,
    "Diagnostic: at each guest swap, log a one-line summary of the draw "
    "outcomes accumulated during the frame - how many IssueDraw calls actually "
    "rendered vs were skipped (no vertex shader, no rasterization/no memexport, "
    "copy), plus indirect-buffer count. Budget-free and reset per frame, unlike "
    "vulkan_trace_draw_state. Used to find whether real geometry draws are being "
    "silently dropped (black 3D) and how much per-frame work has no visible "
    "effect (slowness).",
    "GPU");
DEFINE_bool(gpu_early_primary_read_pointer_writeback, false,
            "Experimental Android bring-up: update the primary ring read "
            "pointer before executing long indirect buffers, matching hardware "
            "read-pointer progress more closely for guest watchdogs.",
            "GPU");

DEFINE_bool(
    gpu_bulk_pm4_type0, true,
    "Bulk-process contiguous PM4 type-0 register-write runs: byte-swap the "
    "whole dword run at once (NEON-vectorized on ARM64) instead of one "
    "ReadAndSwap per dword, then still call WriteRegister per register so all "
    "per-register side effects are preserved. Reduces per-dword overhead on "
    "the command-processor thread for draw-heavy guests. Disable to A/B test.",
    "GPU");

DEFINE_bool(
    gpu_edram_passes_dont_care, false,
    "Thor/Adreno DIAGNOSTIC: force EDRAM render-pass attachments to "
    "loadOp/storeOp=DONT_CARE instead of LOAD/STORE. This skips the per-pass "
    "full-framebuffer tile reload+store on the tiler. EXPECTED to corrupt "
    "rendering (contents not preserved across pass breaks) - used only to "
    "measure how much of the GPU frame time is tile load/store traffic via "
    "gpu_frame_us. NOT a shippable fix; the real fix is reducing pass breaks.",
    "GPU");

DEFINE_bool(
    non_seamless_cube_map, true,
    "Disable filtering between cube map faces near edges where possible "
    "(Vulkan with VK_EXT_non_seamless_cube_map) to reproduce the Direct3D 9 "
    "behavior.",
    "GPU");

// Extremely bright screen borders in 4D5307E6.
// Reading between texels with half-pixel offset in 58410954.
DEFINE_bool(
    half_pixel_offset, true,
    "Enable support of vertex half-pixel offset (D3D9 PA_SU_VTX_CNTL "
    "PIX_CENTER). Generally games are aware of the half-pixel offset, and "
    "having this enabled is the correct behavior (disabling this may "
    "significantly break post-processing in some games), but in certain games "
    "it might have been ignored, resulting in slight blurriness of UI "
    "textures, for instance, when they are read between texels rather than "
    "at texel centers, or the leftmost/topmost pixels may not be fully covered "
    "when MSAA is used with fullscreen passes.",
    "GPU");

DEFINE_int32(query_occlusion_fake_sample_count, 1000,
             "If set to -1 no sample counts are written, games may hang. Else, "
             "the sample count of every tile will be incremented on every "
             "EVENT_WRITE_ZPD by this number. Setting this to 0 means "
             "everything is reported as occluded.",
             "GPU");
