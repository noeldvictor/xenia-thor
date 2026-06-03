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
DEFINE_bool(spirv_no_invariant_position_output, false,
            "Do not decorate the vertex-shader gl_Position / interpolator / point "
            "outputs with Invariant. If the Mesa Turnip ir3 backend mishandles an "
            "Invariant-decorated vertex position, geometry comes out wrong/"
            "degenerate and nothing rasterizes (black). Default off keeps the "
            "proprietary path bit-identical.",
            "GPU");
DEFINE_bool(spirv_no_invariant_color_output, false,
            "Do not decorate the fragment-shader color output with Invariant. "
            "The Invariant+NoContraction scheme is only needed for the "
            "fragment-shader-interlock multipass EDRAM path (absent on Adreno "
            "740); on the host-render-target path the Mesa Turnip ir3 backend "
            "appears to drop the Invariant-decorated float color output so "
            "color never lands in an R16G16B16A16_SFLOAT (k_2_10_10_10_FLOAT) "
            "attachment. Default off keeps the proprietary path bit-identical.",
            "GPU");
DEFINE_bool(spirv_debug_force_fullscreen_position, false,
            "DIAGNOSTIC: ignore the computed vertex position and emit a "
            "fullscreen triangle keyed off gl_VertexIndex (every 3 consecutive "
            "verts form a screen-covering triangle). Splits 'the VS position is "
            "wrong' from 'everything downstream is wrong' on a driver that "
            "renders black (Turnip): if color appears, the bug is the VS position "
            "computation (guest VS / vertex-fetch / system-constant uniforms); if "
            "still black, the bug is downstream (clip/raster state / FS / "
            "resolve). Pair with a forced constant FS color for a both-ways "
            "conclusive result. Default off.",
            "GPU");
DEFINE_int32(spirv_debug_position_probe, 0,
             "DIAGNOSTIC (use with spirv_debug_force_fullscreen_position + a "
             "magenta FS): gate the forced fullscreen triangle on the SHAPE of "
             "the real computed vertex position, to pin a degenerate-gl_Position "
             "black screen. 0 = always fullscreen (downstream sanity). 1 = only "
             "if any position component is NaN (bad math / 0-over-0 W divide). "
             "2 = only if XYZ are all ~zero (vfetch/constant read returns 0 -> "
             "data/binding). 3 = only if any component is Inf (overflow / "
             "divide-by-zero). 4 = only if finite-and-nonzero (a GOOD position). "
             "5 = only if xenia's own system-constant uniform (ndc_scale, always "
             "nonzero for a real draw) reads nonzero - discriminates 'uniform "
             "buffers read zero' (black) from 'only vertex-fetch reads zero' "
             "(magenta). Whichever probe lights up (color in the RT dump) names "
             "the failure shape. Default 0.",
             "GPU");
DEFINE_bool(spirv_debug_identity_ndc, false,
            "DIAGNOSTIC: skip the guest->host NDC scale/offset transform in the "
            "vertex-shader epilogue (do not read the ndc_scale/ndc_offset system "
            "constants; pass the guest clip-space XYZ straight through). Bisects a "
            "degenerate-gl_Position black screen: if the color RTs go nonzero with "
            "this on, the guest VS produces FINITE positions and the bug is the "
            "NDC/system-constant epilogue (a uniform read); if still empty, the "
            "guest VS itself is degenerate (guest constants / vertex-fetch / ALU). "
            "Geometry is positionally wrong either way - only the nonzero-ness of "
            "the RT dump is meaningful. Default off.",
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
    gpu_skip_edram_transfers, false,
    "Thor/Adreno accuracy-for-speed: skip EDRAM ownership-transfer GPU work "
    "(the per-draw copies that move EDRAM range contents between host render "
    "targets when the guest aliases EDRAM). These transfers force render-pass "
    "breaks = tiler tile store/reload (the dominant GPU cost). Skipping them "
    "cuts tile traffic but does NOT preserve aliased EDRAM content, so it may "
    "cause rendering glitches. Resolve-clears are still performed. Diagnostic / "
    "performance-mode experiment; measure via gpu_frame_us + read the frame.",
    "GPU");

DEFINE_int32(
    gpu_skip_draws_below_verts, 0,
    "Thor/Adreno measurement+perf: skip emitting the GPU draw command for draws "
    "whose host vertex count is below this threshold (all CPU-side state setup "
    "still runs). 0 disables. Diagnostic: sizes how much GPU time the tiny draws "
    "themselves cost (Blue Dragon heavy scene is ~1200 tiny draws, GPU 99% busy). "
    "Also a crude accuracy-for-speed lever (drops tiny geometry). Measure via "
    "gpu_frame_us + read the frame.",
    "GPU");

DEFINE_int32(
    gpu_merge_vf_index_stride_fix, 0,
    "Thor/Adreno draw-merge MEASUREMENT-ONLY fix (no emission change, default 0 = "
    "bit-identical). Corrects the merge_vf_ index-contiguity classifier in the "
    "draw-outcomes trace: the default classifier compares guest_index_base (a BYTE "
    "address) against prev + host_draw_vertex_count (a VERTEX COUNT, unscaled by "
    "index stride), so it over-counts 'scattered' for guest-DMA index runs. When "
    ">0, scale by the index stride (kInt16=2, kInt32=4) and classify ONLY kGuestDMA "
    "sources (others have no guest index byte-range to compare). Sizes the true "
    "coalescable draw population for the concatenation lever. Read merge_vf_*/"
    "merge_run_hist_ via vulkan_trace_draw_outcomes_per_frame; zero gpu_frame_us "
    "effect by design.",
    "GPU");

DEFINE_bool(
    vulkan_dynamic_state_cull_front, false,
    "Thor/Adreno binning re-arch (Lever 1b, EDS): promote rasterizer cull mode + "
    "front face to Vulkan extended dynamic state (VK_DYNAMIC_STATE_CULL_MODE / "
    "FRONT_FACE, core in Vulkan 1.3) so draws differing only in cull/front-face "
    "(with the same resulting polygon mode) collapse onto ONE VkPipeline - fewer "
    "Adreno context rolls / pipeline rebinds and longer same-pipeline runs (the "
    "prerequisite for draw concatenation). No effect unless the device is Vulkan "
    "1.3+. Default off; STARTUP cvar - do not toggle live (mixes pipeline-cache "
    "keys). Default path is bit-identical when off.",
    "GPU");

DEFINE_bool(
    vulkan_dynamic_state_depth, false,
    "Thor/Adreno binning re-arch (Lever 1, EDS): promote depth test enable + "
    "depth write enable + depth compare op to Vulkan extended dynamic state "
    "(VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE/WRITE_ENABLE/COMPARE_OP, core in Vulkan "
    "1.3) so draws differing only in depth state collapse onto ONE VkPipeline - "
    "fewer Adreno context rolls and longer same-pipeline runs. No effect unless "
    "the device is Vulkan 1.3+ and the host-render-target path is used (not FSI). "
    "Default off; STARTUP cvar - do not toggle live. Bit-identical when off.",
    "GPU");

DEFINE_bool(
    vulkan_dynamic_state_stencil, false,
    "Thor/Adreno binning re-arch (Lever 1, EDS): promote stencil test enable + "
    "stencil ops (front/back fail/pass/depth-fail/compare) to Vulkan extended "
    "dynamic state (VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE/STENCIL_OP, core in "
    "Vulkan 1.3) so draws differing only in stencil state collapse onto ONE "
    "VkPipeline. No effect unless Vulkan 1.3+ and the host-render-target path "
    "(not FSI). Default off; STARTUP cvar - do not toggle live. Bit-identical "
    "when off. (Stencil compare/write masks + reference are already dynamic.)",
    "GPU");

DEFINE_bool(
    vulkan_dynamic_state_topology, false,
    "Thor/Adreno binning re-arch (Lever 1, EDS): promote primitive topology to "
    "Vulkan extended dynamic state (VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, core in "
    "Vulkan 1.3) for triangle LIST + STRIP draws (same triangle class - safe "
    "without dynamicPrimitiveTopologyUnrestricted) so list/strip variants of one "
    "shader+state collapse onto ONE VkPipeline. High value for Blue Dragon "
    "(~931 list + ~266 strip draws/frame). Only triangle list/strip are promoted "
    "(non-GS, triangle class); fan/line/point/rect/quad stay static. Also promotes "
    "primitive_restart_enable to dynamic for these draws (REQUIRED: the input-"
    "assembly path asserts restart==false for the kTriangleList key a normalized "
    "strip collapses to, so restart must be dynamic). No effect unless Vulkan "
    "1.3+. Default off; STARTUP cvar - do not toggle live. Bit-identical when off.",
    "GPU");

DEFINE_bool(
    gpu_trace_cullable_tris, false,
    "Thor/Adreno binning re-arch (Front B, READ-ONLY decision instrument): on the "
    "idle CPU, replay each draw's guest VS position transform (ShaderInterpreter) "
    "and COUNT how many triangles a CPU-side frustum/backface cull WOULD drop "
    "before the GPU bins them - logged as cullable_tris in the GPU draw outcomes/"
    "frame line (needs vulkan_trace_draw_outcomes_per_frame). Mutates NO geometry "
    "(provably rendering-neutral); produces the go/no-go number for whether an "
    "on-device CPU cull (held for the device A/B) is worth building. Default off. "
    "See docs/research/20260601-frontb-cpu-cull-design.md.",
    "GPU");

DEFINE_bool(
    vulkan_merge_draws, false,
    "Thor/Adreno binning re-arch (Lever 2): zero-copy DRAW CONCATENATION - merge "
    "consecutive same-state kGuestDMA indexed draws that index a contiguous byte "
    "run of the shared-memory buffer into ONE vkCmdBindIndexBuffer + ONE "
    "vkCmdDrawIndexed, paying the Adreno per-draw fixed cost once per run while "
    "binning each triangle exactly once (NOT instancing). LIST topologies only "
    "(strips would stitch across draw boundaries). Front A EDS dynamic-state "
    "promotion lengthens the same-pipeline runs this merges. Default off; STARTUP "
    "cvar - do not toggle live. Bit-identical when off. Built incrementally - see "
    "docs/research/20260601-draw-concatenation-design.md.",
    "GPU");

DEFINE_bool(
    vulkan_merge_draws_indirect, false,
    "Thor/Adreno binning re-arch (Lever 2b): batch consecutive same-state "
    "kGuestDMA indexed draws into ONE vkCmdDrawIndexedIndirect (drawCount=N) "
    "instead of N separate vkCmdDrawIndexed, paying the per-draw command/submit "
    "cost once per run. Unlike concatenation (vulkan_merge_draws), each indirect "
    "command is a SEPARATE draw, so this ALSO batches triangle STRIPS (no "
    "cross-draw primitive stitching) and NON-contiguous index ranges (per-command "
    "firstIndex) - exactly the Blue Dragon intro case (all strips, scattered). "
    "vertexOffset stays 0 (VGT_INDX_OFFSET via system constants), so a run still "
    "requires unchanged constants/state between draws and equal VGT_INDX_OFFSET; "
    "bit-identical to the per-draw path. Requires the multiDrawIndirect device "
    "feature (auto-disabled if absent). Mutually exclusive with vulkan_merge_draws "
    "(that wins if both set). Default off; STARTUP cvar - do not toggle live.",
    "GPU");

DEFINE_bool(
    gpu_cull_compaction, false,
    "Thor/Adreno binning re-arch (Lever 2 cull, STEP 1 plumbing): route each "
    "kGuestDMA indexed draw through a transient index buffer - copy the raw guest "
    "index bytes VERBATIM into a per-frame upload buffer and draw from it instead "
    "of binding the shared-memory buffer directly. This is the structural pipe the "
    "CPU/NEON triangle cull reuses (it will later read+cull+compact those indices); "
    "with no cull yet it is provably BYTE-IDENTICAL to off. No endian swap / mask / "
    "base-add (the vertex shader does those); vertexOffset stays 0. Live-toggleable "
    "(the pool is created at init). Default off. See "
    "docs/research/20260602-neon-cull-impl-plan.md.",
    "GPU");

DEFINE_bool(
    gpu_cull_fast_replay, true,
    "Thor cull Step 2b: when culling (gpu_cull_compaction), compute each vertex's "
    "clip position via the fast AFFINE MATRIX-RECOVERY replay (recover the 4x4 M "
    "from a few ShaderInterpreter basis vertices, then per vertex decode the "
    "position attribute + 4 dp4s) instead of running the full per-vertex "
    "ShaderInterpreter - ~orders of magnitude cheaper, and exact since position is "
    "affine. Falls back to the interpreter when the position input isn't a single "
    "k_32_32_32_FLOAT vfetch or M can't be recovered. Set false to force the "
    "interpreter path (correctness reference). Default on.",
    "GPU");

DEFINE_bool(
    gpu_cull_fast_only, true,
    "Thor cull Step 2b-iii: cull a draw ONLY when the fast affine matrix-recovery "
    "replay is available; if SetupFastAffineReplay fails (no single vfetch position "
    "leaf, multi-input/skinned position, or M not recoverable) draw the triangles "
    "VERBATIM instead of falling back to the per-vertex ShaderInterpreter. The "
    "interpreter fallback costs ~9.5us/vert - on Blue Dragon ~1s/frame, dwarfing the "
    "GPU binning it saves and making the cull a net fps LOSS. Fast-only bounds the "
    "cull's CPU cost to the cheap fast-decode draws. Set false to restore the "
    "interpreter fallback (the GPU-frame-time proof reference). Default on. Only "
    "meaningful with gpu_cull_compaction=true.",
    "GPU");

DEFINE_bool(
    gpu_whole_draw_only, false,
    "Thor cull Step 2c: cull ONLY whole draws, never per-triangle. When a draw's "
    "fast-affine clip positions are ALL outside one frustum plane (behind the "
    "camera, or all beyond one X/Y clip plane) the entire draw is skipped (the GPU "
    "never bins it); otherwise it is drawn VERBATIM as its original triangle strip "
    "- NO strip->list conversion. The strip->list conversion of the per-triangle "
    "cull triples the survivor index count and ~3x the binning vertex-invocations, "
    "which on strip-dominated Blue Dragon costs MORE than the per-triangle saving "
    "(content-matched A/B: per-tri cull was +6% gpu_frame_us). Whole-draw-only "
    "captures the off-screen-draw saving without that overhead. Only meaningful "
    "with gpu_cull_compaction=true. Default off.",
    "GPU");

DEFINE_bool(
    gpu_cull_replay_validate, false,
    "Thor cull Step 2b validation (read-only): for qualifying draws, recover the "
    "affine position matrix M from a few ShaderInterpreter basis vertices (clip = "
    "M*[input,1], the input read from the position slice's leaf register) and check "
    "M against held-out vertices' interpreter clip positions. Logs leaf/support and "
    "the max relative error in the cull line (replay[...]) to confirm the fast "
    "matrix-recovery replay reproduces the interpreter before it displaces it. "
    "Default off.",
    "GPU");

DEFINE_uint32(
    gpu_cull_max_per_frame, 0,
    "Thor cull VALIDATION throttle: cap how many draws per frame run the slow "
    "interpreter-based gpu_cull_compaction triangle cull; the rest draw verbatim. "
    "0 = unlimited. Lets the heavy scene still render at an interactive-ish rate so "
    "the cull's visual correctness (no holes) can be checked before the NEON "
    "micro-interpreter (Step 2b) makes the full cull cheap. Only meaningful with "
    "gpu_cull_compaction=true.",
    "GPU");

DEFINE_uint32(
    gpu_freeze_at_guest_ms, 0,
    "Thor measurement harness: once the guest uptime reaches this many "
    "milliseconds, near-freeze the guest clock (time_scalar->0.0001) so the "
    "rendered scene becomes static and identical across configs (guest content "
    "is a function of guest uptime). Enables confound-free gpu_frame_us A/B on "
    "the same frame. 0 disables.",
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
