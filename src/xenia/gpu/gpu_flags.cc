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
DEFINE_int32(gpu_trace_packet_budget, 128,
             "Maximum packet-level GPU swap trace lines.", "GPU");
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
DEFINE_bool(vulkan_trace_shader_constants, false,
            "Trace active pixel shader constant maps and values around Vulkan "
            "draws. Research-only Android GPU bring-up probe.",
            "GPU");
DEFINE_int32(vulkan_trace_shader_constants_budget, 32,
             "Maximum active pixel shader constant trace groups.", "GPU");
DEFINE_bool(vulkan_trace_texture_source_checksum, false,
            "Read back active texture source ranges from Vulkan shared memory "
            "and log sparse checksums. Research-only and very slow.",
            "GPU");
DEFINE_int32(vulkan_trace_texture_source_checksum_budget, 24,
             "Maximum active texture source checksum readbacks.", "GPU");
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
DEFINE_bool(
    vulkan_force_signed_2101010_unorm_fallback, false,
    "Research-only Android/Adreno probe: when signed A2B10G10R10 texture "
    "sampling is unavailable, load signed 2_10_10_10 textures through the "
    "unsigned UNORM host format. This is color-incorrect and only for "
    "black-screen triage.",
    "GPU");
DEFINE_bool(gpu_early_primary_read_pointer_writeback, false,
            "Experimental Android bring-up: update the primary ring read "
            "pointer before executing long indirect buffers, matching hardware "
            "read-pointer progress more closely for guest watchdogs.",
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
