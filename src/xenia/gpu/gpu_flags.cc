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
    vsync_on_swap, false,
    "EXPERIMENTAL (Thor): fire the guest vblank interrupt immediately when a "
    "slower-than-60fps title issues a swap, instead of making its frame wait "
    "out the remainder of the fixed 16.7ms vblank tick. Measured (B86i/j): "
    "every title's frame interval is quantized to exact vblank multiples "
    "(Burnout 4 ticks, Blue Dragon 8, Gears 2); this removes the rounding "
    "for sub-60fps frames while at-60fps content keeps the timer cadence.",
    "GPU");

DEFINE_uint32(
    gpu_frame_limit_fps, 60,
    "Host-side frame-rate limiter: cap the guest swap (present) rate to this "
    "many frames per second by pacing the command-processor swap handler "
    "(ring-buffer backpressure then paces the guest). 0 = disabled (prior "
    "behavior). Tames light/loading/menu screens that otherwise render hundreds "
    "of fps and peg the GPU at ~99%, overheating the handheld (device-observed: "
    "Lost Odyssey loading screen ~943fps -> 72.5C); real <=cap gameplay is "
    "unaffected. Note: this caps real frames/sec, NOT guest time (time_scalar).",
    "GPU");

DEFINE_bool(
    gpu_adpf_performance_hints, true,
    "Android only (ADPF Performance Hint API): report the GPU command-processor "
    "(frame-critical) thread's per-frame work duration vs the target frame "
    "budget to the OS so the scheduler boosts the right CPU core when frames "
    "run long and relaxes it (saving power/heat) when there is slack. Pure "
    "scheduling hint - does not change rendering. No-op off Android or on ROMs "
    "without the API. Default ON (device-validated functional + safe 2026-06-07: "
    "session created, 130s stable on Blue Dragon, rendering unaffected; benefit "
    "is largest on CPU-bound/loading scenes).",
    "GPU");

DEFINE_bool(
    gpu_adpf_thermal_throttle, false,
    "Android only (ADPF Thermal API): read thermal headroom each frame and "
    "pre-emptively cap the present rate (45/30/20 fps as headroom approaches "
    "the throttling threshold) so the fanless handheld sheds heat before the OS "
    "throttles hard. Composes with gpu_frame_limit_fps (takes the lower cap). "
    "No-op off Android or on ROMs without the API. Default off.",
    "GPU");

DEFINE_bool(
    gpu_vulkan_edram_roaa, false,
    "Track #6 (EXPERIMENTAL, in progress): use the rasterization-order "
    "attachment-access EDRAM render path that keeps EDRAM resident in tile "
    "memory and eliminates the EDRAM ownership-transfer copies (device-measured "
    "~9ms / ~22% of the BTTF GPU frame). Requires "
    "VK_EXT_rasterization_order_attachment_access (present on Turnip Adreno "
    "740). Falls back to the default host-render-target (transfer) path when "
    "off or unsupported. Default off until the path is complete + validated.",
    "GPU");

DEFINE_int32(
    gpu_vulkan_inpass_edram_transfers, 0,
    "EXPERIMENTAL (Thor/TBDR): perform EDRAM ownership-transfer draws INSIDE "
    "the guest render pass instead of dedicated single-attachment transfer "
    "passes, cutting render-pass breaks + GMEM tile flush/reload on tilers "
    "(completes the upstream pass-reuse TODO). 0 = off (default, legacy "
    "transfer passes). 1 = color destinations only (excludes "
    "integer-reinterpret formats like float16, which keep the legacy pass for "
    "NaN bit-exactness). 2 = color + depth destinations (depth requires "
    "VK_EXT_shader_stencil_export; self-gates off it). Transfers whose source "
    "is bound as a current framebuffer attachment (feedback) always use the "
    "legacy path. No new extensions required.",
    "GPU");

DEFINE_bool(
    gpu_edram_passes_dont_care_safe, false,
    "EXPERIMENTAL (Thor/TBDR): when a render pass's FIRST draw provably "
    "overwrites the entire render area unconditionally (a one-rectangle "
    "rectangle-list draw - the guest clear idiom - with always-pass depth "
    "write or replace-mode full-mask color write, verified by replaying its "
    "vertex positions on the CPU), begin the pass with "
    "VK_ATTACHMENT_LOAD_OP_DONT_CARE for the proven attachments, skipping "
    "their GMEM tile loads on tile-based GPUs. Per-pass and per-attachment - "
    "unlike the raw gpu_edram_passes_dont_care diagnostic (which elides all "
    "loads AND stores and corrupts titles that need the contents). Any "
    "uncertainty falls back to loading.",
    "GPU");

DEFINE_bool(
    gpu_binning_deinterleave_pos, false,
    "EXPERIMENTAL (Thor/TBDR): de-interleave the position vertex stream into a "
    "compact raw-dword buffer so the tiler's binning pass fetches only "
    "popcount(needed_words)*4 bytes per vertex instead of the full interleaved "
    "guest stride (the measured per-vertex binning drain). Eligible vertex "
    "shaders (single statically-provable position vfetch) gain a uniform "
    "runtime branch; the CPU gathers the needed dwords verbatim, so the "
    "shader's unchanged endian-swap/unpack path stays bit-exact. Any "
    "ineligibility or gather bail falls back to the verbatim draw. Changes the "
    "shared-memory descriptor set layout, so toggling invalidates cached "
    "pipelines once.",
    "GPU");

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
DEFINE_bool(gpu_log_interrupt_counts, false,
            "Non-fatal interrupt-activity probe: log cumulative source-0/source-1 "
            "(PM4_INTERRUPT/CP-driven) interrupt-dispatch counts ~once/sec from "
            "the vblank, without the per-event latency of gpu_trace_interrupts "
            "(which is fatal to Lost Odyssey's interrupt-timing-sensitive init).",
            "GPU");
// Lost Odyssey render-gate watch. Device-free RE localized LO's black-screen to a
// render-thread latch: the per-frame render fn (guest 0x82821110) skips both the
// scene-draw build and present while global latch 0x832631a8 == 0; that latch is
// set by the render-worker (0x828204a8) only while run-flag 0x832631b8 != 0; both
// read 0 at the stall. This probe samples those globals every vblank and logs only
// on a transition, from boot, so a capture pinpoints WHEN (and at what guest_ms) LO
// disables rendering -> correlate with the disable caller 0x827c98a8. Default-off.
DEFINE_bool(gpu_watch_lo_render_gate, false,
            "Log Lost Odyssey render-gate global transitions (latch 0x832631a8 / "
            "run-flag 0x832631b8) every vblank, from boot, to localize when LO "
            "stops issuing draws. Inert for other titles.",
            "GPU");
// Bold forward experiment (default-off): Lost Odyssey renders ~54 frames then
// DISABLES its render thread (latch 0x832631a8 + run-flag 0x832631b8 -> 0) ->
// black screen. This cvar, AFTER LO has disabled rendering (seen latch 1->0),
// forces both globals back to 1 every vblank so LO's per-frame render fn keeps
// building+presenting draws. Tests whether the frame-54 disable is THE gate (LO
// shows content) or whether the scene is simply not produced (stays black). Only
// engages post-disable so it does not perturb LO's init.
DEFINE_bool(gpu_force_lo_render_latch, false,
            "Lost Odyssey: after LO disables its render thread (~frame 54), force "
            "the render latch 0x832631a8 + run-flag 0x832631b8 back to 1 each "
            "vblank to keep rendering active. Experiment; inert for other titles.",
            "GPU");
// One-shot guest-memory dump (RE enabler: e.g. scene-diff Burnout's traffic-car
// count to author the traffic-density patch, or inspect a stuck game's state).
// When dump_guest_mem_at_ms > 0, at the first vblank where guest uptime >= it,
// dump dump_guest_mem_size_mb MB of guest virtual memory starting at guest addr
// dump_guest_mem_base to dump_guest_mem_path. Default-off / inert.
DEFINE_int32(dump_guest_mem_at_ms, 0,
             "Guest-uptime ms at which to one-shot dump guest memory (0=off).",
             "GPU");
DEFINE_uint32(dump_guest_mem_base, 0x82000000,
              "Guest virtual address to start the guest-memory dump from.", "GPU");
DEFINE_int32(dump_guest_mem_size_mb, 64,
             "Size in MB of the guest-memory dump.", "GPU");
DEFINE_string(dump_guest_mem_path, "/data/local/tmp/guestmem.bin",
              "Output path for the guest-memory dump.", "GPU");
DEFINE_bool(gpu_blue_dragon_kick_wait_token, false,
            "Experimental Blue Dragon Android bring-up hack: increment the "
            "graphics wait token when a GPU interrupt is dispatched.",
            "GPU");
DEFINE_int32(gpu_blue_dragon_kick_wait_token_budget, 16,
             "Maximum Blue Dragon wait-token kick log lines.", "GPU");
DEFINE_int32(
    gpu_vrs_foliage_rate, 0,
    "Thor novel-hardware speed lever (VK_KHR_fragment_shading_rate, confirmed "
    "present on both Thor drivers): apply a coarse NxN fragment shading rate to "
    "alpha-test (foliage) draws so the fragment shader + alpha-test discard run "
    "once per NxN block instead of per covered sample - up to N*N fewer "
    "alpha-test invocations on the overdraw-heavy BD foliage. 0=off (1x1, normal); "
    "2=2x2 (~4x fewer); 4=4x4. Quality-toggle (coarsens foliage edges). Gating "
    "the extension REQUEST behind this cvar keeps the default device-creation "
    "path byte-identical when off. Default off, validate per-title.",
    "GPU");
DEFINE_uint32(
    gpu_vrs_enable_after_guest_ms, 0,
    "Measurement helper for gpu_vrs_foliage_rate: when >0, VRS coarse-shading is "
    "SUPPRESSED until guest uptime reaches this many ms, then applied. Lets a "
    "title boot + auto-navigate VRS-OFF (so the wall-clock nav pacing matches the "
    "VRS-off baseline and reaches the SAME scene), then turn VRS ON only in the "
    "target scene - giving a matched-scene A/B that VRS's frame-pacing change would "
    "otherwise break (e.g. Blue Dragon can't reach its field with VRS on). 0 = no "
    "gate (VRS active from the start). Diagnostic; default 0.",
    "GPU");
DEFINE_bool(
    gpu_freeze_ab_alternate_vrs, false,
    "Single-run VRS A/B validator. ALTERNATE gpu_vrs_foliage_rate ON/OFF in "
    "16-frame blocks within the SAME run and emit a 'VRS_AB: phase=...' line per "
    "frame; bucket gpu_frame_us by phase (median of each block's middle frames, "
    "discard the first ~3 GPU-latency transition frames). Two modes: (a) FROZEN - "
    "with gpu_freeze_at_guest_ms, the scene is re-rendered identically every frame "
    "so the A/B is byte-identical (cleanest); (b) FREE-RUNNING - with "
    "gpu_vrs_enable_after_guest_ms>0 and no freeze, alternation starts the instant "
    "guest uptime crosses the gate, so it begins the moment the target scene "
    "renders (robust to variable scene-reach wall-time + the thermal watchdog; over "
    "a near-static scene the tight blocks make residual drift affect both phases "
    "about equally). Sidesteps BD boot non-determinism AND the save-state path "
    "(no two-run scene matching). Requires gpu_vrs_foliage_rate>0 (the ON rate). "
    "Default off.",
    "GPU");
DEFINE_int32(
    thor_gpu_thread_affinity_cpu, -1,
    "AYN Thor perf (roadmap R1): pin the 'GPU Commands' command-processor worker "
    "thread to this CPU core index (-1 = off / let the scheduler float it). The "
    "~197ms/frame critical path is single-thread-bound; the Thor's prime Cortex-X3 "
    "is cpu7 @3.19GHz vs A510 @2.0GHz, so pinning the hot thread to 7 holds it at "
    "max clock and avoids cross-cluster migration. Hint only (no guest-visible "
    "effect). Set 7 to test the X3 on this device.",
    "GPU");
DEFINE_int32(
    gpu_cp_worker_nice, 0,
    "AYN Thor perf: lower the 'GPU Commands' command-processor worker thread's "
    "nice value (Android setpriority) so the OS does not deschedule it under "
    "guest-thread CPU contention - the GPU then stays fed instead of idling on "
    "command submission (BTTF DeLorean measured ~16ms/frame GPU-idle, busy 66%, "
    "from the CP thread being descheduled). A global-SAFE alternative to pinning "
    "the X3 (thor_gpu_thread_affinity_cpu): it raises priority WITHOUT stealing a "
    "core from the guest, so it should not regress CPU-bound titles. Android "
    "forbids SCHED_FIFO (set_priority EPERMs) but lets an app lower its own "
    "thread's nice (audio threads use -19). 0 = no change (default). Negative = "
    "higher priority; try -19. Android-only.",
    "GPU");
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
             "(magenta). 6 = only if a DIRECT shared-memory binding-0 read (no "
             "OpSwitch) is nonzero - discriminates 'the storage buffer reads "
             "zero' (black) from 'only the multi-binding OpSwitch load is "
             "miscompiled' (magenta). Whichever probe lights up (color in the RT "
             "dump) names the failure shape. Default 0.",
             "GPU");
DEFINE_bool(gpu_vulkan_shared_memory_no_switch, false,
            "Replace the OpSwitch+OpPhi over the multi-binding shared-memory SSBO "
            "array in LoadUint32FromSharedMemory with constant-index loads from "
            "every binding + an OpSelect chain (selected by the binding index). "
            "The Mesa Turnip ir3 backend appears to miscompile the switch/phi-over-"
            "descriptor-array load so vertex-fetch reads return zero (degenerate "
            "all-zero gl_Position, black screen); this avoids it. Costs N loads per "
            "dword instead of 1 (N = guest RAM / maxStorageBufferRange, 4 on a "
            "128 MB-range device). Default off (proprietary path unchanged).",
            "GPU");
DEFINE_bool(gpu_vulkan_float_constants_ssbo, false,
            "Declare the guest float-constant buffer as an SSBO "
            "(StorageClassStorageBuffer) instead of a UBO. The Mesa Turnip ir3 "
            "backend appears to miscompile DYNAMIC (a0-relative, i.e. indexed/"
            "skinning) OpAccessChain reads of the UNIFORM float_constants array "
            "(returning wrong values -> degenerate vertex positions -> black 3D, "
            "e.g. Magna Carta 2's skinned scenes); Turnip indexes storage buffers "
            "robustly. Must be matched by the descriptor-set layout, pool, and "
            "write (all STORAGE_BUFFER) - the CP gates those on this cvar too. "
            "Default off (UBO path unchanged); likely fixes any indexed-constant/"
            "skinned-VS title when on.",
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
    spirv_pos_binning_passthrough, false,
    "DIAGNOSTIC (Position-Only-Shading A/B): in the vertex shader, replace the "
    "guest-computed clip-space gl_Position with a trivial per-vertex value "
    "derived only from gl_VertexIndex, so the expensive guest VS math (skinning "
    "/ matrix-palette transforms) that fed the position no longer reaches the "
    "position output. Turnip's auto-derived position-only BINNING clone then "
    "dead-code-eliminates that math. Tests whether the per-vertex binning drain "
    "(Blue Dragon's ~12.7ms 'irreducible' floor) is actually reducible VS math "
    "(POS-friendly SPIR-V would then be the fix) vs truly irreducible per-vertex "
    "fixed overhead (fetch / primitive assembly / tiling). BREAKS RENDERING - "
    "TIMING DIAGNOSTIC ONLY: compare gpu_frame_us off vs on at a matched "
    "guest_ms. Default off.",
    "GPU");
DEFINE_bool(
    spirv_pos_collapse_only, false,
    "DIAGNOSTIC companion to spirv_pos_binning_passthrough: KEEP the full guest "
    "position math (so Turnip's binning clone still runs it - NOT pruned) but "
    "scale the final clip-space XY to collapse on-screen coverage to ~0, exactly "
    "like passthrough does. This isolates the fragment-work confound: passthrough "
    "(binning math pruned + coverage collapsed) vs collapse_only (binning math "
    "kept + coverage collapsed) at a matched guest_ms cancels the fragment delta, "
    "so the gpu_frame_us difference between the two = the per-vertex binning "
    "VS-math cost alone. If passthrough is much faster than collapse_only, the "
    "binning floor is reducible VS math; if they're equal, it's irreducible "
    "fixed overhead. BREAKS RENDERING - timing diagnostic only. Default off.",
    "GPU");
DEFINE_bool(
    gpu_opaque_depth_prepass, false,
    "Opaque depth PRE-PASS (WIP, build target): within a render pass, render the "
    "OPAQUE draws (depth-write, no alpha-test, no blend - see the comp[opaque=] "
    "draw-outcomes counter) DEPTH-ONLY first, so the full color stream (including "
    "alpha-test foliage behind opaque terrain) early-Z-rejects occluded fragments "
    "before their fragment work runs - attacking Blue Dragon's overdraw-bound GPU "
    "frame. Needs draw deferral + stream splice (see docs/research/20260617-bd-"
    "opaque-depth-prepass-plan.md). NOT YET FUNCTIONAL - scaffold only. Default off.",
    "GPU");
DEFINE_bool(
    gpu_foliage_lrz_force_depth, false,
    "Lever A (overdraw attack): force the alpha-test foliage class to depth-TEST "
    "(zfunc=Less) with depth-WRITE OFF against the opaque depth field, so foliage "
    "behind opaque geometry early-Z-rejects on the Adreno (LRZ-TEST survives "
    "discard; LRZ-WRITE does not) instead of fully shading every occluded leaf. "
    "Pair with gpu_opaque_depth_prepass=true (primes opaque depth first) for the "
    "full effect. Host-RT path only. Quality tradeoff: changes foliage depth "
    "test/write semantics (sorting), validate visually. Validate via a gpu_freeze "
    "A/B (force_depth off vs on, BOTH legs with the prepass on) at a matched "
    "frozen frame. Default off.",
    "GPU");
DEFINE_bool(
    gpu_force_no_color_write, false,
    "DIAGNOSTIC (overdraw ROP/blend split): force colorWriteMask=0 on all color "
    "render targets, so every draw still rasterizes + depth-tests + runs (the "
    "shader) but writes NO color and does NO blend. Keeps coverage, draws, "
    "verts, RT transfers, depth. Splits the per-covered-fragment cost: if "
    "gpu_frame_us collapses (e.g. BD ~696ms -> tens of ms) the dominant cost is "
    "the COLOR-WRITE + ALPHA-BLEND ROP path (overdraw) -> a depth pre-pass / LRZ "
    "that stops occluded fragments from reaching the ROP will pay; if it stays "
    "high, the cost is raster/depth/discard, not the color path. BREAKS "
    "RENDERING - timing diagnostic only. Default off.",
    "GPU");
DEFINE_bool(
    gpu_force_tiny_draws, false,
    "DIAGNOSTIC (per-draw vs per-vertex overhead split): clamp every indexed "
    "draw's index count to 3 (one tiny triangle), keeping ALL the per-draw work "
    "intact (the ~1200 draw calls, 208 pipeline binds, ~1038 descriptor binds, "
    "index-buffer binds, ~25 RT transfers) but collapsing per-vertex work to ~0 "
    "(only ~3 verts/draw fetched/transformed/binned/shaded, fragment ~0). So "
    "gpu_frame_us with this ON = the PER-DRAW floor alone (command-processor / "
    "state / descriptor / RT-transfer / GMEM cost). Compare to the normal "
    "~263k-vertex frame (or the spirv_pos_binning_passthrough ~33.7ms point) at "
    "a matched guest_ms: if ON stays near the full frame -> the overhead is "
    "PER-DRAW (draw-batching / bindless is a big GPU win); if ON collapses to a "
    "few ms -> it's PER-VERTEX fetch/tiling (draw-batching only helps CPU; need "
    "fewer verts / bindless vertex fetch). BREAKS RENDERING - timing diagnostic "
    "only. Default off.",
    "GPU");
DEFINE_bool(
    gpu_collapse_alphatest_coverage, false,
    "DIAGNOSTIC (alpha-test foliage overdraw isolation - the BD load-bearing "
    "falsifier): clamp index count to 3 ONLY for ALPHA-TEST draws "
    "(RB_COLORCONTROL.alpha_test_enable != 0 - the ~61% foliage draws), leaving "
    "opaque + blended draws at full geometry. So gpu_frame_us ON vs OFF at a "
    "matched/frozen guest_ms = exactly the per-vertex + per-covered-fragment "
    "cost of the alpha-test foliage. If the delta is most of the ~868ms BD "
    "frame -> alpha-test foliage overdraw IS the bottleneck and a foliage-cut "
    "lever is worth pursuing; if small -> the cost is elsewhere and the guest "
    "foliage RE should be shelved. BREAKS foliage rendering - timing diagnostic "
    "only. Pair with gpu_freeze_at_guest_ms for a clean A/B. Default off.",
    "GPU");
DEFINE_bool(
    gpu_collapse_blended_coverage, false,
    "DIAGNOSTIC (transparency overdraw isolation): like "
    "gpu_collapse_alphatest_coverage but for BLENDED draws (alpha-blend "
    "transparency - water, effects, particles; cannot early-Z so they "
    "overdraw). gpu_frame_us ON vs OFF at a matched frame = the blended "
    "fragment cost. Default off.",
    "GPU");
DEFINE_bool(
    gpu_collapse_opaque_coverage, false,
    "DIAGNOSTIC (opaque overdraw isolation): like "
    "gpu_collapse_alphatest_coverage but for OPAQUE draws (depth-write, no "
    "alpha-test, no blend - terrain/buildings; these GET Adreno LRZ/early-Z so "
    "their overdraw should be LOW). gpu_frame_us ON vs OFF = the opaque "
    "fragment+vertex cost; if large, opaque overdraw isn't being early-Z "
    "rejected (a depth-prepass lever). Default off.",
    "GPU");
DEFINE_int32(
    gpu_blended_thin_factor, 0,
    "SPEED HACK (transparency overdraw reduction): thin BLENDED (alpha-blend) "
    "draws - keep 1 of every N, collapse the rest. Device-proven on Blue "
    "Dragon: blended transparency overdraw is ~34% of the GPU frame "
    "(865ms->571ms collapsed). Riskier visually than foliage thinning "
    "(transparency = fog/shadow/glow/particles, more load-bearing), so "
    "validate per-game. 0/1 = off; 2 = keep 1/2; 4 = keep 1/4. Default 0.",
    "GPU");
DEFINE_int32(
    gpu_foliage_thin_factor, 0,
    "SPEED HACK (foliage overdraw reduction - 'performance mode'): thin "
    "alpha-test foliage by keeping only 1 of every N alpha-test draws and "
    "collapsing the rest to ~0 coverage (RB_COLORCONTROL.alpha_test_enable). "
    "Device-proven on Blue Dragon: alpha-test foliage overdraw is ~43% of the "
    "GPU frame (865ms->492ms when fully collapsed), so thinning trades foliage "
    "density for a large, tunable GPU-frame-time win on alpha-test-heavy TBDR "
    "scenes (the overdraw defeats Adreno LRZ). 0 or 1 = off (keep all foliage); "
    "2 = keep 1/2 (drop ~50%, ~1.27x); 4 = keep 1/4 (drop ~75%, ~1.48x); higher "
    "= sparser + faster. Correct/safe (only reduces guest-drawn foliage "
    "geometry); the visual cost is thinner grass/detail. Default 0 (off).",
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
    vulkan_merge_draws_rewrite, false,
    "Thor/Adreno binning re-arch (Lever 2 rank-3): INDEX-REWRITING draw "
    "concatenation - merge consecutive same-state kGuestDMA indexed LIST draws "
    "into ONE vkCmdDrawIndexed by COPYING each draw's raw guest index bytes "
    "(verbatim, no swap - the vertex shader applies the index endian) appended "
    "into a fixed-cap transient index block, so SCATTERED index ranges "
    "concatenate too (unlike the zero-copy vulkan_merge_draws, which requires "
    "byte-contiguous ranges and almost never engages). Run boundaries: any "
    "recorded state change, primitive type/pipeline/constants change, "
    "VGT_INDX_OFFSET/endian change, or block capacity (64KB). Wins over "
    "vulkan_merge_draws if both set. The controlled test for the measured "
    "~31us/draw BTTF per-draw slope. Default off; STARTUP cvar.",
    "GPU");

DEFINE_bool(
    vulkan_merge_draws_rewrite_strips, false,
    "Thor/Adreno binning re-arch (Lever 2 rank-3 strip coverage): extend the "
    "index-rewriting draw concatenation (vulkan_merge_draws_rewrite, required) "
    "to TRIANGLE STRIP draws by JOINING consecutive same-state strips inside "
    "the rewritten index block: primitive-restart-enabled runs are joined with "
    "one all-FF restart marker (the host reset index is always 0xFFFF/"
    "0xFFFFFFFF for kGuestDMA); restart-disabled runs are joined with 2 "
    "duplicate indices (3 when the accumulated index count is odd, preserving "
    "strip winding parity) forming zero-area degenerate triangles the "
    "rasterizer drops. Strip-dominated titles (Blue Dragon ts=968/1138, "
    "Burnout ts=1898/2110 draws/frame) have ZERO coverage from the LIST-only "
    "rewrite; this is their per-draw lever. Default off; STARTUP cvar.",
    "GPU");

DEFINE_bool(
    gpu_vulkan_weak_external_subpass_deps, false,
    "DIAGNOSTIC (Thor/Adreno binning serialization probe; knowingly unsafe in "
    "theory): make the guest render passes' EXTERNAL subpass dependencies "
    "no-ops (src TOP_OF_PIPE/0 in, dst BOTTOM_OF_PIPE/0 out) so the tiler may "
    "overlap a pass's binning with prior passes' deferred render. Every BTTF "
    "frame fully drains (~12.7ms = ~30% of the GPU frame) immediately before "
    "its main scene pass; the drain survives transfer elimination and draw "
    "merging, leaving these dependencies as the candidate serializer. If the "
    "drain vanishes with this on and the frame stays pixel-correct, the "
    "productized fix is targeted barriers on actual same-RT reuse only. May "
    "corrupt titles that re-read a just-written render target - read the "
    "frame. Default off; STARTUP cvar.",
    "GPU");

DEFINE_bool(
    vulkan_hoist_request_range_lock, true,
    "Thor CPU-hygiene: acquire the SharedMemory global lock ONCE around the "
    "per-draw vertex + memexport RequestRange calls in IssueDraw, instead of "
    "letting each RequestRange re-acquire the (recursive) global lock. "
    "Semantically identical - just fewer lock/unlock round-trips per draw on "
    "the hot draw-issue path. Zero rendering change. Ported from xenia-edge "
    "c2674b19d (the lock-hoist half; we keep our per-draw residency dedup). "
    "Default off; enabled via the in-app optimization toggle.",
    "GPU");

DEFINE_bool(
    vulkan_cache_vertex_residency, false,
    "Thor CPU lever (high-draw CPU-bound titles, e.g. Burnout in-race ~3442 "
    "draws): cache vertex-buffer residency ACROSS draws WITHIN a frame so the "
    "per-draw 'ensure vertex buffers resident' RequestRange loop skips ranges "
    "already requested this frame, instead of re-calling RequestRange for every "
    "binding of every draw (~5000/frame). Implements the existing "
    "TODO(Triang3l) to cache residency like texture validity. The cache is "
    "frame-stamped (cleared when the frame index changes) so each guest "
    "vertex-buffer write is re-uploaded next frame. CORRECTNESS: assumes the "
    "guest does not CPU-write an already-requested vertex range MID-frame "
    "(between draws of the same frame); rare but possible, so default OFF + "
    "device A/B (verify pixel-correct on BD + Burnout) before enabling. "
    "Measured by cpu_vfres_us in the draw-outcomes log. Default off.",
    "GPU");

DEFINE_bool(
    vulkan_gate_rt_update, true,
    "Thor CPU lever (high-draw CPU-bound titles, e.g. Burnout in-race ~3442 "
    "draws): skip the per-draw RenderTargetCache::Update (~15ms/frame cpu_rt) "
    "when the render-target CONFIG is byte-identical to the last draw AND the "
    "render pass is still open (current_render_pass_ != null). Render targets "
    "change only ~9x/frame, so most Update calls are redundant. SAFE because a "
    "pass break, EDRAM transfer, or frame/submission boundary nulls "
    "current_render_pass_, which forces a real Update (re-entering the pass + "
    "running any EDRAM ownership transfers); the config snapshot covers every "
    "register Update reads (RB_SURFACE_INFO, RB_DEPTH_INFO, RB_COLOR_INFO[0-3]) "
    "plus the normalized depth-control / color-mask / vertex-shader / "
    "rasterization-done inputs. CORRECTNESS: any missed dependency = stale RT = "
    "GPU corruption. DEVICE-VALIDATED cross-game: Burnout in-race 5.9->7.9fps "
    "(+34%) PIXEL-PERFECT [CPU-bound] + BTTF gameplay PIXEL-CORRECT [GPU-bound] "
    "-> DEFAULT ON (matches the default-on opt_gate_rt_update toggle). Measured "
    "by cpu_rt_us.",
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
    vulkan_trace_pass_timestamps, false,
    "Route A per-pass GPU timing (Thor binning diagnostic): bracket each GAME "
    "render-pass span with GPU timestamps and log gpu_pass_us (time INSIDE "
    "render passes) on the GPU draw outcomes line. gpu_frame_us - gpu_pass_us = "
    "time BETWEEN passes (EDRAM transfers / compute / barriers / stalls), the "
    "measured pass-fragmentation floor. Default off; needs "
    "vulkan_trace_draw_outcomes_per_frame for the log line.",
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
    vulkan_dynamic_constants_arena, true,
    "R2 (Thor hyperopt): hold the guest draw constant buffers in a persistent, "
    "per-frame-segmented UMA ring arena and bind them once via a "
    "UNIFORM_BUFFER_DYNAMIC descriptor set with per-draw pDynamicOffsets, instead "
    "of allocating + writing a transient descriptor set per draw. Needs a dynamic "
    "pipeline-layout variant (built only while this is on) so layouts never mix. "
    "Default off = byte-identical (uniform_buffer_pool_ + per-draw transient set). "
    "Experimental; being landed incrementally.",
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
