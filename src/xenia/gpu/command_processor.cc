/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/command_processor.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstring>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/thor_topology.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"

#if XE_PLATFORM_ANDROID
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <unordered_map>
#endif  // XE_PLATFORM_ANDROID

DEFINE_bool(
    gpu_bd_sync_event_write_fences, false,
    "BD Turnip crash-race candidate fix (gated, default off): before an "
    "EVENT_WRITE_SHD packet stores its completion fence to guest memory, force "
    "the submitted GPU work to COMPLETE (backend flush + await current "
    "submission). The crash is ledger-confirmed a GPU-completion TIMING RACE - "
    "the fence is written synchronously on the CP thread with no GPU wait, so "
    "with the native renderer / skip_resolves the guest can poll the fence and "
    "read a result still in flight on fast Turnip -> intermittent crash "
    "(~180s). No-op on backends that don't override SyncGpuForEventWriteFence. "
    "HYPOTHESIS-DRIVEN: validate on device (crash-mapper: does the racing fn "
    "poll this fence? does the crash vanish? fps cost?), then narrow to BD's "
    "field-resolve fences. See memory bd-turnip-crash-race-diagnosis.",
    "GPU");
DEFINE_bool(gpu_hle_surface_trace, false,
            "GPU D3D-HLE: log BD's surface/tiling/copy register writes (RB_SURFACE_"
            "INFO / COLOR / DEPTH / MODECONTROL / COPY_CONTROL / COPY_DEST_INFO) as "
            "they stream from the guest indirect buffers - to RE the tiling-setup "
            "intent for the register-level bin-once rewrite.",
            "GPU");
DEFINE_bool(gpu_hle_surface_binonce, false,
            "GPU D3D-HLE bin-once at the register (intent) level: rewrite BD's "
            "tile-strip RB_SURFACE_INFO surface_pitch (~360) to the full 720 "
            "surface. Pairs with the walker count=1 (the freed 2nd-strip EDRAM fits "
            "the wider surface) = render once, full width. Host-side, no reentrancy.",
            "GPU");

// --- Blue Dragon FULL native D3D9->Vulkan HLE RENDERER (separate native path) ---
DEFINE_bool(
    gpu_bd_native_renderer, false,
    "Blue Dragon FULL D3D9->Vulkan HLE renderer (the DXVK-for-360 reimagination, "
    "default off, Vulkan only): a SEPARATE native Vulkan renderer (BdNativeRenderer) "
    "that captures BD's D3D9 draws at seam 0x82489F40 and renders the whole frame "
    "into ONE persistent full-surface host RT in a FEW held-open passes with native "
    "pipelines (VkBuffer vertex-input, Xenos->SPIR-V, hardware ROP blend, depth-"
    "prepass+early-Z) - BYPASSING xenia's PM4/EDRAM/95-pass LLE back-end (which the "
    "partial gpu_bd_native_hle KEPT, hence perf-flat). Structure/correctness on "
    "desktop --gpu=vulkan; super-optimize for Thor Turnip/Adreno TBDR (minimize "
    "passes=GMEM flushes, GMEM-resident RT). See docs/research/20260705-native-"
    "vulkan-renderer-plan.md. Brick 1 = the persistent RT + one render pass.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_keep_scissor, false,
    "BD tile-fanout fix (5.6-sol 2026-07-11): when true, KEEP GetScissor's already-"
    "correct per-group scissor (it applies PA_SC_WINDOW_OFFSET: left field group -> "
    "[0,672], right -> [608,1280]) in the native renderer path instead of OVERRIDING "
    "it to the full native extent. The override was the failed 'widen the scissor, "
    "ignore the offset' approach that STRIPED (both groups leak full-width + overlap). "
    "Keeping the per-group scissor renders each group into only its region -> no "
    "stripe (if both groups are caught). Needs gpu_bd_native_renderer. Default off "
    "(legacy override) for A/B. PC-verify via screenshot (no stripe).",
    "GPU");
DEFINE_bool(
    gpu_bd_field_decouple, false,
    "BD field DECOUPLING (capture-replay, 5.6-sol design 2026-07-11): BD renders the "
    "field in ~4 segments interleaved with shadow RTs, which forces the field MSAA "
    "producer to spill across A->B switches (negating the on-tile custom-resolve win) "
    "and desyncs the render-pass state machine. When true, CAPTURE the field's draws "
    "as self-contained packets into bd_field_command_buffer_ and REPLAY them "
    "contiguously into ONE 2-subpass VK_EXT_custom_resolve pass at the publication "
    "IssueCopy (subpass0 splice + subpass1 on-tile resolve+convert). Stage 1 = "
    "correctness-only duplicate; Stage 2 = suppress originals + drop LLE + measure. "
    "Turnip-only; needs gpu_bd_native_keep_scissor + customResolve. Default off.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_skip_resolves, false,
    "BD-30 pass-collapse (default off, needs gpu_bd_native_renderer): once the "
    "native renderer has rendered the field this frame, skip the LLE EDRAM "
    "resolves that follow (redundant - the native RT is presented, not the "
    "resolved LLE surface). Drops the bulk of the 79-pass EDRAM overhead. Resolves "
    "before the field (textures/shadows) are kept.",
    "GPU");
DEFINE_uint32(
    gpu_bd_native_rt_width, 1280,
    "BD native RT width (default 1280). The bin-once renders BD's field into a "
    "W-wide region (surface desc W=672); set this to that W so the field content "
    "FILLS the native RT and the presenter stretches it UV[0,1]->1280 to full "
    "width (full-scene-in-half -> full-screen). 1280 = no stretch.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_whole_frame, false,
    "BD WHOLE-FRAME HLE (default off, needs gpu_bd_native_renderer): the pitch-720 "
    "redirect catches only ~40% of draws (the 2x foliage); ~60% (non-720 1x opaque) "
    "still hits LLE EDRAM = the perf confound (measured). This catches ALL field-"
    "format (A2B10G10R10) draws - foliage AND opaque - and forces the native pass "
    "to 1x so xenia builds matching 1x pipelines, rendering the WHOLE frame in ONE "
    "native pass and bypassing LLE for the opaque majority too. The direct 11->30fps "
    "lever on the Thor. Drops the foliage 2x MSAA (also cheaper).",
    "GPU");
DEFINE_bool(
    gpu_bd_native_aux_fmt37, true,
    "BD REAL-HLE: also cover vk-format-37 (RGBA8 opaque/background) aux surfaces "
    "(default on). Set FALSE to cover vk-97 only — isolates whether format-37 "
    "surface handling is the DESKTOP x64-backend crash (unblocks PC-primary HLE "
    "dev). On the Thor keep it on (the opaque needs covering).",
    "GPU");
DEFINE_bool(
    gpu_bd_native_diag_coverage, false,
    "BD REAL-HLE diagnostic: log the >2048px-wide RTs that black-screen when "
    "aux-covered, with their dims + whether they have a resolve-edge. Pins the "
    "consume-side coverage gap. Default off.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_aux_rt, false,
    "BD REAL-HLE aux render-to-texture (default off, needs gpu_bd_native_renderer): "
    "redirect a NON-field EDRAM-bound draw into its OWN persistent native surface "
    "keyed by the resolve-dest guest address (the stable D3D9 resource identity), so "
    "shadows/reflections/RT-textures render natively and the field SAMPLES those "
    "images directly (native texture binding) instead of the EDRAM-resolved upload. "
    "This is the EDRAM-deletion step: with the content native, the 35 ownership-"
    "transfer passes (~110ms) are no longer load-bearing. DEV/iterate on desktop "
    "--gpu=vulkan + RenderDoc (dims/format/pass-compat), then Thor-measure.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_tex_bind, true,
    "BD REAL-HLE Brick B (default on, needs gpu_bd_native_aux_rt): bind native "
    "surfaces to the field's pixel-shader texture fetches. Set FALSE to isolate the "
    "render-redirect from the texture-bind: if the frame is still black with this "
    "OFF, the black is render-pass STATE corruption from the redirect; if it renders "
    "(minus the stolen RTs), the black was wrong-content binding via Brick B.",
    "GPU");
DEFINE_uint32(
    gpu_bd_native_aux_max_width, 800,
    "BD REAL-HLE aux redirect max surface width (default 800). Only redirect "
    "intermediate texture RTs up to this width into native surfaces; larger "
    "(main-scene/frontbuffer) RTs stay on the EDRAM path (their composite/present "
    "readers aren't redirected, so stealing them black-screens). Raise once the "
    "consume side covers all readers.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_mainscene_redirect, false,
    "BD REAL-HLE main-scene native redirect (default off, needs "
    "gpu_bd_native_renderer + gpu_bd_native_aux_rt): extend resource-keyed native "
    "surface production, Brick-B/composite sampling, and present to 1x, "
    "identity-format frontbuffer-sized color RTs. With the existing native "
    "drop-resolves/drop-transfers switches, their EDRAM publication chain is "
    "deleted only after the current frame produced the native image and the prior "
    "completed frame proved every observed consumer native (no NonNative bit). "
    "Format-converting/MSAA and all depth chains stay on EDRAM. ",
    "GPU");
DEFINE_bool(
    gpu_bd_native_drop_transfers, false,
    "BD REAL-HLE surgical EDRAM-transfer deletion (default off, needs "
    "gpu_bd_native_aux_rt): drop ONLY the EDRAM ownership transfers whose dest RT "
    "resolves to a guest address a native surface already serves (dead weight — the "
    "field samples native content there). Keeps transfers for RTs not covered "
    "natively, so it does NOT black the main scene like the blunt "
    "gpu_skip_edram_transfers. THE EDRAM-deletion lever for the ~97ms transfer wall "
    "(only as effective as the native coverage — extend the redirect to the main "
    "framebuffer to drop its heavy transfer).",
    "GPU");
DEFINE_bool(
    gpu_bd_native_drop_depth_downscale, false,
    "BD REAL-HLE (PC-verified target): drop ONLY the DEPTH ownership transfers whose "
    "src/dst PITCH differs (the 720->400 depth-DOWNSCALE transfers that produce "
    "downscaled depth views for post-processing depth samples), KEEPING the "
    "same-pitch depth transfers (the MSAA 1x<->2x conversions the field's depth test "
    "needs). If the field stays correct, the downscale transfers are redundant to the "
    "main scene => EDRAM depth cost drops without collapse. Barrier-preserved.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_depth_convert, false,
    "BD REAL-HLE — THE 30fps lever (rigorously identified 2026-07-09): BD's field GPU "
    "frame = 42.7ms = 12.6ms in-pass draws + 30.1ms BETWEEN-pass, and the between-pass "
    "cost is the mixed-resolution DEPTH conversion transfers (BD depth-tests at full 720 "
    "AND downscaled ~400 views; ~23-40 EDRAM tile-reinterpreting transfers/frame convert "
    "between them + MSAA 1x<->2x). Fragment levers are dead (only touch the 12.6ms), and "
    "dropping the depth transfers collapses the field (Thor). This lever REPLACES the "
    "tile-reinterpreting depth-conversion transfers (pitch OR msaa mismatch) with a "
    "NATIVE depth downsample/resolve (sample the source depth image, write the dest depth "
    "attachment via a graphics pipeline) so the ~30ms EDRAM depth-conversion GAP is "
    "eliminated while depth stays correct. Same-layout depth transfers keep the existing "
    "path. THOR-GATED: desktop cannot validate depth (immediate-mode differs from TBDR) — "
    "correctness must be Thor-verified. Default off; needs gpu_bd_native_renderer.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_depth_handoff, false,
    "BD native depth handoff falsifier (default off): for only the base-0, "
    "single-sample pitch-16 to pitch-13 depth ownership edge, run the existing "
    "Xenos depth-transfer shader from the resource-keyed source host depth image "
    "into the resource-keyed destination host depth image as the first draw of "
    "the destination consumer pass. The edge stays on the legacy EDRAM transfer "
    "path unless that exact destination image is proven bound as the consumer's "
    "depth attachment. Mixed-MSAA and every other edge are unchanged.",
    "GPU");
DEFINE_bool(
    gpu_bd_framegraph_depth, false,
    "BD minimal framegraph depth milestone (default off): defer one exact "
    "base-810 pitch-9 2x-to-4x depth ownership transfer until its non-adjacent "
    "resource-keyed destination is bound as a guest depth attachment, then run "
    "the existing transfer shader as the first draw of that consumer pass. Any "
    "identity, generation, load-semantics, or source-lifetime ambiguity falls "
    "back eagerly to the legacy standalone transfer pass.",
    "GPU");
DEFINE_bool(
    gpu_bd_framegraph_depth_dump, false,
    "BD framegraph depth diagnostic (default off): log a comparable command-"
    "state marker for the exact base-810 pitch-9 2x-to-4x depth transfer after "
    "the fused or standalone transfer draw is recorded. Cheap and readback-"
    "free; use matching markers to separate transfer setup from later draws.",
    "GPU");
DEFINE_bool(
    gpu_bd_framegraph_depth_shadow, false,
    "BD minimal framegraph depth shadow diagnostic (default off): recognize "
    "and trace the exact base-810 pitch-9 2x-to-4x depth ownership edge and "
    "its first destination consumer without deferring or skipping the legacy "
    "standalone transfer. This is instrumentation only and never records an "
    "in-pass transfer draw. When enabled it overrides gpu_bd_framegraph_depth "
    "for the recognized edge so rendering remains on the legacy path.",
    "GPU");
DEFINE_bool(
    gpu_bd_patha_depth_snapshot, false,
    "BD Path A Stage 1 (default off): redirect the ONE base-810 pitch-9 4x "
    "HDR-effect depth consumer (color base-0 pitch-9 2:10:10:10-float 4x) to a "
    "PRIVATE native 4x depth image. Before the consumer pass the EDRAM depth is "
    "seeded (copied) into the native image; the consumer depth-tests the native "
    "snapshot; after the pass it is mirrored back to EDRAM so downstream stays "
    "correct. The legacy ownership transfer is untouched. This validates the "
    "native-depth redirect machinery + measures the snapshot cost; any identity "
    "or format mismatch falls back to the plain LLE framebuffer.",
    "GPU");
DEFINE_bool(
    gpu_bd_perfmode_hdr_2x, false,
    "BD PERFORMANCE MODE Build 1 (default off, 5.6-sol arch): substitute the 4x "
    "HDR effect pass (color base-0 pitch-9 2:10:10:10-float 4x + depth base-810 "
    "pitch-9 4x - the SOLE consumer of the 4x field depth) with a NATIVE render "
    "at 2x: native 2x color + the native 2x field-depth snapshot, 2x pipeline "
    "variant, resolved color redirected downstream. This orphans the ~23 "
    "base-810 2x->4x depth ownership transfers (no 4x consumer) so they can be "
    "dropped, reclaiming their tile-store WITHOUT the temporal-snapshot problem. "
    "Fail-closed: any signature/state mismatch renders the original 4x guest "
    "pass + transfers. Small visual cost (slightly rougher glow/translucency).",
    "GPU");
DEFINE_bool(
    gpu_bd_native_reserve_captured_surfaces, false,
    "BD full-native HLE substrate (default off): drive BdNativeRenderer's "
    "resource-keyed AcquireSurface() from the live per-frame draw capture "
    "(requires gpu_bd_full_native>=1). At frame end, for each DISTINCT "
    "single-sample color surface seen this frame (keyed by "
    "base_tiles+pitch+format+msaa, sized from the pitch + max screen-scissor "
    "extent), ensure a persistent native color+depth VkImage exists - building "
    "the full resource-keyed native RT set the EDRAM-deletion HLE needs. This "
    "only ALLOCATES the surfaces (no render redirect yet); MSAA surfaces are "
    "deferred (AcquireSurface needs a resolve, not yet implemented). Inert until "
    "enabled; needs on-device validation of the full render redirect that "
    "consumes these surfaces.",
    "GPU");
DEFINE_int32(
    gpu_bd_native_color_lifetime_hle, 0,
    "BD REAL-HLE color-only native surface lifetime (Codex-designed 2026-07-10, "
    "docs/research/20260710-color-only-native-hle-plan.txt) — THE 30fps path. The "
    "wall (rigorously measured) = 35 standalone EDRAM COLOR ownership-transfer "
    "passes' deferred TBDR tile-store (~25ms). This renders BD's wide/main/composite "
    "COLOR surfaces into per-surface native color images (keyed by resolve-dest), "
    "substituting ONLY the color attachment while keeping LLE DEPTH authoritative "
    "(the prior full native-RT HLE black-band-collapsed on Turnip because it "
    "disturbed depth; keeping LLE depth avoids that), redirects the consumers "
    "(composite pixel-fetch + present), then drops the color transfers ONLY when all "
    "consumers are proven native-redirected (ColorDropSafe). Staged levels (Codex "
    "landing order): 1 = consumer-tracking instrumentation only (no redirects/drops, "
    "logs BD COLOR CONSUMERS); 2 = +present redirect; 3 = +composite redirect; 4 = "
    "+wide native color render (no drops); 5 = +drop color resolves; 6 = +drop color "
    "transfers (the win); 7 = +MSAA. THOR-GATED, incremental. 0 = off.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_field_convert, true,
    "BD color-HLE: when true (default), the MSAA field producer is converted via "
    "the fragment shader path (do_convert_shader) - PROVEN perf-DEAD on Turnip "
    "(1.8fps: the separate-pass MSAA-texture sample spills the 4x image out of "
    "tile memory + adds a GMEM-flush pass). Set FALSE to isolate the BLOOM-only "
    "deletion (1x blit-convert, no MSAA spill): the MSAA field falls back to LLE, "
    "only the cheap 1x bloom/HDR producers convert + their transfers drop. "
    "Diagnostic A/B to measure whether the bloom deletion alone (cheap blits) is "
    "net-positive vs the field convert's catastrophe. Only affects lifetime_hle>=9.",
    "GPU");
DEFINE_double(
    gpu_bd_native_depth_clear, 0.0,
    "BD REAL-HLE native depth-buffer CLEAR value (default 0.0 = reverse-Z far). The "
    "field's color pass depth-TESTS against this cleared native depth (the depth "
    "prepass is not redirected to the native buffer). If BD uses standard-Z (test "
    "LESS, far=1.0), a 0.0 clear culls the whole field to a strip when EDRAM is "
    "dropped — set 1.0 to test. THE candidate fix for the depth-cull collapse.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_drop_all_xfer, false,
    "BD REAL-HLE drop ALL EDRAM transfers (color AND depth) WITH the sync barrier "
    "(default off, needs gpu_bd_native_drop_transfers). The blunt "
    "gpu_skip_edram_transfers drops them WITHOUT a barrier -> field collapses. "
    "Hypothesis: the collapse is the missing depth BARRIER (sync/layout), not depth "
    "content. If the per-transfer barrier prevents the collapse, this deletes the "
    "full ~97ms transfer wall => 30fps + correct. THE candidate final EDRAM-deletion "
    "lever.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_drop_all_color_xfer, false,
    "BD REAL-HLE aggressive color-transfer deletion (default off, needs "
    "gpu_bd_native_drop_transfers + gpu_bd_native_aux_rt): drop ALL non-depth EDRAM "
    "ownership transfers (the ~97ms wall) while KEEPING depth transfers. The blunt "
    "gpu_skip_edram_transfers dropped depth too -> broke the field's depth test -> "
    "geometry depth-fails -> collapse to a strip (Thor) / crash (desktop). Keeping "
    "depth + dropping heavy color (field presents native color) targets 30fps + "
    "correct. THE candidate final lever for full EDRAM color-transfer deletion.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_drop_resolves, false,
    "BD REAL-HLE EDRAM-deletion probe (default off, needs gpu_bd_native_aux_rt): "
    "SURGICALLY drop the EDRAM->RAM color resolve when a live native surface serves "
    "the dest (the field samples native content via Brick B, so the resolve is "
    "redundant). SEPARATE from gpu_bd_native_aux_rt so native rendering+sampling can "
    "run WITHOUT the drop (isolation): dropping is only safe once ALL readers of the "
    "resolved texture (pixel fetches AND the frontbuffer composite/present) sample "
    "native — else the un-redirected reader goes black. THE payoff lever, but the "
    "consume side must be complete first.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_force_samples1, false,
    "BD native renderer Adreno-crash isolation (default off): force the native RT "
    "to single-sample (drop MSAA + the in-renderpass resolve attachment). The "
    "Adreno driver crashes processing the native pass; the MSAA resolve is the top "
    "suspect. Set true to test whether dropping it stops the driver crash.",
    "GPU");
DEFINE_uint32(
    gpu_bd_native_stretch_width, 0,
    "BD fill-the-screen stretch (default 0 = off). The bin-once renders BD's field "
    "into the left W px of the 1280 native RT (BD's field surface is 672 wide and "
    "BD upscales it in its resolve). Set to that W (672) so the native renderer "
    "BLITS the rendered region stretched to full width on present - the full field "
    "fills the screen. Post-composition (unlike the geometry-spreading viewport "
    "scale). Pairs with the BeginTiling bin-once + gpu_bd_native_renderer.",
    "GPU");
DEFINE_double(
    gpu_bd_native_viewport_scale_x, 1.0,
    "BD native horizontal viewport stretch (default 1.0 = off). BD's field draws "
    "use a giant 8192 viewport with NDC-positioned geometry; the bin-once lands it "
    "in the left ~672 of the 1280 RT. Scale the viewport width by this (1280/672 = "
    "~1.905) so the same geometry fills the full width. Pairs with the scissor "
    "widen + gpu_bd_native_renderer + the BeginTiling bin-once.",
    "GPU");
DEFINE_int32(
    gpu_bd_renderdoc_capture_frame, 0,
    "If non-zero and running under RenderDoc, trigger a frame capture at this "
    "swap number (e.g. 2000 for a field frame). 0 = off.",
    "GPU");
DEFINE_int32(
    gpu_bd_native_tile_filter, 0,
    "BD native renderer config-isolation diagnostic: 0=all field draws, "
    "1=only window-offset-zero draws (config-A), 2=only offset-carrying draws "
    "(config-B). Screenshots isolate where each tile config renders.",
    "GPU");
// --- Blue Dragon FULL NATIVE-DRAW HLE (the DXVK/Cemu model), Brick 1 ---
DEFINE_bool(
    gpu_bd_native_hle, false,
    "Blue Dragon native-draw HLE (Brick 1 foundation, default off): master switch "
    "for the DXVK-style path that intercepts BD's D3D9 draw, reconstructs the draw "
    "state, and feeds xenia's IssueDraw ONE native draw - bypassing the guest PM4 "
    "build + per-tile replay + xenia's LLE re-parse. Brick 1 wires the capture + the "
    "CP-thread submission point; full native emission is staged (Brick 2+).",
    "GPU");
DEFINE_uint32(
    gpu_bd_draw_contract_trace, 0,
    "Blue Dragon native-draw HLE diagnostic (default 0 = off): dump the fully-"
    "decoded per-draw 'translation contract' that CommandProcessor::IssueDraw reads "
    "from register_file_ (VS/PS ucode hashes, primitive+index buffer, vertex/texture "
    "fetch bindings, blend/depth/cull/scissor/viewport render state) for this many "
    "FIELD draws (RB_SURFACE_INFO pitch 360/720 = the foliage passes), then stop. "
    "This is the state a native front-end must reproduce per draw.",
    "GPU");
DEFINE_uint32(
    gpu_bd_native_hle_decouple, 0,
    "Blue Dragon native-draw HLE Half B (default 0 = off): when nonzero, the "
    "native synthetic draw redirects RB_COLOR_INFO to THIS EDRAM color-base "
    "tile (must be a non-aliasing base past BD's live color[0]/depth tiles, "
    "e.g. 1400) so it renders into its OWN dedicated full-surface host render "
    "target - decoupled from BD's live scene EDRAM tiles - which the D3D12 "
    "backend then reads back and dumps to bd_native_hle_decoupled_N.png at the "
    "next swap. Requires gpu_bd_native_hle=true.",
    "GPU");
DEFINE_bool(
    gpu_bd_native_hle_replace, false,
    "Blue Dragon native-draw HLE step 3 (default off, the front-end "
    "replacement / real win): when true, DROP BD's original LLE per-tile "
    "foliage submission for every draw the native pass covers, so the field "
    "foliage is drawn ONCE natively instead of twice (native emit alongside "
    "LLE). The skipped draw's full decoded state already lives in register_file_ "
    "from the PM4 parse, so the native draw reproduces it exactly. Requires "
    "gpu_bd_native_hle=true.",
    "GPU");
DEFINE_bool(
    gpu_bd_hle_present_decoupled, false,
    "Blue Dragon native-draw HLE step 2 (default off, Vulkan): at the guest "
    "swap, present the decoupled full-surface host RT the native field draws "
    "rendered into (gpu_bd_native_hle_decouple) INSTEAD of BD's resolved guest "
    "front buffer - so the screen shows the decoupled render directly, and BD's "
    "per-tile base-0 resolve is no longer needed for display. Pairs with "
    "gpu_bd_hle_drop_resolve. Requires gpu_bd_native_hle + _decouple.",
    "GPU");
DEFINE_uint32(
    gpu_bd_hle_drop_resolve, 0,
    "Blue Dragon native-draw HLE step 3 (default 0 = off, Vulkan, THE perf "
    "lever): skip BD's emulated per-tile EDRAM->RAM resolve COPY (the ~120ms "
    "field GPU fence) - the decoupled full-surface RT is the source of truth, so "
    "the base-0 resolve is dead weight. 1 = drop only base-0 color-copy resolves "
    "(BD's field); 2 = drop ALL color-copy resolves (upper-bound perf probe). "
    "Depth resolves and clears still run. Pair with gpu_bd_hle_present_decoupled "
    "so the screen still shows the field. Requires the decoupled path for a "
    "correct image; on its own it measures the resolve's raw GPU cost.",
    "GPU");

DECLARE_uint32(cpu_watch_guest_write_page);

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

namespace {

constexpr uint32_t kCpRbRptrRegister = 0x01C4;
constexpr uint32_t kCpRbWptrRegister = 0x01C5;

#if XE_PLATFORM_ANDROID
// Thor "hot-thread on the prime core" tracker (thor_hot_thread_prime_core).
// A low-overhead monitor that periodically finds the single busiest thread in
// the process and pins it to the prime Cortex-X3, releasing the previous one
// back to the big cluster. Generalizes the static GPU-command-thread pin: on a
// GPU-bound title the command worker is hottest (it keeps the X3, as the static
// router did), but on a CPU/sync-bound title (Blue Dragon's heavy field) the
// hot guest game-logic thread is hottest and gets the X3 instead. Device-
// measured 2026-06-24: BD's field is bound by a single guest XThread at ~95%
// CPU - NOT the fence-blocked command worker, NOR the guest "main" thread
// (~14%) - so any fixed pin guesses wrong; this follows the actual hot thread.
// /proc/self/task sampling + sched_setaffinity, hint only, Android-only.
class HotThreadPinner {
 public:
  void Start(int prime_core, int interval_ms) {
    if (running_.exchange(true)) {
      return;
    }
    prime_core_ = prime_core;
    interval_ms_ = interval_ms > 0 ? interval_ms : 700;
    thread_ = std::thread([this] { Run(); });
  }

  void Stop() {
    if (!running_.exchange(false)) {
      return;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  // Sum of utime+stime (clock ticks) for one thread, from /proc/self/task/TID/
  // stat. comm (field 2) can contain spaces/parens, so parse after the LAST ')'.
  static uint64_t ReadThreadTicks(int tid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
    FILE* f = std::fopen(path, "re");
    if (!f) {
      return 0;
    }
    char buf[512];
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    if (n == 0) {
      return 0;
    }
    buf[n] = '\0';
    char* after_comm = std::strrchr(buf, ')');
    if (!after_comm) {
      return 0;
    }
    ++after_comm;
    // Tokens after ')' start at field 3 (state); utime=14, stime=15.
    uint64_t utime = 0, stime = 0;
    int field = 3;
    for (char* tok = std::strtok(after_comm, " "); tok;
         tok = std::strtok(nullptr, " "), ++field) {
      if (field == 14) {
        utime = std::strtoull(tok, nullptr, 10);
      } else if (field == 15) {
        stime = std::strtoull(tok, nullptr, 10);
        break;
      }
    }
    return utime + stime;
  }

  static void SetAffinity(int tid, uint64_t mask) {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int core = 0; core < 64; ++core) {
      if (mask & (uint64_t(1) << core)) {
        CPU_SET(core, &set);
      }
    }
    sched_setaffinity(static_cast<pid_t>(tid), sizeof(set), &set);
  }

  void Run() {
    pthread_setname_np(pthread_self(), "ThorHotPin");
    const uint64_t reset_mask = ThorTopology::BigCoreMask();
    const uint64_t prime_mask = uint64_t(1) << uint32_t(prime_core_);
    std::unordered_map<int, uint64_t> prev;
    int pinned_tid = -1;
    while (running_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms_));
      if (!running_.load(std::memory_order_relaxed)) {
        break;
      }
      DIR* dir = opendir("/proc/self/task");
      if (!dir) {
        continue;
      }
      std::unordered_map<int, uint64_t> cur;
      int best_tid = -1;
      uint64_t best_delta = 0;
      uint64_t pinned_delta = 0;
      for (struct dirent* ent = readdir(dir); ent; ent = readdir(dir)) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') {
          continue;
        }
        int tid = std::atoi(ent->d_name);
        uint64_t ticks = ReadThreadTicks(tid);
        cur[tid] = ticks;
        auto it = prev.find(tid);
        uint64_t delta =
            (it != prev.end() && ticks >= it->second) ? ticks - it->second : 0;
        if (delta > best_delta) {
          best_delta = delta;
          best_tid = tid;
        }
        if (tid == pinned_tid) {
          pinned_delta = delta;
        }
      }
      closedir(dir);
      prev.swap(cur);
      // Hysteresis: only migrate when the new candidate clearly beats the
      // currently-pinned thread (avoid ping-pong between near-equal threads).
      if (best_tid >= 0 && best_tid != pinned_tid &&
          best_delta > pinned_delta + pinned_delta / 4 + 1) {
        if (pinned_tid >= 0) {
          SetAffinity(pinned_tid, reset_mask);
        }
        SetAffinity(best_tid, prime_mask);
        pinned_tid = best_tid;
      }
    }
    if (pinned_tid >= 0) {
      SetAffinity(pinned_tid, reset_mask);
    }
  }

  std::atomic<bool> running_{false};
  std::thread thread_;
  int prime_core_ = ThorTopology::kPrimeCore;
  int interval_ms_ = 700;
};

HotThreadPinner* hot_thread_pinner() {
  static HotThreadPinner pinner;
  return &pinner;
}
#endif  // XE_PLATFORM_ANDROID

std::atomic<int32_t> gpu_packet_trace_count{0};
std::atomic<int32_t> gpu_interrupt_packet_trace_count{0};
std::atomic<int32_t> gpu_swap_frontbuffer_checksum_count{0};
std::atomic<int32_t> gpu_swap_render_targets_trace_count{0};
std::atomic<int32_t> gpu_unknown_register_write_log_count{0};
std::atomic<bool> gpu_swap_probe_cvars_logged{false};

#if XE_PLATFORM_ANDROID
// ADPF / thermal NDK entry points, resolved at runtime via dlsym so the build
// links against any minSdk and gracefully no-ops on devices/ROMs that lack them
// (APerformanceHint_* arrived in API 33, AThermal_getThermalHeadroom in API 31).
struct AdpfApi {
  // android/performance_hint.h
  void* (*get_manager)() = nullptr;
  void* (*create_session)(void* manager, const int32_t* thread_ids, size_t size,
                          int64_t initial_target_ns) = nullptr;
  int (*update_target)(void* session, int64_t target_ns) = nullptr;
  int (*report_actual)(void* session, int64_t actual_ns) = nullptr;
  void (*close_session)(void* session) = nullptr;
  // android/thermal.h
  void* (*thermal_acquire)() = nullptr;
  void (*thermal_release)(void* manager) = nullptr;
  float (*thermal_headroom)(void* manager, int forecast_seconds) = nullptr;
  bool perf_hint_available = false;
  bool thermal_available = false;
};

const AdpfApi& GetAdpfApi() {
  static const AdpfApi api = []() {
    AdpfApi a;
    void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) {
      lib = dlopen("libandroid.so", RTLD_NOW);
    }
    if (!lib) {
      return a;
    }
    a.get_manager = reinterpret_cast<decltype(a.get_manager)>(
        dlsym(lib, "APerformanceHint_getManager"));
    a.create_session = reinterpret_cast<decltype(a.create_session)>(
        dlsym(lib, "APerformanceHint_createSession"));
    a.update_target = reinterpret_cast<decltype(a.update_target)>(
        dlsym(lib, "APerformanceHint_updateTargetWorkDuration"));
    a.report_actual = reinterpret_cast<decltype(a.report_actual)>(
        dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
    a.close_session = reinterpret_cast<decltype(a.close_session)>(
        dlsym(lib, "APerformanceHint_closeSession"));
    a.thermal_acquire = reinterpret_cast<decltype(a.thermal_acquire)>(
        dlsym(lib, "AThermal_acquireManager"));
    a.thermal_release = reinterpret_cast<decltype(a.thermal_release)>(
        dlsym(lib, "AThermal_releaseManager"));
    a.thermal_headroom = reinterpret_cast<decltype(a.thermal_headroom)>(
        dlsym(lib, "AThermal_getThermalHeadroom"));
    a.perf_hint_available = a.get_manager && a.create_session &&
                            a.update_target && a.report_actual;
    a.thermal_available = a.thermal_acquire && a.thermal_headroom;
    return a;
  }();
  return api;
}

int64_t AdpfNowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
#endif  // XE_PLATFORM_ANDROID

bool ShouldLogUnknownGpuRegister(std::atomic<int32_t>& counter) {
  int32_t budget = cvars::gpu_unknown_register_log_budget;
  if (budget < 0) {
    return true;
  }
  if (budget == 0) {
    return false;
  }
  return counter.fetch_add(1) < budget;
}

bool ShouldTraceGpuPacket() {
  return cvars::gpu_trace_swap &&
         gpu_packet_trace_count.fetch_add(1) < cvars::gpu_trace_packet_budget;
}

bool ShouldTraceGpuInterruptPacket() {
  if (!cvars::gpu_trace_interrupts) {
    return false;
  }
  int32_t budget = cvars::gpu_trace_interrupts_budget;
  return budget < 0 || gpu_interrupt_packet_trace_count.fetch_add(1) < budget;
}

bool ShouldTraceSwapFrontbufferChecksum() {
  if (!cvars::gpu_trace_swap_frontbuffer_checksum) {
    return false;
  }
  int32_t budget = cvars::gpu_trace_swap_frontbuffer_checksum_budget;
  return budget < 0 ||
         gpu_swap_frontbuffer_checksum_count.fetch_add(1) < budget;
}

bool ShouldTraceSwapRenderTargets() {
  if (!cvars::gpu_trace_swap_render_targets) {
    return false;
  }
  int32_t budget = cvars::gpu_trace_swap_render_targets_budget;
  return budget < 0 ||
         gpu_swap_render_targets_trace_count.fetch_add(1) < budget;
}

void TraceSwapRenderTargets(const RegisterFile* regs, uint32_t frontbuffer_ptr,
                            uint32_t frontbuffer_width,
                            uint32_t frontbuffer_height) {
  if (!ShouldTraceSwapRenderTargets() || !regs) {
    return;
  }

  auto rb_surface_info = regs->Get<reg::RB_SURFACE_INFO>();
  auto rb_modecontrol = regs->Get<reg::RB_MODECONTROL>();
  auto rb_colorcontrol = regs->Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = regs->Get<reg::RB_DEPTH_INFO>();
  auto rb_copy_control = regs->Get<reg::RB_COPY_CONTROL>();
  uint32_t rb_copy_dest_base = (*regs)[XE_GPU_REG_RB_COPY_DEST_BASE];
  auto rb_copy_dest_pitch = regs->Get<reg::RB_COPY_DEST_PITCH>();
  uint32_t rb_color_mask = (*regs)[XE_GPU_REG_RB_COLOR_MASK];

  XELOGI(
      "GPU swap RT trace: frontbuffer={:08X} size={}x{} rb_surface={:08X} "
      "surface_pitch={} msaa={} rb_mode={:08X} edram_mode={} "
      "rb_colorcontrol={:08X} rb_color_mask={:04X} depth_info={:08X} "
      "depth_base={} depth_base_bit11={} depth_format={} copy_control={:08X} "
      "copy_src={} copy_command={} copy_dest_base={:08X} copy_dest_pitch={:08X}",
      frontbuffer_ptr, frontbuffer_width, frontbuffer_height,
      rb_surface_info.value, rb_surface_info.surface_pitch,
      uint32_t(rb_surface_info.msaa_samples), rb_modecontrol.value,
      uint32_t(rb_modecontrol.edram_mode), rb_colorcontrol.value,
      rb_color_mask & 0xFFFF, rb_depth_info.value, rb_depth_info.depth_base,
      rb_depth_info.depth_base_bit_11, uint32_t(rb_depth_info.depth_format),
      rb_copy_control.value, rb_copy_control.copy_src_select,
      uint32_t(rb_copy_control.copy_command), rb_copy_dest_base,
      rb_copy_dest_pitch.value);

  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    auto color_info = regs->Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[i]);
    uint32_t write_mask = (rb_color_mask >> (i * 4)) & 0xF;
    XELOGI(
        "GPU swap RT trace: color{} info={:08X} base_tiles={} "
        "base_bit11={} format={}({}) exp_bias={} write_mask={:X} "
        "pitch_pixels={} approx_edram_byte_offset={:08X}",
        i, color_info.value, color_info.color_base,
        color_info.color_base_bit_11, uint32_t(color_info.color_format),
        xenos::GetColorRenderTargetFormatName(color_info.color_format),
        color_info.color_exp_bias, write_mask, rb_surface_info.surface_pitch,
        color_info.color_base * xenos::kEdramTileWidthSamples *
            xenos::kEdramTileHeightSamples * uint32_t(sizeof(uint32_t)));
  }
}

void TraceSwapProbeCvarsOnce() {
  bool expected = false;
  if (!gpu_swap_probe_cvars_logged.compare_exchange_strong(expected, true)) {
    return;
  }
  XELOGI(
      "GPU swap probe cvars: gpu_trace_swap={} frontbuffer_checksum={} "
      "frontbuffer_budget={} render_targets={} render_targets_budget={}",
      cvars::gpu_trace_swap, cvars::gpu_trace_swap_frontbuffer_checksum,
      cvars::gpu_trace_swap_frontbuffer_checksum_budget,
      cvars::gpu_trace_swap_render_targets,
      cvars::gpu_trace_swap_render_targets_budget);
}

void TraceSwapFrontbufferChecksum(Memory* memory, uint32_t frontbuffer_ptr,
                                  uint32_t frontbuffer_width,
                                  uint32_t frontbuffer_height) {
  if (!ShouldTraceSwapFrontbufferChecksum()) {
    return;
  }
  if (!memory || !frontbuffer_ptr || !frontbuffer_width ||
      !frontbuffer_height) {
    XELOGI(
        "GPU swap trace: frontbuffer checksum skipped pa={:08X} size={}x{}",
        frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
    return;
  }

  uint64_t byte_count =
      uint64_t(frontbuffer_width) * uint64_t(frontbuffer_height) * 4;
  if (!byte_count) {
    return;
  }
  const uint8_t* bytes =
      memory->TranslatePhysical<const uint8_t*>(frontbuffer_ptr);
  constexpr uint64_t kSampleStride = 4096;
  uint64_t checksum = 1469598103934665603ull;
  uint32_t samples = 0;
  uint32_t nonzero_samples = 0;
  uint64_t first_nonzero_offset = UINT64_MAX;
  uint32_t first_nonzero_value = 0;
  for (uint64_t offset = 0; offset + sizeof(uint32_t) <= byte_count;
       offset += kSampleStride) {
    uint32_t word = 0;
    std::memcpy(&word, bytes + offset, sizeof(word));
    checksum ^= uint64_t(word) + (offset << 1);
    checksum *= 1099511628211ull;
    ++samples;
    if (word) {
      ++nonzero_samples;
      if (first_nonzero_offset == UINT64_MAX) {
        first_nonzero_offset = offset;
        first_nonzero_value = word;
      }
    }
  }

  uint32_t first_words[8] = {};
  uint32_t first_word_count =
      uint32_t(std::min<uint64_t>(xe::countof(first_words), byte_count / 4));
  for (uint32_t i = 0; i < first_word_count; ++i) {
    std::memcpy(&first_words[i], bytes + i * sizeof(uint32_t),
                sizeof(uint32_t));
  }

  XELOGI(
      "GPU swap trace: frontbuffer checksum pa={:08X} size={}x{} bytes={} "
      "stride={} samples={} nonzero={} checksum={:016X} first_nonzero={} "
      "first_nonzero_value={:08X} first={:08X},{:08X},{:08X},{:08X},"
      "{:08X},{:08X},{:08X},{:08X}",
      frontbuffer_ptr, frontbuffer_width, frontbuffer_height, byte_count,
      kSampleStride, samples, nonzero_samples, checksum,
      first_nonzero_offset == UINT64_MAX ? -1
                                         : int64_t(first_nonzero_offset),
      first_nonzero_value, first_words[0], first_words[1], first_words[2],
      first_words[3], first_words[4], first_words[5], first_words[6],
      first_words[7]);
}

const char* Type3OpcodeName(uint32_t opcode) {
  switch (opcode) {
    case PM4_ME_INIT:
      return "PM4_ME_INIT";
    case PM4_NOP:
      return "PM4_NOP";
    case PM4_INTERRUPT:
      return "PM4_INTERRUPT";
    case PM4_XE_SWAP:
      return "PM4_XE_SWAP";
    case PM4_INDIRECT_BUFFER:
      return "PM4_INDIRECT_BUFFER";
    case PM4_INDIRECT_BUFFER_PFD:
      return "PM4_INDIRECT_BUFFER_PFD";
    case PM4_WAIT_REG_MEM:
      return "PM4_WAIT_REG_MEM";
    case PM4_REG_RMW:
      return "PM4_REG_RMW";
    case PM4_REG_TO_MEM:
      return "PM4_REG_TO_MEM";
    case PM4_MEM_WRITE:
      return "PM4_MEM_WRITE";
    case PM4_COND_WRITE:
      return "PM4_COND_WRITE";
    case PM4_EVENT_WRITE:
      return "PM4_EVENT_WRITE";
    case PM4_EVENT_WRITE_SHD:
      return "PM4_EVENT_WRITE_SHD";
    case PM4_EVENT_WRITE_EXT:
      return "PM4_EVENT_WRITE_EXT";
    case PM4_EVENT_WRITE_ZPD:
      return "PM4_EVENT_WRITE_ZPD";
    case PM4_DRAW_INDX:
      return "PM4_DRAW_INDX";
    case PM4_DRAW_INDX_2:
      return "PM4_DRAW_INDX_2";
    case PM4_SET_CONSTANT:
      return "PM4_SET_CONSTANT";
    case PM4_SET_CONSTANT2:
      return "PM4_SET_CONSTANT2";
    case PM4_LOAD_ALU_CONSTANT:
      return "PM4_LOAD_ALU_CONSTANT";
    case PM4_SET_SHADER_CONSTANTS:
      return "PM4_SET_SHADER_CONSTANTS";
    case PM4_IM_LOAD:
      return "PM4_IM_LOAD";
    case PM4_IM_LOAD_IMMEDIATE:
      return "PM4_IM_LOAD_IMMEDIATE";
    case PM4_INVALIDATE_STATE:
      return "PM4_INVALIDATE_STATE";
    case PM4_VIZ_QUERY:
      return "PM4_VIZ_QUERY";
    case PM4_SET_BIN_MASK_LO:
      return "PM4_SET_BIN_MASK_LO";
    case PM4_SET_BIN_MASK_HI:
      return "PM4_SET_BIN_MASK_HI";
    case PM4_SET_BIN_SELECT_LO:
      return "PM4_SET_BIN_SELECT_LO";
    case PM4_SET_BIN_SELECT_HI:
      return "PM4_SET_BIN_SELECT_HI";
    case PM4_SET_BIN_MASK:
      return "PM4_SET_BIN_MASK";
    case PM4_SET_BIN_SELECT:
      return "PM4_SET_BIN_SELECT";
    case PM4_CONTEXT_UPDATE:
      return "PM4_CONTEXT_UPDATE";
    case PM4_WAIT_FOR_IDLE:
      return "PM4_WAIT_FOR_IDLE";
    default:
      return "PM4_UNKNOWN";
  }
}

}  // namespace

CommandProcessor::CommandProcessor(GraphicsSystem* graphics_system,
                                   kernel::KernelState* kernel_state)
    : memory_(graphics_system->memory()),
      kernel_state_(kernel_state),
      graphics_system_(graphics_system),
      register_file_(graphics_system_->register_file()),
      trace_writer_(graphics_system->memory()->physical_membase()),
      worker_running_(true),
      write_ptr_index_event_(xe::threading::Event::CreateAutoResetEvent(false)),
      write_ptr_index_(0) {
  assert_not_null(write_ptr_index_event_);
}

CommandProcessor::~CommandProcessor() = default;

bool CommandProcessor::Initialize() {
  // Initialize the gamma ramps to their default (linear) values - taken from
  // what games set when starting with the sRGB (return value 1)
  // VdGetCurrentDisplayGamma.
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t value = i * 0x3FF / 0xFF;
    reg::DC_LUT_30_COLOR& gamma_ramp_entry = gamma_ramp_256_entry_table_[i];
    gamma_ramp_entry.color_10_blue = value;
    gamma_ramp_entry.color_10_green = value;
    gamma_ramp_entry.color_10_red = value;
  }
  for (uint32_t i = 0; i < 128; ++i) {
    reg::DC_LUT_PWL_DATA gamma_ramp_entry = {};
    gamma_ramp_entry.base = (i * 0xFFFF / 0x7F) & ~UINT32_C(0x3F);
    gamma_ramp_entry.delta = i < 0x7F ? 0x200 : 0;
    for (uint32_t j = 0; j < 3; ++j) {
      gamma_ramp_pwl_rgb_[i][j] = gamma_ramp_entry;
    }
  }

  worker_running_ = true;
  worker_thread_ = kernel::object_ref<kernel::XHostThread>(
      new kernel::XHostThread(kernel_state_, 128 * 1024, 0, [this]() {
        WorkerThreadMain();
        return 0;
      }));
  worker_thread_->set_name("GPU Commands");
  worker_thread_->Create();

  // Thor perf (roadmap R1): the command-processor worker is the single hot
  // critical-path thread (~197ms/frame of guest-JIT + per-draw submit). Pin it
  // to a chosen core so it stays on the prime Cortex-X3 (cpu7 @3.19GHz on the AYN
  // Thor) at max DVFS instead of floating onto a 2.0GHz A510. Affinity is a hint
  // only - no guest-visible effect; default -1 leaves scheduling to the OS.
  if (cvars::thor_gpu_thread_affinity_cpu >= 0 && worker_thread_->thread()) {
    worker_thread_->thread()->set_affinity_mask(
        uint64_t(1) << uint32_t(cvars::thor_gpu_thread_affinity_cpu));
  }

#if XE_PLATFORM_ANDROID
  // Thor dynamic hot-thread pin: keep whatever thread is busiest on the prime
  // core. Supersedes the static pin above for CPU/sync-bound titles where the
  // hot thread is a guest XThread, not the (fence-blocked) command worker.
  if (cvars::thor_hot_thread_prime_core >= 0) {
    hot_thread_pinner()->Start(cvars::thor_hot_thread_prime_core,
                               cvars::thor_hot_thread_interval_ms);
  }
#endif  // XE_PLATFORM_ANDROID

  return true;
}

void CommandProcessor::Shutdown() {
  EndTracing();

#if XE_PLATFORM_ANDROID
  hot_thread_pinner()->Stop();
  AdpfShutdown();
#endif  // XE_PLATFORM_ANDROID

  worker_running_ = false;
  write_ptr_index_event_->Set();
  worker_thread_->Wait(0, 0, 0, nullptr);
  worker_thread_.reset();
}

void CommandProcessor::InitializeShaderStorage(
    const std::filesystem::path& cache_root, uint32_t title_id, bool blocking) {
}

void CommandProcessor::RequestFrameTrace(
    const std::filesystem::path& root_path) {
  if (trace_state_ == TraceState::kStreaming) {
    XELOGE("Streaming trace; cannot also trace frame.");
    return;
  }
  if (trace_state_ == TraceState::kSingleFrame) {
    XELOGE("Frame trace already pending; ignoring.");
    return;
  }
  trace_state_ = TraceState::kSingleFrame;
  trace_frame_path_ = root_path;
}

void CommandProcessor::BeginTracing(const std::filesystem::path& root_path) {
  if (trace_state_ == TraceState::kStreaming) {
    XELOGE("Streaming already active; ignoring request.");
    return;
  }
  if (trace_state_ == TraceState::kSingleFrame) {
    XELOGE("Frame trace pending; ignoring streaming request.");
    return;
  }
  // Streaming starts on the next primary buffer execute.
  trace_state_ = TraceState::kStreaming;
  trace_stream_path_ = root_path;
}

void CommandProcessor::EndTracing() {
  if (!trace_writer_.is_open()) {
    return;
  }
  assert_true(trace_state_ == TraceState::kStreaming);
  trace_state_ = TraceState::kDisabled;
  trace_writer_.Close();
}

void CommandProcessor::RestoreRegisters(uint32_t first_register,
                                        const uint32_t* register_values,
                                        uint32_t register_count,
                                        bool execute_callbacks) {
  if (first_register > RegisterFile::kRegisterCount ||
      RegisterFile::kRegisterCount - first_register < register_count) {
    XELOGW(
        "CommandProcessor::RestoreRegisters out of bounds (0x{:X} registers "
        "starting with 0x{:X}, while a total of 0x{:X} registers are stored)",
        register_count, first_register, RegisterFile::kRegisterCount);
    if (first_register > RegisterFile::kRegisterCount) {
      return;
    }
    register_count =
        std::min(uint32_t(RegisterFile::kRegisterCount) - first_register,
                 register_count);
  }
  if (execute_callbacks) {
    for (uint32_t i = 0; i < register_count; ++i) {
      WriteRegister(first_register + i, register_values[i]);
    }
  } else {
    std::memcpy(register_file_->values + first_register, register_values,
                sizeof(uint32_t) * register_count);
  }
}

void CommandProcessor::RestoreGammaRamp(
    const reg::DC_LUT_30_COLOR* new_gamma_ramp_256_entry_table,
    const reg::DC_LUT_PWL_DATA* new_gamma_ramp_pwl_rgb,
    uint32_t new_gamma_ramp_rw_component) {
  std::memcpy(gamma_ramp_256_entry_table_, new_gamma_ramp_256_entry_table,
              sizeof(reg::DC_LUT_30_COLOR) * 256);
  std::memcpy(gamma_ramp_pwl_rgb_, new_gamma_ramp_pwl_rgb,
              sizeof(reg::DC_LUT_PWL_DATA) * 3 * 128);
  gamma_ramp_rw_component_ = new_gamma_ramp_rw_component;
  OnGammaRamp256EntryTableValueWritten();
  OnGammaRampPWLValueWritten();
}

void CommandProcessor::CallInThread(std::function<void()> fn) {
  if (pending_fns_.empty() &&
      kernel::XThread::IsInThread(worker_thread_.get())) {
    fn();
  } else {
    pending_fns_.push(std::move(fn));
  }
}

void CommandProcessor::ClearCaches() {}

void CommandProcessor::SetDesiredSwapPostEffect(
    SwapPostEffect swap_post_effect) {
  if (swap_post_effect_desired_ == swap_post_effect) {
    return;
  }
  swap_post_effect_desired_ = swap_post_effect;
  CallInThread([this, swap_post_effect]() {
    swap_post_effect_actual_ = swap_post_effect;
  });
}

void CommandProcessor::WorkerThreadMain() {
#if XE_PLATFORM_ANDROID
  // Thor perf: raise this GPU-command worker's scheduling priority (nice) so the
  // OS keeps it running under guest-thread CPU contention. Otherwise busy guest
  // threads deschedule it and the GPU starves waiting on command submission
  // (measured ~16ms/frame GPU-idle on BTTF, busy 66%). Android forbids SCHED_FIFO
  // (set_priority EPERMs) but lets an app lower its own thread's nice (audio
  // threads use -19). Global-SAFE alternative to pinning the X3
  // (thor_gpu_thread_affinity_cpu) - it does NOT steal a core from the guest, so
  // it should not regress CPU-bound titles. 0 = no change (default).
  if (cvars::gpu_cp_worker_nice != 0) {
    const id_t cp_tid = static_cast<id_t>(syscall(SYS_gettid));
    if (setpriority(PRIO_PROCESS, cp_tid, cvars::gpu_cp_worker_nice) == -1) {
      XELOGW("CommandProcessor: setpriority(GPU worker nice={}) failed",
             int32_t(cvars::gpu_cp_worker_nice));
    } else {
      XELOGI("CommandProcessor: GPU-command worker nice set to {}",
             int32_t(cvars::gpu_cp_worker_nice));
    }
  }
#endif  // XE_PLATFORM_ANDROID
  if (!SetupContext()) {
    xe::FatalError("Unable to setup command processor internal state");
    return;
  }

  while (worker_running_) {
    while (!pending_fns_.empty()) {
      auto fn = std::move(pending_fns_.front());
      pending_fns_.pop();
      fn();
    }

    uint32_t write_ptr_index = write_ptr_index_.load();
    if (write_ptr_index == 0xBAADF00D || read_ptr_index_ == write_ptr_index) {
      SCOPE_profile_cpu_i("gpu", "xe::gpu::CommandProcessor::Stall");
      // We've run out of commands to execute.
      // We spin here waiting for new ones, as the overhead of waiting on our
      // event is too high.
      PrepareForWait();
      uint32_t loop_count = 0;
      do {
        // If we spin around too much, revert to a "low-power" state.
        if (loop_count > 500) {
          const int wait_time_ms = 5;
          xe::threading::Wait(write_ptr_index_event_.get(), true,
                              std::chrono::milliseconds(wait_time_ms));
        }

        xe::threading::MaybeYield();
        loop_count++;
        write_ptr_index = write_ptr_index_.load();
      } while (worker_running_ && pending_fns_.empty() &&
               (write_ptr_index == 0xBAADF00D ||
                read_ptr_index_ == write_ptr_index));
      ReturnFromWait();
      if (!worker_running_ || !pending_fns_.empty()) {
        continue;
      }
    }
    assert_true(read_ptr_index_ != write_ptr_index);

    // Execute. Note that we handle wraparound transparently.
    if (cvars::gpu_trace_swap) {
      XELOGI(
          "GPU swap trace: CommandProcessor ExecutePrimaryBuffer begin "
          "read_ptr={:08X} write_ptr={:08X} primary_ptr={:08X}",
          read_ptr_index_, write_ptr_index, primary_buffer_ptr_);
    }
    read_ptr_index_ = ExecutePrimaryBuffer(read_ptr_index_, write_ptr_index);
    if (cvars::gpu_trace_swap) {
      XELOGI(
          "GPU swap trace: CommandProcessor ExecutePrimaryBuffer end "
          "read_ptr={:08X} write_ptr={:08X}",
          read_ptr_index_, write_ptr_index);
    }
    UpdatePrimaryReadPointer(read_ptr_index_, "primary_buffer_end");

    if (cvars::gpu_interrupt_on_ring_idle) {
      if (ShouldTraceGpuInterruptPacket()) {
        XELOGI(
            "GPU interrupt trace: ring-idle source=1 dispatch "
            "read_ptr={:08X} write_ptr={:08X} primary_ptr={:08X}",
            read_ptr_index_, write_ptr_index, primary_buffer_ptr_);
      }
      graphics_system_->DispatchInterruptCallback(1, 2);
    }

    // FIXME: We're supposed to process the WAIT_UNTIL register at this point,
    // but no games seem to actually use it.
  }

  ShutdownContext();
}

void CommandProcessor::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  threading::Fence fence;
  CallInThread([&fence]() {
    fence.Signal();
    threading::Thread::GetCurrentThread()->Suspend();
  });

  fence.Wait();
}

void CommandProcessor::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;

  worker_thread_->thread()->Resume();
}

bool CommandProcessor::Save(ByteStream* stream) {
  assert_true(paused_);

  stream->Write<uint32_t>(primary_buffer_ptr_);
  stream->Write<uint32_t>(primary_buffer_size_);
  stream->Write<uint32_t>(read_ptr_index_);
  stream->Write<uint32_t>(read_ptr_update_freq_);
  stream->Write<uint32_t>(read_ptr_writeback_ptr_);
  stream->Write<uint32_t>(write_ptr_index_.load());

  return true;
}

bool CommandProcessor::Restore(ByteStream* stream) {
  assert_true(paused_);

  primary_buffer_ptr_ = stream->Read<uint32_t>();
  primary_buffer_size_ = stream->Read<uint32_t>();
  read_ptr_index_ = stream->Read<uint32_t>();
  read_ptr_update_freq_ = stream->Read<uint32_t>();
  read_ptr_writeback_ptr_ = stream->Read<uint32_t>();
  write_ptr_index_.store(stream->Read<uint32_t>());
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbRptrRegister]) = read_ptr_index_;
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbWptrRegister]) = write_ptr_index_.load();

  return true;
}

bool CommandProcessor::SetupContext() { return true; }

void CommandProcessor::ShutdownContext() {}

void CommandProcessor::InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) {
  read_ptr_index_ = 0;
  primary_buffer_ptr_ = ptr;
  primary_buffer_size_ = uint32_t(1) << (size_log2 + 3);
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: CommandProcessor InitializeRingBuffer ptr={:08X} "
        "size_log2={} primary_size={} current_wptr={:08X}",
        primary_buffer_ptr_, size_log2, primary_buffer_size_,
        write_ptr_index_.load());
  }
  std::memset(kernel_state_->memory()->TranslatePhysical(primary_buffer_ptr_),
              0, primary_buffer_size_);
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbRptrRegister]) = read_ptr_index_;
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbWptrRegister]) = write_ptr_index_.load();
}

void CommandProcessor::EnableReadPointerWriteBack(uint32_t ptr,
                                                  uint32_t block_size_log2) {
  // CP_RB_RPTR_ADDR Ring Buffer Read Pointer Address 0x70C
  // ptr = RB_RPTR_ADDR, pointer to write back the address to.
  read_ptr_writeback_ptr_ = ptr;
  // CP_RB_CNTL Ring Buffer Control 0x704
  // block_size = RB_BLKSZ, log2 of number of quadwords read between updates of
  //              the read pointer.
  read_ptr_update_freq_ = uint32_t(1) << block_size_log2 >> 2;
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: CommandProcessor EnableReadPointerWriteBack "
        "ptr={:08X} block_size_log2={} update_freq={}",
        read_ptr_writeback_ptr_, block_size_log2, read_ptr_update_freq_);
  }
}

void CommandProcessor::UpdateWritePointer(uint32_t value) {
  uint32_t old_value = write_ptr_index_.load();
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: CommandProcessor UpdateWritePointer {:08X}->{:08X} "
        "read_ptr={:08X} primary_ptr={:08X} primary_size={}",
        old_value, value, read_ptr_index_, primary_buffer_ptr_,
        primary_buffer_size_);
  }
  write_ptr_index_ = value;
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbWptrRegister]) = value;
  write_ptr_index_event_->Set();
}

void CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  RegisterFile& regs = *register_file_;
  if (index >= RegisterFile::kRegisterCount) {
    XELOGW("CommandProcessor::WriteRegister index out of bounds: {}", index);
    return;
  }

  // GPU D3D-HLE at the register (intent) level. BD's tiling "intent" (BeginTiling /
  // Resolve) is written as RB_SURFACE_INFO + sibling surface/copy registers from
  // inside the guest indirect buffers - there is no higher guest-function boundary
  // (the replay only emits IB pointers). So we recognize + rewrite the intent HERE:
  // host-side, no reentrancy, every related register visible. Gated, default-off.
  if (cvars::gpu_hle_surface_trace &&
      (index == 0x2000 || index == 0x2001 || index == 0x2002 || index == 0x2208 ||
       index == 0x2081 || index == 0x2082 || index == 0x2318 || index == 0x231B)) {
    static std::atomic<int> sfx{0};
    if (sfx.fetch_add(1) < 120) {
      XELOGI("HLE SURFACE reg {:04X} = {:08X}", index, value);
    }
  }
  // Dedicated RESOLVE (RB_COPY_*) trace - higher cap so the FIELD's resolves survive
  // past the boot flood. RB_COPY_CONTROL(2318 = the resolve trigger) + DEST_BASE(2319)
  // + DEST_PITCH(231A) + DEST_INFO(231B) = the 2-strip resolve to collapse to one 720.
  if (cvars::gpu_hle_surface_trace &&
      (index == 0x2318 || index == 0x2319 || index == 0x231A || index == 0x231B)) {
    static std::atomic<int> cfx{0};
    if (cfx.fetch_add(1) < 400) {
      XELOGI("HLE COPY reg {:04X} = {:08X}", index, value);
    }
  }
  if (cvars::gpu_hle_surface_binonce &&
      index == XE_GPU_REG_RB_SURFACE_INFO) {
    uint32_t pitch = value & 0x3FFFu;  // surface_pitch : 14 (pixels)
    if (pitch >= 32u && pitch <= 512u) {  // a tile STRIP, not the full surface
      value = (value & ~0x3FFFu) | ((pitch * 2u) & 0x3FFFu);
      static std::atomic<int> pfx{0};
      if (pfx.fetch_add(1) < 20) {
        XELOGI("HLE BINONCE: RB_SURFACE_INFO pitch {}->{}", pitch, pitch * 2u);
      }
    }
  }
  // Volatile for the WAIT_REG_MEM loop.
  const_cast<volatile uint32_t&>(regs.values[index]) = value;
  // GetRegisterInfo is a large switch executed on EVERY register write, and the
  // guest issues a huge volume of these (~10,600 draws/frame in Blue Dragon).
  // It is only needed to gate the unknown-register warning. When that logging is
  // suppressed (budget == 0) skip the lookup entirely. With logging enabled the
  // behavior is unchanged: the lookup runs, and the rate-limit budget is only
  // consumed (atomic fetch_add) for actually-unknown registers as before.
  // (Minor win - only removes the per-write lookup when budget==0; the cost is
  // inherent to the unknown-register check otherwise. The dominant per-frame
  // cost is the sheer draw volume, not this.)
  if (cvars::gpu_unknown_register_log_budget != 0 &&
      !regs.GetRegisterInfo(index) &&
      ShouldLogUnknownGpuRegister(gpu_unknown_register_write_log_count)) {
    XELOGW("GPU: Write to unknown register ({:04X} = {:08X})", index, value);
  }

  // Scratch register writeback.
  if (index >= XE_GPU_REG_SCRATCH_REG0 && index <= XE_GPU_REG_SCRATCH_REG7) {
    uint32_t scratch_reg = index - XE_GPU_REG_SCRATCH_REG0;
    if ((1 << scratch_reg) & regs.values[XE_GPU_REG_SCRATCH_UMSK]) {
      // Enabled - write to address.
      uint32_t scratch_addr = regs.values[XE_GPU_REG_SCRATCH_ADDR];
      uint32_t mem_addr = scratch_addr + (scratch_reg * 4);
      xe::store_and_swap<uint32_t>(memory_->TranslatePhysical(mem_addr), value);
    }
  } else {
    switch (index) {
      // If this is a COHER register, set the dirty flag.
      // This will block the command processor the next time it WAIT_REG_MEMs
      // and allow us to synchronize the memory.
      case XE_GPU_REG_COHER_STATUS_HOST: {
        const_cast<volatile uint32_t&>(regs.values[index]) |=
            UINT32_C(0x80000000);
      } break;

      case XE_GPU_REG_DC_LUT_RW_INDEX: {
        // Reset the sequential read / write component index (see the M56
        // DC_LUT_SEQ_COLOR documentation).
        gamma_ramp_rw_component_ = 0;
      } break;

      case XE_GPU_REG_DC_LUT_SEQ_COLOR: {
        // Should be in the 256-entry table writing mode.
        assert_zero(regs[XE_GPU_REG_DC_LUT_RW_MODE] & 0b1);
        auto gamma_ramp_rw_index = regs.Get<reg::DC_LUT_RW_INDEX>();
        // DC_LUT_SEQ_COLOR is in the red, green, blue order, but the write
        // enable mask is blue, green, red.
        bool write_gamma_ramp_component =
            (regs[XE_GPU_REG_DC_LUT_WRITE_EN_MASK] &
             (UINT32_C(1) << (2 - gamma_ramp_rw_component_))) != 0;
        if (write_gamma_ramp_component) {
          reg::DC_LUT_30_COLOR& gamma_ramp_entry =
              gamma_ramp_256_entry_table_[gamma_ramp_rw_index.rw_index];
          // Bits 0:5 are hardwired to zero.
          uint32_t gamma_ramp_seq_color =
              regs.Get<reg::DC_LUT_SEQ_COLOR>().seq_color >> 6;
          switch (gamma_ramp_rw_component_) {
            case 0:
              gamma_ramp_entry.color_10_red = gamma_ramp_seq_color;
              break;
            case 1:
              gamma_ramp_entry.color_10_green = gamma_ramp_seq_color;
              break;
            case 2:
              gamma_ramp_entry.color_10_blue = gamma_ramp_seq_color;
              break;
          }
        }
        if (++gamma_ramp_rw_component_ >= 3) {
          gamma_ramp_rw_component_ = 0;
          reg::DC_LUT_RW_INDEX new_gamma_ramp_rw_index = gamma_ramp_rw_index;
          ++new_gamma_ramp_rw_index.rw_index;
          WriteRegister(
              XE_GPU_REG_DC_LUT_RW_INDEX,
              xe::memory::Reinterpret<uint32_t>(new_gamma_ramp_rw_index));
        }
        if (write_gamma_ramp_component) {
          OnGammaRamp256EntryTableValueWritten();
        }
      } break;

      case XE_GPU_REG_DC_LUT_PWL_DATA: {
        // Should be in the PWL writing mode.
        assert_not_zero(regs[XE_GPU_REG_DC_LUT_RW_MODE] & 0b1);
        auto gamma_ramp_rw_index = regs.Get<reg::DC_LUT_RW_INDEX>();
        // Bit 7 of the index is ignored for PWL.
        uint32_t gamma_ramp_rw_index_pwl = gamma_ramp_rw_index.rw_index & 0x7F;
        // DC_LUT_PWL_DATA is likely in the red, green, blue order because
        // DC_LUT_SEQ_COLOR is, but the write enable mask is blue, green, red.
        bool write_gamma_ramp_component =
            (regs[XE_GPU_REG_DC_LUT_WRITE_EN_MASK] &
             (UINT32_C(1) << (2 - gamma_ramp_rw_component_))) != 0;
        if (write_gamma_ramp_component) {
          reg::DC_LUT_PWL_DATA& gamma_ramp_entry =
              gamma_ramp_pwl_rgb_[gamma_ramp_rw_index_pwl]
                                 [gamma_ramp_rw_component_];
          auto gamma_ramp_value = regs.Get<reg::DC_LUT_PWL_DATA>();
          // Bits 0:5 are hardwired to zero.
          gamma_ramp_entry.base = gamma_ramp_value.base & ~UINT32_C(0x3F);
          gamma_ramp_entry.delta = gamma_ramp_value.delta & ~UINT32_C(0x3F);
        }
        if (++gamma_ramp_rw_component_ >= 3) {
          gamma_ramp_rw_component_ = 0;
          reg::DC_LUT_RW_INDEX new_gamma_ramp_rw_index = gamma_ramp_rw_index;
          // TODO(Triang3l): Should this increase beyond 7 bits for PWL?
          // Direct3D 9 explicitly sets rw_index to 0x80 after writing the last
          // PWL entry. However, the DC_LUT_RW_INDEX documentation says that for
          // PWL, the bit 7 is ignored.
          new_gamma_ramp_rw_index.rw_index =
              (gamma_ramp_rw_index.rw_index & ~UINT32_C(0x7F)) |
              ((gamma_ramp_rw_index_pwl + 1) & 0x7F);
          WriteRegister(
              XE_GPU_REG_DC_LUT_RW_INDEX,
              xe::memory::Reinterpret<uint32_t>(new_gamma_ramp_rw_index));
        }
        if (write_gamma_ramp_component) {
          OnGammaRampPWLValueWritten();
        }
      } break;

      case XE_GPU_REG_DC_LUT_30_COLOR: {
        // Should be in the 256-entry table writing mode.
        assert_zero(regs[XE_GPU_REG_DC_LUT_RW_MODE] & 0b1);
        auto gamma_ramp_rw_index = regs.Get<reg::DC_LUT_RW_INDEX>();
        uint32_t gamma_ramp_write_enable_mask =
            regs[XE_GPU_REG_DC_LUT_WRITE_EN_MASK] & 0b111;
        if (gamma_ramp_write_enable_mask) {
          reg::DC_LUT_30_COLOR& gamma_ramp_entry =
              gamma_ramp_256_entry_table_[gamma_ramp_rw_index.rw_index];
          auto gamma_ramp_value = regs.Get<reg::DC_LUT_30_COLOR>();
          if (gamma_ramp_write_enable_mask & 0b001) {
            gamma_ramp_entry.color_10_blue = gamma_ramp_value.color_10_blue;
          }
          if (gamma_ramp_write_enable_mask & 0b010) {
            gamma_ramp_entry.color_10_green = gamma_ramp_value.color_10_green;
          }
          if (gamma_ramp_write_enable_mask & 0b100) {
            gamma_ramp_entry.color_10_red = gamma_ramp_value.color_10_red;
          }
        }
        // TODO(Triang3l): Should this reset the component write index? If this
        // increase is assumed to behave like a full DC_LUT_RW_INDEX write, it
        // probably should. Currently this also calls WriteRegister for
        // DC_LUT_RW_INDEX, which resets gamma_ramp_rw_component_ as well.
        gamma_ramp_rw_component_ = 0;
        reg::DC_LUT_RW_INDEX new_gamma_ramp_rw_index = gamma_ramp_rw_index;
        ++new_gamma_ramp_rw_index.rw_index;
        WriteRegister(
            XE_GPU_REG_DC_LUT_RW_INDEX,
            xe::memory::Reinterpret<uint32_t>(new_gamma_ramp_rw_index));
        if (gamma_ramp_write_enable_mask) {
          OnGammaRamp256EntryTableValueWritten();
        }
      } break;
    }
  }
}

void CommandProcessor::MakeCoherent() {
  SCOPE_profile_cpu_f("gpu");

  // Status host often has 0x01000000 or 0x03000000.
  // This is likely toggling VC (vertex cache) or TC (texture cache).
  // Or, it also has a direction in here maybe - there is probably
  // some way to check for dest coherency (what all the COHER_DEST_BASE_*
  // registers are for).
  // Best docs I've found on this are here:
  // https://web.archive.org/web/20160711162346/https://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2013/10/R6xx_R7xx_3D.pdf
  // https://cgit.freedesktop.org/xorg/driver/xf86-video-radeonhd/tree/src/r6xx_accel.c?id=3f8b6eccd9dba116cc4801e7f80ce21a879c67d2#n454

  // Volatile because this may be called from the WAIT_REG_MEM loop.
  volatile uint32_t* regs_volatile = register_file_->values;
  auto status_host = xe::memory::Reinterpret<reg::COHER_STATUS_HOST>(
      uint32_t(regs_volatile[XE_GPU_REG_COHER_STATUS_HOST]));
  uint32_t base_host = regs_volatile[XE_GPU_REG_COHER_BASE_HOST];
  uint32_t size_host = regs_volatile[XE_GPU_REG_COHER_SIZE_HOST];

  if (!status_host.status) {
    return;
  }

  const char* action = "N/A";
  if (status_host.vc_action_ena && status_host.tc_action_ena) {
    action = "VC | TC";
  } else if (status_host.tc_action_ena) {
    action = "TC";
  } else if (status_host.vc_action_ena) {
    action = "VC";
  }

  // TODO(benvanik): notify resource cache of base->size and type.
  XELOGD("Make {:08X} -> {:08X} ({}b) coherent, action = {}", base_host,
         base_host + size_host, size_host, action);

  // Mark coherent.
  regs_volatile[XE_GPU_REG_COHER_STATUS_HOST] = 0;
}

void CommandProcessor::PrepareForWait() { trace_writer_.Flush(); }

void CommandProcessor::ReturnFromWait() {}

uint32_t CommandProcessor::ExecutePrimaryBuffer(uint32_t read_index,
                                                uint32_t write_index) {
  SCOPE_profile_cpu_f("gpu");

  // If we have a pending trace stream open it now. That way we ensure we get
  // all commands.
  if (!trace_writer_.is_open() && trace_state_ == TraceState::kStreaming) {
    uint32_t title_id = kernel_state_->GetExecutableModule()
                            ? kernel_state_->GetExecutableModule()->title_id()
                            : 0;
    auto file_name = fmt::format("{:08X}_stream.xtr", title_id);
    auto path = trace_stream_path_ / file_name;
    trace_writer_.Open(path, title_id);
    InitializeTrace();
  }

  // Adjust pointer base.
  uint32_t start_ptr = primary_buffer_ptr_ + read_index * sizeof(uint32_t);
  start_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (start_ptr & 0x1FFFFFFF);
  uint32_t end_ptr = primary_buffer_ptr_ + write_index * sizeof(uint32_t);
  end_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (end_ptr & 0x1FFFFFFF);

  trace_writer_.WritePrimaryBufferStart(start_ptr, write_index - read_index);

  // Execute commands!
  RingBuffer reader(memory_->TranslatePhysical(primary_buffer_ptr_),
                    primary_buffer_size_);
  reader.set_read_offset(read_index * sizeof(uint32_t));
  reader.set_write_offset(write_index * sizeof(uint32_t));
  do {
    if (!ExecutePacket(&reader)) {
      // This probably should be fatal - but we're going to continue anyways.
      XELOGE("**** PRIMARY RINGBUFFER: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader.read_count());

  OnPrimaryBufferEnd();

  trace_writer_.WritePrimaryBufferEnd();

  return write_index;
}

void CommandProcessor::UpdatePrimaryReadPointer(uint32_t read_index,
                                                const char* reason) {
  read_ptr_index_ = read_index;
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbRptrRegister]) = read_ptr_index_;

  // TODO(benvanik): use reader->Read_update_freq_ and only issue after moving
  //     that many indices.
  if (read_ptr_writeback_ptr_) {
    xe::store_and_swap<uint32_t>(
        memory_->TranslatePhysical(read_ptr_writeback_ptr_), read_ptr_index_);
  }
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: CommandProcessor read pointer writeback "
        "read_ptr={:08X} ptr={:08X} reason={}",
        read_ptr_index_, read_ptr_writeback_ptr_, reason ? reason : "");
  }
}

void CommandProcessor::ExecuteIndirectBuffer(uint32_t ptr, uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // PAGE-WATCH fallback (cpu_watch_guest_write_page): when the TileWalker HLE
  // handler doesn't fire (title/menu, no predicated tiling), protect the watched
  // guest page here instead so the CPU backend can log the guest IB-recorder.
  // Indirect buffers are executed for ALL 3D draws, so this fires even at the
  // title screen. The cvar value is the FULL guest-virtual address the CPU writes
  // through (window bits included, e.g. 0xBF6xxxxx = 0xA0000000 physical window);
  // we protect that virtual alias ONCE. The guest keeps re-recording into the IB
  // ring, so the page is re-written (and re-faulted) on the next ring wrap.
  if (cvars::cpu_watch_guest_write_page && memory_) {
    static std::atomic<bool> pw_protected{false};
    bool expected = false;
    if (pw_protected.compare_exchange_strong(expected, true)) {
      size_t ps = xe::memory::page_size();
      uint32_t va_page = cvars::cpu_watch_guest_write_page & ~uint32_t(ps - 1);
      uint8_t* base = memory_->virtual_membase();
      xe::memory::Protect(base + va_page, ps, xe::memory::PageAccess::kReadOnly);
      XELOGI(
          "PAGE_WATCH: protected {:08X} read-only via ExecuteIndirectBuffer "
          "(first IB ptr={:08X} count={})",
          va_page, ptr, count);
    }
  }

  trace_writer_.WriteIndirectBufferStart(ptr, count * sizeof(uint32_t));
  if (cvars::gpu_trace_swap) {
    uint32_t* dwords =
        reinterpret_cast<uint32_t*>(memory_->TranslatePhysical(ptr));
    uint32_t dword_0 = count > 0 ? xe::byte_swap(dwords[0]) : 0;
    uint32_t dword_1 = count > 1 ? xe::byte_swap(dwords[1]) : 0;
    uint32_t dword_2 = count > 2 ? xe::byte_swap(dwords[2]) : 0;
    uint32_t dword_3 = count > 3 ? xe::byte_swap(dwords[3]) : 0;
    XELOGI(
        "GPU swap trace: ExecuteIndirectBuffer begin ptr={:08X} "
        "count={} first={:08X},{:08X},{:08X},{:08X}",
        ptr, count, dword_0, dword_1, dword_2, dword_3);
  }

  // Execute commands!
  RingBuffer reader(memory_->TranslatePhysical(ptr), count * sizeof(uint32_t));
  reader.set_write_offset(count * sizeof(uint32_t));
  do {
    if (!ExecutePacket(&reader)) {
      // Return up a level if we encounter a bad packet.
      XELOGE("**** INDIRECT RINGBUFFER: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader.read_count());

  trace_writer_.WriteIndirectBufferEnd();
  if (cvars::gpu_trace_swap) {
    XELOGI("GPU swap trace: ExecuteIndirectBuffer end ptr={:08X} count={}",
           ptr, count);
  }
}

void CommandProcessor::ExecutePacket(uint32_t ptr, uint32_t count) {
  // Execute commands!
  RingBuffer reader(memory_->TranslatePhysical(ptr), count * sizeof(uint32_t));
  reader.set_write_offset(count * sizeof(uint32_t));
  do {
    if (!ExecutePacket(&reader)) {
      XELOGE("**** ExecutePacket: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader.read_count());
}

bool CommandProcessor::ExecutePacket(RingBuffer* reader) {
  const uint32_t packet = reader->ReadAndSwap<uint32_t>();
  const uint32_t packet_type = packet >> 30;
  const uint32_t packet_ptr = uint32_t(reader->read_ptr() - 4);
  if (packet_type != 3 && ShouldTraceGpuPacket()) {
    XELOGI(
        "GPU swap trace: PM4 packet ptr={:08X} type={} word={:08X} "
        "remaining_bytes={}",
        packet_ptr, packet_type, packet, reader->read_count());
  }
  if (packet == 0) {
    trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1);
    trace_writer_.WritePacketEnd();
    return true;
  }

  if (packet == 0xCDCDCDCD) {
    XELOGW("GPU packet is CDCDCDCD - probably read uninitialized memory!");
  }

  switch (packet_type) {
    case 0x00:
      return ExecutePacketType0(reader, packet);
    case 0x01:
      return ExecutePacketType1(reader, packet);
    case 0x02:
      return ExecutePacketType2(reader, packet);
    case 0x03:
      return ExecutePacketType3(reader, packet);
    default:
      assert_unhandled_case(packet_type);
      return false;
  }
}

bool CommandProcessor::ExecutePacketType0(RingBuffer* reader, uint32_t packet) {
  // Type-0 packet.
  // Write count registers in sequence to the registers starting at
  // (base_index << 2).

  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  if (reader->read_count() < count * sizeof(uint32_t)) {
    XELOGE(
        "ExecutePacketType0 overflow (read count {:08X}, packet count {:08X})",
        reader->read_count(), count * sizeof(uint32_t));
    return false;
  }

  trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1 + count);

  uint32_t base_index = (packet & 0x7FFF);
  uint32_t write_one_reg = (packet >> 15) & 0x1;
  if (cvars::gpu_bulk_pm4_type0 && !write_one_reg) {
    // Hot path for draw-heavy guests (e.g. Blue Dragon, ~10k draws/frame): the
    // contiguous register run dominates. Byte-swap the whole dword run at once
    // with the NEON-vectorized copy_and_swap (vqtbl, 4 dwords/iter on ARM64)
    // instead of paying the per-dword ReadAndSwap overhead. WriteRegister still
    // runs per register so all per-register side effects are preserved (scratch
    // writeback, COHER dirty, and the Vulkan override's constant/texture
    // invalidation).
    uint32_t swapped[256];
    uint32_t remaining = count;
    uint32_t reg = base_index;
    while (remaining) {
      uint32_t chunk = std::min<uint32_t>(remaining, 256);
      size_t chunk_bytes = chunk * sizeof(uint32_t);
      if (reader->read_offset() + chunk_bytes <= reader->capacity()) {
        // No ring wrap: read the contiguous dword block directly and bulk-swap.
        xe::copy_and_swap_32_unaligned(
            swapped, reinterpret_cast<const void*>(reader->read_ptr()), chunk);
        reader->AdvanceRead(chunk_bytes);
      } else {
        // Block straddles the ring tail; fall back to per-dword read for it.
        for (uint32_t m = 0; m < chunk; ++m) {
          swapped[m] = reader->ReadAndSwap<uint32_t>();
        }
      }
      for (uint32_t m = 0; m < chunk; ++m) {
        WriteRegister(reg + m, swapped[m]);
      }
      reg += chunk;
      remaining -= chunk;
    }
  } else {
    for (uint32_t m = 0; m < count; m++) {
      uint32_t reg_data = reader->ReadAndSwap<uint32_t>();
      uint32_t target_index = write_one_reg ? base_index : base_index + m;
      WriteRegister(target_index, reg_data);
    }
  }

  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType1(RingBuffer* reader, uint32_t packet) {
  // Type-1 packet.
  // Contains two registers of data. Type-0 should be more common.
  trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 3);
  uint32_t reg_index_1 = packet & 0x7FF;
  uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
  uint32_t reg_data_1 = reader->ReadAndSwap<uint32_t>();
  uint32_t reg_data_2 = reader->ReadAndSwap<uint32_t>();
  WriteRegister(reg_index_1, reg_data_1);
  WriteRegister(reg_index_2, reg_data_2);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType2(RingBuffer* reader, uint32_t packet) {
  // Type-2 packet.
  // No-op. Do nothing.
  trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType3(RingBuffer* reader, uint32_t packet) {
  // Type-3 packet.
  uint32_t opcode = (packet >> 8) & 0x7F;
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  auto data_start_offset = reader->read_offset();
  if (ShouldTraceGpuPacket()) {
    XELOGI(
        "GPU swap trace: PM4 packet ptr={:08X} type=3 opcode={}({:02X}) "
        "count={} predicated={} word={:08X} remaining_bytes={}",
        uint32_t(reader->read_ptr() - 4), Type3OpcodeName(opcode), opcode,
        count, (packet & 1) != 0, packet, reader->read_count());
  }

  if (reader->read_count() < count * sizeof(uint32_t)) {
    XELOGE(
        "ExecutePacketType3 overflow (read count {:08X}, packet count {:08X})",
        reader->read_count(), count * sizeof(uint32_t));
    return false;
  }

  // To handle nesting behavior when tracing we special case indirect buffers.
  if (opcode == PM4_INDIRECT_BUFFER) {
    trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 2);
  } else {
    trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1 + count);
  }

  // & 1 == predicate - when set, we do bin check to see if we should execute
  // the packet. Only type 3 packets are affected.
  // We also skip predicated swaps, as they are never valid (probably?).
  if (packet & 1) {
    bool any_pass = (bin_select_ & bin_mask_) != 0;
    // gpu_flatten_predicated_tiling: the guest re-submits its scene once per
    // EDRAM tile with predication culling draws per tile - needless on the
    // host where the whole frame fits one RT. Pass indices per frame (BD
    // device-measured): 1 = setup (select FFFFFFFF, ~51 packets), 2 = the
    // FIRST content tile (must render EVERYTHING - stage 2 widens its
    // scissor), >= 3 = tile replays whose predicated draws are dropped
    // (state writes / resolves still run). The original <=1 threshold
    // dropped BOTH content tiles: mostly-empty 11.7ms frames + a guest D3D
    // bookkeeping crash (PC 0x8246DD88) after ~150s.
    if (cvars::gpu_flatten_predicated_tiling) {
      // Only touch packets INSIDE a tile pass (select = specific tile bits).
      // The all-ones select is the outside-tiling state - BD restores it after
      // the tiled scene and the post-process/present chain runs under it;
      // dropping those draws blacked the whole screen (measured: scene rendered
      // at 82% busy but nothing reached the frontbuffer).
      bool in_tile_pass =
          (bin_select_ & 0xFFFFFFFFull) != 0xFFFFFFFFull && bin_select_ != 0;
      if (cvars::gpu_flatten_predicated_tiling_widen) {
        // MERGE mode (widen): BD PARTITIONS its scene across heterogeneous
        // content passes (device-decoded: pass A = 8 draws @ pitch360/msaa2,
        // pass B = 26 draws @ pitch720/msaa1, win_off -608) - NOT one scene
        // re-submitted per identical tile. So dropping later passes LOSES
        // content (the broken right-sliver render). Instead force-pass EVERY
        // content bin pass at true screen position into the one widened
        // full-surface render (offset-zeroed in UpdateFlattenResolveOffsetSkip).
        if (in_tile_pass && flatten_bin_passes_seen_ >= 2) {
          any_pass = true;
        }
      } else if (in_tile_pass && flatten_bin_passes_seen_ >= 3 &&
                 (opcode == PM4_DRAW_INDX || opcode == PM4_DRAW_INDX_2)) {
        if (any_pass) {
          ++flatten_dropped_draws_;
        }
        any_pass = false;
      } else if (in_tile_pass && flatten_bin_passes_seen_ == 2) {
        any_pass = true;
      }
    }
    // gpu_trace_bin_select: count predicated packet outcomes per bin_select so
    // the guest's EDRAM predicated-tiling replay pattern is visible (how many
    // predicated packets pass/skip under each tile's select value).
    if (cvars::gpu_trace_bin_select) {
      if (any_pass) {
        ++bin_trace_pass_count_;
      } else {
        ++bin_trace_skip_count_;
      }
    }
    if (!any_pass || opcode == PM4_XE_SWAP) {
      reader->AdvanceRead(count * sizeof(uint32_t));
      trace_writer_.WritePacketEnd();
      return true;
    }
  }

  bool result = false;
  switch (opcode) {
    case PM4_ME_INIT:
      result = ExecutePacketType3_ME_INIT(reader, packet, count);
      break;
    case PM4_NOP:
      result = ExecutePacketType3_NOP(reader, packet, count);
      break;
    case PM4_INTERRUPT:
      result = ExecutePacketType3_INTERRUPT(reader, packet, count);
      break;
    case PM4_XE_SWAP:
      result = ExecutePacketType3_XE_SWAP(reader, packet, count);
      break;
    case PM4_INDIRECT_BUFFER:
    case PM4_INDIRECT_BUFFER_PFD:
      result = ExecutePacketType3_INDIRECT_BUFFER(reader, packet, count);
      break;
    case PM4_WAIT_REG_MEM:
      result = ExecutePacketType3_WAIT_REG_MEM(reader, packet, count);
      break;
    case PM4_REG_RMW:
      result = ExecutePacketType3_REG_RMW(reader, packet, count);
      break;
    case PM4_REG_TO_MEM:
      result = ExecutePacketType3_REG_TO_MEM(reader, packet, count);
      break;
    case PM4_MEM_WRITE:
      result = ExecutePacketType3_MEM_WRITE(reader, packet, count);
      break;
    case PM4_COND_WRITE:
      result = ExecutePacketType3_COND_WRITE(reader, packet, count);
      break;
    case PM4_EVENT_WRITE:
      result = ExecutePacketType3_EVENT_WRITE(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_SHD:
      result = ExecutePacketType3_EVENT_WRITE_SHD(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_EXT:
      result = ExecutePacketType3_EVENT_WRITE_EXT(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_ZPD:
      result = ExecutePacketType3_EVENT_WRITE_ZPD(reader, packet, count);
      break;
    case PM4_DRAW_INDX:
      result = ExecutePacketType3_DRAW_INDX(reader, packet, count);
      break;
    case PM4_DRAW_INDX_2:
      result = ExecutePacketType3_DRAW_INDX_2(reader, packet, count);
      break;
    case PM4_SET_CONSTANT:
      result = ExecutePacketType3_SET_CONSTANT(reader, packet, count);
      break;
    case PM4_SET_CONSTANT2:
      result = ExecutePacketType3_SET_CONSTANT2(reader, packet, count);
      break;
    case PM4_LOAD_ALU_CONSTANT:
      result = ExecutePacketType3_LOAD_ALU_CONSTANT(reader, packet, count);
      break;
    case PM4_SET_SHADER_CONSTANTS:
      result = ExecutePacketType3_SET_SHADER_CONSTANTS(reader, packet, count);
      break;
    case PM4_IM_LOAD:
      result = ExecutePacketType3_IM_LOAD(reader, packet, count);
      break;
    case PM4_IM_LOAD_IMMEDIATE:
      result = ExecutePacketType3_IM_LOAD_IMMEDIATE(reader, packet, count);
      break;
    case PM4_INVALIDATE_STATE:
      result = ExecutePacketType3_INVALIDATE_STATE(reader, packet, count);
      break;
    case PM4_VIZ_QUERY:
      result = ExecutePacketType3_VIZ_QUERY(reader, packet, count);
      break;

    case PM4_SET_BIN_MASK_LO: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_MASK_HI: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_mask_ =
          (bin_mask_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_LO: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | value;
      ++flatten_bin_passes_seen_;
      UpdateFlattenResolveOffsetSkip();
      if (cvars::gpu_trace_bin_select) {
        uint32_t win_off = (*register_file_)[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
        uint32_t sc_tl = (*register_file_)[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];
        uint32_t sc_br = (*register_file_)[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
        // EDRAM layout at the transition - decides whether scissor widening
        // can be layout-safe (pitch covers full width?) and where the tile's
        // color/depth allocations sit.
        uint32_t surf_info = (*register_file_)[XE_GPU_REG_RB_SURFACE_INFO];
        uint32_t color_info = (*register_file_)[XE_GPU_REG_RB_COLOR_INFO];
        uint32_t depth_info = (*register_file_)[XE_GPU_REG_RB_DEPTH_INFO];
        uint32_t mode_cntl = (*register_file_)[XE_GPU_REG_PA_SU_SC_MODE_CNTL];
        XELOGI(
            "BIN_SELECT_LO -> {:016X} (mask {:016X}); prev select: passed={} "
            "skipped={}; win_off={:08X} scissor_tl={:08X} br={:08X} "
            "surf={:08X} color={:08X} depth={:08X} sumode={:08X}",
            bin_select_, bin_mask_, bin_trace_pass_count_,
            bin_trace_skip_count_, win_off, sc_tl, sc_br, surf_info,
            color_info, depth_info, mode_cntl);
        bin_trace_pass_count_ = 0;
        bin_trace_skip_count_ = 0;
      }
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_HI: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_select_ =
          (bin_select_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;
    case PM4_SET_BIN_MASK: {
      assert_true(count == 2);
      uint64_t val_hi = reader->ReadAndSwap<uint32_t>();
      uint64_t val_lo = reader->ReadAndSwap<uint32_t>();
      bin_mask_ = (val_hi << 32) | val_lo;
      result = true;
    } break;
    case PM4_SET_BIN_SELECT: {
      assert_true(count == 2);
      uint64_t val_hi = reader->ReadAndSwap<uint32_t>();
      uint64_t val_lo = reader->ReadAndSwap<uint32_t>();
      bin_select_ = (val_hi << 32) | val_lo;
      ++flatten_bin_passes_seen_;
      UpdateFlattenResolveOffsetSkip();
      // gpu_trace_bin_select: each select change = a new predicated-tiling tile
      // pass. Log the transition + the pass/skip counts accumulated under the
      // PREVIOUS select value, plus the tile's window offset/scissor registers
      // (tells stage 2 whether offset-zeroing is needed or scissor widening
      // suffices - if tile 1 has offset 0,0 the flatten needs no offset work).
      if (cvars::gpu_trace_bin_select) {
        uint32_t win_off = (*register_file_)[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
        uint32_t sc_tl = (*register_file_)[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];
        uint32_t sc_br = (*register_file_)[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
        XELOGI(
            "BIN_SELECT -> {:016X} (mask {:016X}); prev select: passed={} "
            "skipped={}; win_off={:08X} scissor_tl={:08X} br={:08X}",
            bin_select_, bin_mask_, bin_trace_pass_count_,
            bin_trace_skip_count_, win_off, sc_tl, sc_br);
        bin_trace_pass_count_ = 0;
        bin_trace_skip_count_ = 0;
      }
      result = true;
    } break;
    case PM4_CONTEXT_UPDATE: {
      assert_true(count == 1);
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      XELOGGPU("GPU context update = {:08X}", value);
      assert_true(value == 0);
      result = true;
      break;
    }
    case PM4_WAIT_FOR_IDLE: {
      // This opcode is used by 5454084E while going / being ingame.
      assert_true(count == 1);
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      XELOGGPU("GPU wait for idle = {:08X}", value);
      result = true;
      break;
    }

    default:
      XELOGGPU("Unimplemented GPU OPCODE: 0x{:02X}\t\tCOUNT: {}\n", opcode,
               count);
      assert_always();
      reader->AdvanceRead(count * sizeof(uint32_t));
      break;
  }

  trace_writer_.WritePacketEnd();
  if (opcode == PM4_XE_SWAP) {
    // End the trace writer frame.
    if (trace_writer_.is_open()) {
      trace_writer_.WriteEvent(EventCommand::Type::kSwap);
      trace_writer_.Flush();
      if (trace_state_ == TraceState::kSingleFrame) {
        trace_state_ = TraceState::kDisabled;
        trace_writer_.Close();
      }
    } else if (trace_state_ == TraceState::kSingleFrame) {
      // New trace request - we only start tracing at the beginning of a frame.
      uint32_t title_id = kernel_state_->GetExecutableModule()->title_id();
      auto file_name = fmt::format("{:08X}_{}.xtr", title_id, counter_ - 1);
      auto path = trace_frame_path_ / file_name;
      trace_writer_.Open(path, title_id);
      InitializeTrace();
    }
  }

  assert_true(reader->read_offset() ==
              (data_start_offset + (count * sizeof(uint32_t))) %
                  reader->capacity());
  return result;
}

bool CommandProcessor::ExecutePacketType3_ME_INIT(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // initialize CP's micro-engine
  me_bin_.clear();
  for (uint32_t i = 0; i < count; i++) {
    me_bin_.push_back(reader->ReadAndSwap<uint32_t>());
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_NOP(RingBuffer* reader,
                                              uint32_t packet, uint32_t count) {
  // skip N 32-bit words to get to the next packet
  // No-op, ignore some data.
  reader->AdvanceRead(count * sizeof(uint32_t));
  return true;
}

bool CommandProcessor::ExecutePacketType3_INTERRUPT(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // generate interrupt from the command stream
  uint32_t cpu_mask = reader->ReadAndSwap<uint32_t>();
  if (ShouldTraceGpuInterruptPacket()) {
    XELOGI(
        "GPU interrupt trace: PM4_INTERRUPT cpu_mask={:08X} count={} "
        "read_ptr={:08X} read_offset={:08X} counter={:08X} "
        "rb_rptr={:08X} rb_wptr={:08X}",
        cpu_mask, count, read_ptr_index_, uint32_t(reader->read_offset()),
        counter_, uint32_t(register_file_->values[kCpRbRptrRegister]),
        uint32_t(register_file_->values[kCpRbWptrRegister]));
  }
  for (int n = 0; n < 6; n++) {
    if (cpu_mask & (1 << n)) {
      graphics_system_->DispatchInterruptCallback(1, n);
    }
  }
  return true;
}

#if XE_PLATFORM_ANDROID
void CommandProcessor::AdpfBeginSwap() {
  const bool want_hints = cvars::gpu_adpf_performance_hints;
  const bool want_thermal = cvars::gpu_adpf_thermal_throttle;
  if (!want_hints && !want_thermal) {
    return;
  }
  const AdpfApi& api = GetAdpfApi();

  if (!adpf_init_attempted_) {
    adpf_init_attempted_ = true;
    if (want_hints && api.perf_hint_available) {
      adpf_hint_manager_ = api.get_manager();
      if (adpf_hint_manager_) {
        // Register THIS thread (the CP worker = the frame-critical thread).
        int32_t tid = static_cast<int32_t>(syscall(__NR_gettid));
        uint32_t fps =
            cvars::gpu_frame_limit_fps ? cvars::gpu_frame_limit_fps : 60u;
        int64_t target_ns = int64_t(1000000000ull / fps);
        adpf_hint_session_ =
            api.create_session(adpf_hint_manager_, &tid, 1, target_ns);
      }
    }
    if (want_thermal && api.thermal_available) {
      adpf_thermal_manager_ = api.thermal_acquire();
    }
    XELOGI(
        "ADPF init: perf-hints {} (session {}), thermal-throttle {} (mgr {})",
        want_hints ? "on" : "off",
        adpf_hint_session_ ? "created" : "unavailable",
        want_thermal ? "on" : "off",
        adpf_thermal_manager_ ? "acquired" : "unavailable");
  }

  // Report the previous frame's actual CP work duration. AdpfEndSwap timestamps
  // AFTER the frame-limiter sleep, so this interval excludes the pacing sleep -
  // letting ADPF drop CPU power (and heat) when there is genuine slack.
  if (adpf_hint_session_ && adpf_last_frame_end_ns_) {
    int64_t actual_ns = AdpfNowNs() - int64_t(adpf_last_frame_end_ns_);
    if (actual_ns > 0) {
      uint32_t fps =
          cvars::gpu_frame_limit_fps ? cvars::gpu_frame_limit_fps : 60u;
      api.update_target(adpf_hint_session_, int64_t(1000000000ull / fps));
      api.report_actual(adpf_hint_session_, actual_ns);
    }
  }

  // Thermal headroom changes slowly; polling every ~30 swaps is plenty.
  if (adpf_thermal_manager_ && (adpf_thermal_poll_counter_++ % 30u) == 0u) {
    adpf_thermal_headroom_ = api.thermal_headroom(adpf_thermal_manager_, 10);
  }
}

void CommandProcessor::AdpfEndSwap() {
  if (adpf_hint_session_) {
    adpf_last_frame_end_ns_ = uint64_t(AdpfNowNs());
  }
}

uint32_t CommandProcessor::AdpfThermalAdjustedFrameLimit(
    uint32_t configured_fps) const {
  if (!cvars::gpu_adpf_thermal_throttle || !adpf_thermal_manager_) {
    return configured_fps;
  }
  // Headroom is normalized: 0 = cool, 1.0 = at the throttling threshold (may
  // exceed 1.0). NaN (unsupported) fails every comparison -> no throttle.
  float h = adpf_thermal_headroom_;
  uint32_t cap;
  if (h >= 1.0f) {
    cap = 20u;
  } else if (h >= 0.95f) {
    cap = 30u;
  } else if (h >= 0.90f) {
    cap = 45u;
  } else {
    return configured_fps;
  }
  return configured_fps ? std::min(configured_fps, cap) : cap;
}

void CommandProcessor::AdpfShutdown() {
  const AdpfApi& api = GetAdpfApi();
  if (adpf_hint_session_ && api.close_session) {
    api.close_session(adpf_hint_session_);
  }
  adpf_hint_session_ = nullptr;
  if (adpf_thermal_manager_ && api.thermal_release) {
    api.thermal_release(adpf_thermal_manager_);
  }
  adpf_thermal_manager_ = nullptr;
}
#endif  // XE_PLATFORM_ANDROID

void CommandProcessor::UpdateFlattenResolveOffsetSkip() {
  bool in_tile_pass =
      (bin_select_ & 0xFFFFFFFFull) != 0xFFFFFFFFull && bin_select_ != 0;
  bool flatten_widen = cvars::gpu_flatten_predicated_tiling &&
                       cvars::gpu_flatten_predicated_tiling_widen;
  draw_util::resolve_ignore_window_offset =
      flatten_widen && in_tile_pass && flatten_bin_passes_seen_ >= 3;
  // MERGE mode: every content bin pass (>= 2) is force-passed into the one
  // widened full-surface render, so ALL of them must drop the per-tile window
  // offset (esp. pass B's -608) and rasterize at true screen position instead
  // of their tile's EDRAM origin. (Was ==2 = only the first content pass, which
  // lost pass B's 26 draws + mis-placed geometry = the broken render.)
  draw_util::draw_ignore_window_offset =
      flatten_widen && in_tile_pass && flatten_bin_passes_seen_ >= 2;
  // Bin-once: accumulate the full-surface height = the max scissor br_y across
  // this frame's tile passes (the bottom tile's 1280 vs the top tile's 672).
  // Published at frame end (XE_SWAP) for the next frame's force-pass scissor.
  if (flatten_widen && in_tile_pass) {
    int32_t br_y =
        int32_t(register_file_->Get<reg::PA_SC_WINDOW_SCISSOR_BR>().br_y);
    if (br_y > flatten_max_scissor_br_y_) {
      flatten_max_scissor_br_y_ = br_y;
    }
  }
}

bool CommandProcessor::ExecutePacketType3_XE_SWAP(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  if (cvars::gpu_trace_swap) {
    XELOGI("XE_SWAP");
  }
  // gpu_flatten_predicated_tiling: frame boundary - the next SET_BIN_SELECT
  // begins a new frame's first bin pass. Log the flatten summary when tracing.
  if (cvars::gpu_trace_bin_select &&
      (flatten_bin_passes_seen_ || flatten_dropped_draws_)) {
    XELOGI("BIN frame summary: bin_passes={} flatten_dropped_draws={}",
           flatten_bin_passes_seen_, flatten_dropped_draws_);
  }
  flatten_bin_passes_seen_ = 0;
  flatten_dropped_draws_ = 0;
  // Bin-once: publish this frame's full-surface height for the next frame's
  // force-pass scissor extension, then reset the accumulator. Scene structure is
  // stable frame-to-frame so last-frame's value is correct for this frame.
  if (flatten_max_scissor_br_y_ > 0) {
    draw_util::flatten_full_scissor_br_y = flatten_max_scissor_br_y_;
  }
  flatten_max_scissor_br_y_ = 0;
  UpdateFlattenResolveOffsetSkip();

  Profiler::Flip();

  // Xenia-specific VdSwap hook.
  // VdSwap will post this to tell us we need to swap the screen/fire an
  // interrupt.
  // 63 words here, but only the first has any data.
  uint32_t magic = reader->ReadAndSwap<fourcc_t>();
  assert_true(magic == kSwapSignature);

  // TODO(benvanik): only swap frontbuffer ptr.
  uint32_t frontbuffer_ptr = reader->ReadAndSwap<uint32_t>();
  uint32_t frontbuffer_width = reader->ReadAndSwap<uint32_t>();
  uint32_t frontbuffer_height = reader->ReadAndSwap<uint32_t>();
  uint32_t display_width = frontbuffer_width;
  uint32_t display_height = frontbuffer_height;
  if (count >= 6) {
    display_width = reader->ReadAndSwap<uint32_t>();
    display_height = reader->ReadAndSwap<uint32_t>();
    reader->AdvanceRead((count - 6) * sizeof(uint32_t));
  } else if (count > 4) {
    reader->AdvanceRead((count - 4) * sizeof(uint32_t));
  }
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: XE_SWAP packet magic={:08X} frontbuffer={:08X} "
        "size={}x{} display={}x{} count={} read_ptr={:08X} "
        "read_offset={:08X}",
        magic, frontbuffer_ptr, frontbuffer_width, frontbuffer_height,
        display_width, display_height, count, uint32_t(reader->read_ptr()),
        uint32_t(reader->read_offset()));
  }
  TraceSwapProbeCvarsOnce();
  TraceSwapFrontbufferChecksum(memory_, frontbuffer_ptr, frontbuffer_width,
                               frontbuffer_height);
  TraceSwapRenderTargets(register_file_, frontbuffer_ptr, frontbuffer_width,
                         frontbuffer_height);

#if XE_PLATFORM_ANDROID
  // ADPF: report the previous frame's CP work + refresh thermal headroom before
  // the frame limiter (below) applies any thermal-derived cap.
  AdpfBeginSwap();
#endif  // XE_PLATFORM_ANDROID

  // Host-side frame-rate limiter (gpu_frame_limit_fps): pace the swap so light/
  // loading/menu screens don't render hundreds of fps and peg+overheat the GPU
  // (device-observed: Lost Odyssey loading ~943fps -> 72.5C). Sleeping here
  // throttles the CP worker; ring-buffer backpressure then paces the guest.
  // 0 = disabled (prior behavior). Caps real frames/sec, not guest time.
  uint32_t frame_limit_fps = cvars::gpu_frame_limit_fps;
#if XE_PLATFORM_ANDROID
  // Pre-emptive thermal throttle (gpu_adpf_thermal_throttle): lower the present
  // cap as ADPF thermal headroom approaches the throttling threshold.
  frame_limit_fps = AdpfThermalAdjustedFrameLimit(frame_limit_fps);
#endif  // XE_PLATFORM_ANDROID
  if (frame_limit_fps) {
    uint64_t target_interval_ms = 1000ull / frame_limit_fps;
    if (target_interval_ms) {
      if (last_swap_host_millis_) {
        uint64_t elapsed_ms =
            xe::Clock::QueryHostUptimeMillis() - last_swap_host_millis_;
        if (elapsed_ms < target_interval_ms) {
          xe::threading::Sleep(
              std::chrono::milliseconds(target_interval_ms - elapsed_ms));
        }
      }
      last_swap_host_millis_ = xe::Clock::QueryHostUptimeMillis();
    }
  }

  IssueSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height,
            display_width, display_height);

#if XE_PLATFORM_ANDROID
  // Mark end-of-frame AFTER the limiter sleep + swap so the next frame's
  // reported ADPF work duration excludes the intentional pacing sleep.
  AdpfEndSwap();
#endif  // XE_PLATFORM_ANDROID

  ++counter_;
  if (ShouldTraceGpuInterruptPacket()) {
    XELOGI(
        "GPU interrupt trace: XE_SWAP complete frontbuffer={:08X} size={}x{} "
        "counter={:08X} read_ptr={:08X} read_offset={:08X} "
        "swap_interrupt={}",
        frontbuffer_ptr, frontbuffer_width, frontbuffer_height, counter_,
        read_ptr_index_, uint32_t(reader->read_offset()),
        cvars::gpu_interrupt_on_swap);
  }
  if (cvars::gpu_interrupt_on_swap) {
    graphics_system_->DispatchInterruptCallback(1, 2);
  }

  // Event-driven vblank (vsync_on_swap): fire the pending vblank for
  // slower-than-60fps titles now instead of letting the guest's frame round
  // up to the next fixed 16.7ms tick (worklog B86i/B86j). MUST be after
  // ++counter_: the wait probe (B86m/n) showed the guest render thread POLLS
  // its swap-completion state (no kernel wait between swaps; ~257 cheap
  // export calls per inter-swap window = a poll loop), and the guest's vblank
  // ISR derives that state from the CP's progress - both earlier placements
  // (inside the backend IssueSwap, before the counter increment) measured
  // NEUTRAL because the early-run ISR still saw the swap as incomplete and
  // fell back to waiting for the next fixed tick.
  graphics_system_->RequestSwapVblank();

  return true;
}

bool CommandProcessor::ExecutePacketType3_INDIRECT_BUFFER(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // indirect buffer dispatch
  uint32_t raw_list_ptr = reader->ReadAndSwap<uint32_t>();
  uint32_t list_ptr = CpuToGpu(raw_list_ptr);
  uint32_t list_length = reader->ReadAndSwap<uint32_t>();
  assert_zero(list_length & ~0xFFFFF);
  list_length &= 0xFFFFF;
  bool primary_reader =
      reader->buffer() == memory_->TranslatePhysical(primary_buffer_ptr_);
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: INDIRECT_BUFFER raw={:08X} gpu={:08X} cpu={:08X} "
        "length={} primary={} read_offset={:08X}",
        raw_list_ptr, list_ptr, GpuToCpu(list_ptr), list_length,
        primary_reader, uint32_t(reader->read_offset()));
  }
  if (cvars::gpu_early_primary_read_pointer_writeback && primary_reader) {
    UpdatePrimaryReadPointer(uint32_t(reader->read_offset() / sizeof(uint32_t)),
                             "before_indirect_buffer");
  }
  ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
  return true;
}

bool CommandProcessor::ExecutePacketType3_WAIT_REG_MEM(RingBuffer* reader,
                                                       uint32_t packet,
                                                       uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // wait until a register or memory location is a specific value

  uint32_t wait_info = reader->ReadAndSwap<uint32_t>();
  uint32_t poll_reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t ref = reader->ReadAndSwap<uint32_t>();
  uint32_t mask = reader->ReadAndSwap<uint32_t>();
  uint32_t wait = reader->ReadAndSwap<uint32_t>();

  bool is_memory = (wait_info & 0x10) != 0;

  assert_true(is_memory || poll_reg_addr < RegisterFile::kRegisterCount);
  const volatile uint32_t& value_ref =
      is_memory ? *reinterpret_cast<uint32_t*>(memory_->TranslatePhysical(
                      poll_reg_addr & ~uint32_t(0x3)))
                : register_file_->values[poll_reg_addr];

  bool matched = false;
  uint32_t wait_loops = 0;
  uint32_t last_value = 0;
  do {
    uint32_t value = value_ref;
    if (is_memory) {
      trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr & ~uint32_t(0x3)),
                                    sizeof(uint32_t));
      value = xenos::GpuSwap(value,
                             static_cast<xenos::Endian>(poll_reg_addr & 0x3));
    } else {
      if (poll_reg_addr == XE_GPU_REG_COHER_STATUS_HOST) {
        MakeCoherent();
        value = value_ref;
      }
    }
    last_value = value;
    switch (wait_info & 0x7) {
      case 0x0:  // Never.
        matched = false;
        break;
      case 0x1:  // Less than reference.
        matched = (value & mask) < ref;
        break;
      case 0x2:  // Less than or equal to reference.
        matched = (value & mask) <= ref;
        break;
      case 0x3:  // Equal to reference.
        matched = (value & mask) == ref;
        break;
      case 0x4:  // Not equal to reference.
        matched = (value & mask) != ref;
        break;
      case 0x5:  // Greater than or equal to reference.
        matched = (value & mask) >= ref;
        break;
      case 0x6:  // Greater than reference.
        matched = (value & mask) > ref;
        break;
      case 0x7:  // Always
        matched = true;
        break;
    }
    if (!matched) {
      ++wait_loops;
      // Wait.
      if (wait >= 0x100) {
        PrepareForWait();
        if (!cvars::vsync) {
          // User wants it fast and dangerous.
          xe::threading::MaybeYield();
        } else {
          xe::threading::Sleep(std::chrono::milliseconds(wait / 0x100));
        }
        xe::threading::SyncMemory();
        ReturnFromWait();

        if (!worker_running_) {
          // Short-circuited exit.
          return false;
        }
      } else {
        xe::threading::MaybeYield();
      }
    }
  } while (!matched);

  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: WAIT_REG_MEM info={:08X} poll={:08X} ref={:08X} "
        "mask={:08X} wait={:08X} memory={} final={:08X} loops={}",
        wait_info, poll_reg_addr, ref, mask, wait, is_memory, last_value,
        wait_loops);
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_RMW(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // register read/modify/write
  // ? (used during shader upload and edram setup)
  uint32_t rmw_info = reader->ReadAndSwap<uint32_t>();
  uint32_t and_mask = reader->ReadAndSwap<uint32_t>();
  uint32_t or_mask = reader->ReadAndSwap<uint32_t>();
  uint32_t value = register_file_->values[rmw_info & 0x1FFF];
  if ((rmw_info >> 31) & 0x1) {
    // & reg
    value &= register_file_->values[and_mask & 0x1FFF];
  } else {
    // & imm
    value &= and_mask;
  }
  if ((rmw_info >> 30) & 0x1) {
    // | reg
    value |= register_file_->values[or_mask & 0x1FFF];
  } else {
    // | imm
    value |= or_mask;
  }
  WriteRegister(rmw_info & 0x1FFF, value);
  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_TO_MEM(RingBuffer* reader,
                                                     uint32_t packet,
                                                     uint32_t count) {
  // Copy Register to Memory (?)
  // Count is 2, assuming a Register Addr and a Memory Addr.

  uint32_t reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t mem_addr = reader->ReadAndSwap<uint32_t>();

  uint32_t reg_val;

  assert_true(reg_addr < RegisterFile::kRegisterCount);
  reg_val = register_file_->values[reg_addr];

  auto endianness = static_cast<xenos::Endian>(mem_addr & 0x3);
  mem_addr &= ~0x3;
  reg_val = GpuSwap(reg_val, endianness);
  xe::store(memory_->TranslatePhysical(mem_addr), reg_val);
  trace_writer_.WriteMemoryWrite(CpuToGpu(mem_addr), 4);

  return true;
}

bool CommandProcessor::ExecutePacketType3_MEM_WRITE(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  uint32_t write_addr = reader->ReadAndSwap<uint32_t>();
  for (uint32_t i = 0; i < count - 1; i++) {
    uint32_t write_data = reader->ReadAndSwap<uint32_t>();

    auto endianness = static_cast<xenos::Endian>(write_addr & 0x3);
    auto addr = write_addr & ~0x3;
    write_data = GpuSwap(write_data, endianness);
    xe::store(memory_->TranslatePhysical(addr), write_data);
    trace_writer_.WriteMemoryWrite(CpuToGpu(addr), 4);
    write_addr += 4;
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_COND_WRITE(RingBuffer* reader,
                                                     uint32_t packet,
                                                     uint32_t count) {
  // conditional write to memory or register
  uint32_t wait_info = reader->ReadAndSwap<uint32_t>();
  uint32_t poll_reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t ref = reader->ReadAndSwap<uint32_t>();
  uint32_t mask = reader->ReadAndSwap<uint32_t>();
  uint32_t write_reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t write_data = reader->ReadAndSwap<uint32_t>();
  uint32_t value;
  if (wait_info & 0x10) {
    // Memory.
    auto endianness = static_cast<xenos::Endian>(poll_reg_addr & 0x3);
    poll_reg_addr &= ~0x3;
    trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr), 4);
    value = xe::load<uint32_t>(memory_->TranslatePhysical(poll_reg_addr));
    value = GpuSwap(value, endianness);
  } else {
    // Register.
    assert_true(poll_reg_addr < RegisterFile::kRegisterCount);
    value = register_file_->values[poll_reg_addr];
  }
  bool matched = false;
  switch (wait_info & 0x7) {
    case 0x0:  // Never.
      matched = false;
      break;
    case 0x1:  // Less than reference.
      matched = (value & mask) < ref;
      break;
    case 0x2:  // Less than or equal to reference.
      matched = (value & mask) <= ref;
      break;
    case 0x3:  // Equal to reference.
      matched = (value & mask) == ref;
      break;
    case 0x4:  // Not equal to reference.
      matched = (value & mask) != ref;
      break;
    case 0x5:  // Greater than or equal to reference.
      matched = (value & mask) >= ref;
      break;
    case 0x6:  // Greater than reference.
      matched = (value & mask) > ref;
      break;
    case 0x7:  // Always
      matched = true;
      break;
  }
  if (matched) {
    // Write.
    if (wait_info & 0x100) {
      // Memory.
      auto endianness = static_cast<xenos::Endian>(write_reg_addr & 0x3);
      write_reg_addr &= ~0x3;
      write_data = GpuSwap(write_data, endianness);
      xe::store(memory_->TranslatePhysical(write_reg_addr), write_data);
      trace_writer_.WriteMemoryWrite(CpuToGpu(write_reg_addr), 4);
    } else {
      // Register.
      WriteRegister(write_reg_addr, write_data);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE(RingBuffer* reader,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // generate an event that creates a write to memory when completed
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  if (count == 1) {
    // Just an event flag? Where does this write?
  } else {
    // Write to an address.
    assert_always();
    reader->AdvanceRead((count - 1) * sizeof(uint32_t));
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_SHD(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // generate a VS|PS_done event
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  uint32_t address = reader->ReadAndSwap<uint32_t>();
  uint32_t value = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  uint32_t data_value;
  if ((initiator >> 31) & 0x1) {
    // Write counter (GPU vblank counter?).
    data_value = counter_;
  } else {
    // Write value.
    data_value = value;
  }
  auto endianness = static_cast<xenos::Endian>(address & 0x3);
  address &= ~0x3;
  data_value = GpuSwap(data_value, endianness);
  // BD Turnip crash-race candidate fix: make the fence lag actual GPU completion
  // so the guest cannot poll it and read still-in-flight GPU data (gated; no-op
  // unless the backend overrides SyncGpuForEventWriteFence).
  if (cvars::gpu_bd_sync_event_write_fences) {
    SyncGpuForEventWriteFence();
  }
  xe::store(memory_->TranslatePhysical(address), data_value);
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), 4);
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: EVENT_WRITE_SHD initiator={:08X} address={:08X} "
        "value={:08X} stored={:08X} endian={}",
        initiator, address, value, data_value, uint32_t(endianness));
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_EXT(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // generate a screen extent event
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  uint32_t address = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  auto endianness = static_cast<xenos::Endian>(address & 0x3);
  address &= ~0x3;

  // Let us hope we can fake this.
  // This callback tells the driver the xy coordinates affected by a previous
  // drawcall.
  // https://www.google.com/patents/US20060055701
  uint16_t extents[] = {
      0 >> 3,                                    // min x
      xenos::kTexture2DCubeMaxWidthHeight >> 3,  // max x
      0 >> 3,                                    // min y
      xenos::kTexture2DCubeMaxWidthHeight >> 3,  // max y
      0,                                         // min z
      1,                                         // max z
  };
  assert_true(endianness == xenos::Endian::k8in16);
  xe::copy_and_swap_16_unaligned(memory_->TranslatePhysical(address), extents,
                                 xe::countof(extents));
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), sizeof(extents));
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // Set by D3D as BE but struct ABI is LE
  const uint32_t kQueryFinished = xe::byte_swap(0xFFFFFEED);
  assert_true(count == 1);
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);

  // Occlusion queries:
  // This command is send on query begin and end.
  // As a workaround report some fixed amount of passed samples.
  auto fake_sample_count = cvars::query_occlusion_fake_sample_count;
  if (fake_sample_count >= 0) {
    auto* pSampleCounts =
        memory_->TranslatePhysical<xe_gpu_depth_sample_counts*>(
            register_file_->values[XE_GPU_REG_RB_SAMPLE_COUNT_ADDR]);
    // 0xFFFFFEED is written to this two locations by D3D only on D3DISSUE_END
    // and used to detect a finished query.
    bool is_end_via_z_pass = pSampleCounts->ZPass_A == kQueryFinished &&
                             pSampleCounts->ZPass_B == kQueryFinished;
    // Older versions of D3D also checks for ZFail (4D5307D5).
    bool is_end_via_z_fail = pSampleCounts->ZFail_A == kQueryFinished &&
                             pSampleCounts->ZFail_B == kQueryFinished;
    std::memset(pSampleCounts, 0, sizeof(xe_gpu_depth_sample_counts));
    if (is_end_via_z_pass || is_end_via_z_fail) {
      pSampleCounts->ZPass_A = fake_sample_count;
      pSampleCounts->Total_A = fake_sample_count;
    }
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3Draw(RingBuffer* reader,
                                              uint32_t packet,
                                              const char* opcode_name,
                                              uint32_t viz_query_condition,
                                              uint32_t count_remaining) {
  // if viz_query_condition != 0, this is a conditional draw based on viz query.
  // This ID matches the one issued in PM4_VIZ_QUERY
  // uint32_t viz_id = viz_query_condition & 0x3F;
  // when true, render conditionally based on query result
  // uint32_t viz_use = viz_query_condition & 0x100;

  assert_not_zero(count_remaining);
  if (!count_remaining) {
    XELOGE("{}: Packet too small, can't read VGT_DRAW_INITIATOR", opcode_name);
    return false;
  }
  reg::VGT_DRAW_INITIATOR vgt_draw_initiator;
  vgt_draw_initiator.value = reader->ReadAndSwap<uint32_t>();
  --count_remaining;
  WriteRegister(XE_GPU_REG_VGT_DRAW_INITIATOR, vgt_draw_initiator.value);

  bool draw_succeeded = true;
  // TODO(Triang3l): Remove IndexBufferInfo and replace handling of all this
  // with PrimitiveProcessor when the old Vulkan renderer is removed.
  bool is_indexed = false;
  IndexBufferInfo index_buffer_info;
  switch (vgt_draw_initiator.source_select) {
    case xenos::SourceSelect::kDMA: {
      // Indexed draw.
      is_indexed = true;

      // Two separate bounds checks so if there's only one missing register
      // value out of two, one uint32_t will be skipped in the command buffer,
      // not two.
      assert_not_zero(count_remaining);
      if (!count_remaining) {
        XELOGE("{}: Packet too small, can't read VGT_DMA_BASE", opcode_name);
        return false;
      }
      uint32_t vgt_dma_base = reader->ReadAndSwap<uint32_t>();
      --count_remaining;
      WriteRegister(XE_GPU_REG_VGT_DMA_BASE, vgt_dma_base);
      reg::VGT_DMA_SIZE vgt_dma_size;
      assert_not_zero(count_remaining);
      if (!count_remaining) {
        XELOGE("{}: Packet too small, can't read VGT_DMA_SIZE", opcode_name);
        return false;
      }
      vgt_dma_size.value = reader->ReadAndSwap<uint32_t>();
      --count_remaining;
      WriteRegister(XE_GPU_REG_VGT_DMA_SIZE, vgt_dma_size.value);

      uint32_t index_size_bytes =
          vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16
              ? sizeof(uint16_t)
              : sizeof(uint32_t);
      // The base address must already be word-aligned according to the R6xx
      // documentation, but for safety.
      index_buffer_info.guest_base = vgt_dma_base & ~(index_size_bytes - 1);
      index_buffer_info.endianness = vgt_dma_size.swap_mode;
      index_buffer_info.format = vgt_draw_initiator.index_size;
      index_buffer_info.length = vgt_dma_size.num_words * index_size_bytes;
      index_buffer_info.count = vgt_draw_initiator.num_indices;
    } break;
    case xenos::SourceSelect::kImmediate: {
      // TODO(Triang3l): VGT_IMMED_DATA.
      XELOGE(
          "{}: Using immediate vertex indices, which are not supported yet. "
          "Report the game to Xenia developers!",
          opcode_name, uint32_t(vgt_draw_initiator.source_select));
      draw_succeeded = false;
      assert_always();
    } break;
    case xenos::SourceSelect::kAutoIndex: {
      // Auto draw.
      index_buffer_info.guest_base = 0;
      index_buffer_info.length = 0;
    } break;
    default: {
      // Invalid source selection.
      draw_succeeded = false;
      assert_unhandled_case(vgt_draw_initiator.source_select);
    } break;
  }

  // Skip to the next command, for example, if there are immediate indexes that
  // we don't support yet.
  reader->AdvanceRead(count_remaining * sizeof(uint32_t));

  if (draw_succeeded) {
    auto viz_query = register_file_->Get<reg::PA_SC_VIZ_QUERY>();
    if (!(viz_query.viz_query_ena && viz_query.kill_pix_post_hi_z)) {
      // --- Blue Dragon native-draw HLE (Brick 1): translation-contract capture.
      // This runs on the CP worker thread, where register_file_ is fully
      // populated by the PM4 parse - the ONLY place the complete decoded draw
      // state exists in xenia's format. Dump exactly what IssueDraw consumes so a
      // native (DXVK-style) front-end knows what it must reproduce per draw.
      // Bounded + filtered to the FIELD passes (pitch 360/720) so the log lands
      // on the foliage. Default off (gpu_bd_draw_contract_trace).
      if (cvars::gpu_bd_draw_contract_trace && is_indexed) {
        const RegisterFile& regs = *register_file_;
        uint32_t surf = regs[XE_GPU_REG_RB_SURFACE_INFO];
        uint32_t pitch = surf & 0x3FFF;  // RB_SURFACE_INFO.surface_pitch [13:0]
        static std::atomic<uint32_t> s_dumped{0};
        static std::atomic<uint32_t> s_draw_idx{0};
        uint32_t di = s_draw_idx.fetch_add(1);
        if ((pitch == 360 || pitch == 720) &&
            s_dumped.load() < cvars::gpu_bd_draw_contract_trace) {
          s_dumped.fetch_add(1);
          Shader* vs = active_vertex_shader_;
          Shader* ps = active_pixel_shader_;
          XELOGI(
              "BDCONTRACT[{}] bin_sel={:08X} bin_mask={:08X} prim={} idx_cnt={} "
              "src={} ifmt={} iendian={} ibase={:08X} ilen={}",
              di, uint32_t(bin_select_), uint32_t(bin_mask_),
              uint32_t(vgt_draw_initiator.prim_type),
              uint32_t(vgt_draw_initiator.num_indices),
              uint32_t(vgt_draw_initiator.source_select),
              uint32_t(index_buffer_info.format),
              uint32_t(index_buffer_info.endianness),
              index_buffer_info.guest_base, uint32_t(index_buffer_info.length));
          XELOGI(
              "BDCONTRACT[{}]   VS hash={:016X} dw={} PS hash={:016X} dw={} "
              "SQ_PROG={:08X} SQ_CTXMISC={:08X}",
              di, vs ? vs->ucode_data_hash() : 0ull,
              vs ? uint32_t(vs->ucode_data().size()) : 0u,
              ps ? ps->ucode_data_hash() : 0ull,
              ps ? uint32_t(ps->ucode_data().size()) : 0u,
              regs[XE_GPU_REG_SQ_PROGRAM_CNTL],
              regs[XE_GPU_REG_SQ_CONTEXT_MISC]);
          XELOGI(
              "BDCONTRACT[{}]   MODE={:08X} SURF={:08X} COLORI={:08X} "
              "DEPTHI={:08X} DEPTHCTL={:08X} COLORCTL={:08X} BLEND0={:08X} "
              "CMASK={:08X} SUSC={:08X} SCIS_TL={:08X} SCIS_BR={:08X} "
              "WINOFF={:08X} VTE={:08X}",
              di, regs[XE_GPU_REG_RB_MODECONTROL], surf,
              regs[XE_GPU_REG_RB_COLOR_INFO], regs[XE_GPU_REG_RB_DEPTH_INFO],
              regs[XE_GPU_REG_RB_DEPTHCONTROL], regs[XE_GPU_REG_RB_COLORCONTROL],
              regs[XE_GPU_REG_RB_BLENDCONTROL0], regs[XE_GPU_REG_RB_COLOR_MASK],
              regs[XE_GPU_REG_PA_SU_SC_MODE_CNTL],
              regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL],
              regs[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR],
              regs[XE_GPU_REG_PA_SC_WINDOW_OFFSET],
              regs[XE_GPU_REG_PA_CL_VTE_CNTL]);
          if (vs) {
            const Shader::ConstantRegisterMap& crm = vs->constant_register_map();
            for (uint32_t wi = 0; wi < xe::countof(crm.vertex_fetch_bitmap);
                 ++wi) {
              uint32_t bits = crm.vertex_fetch_bitmap[wi];
              uint32_t bi;
              while (xe::bit_scan_forward(bits, &bi)) {
                bits &= ~(uint32_t(1) << bi);
                uint32_t vfi = wi * 32 + bi;
                xenos::xe_gpu_vertex_fetch_t vf = regs.GetVertexFetch(vfi);
                XELOGI(
                    "BDCONTRACT[{}]     VFETCH[{}] d0={:08X} d1={:08X} type={} "
                    "addr={:08X}B size={}B endian={}",
                    di, vfi, vf.dword_0, vf.dword_1, uint32_t(vf.type),
                    vf.address << 2, vf.size << 2, uint32_t(vf.endian));
              }
            }
          }
          for (uint32_t ti = 0; ti < 32; ++ti) {
            xenos::xe_gpu_texture_fetch_t tf = regs.GetTextureFetch(ti);
            if (tf.type == xenos::FetchConstantType::kTexture) {
              XELOGI(
                  "BDCONTRACT[{}]     TFETCH[{}] d0={:08X} d1={:08X} d2={:08X} "
                  "d3={:08X} d4={:08X} d5={:08X}",
                  di, ti, tf.dword_0, tf.dword_1, tf.dword_2, tf.dword_3,
                  tf.dword_4, tf.dword_5);
            }
          }
        }
      }
      // --- Blue Dragon native-draw HLE: classify this draw BEFORE issuing it,
      // so replace-mode (step 3) can DROP BD's per-tile LLE foliage submission
      // for the draws the native pass covers - rendering the field ONCE
      // natively, not twice. Foliage = the field's unique VS ucode hash at
      // pitch 720 (from the draw-contract capture).
      static thread_local bool s_in_bd_native_emit = false;
      uint32_t nhle_pitch =
          register_file_->values[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFF;
      bool nhle_is_foliage =
          active_vertex_shader_ &&
          active_vertex_shader_->ucode_data_hash() == 0xF5CE501A336FEE16ull;
      // A "covered" draw = a field draw (the field surface = pitch 720) on the
      // OUTER (non-reentrant) pass while native HLE is armed. The WHOLE field
      // pass is covered - EVERY pitch-720 indexed draw, no per-frame budget cap
      // and no single-shader restriction - so the entire field renders natively
      // into one shared decoupled full-surface RT. (The 0xF5CE501A hash is the
      // dominant foliage/environment shader = ~404 of the draws; the rest of
      // the field pass uses sibling shaders. nhle_is_foliage is kept for the
      // diagnostic log.)
      bool nhle_cover = cvars::gpu_bd_native_hle && is_indexed &&
                        !s_in_bd_native_emit && nhle_pitch == 720;

      // TODO(Triang3l): Don't drop the draw call completely if the vertex
      // shader has memexport.
      // TODO(Triang3l || JoelLinn): Handle this properly in the render
      // backends.
      if (nhle_cover && cvars::gpu_bd_native_hle_replace) {
        // Front-end replacement: skip BD's original LLE foliage draw entirely;
        // the synthetic native emit below is the SOLE submit for this draw.
        // register_file_ already holds BD's full decoded state (the PM4 parse
        // ran before this), so the native draw reproduces it exactly.
        draw_succeeded = true;
      } else {
        draw_succeeded = IssueDraw(
            vgt_draw_initiator.prim_type, vgt_draw_initiator.num_indices,
            is_indexed ? &index_buffer_info : nullptr,
            xenos::IsMajorModeExplicit(vgt_draw_initiator.major_mode,
                                       vgt_draw_initiator.prim_type));
        if (!draw_succeeded) {
          XELOGE("{}({}, {}, {}): Failed in backend", opcode_name,
                 vgt_draw_initiator.num_indices,
                 uint32_t(vgt_draw_initiator.prim_type),
                 uint32_t(vgt_draw_initiator.source_select));
        }
      }
      // --- Blue Dragon native-draw HLE (Half A/B): synthetic-PM4 submission.
      // The DXVK/Cemu-model front-end feeds ONE clean synthetic PM4 stream (a
      // single DRAW_INDX built from the decoded draw contract) back through
      // xenia's EXISTING ExecutePacket -> ExecutePacketType3Draw -> IssueDraw
      // decode, on the CP worker thread where register_file_ owns the state.
      // Runs for EVERY covered foliage draw (the whole field pass) into the
      // shared decoupled full-surface RT. The thread-local guard stops the
      // synthetic DRAW_INDX from recursively re-entering this block. Default
      // off (gpu_bd_native_hle).
      if (nhle_cover && draw_succeeded) {
        ++bd_native_emits_this_frame_;
        // Minimal self-contained DRAW_INDX packet, layout per
        // ExecutePacketType3_DRAW_INDX: [type3 hdr | viz_token |
        // VGT_DRAW_INITIATOR | VGT_DMA_BASE | VGT_DMA_SIZE]. Stored big-endian
        // (guest order) so ExecutePacket's ReadAndSwap decodes it exactly like
        // a real guest packet. Buffer padded (8 dwords) larger than the 5-dword
        // payload: RingBuffer::set_write_offset does (offset % capacity), so a
        // capacity EQUAL to the payload would wrap write_offset to 0 ==
        // read_offset and make the stream read as EMPTY (read_count()==0) - the
        // packet would silently never execute. Keep capacity (32B) > 20B.
        uint32_t pm4[8] = {};
        pm4[0] = xe::byte_swap(MakePacketType3(PM4_DRAW_INDX, 4));
        pm4[1] = xe::byte_swap(uint32_t(0));  // viz token = unconditional
        pm4[2] = xe::byte_swap(
            register_file_->values[XE_GPU_REG_VGT_DRAW_INITIATOR]);
        pm4[3] = xe::byte_swap(register_file_->values[XE_GPU_REG_VGT_DMA_BASE]);
        pm4[4] = xe::byte_swap(register_file_->values[XE_GPU_REG_VGT_DMA_SIZE]);
        RingBuffer synth_reader(reinterpret_cast<uint8_t*>(pm4), sizeof(pm4));
        synth_reader.set_write_offset(5 * sizeof(uint32_t));

        // Half B: optionally redirect this native draw's color RT to a
        // non-aliasing EDRAM base so it renders into its OWN dedicated
        // full-surface host RT, SHARED across the whole foliage pass (all
        // covered draws use the same base, so they accumulate into one RT which
        // the backend presents/dumps at swap). Keep MSAA + depth as BD's (the
        // depth write is idempotent). Saved + restored around the emit so BD's
        // live draws are unaffected.
        uint32_t saved_color_info =
            register_file_->values[XE_GPU_REG_RB_COLOR_INFO];
        uint32_t decouple_base = cvars::gpu_bd_native_hle_decouple;
        if (decouple_base) {
          uint32_t redirected =
              (saved_color_info & ~uint32_t(0xFFF)) | (decouple_base & 0xFFF);
          WriteRegister(XE_GPU_REG_RB_COLOR_INFO, redirected);
          BdArmDecoupledCapture(true);
        }

        uint32_t idc_before = BdDebugIssueDrawCount();
        // Diagnostic (gated by gpu_bd_native_hle): the read_count BEFORE the
        // loop distinguishes the RingBuffer capacity==payload WRAP bug
        // (read_count==0 => loop skipped => packet never executes, ok stays a
        // loop-skip-safe 1) from a genuine reach into ExecutePacket/IssueDraw.
        uint32_t synth_read_count_before = uint32_t(synth_reader.read_count());
        s_in_bd_native_emit = true;
        bool synth_ok = true;
        while (synth_reader.read_count()) {
          if (!ExecutePacket(&synth_reader)) {
            synth_ok = false;
            break;
          }
        }
        s_in_bd_native_emit = false;
        uint32_t idc_delta = BdDebugIssueDrawCount() - idc_before;

        if (decouple_base) {
          BdArmDecoupledCapture(false);
          WriteRegister(XE_GPU_REG_RB_COLOR_INFO, saved_color_info);
        }
        // Throttled: the per-frame native emit COUNT is logged at swap; log
        // only the first few individual emits to confirm the pipe end to end.
        static std::atomic<uint32_t> s_native_log{0};
        if (s_native_log.fetch_add(1) < 4) {
          XELOGI(
              "BD NATIVE-HLE: field draw via SYNTHETIC DRAW_INDX PM4 -> "
              "ExecutePacket -> IssueDraw on CP thread (prim={} idx={} pitch={} "
              "foliage={} synth_read_count={} ok={} decouple_base={} replace={} "
              "issuedraw_delta={})",
              uint32_t(vgt_draw_initiator.prim_type),
              uint32_t(vgt_draw_initiator.num_indices), nhle_pitch,
              nhle_is_foliage ? 1 : 0, synth_read_count_before, synth_ok ? 1 : 0,
              decouple_base, cvars::gpu_bd_native_hle_replace ? 1 : 0, idc_delta);
        }
      }
    }
  }

  // If read the packed correctly, but merely couldn't execute it (because of,
  // for instance, features not supported by the host), don't terminate command
  // buffer processing as that would leave rendering in a way more inconsistent
  // state than just a single dropped draw command.
  return true;
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // "initiate fetch of index buffer and draw"
  // Generally used by Xbox 360 Direct3D 9 for kDMA and kAutoIndex sources.
  // With a viz query token as the first one.
  uint32_t count_remaining = count;
  assert_not_zero(count_remaining);
  if (!count_remaining) {
    XELOGE("PM4_DRAW_INDX: Packet too small, can't read the viz query token");
    return false;
  }
  uint32_t viz_query_condition = reader->ReadAndSwap<uint32_t>();
  --count_remaining;
  return ExecutePacketType3Draw(reader, packet, "PM4_DRAW_INDX",
                                viz_query_condition, count_remaining);
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX_2(RingBuffer* reader,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // "draw using supplied indices in packet"
  // Generally used by Xbox 360 Direct3D 9 for kAutoIndex source.
  // No viz query token.
  return ExecutePacketType3Draw(reader, packet, "PM4_DRAW_INDX_2", 0, count);
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT(RingBuffer* reader,
                                                       uint32_t packet,
                                                       uint32_t count) {
  // load constant into chip and to memory
  // PM4_REG(reg) ((0x4 << 16) | (GSL_HAL_SUBBLOCK_OFFSET(reg)))
  //                                     reg - 0x2000
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0x7FF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  switch (type) {
    case 0:  // ALU
      index += 0x4000;
      break;
    case 1:  // FETCH
      index += 0x4800;
      break;
    case 2:  // BOOL
      index += 0x4900;
      break;
    case 3:  // LOOP
      index += 0x4908;
      break;
    case 4:  // REGISTERS
      index += 0x2000;
      break;
    default:
      assert_always();
      reader->AdvanceRead((count - 1) * sizeof(uint32_t));
      return true;
  }
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->ReadAndSwap<uint32_t>();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT2(RingBuffer* reader,
                                                        uint32_t packet,
                                                        uint32_t count) {
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->ReadAndSwap<uint32_t>();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_LOAD_ALU_CONSTANT(RingBuffer* reader,
                                                            uint32_t packet,
                                                            uint32_t count) {
  // load constants from memory
  uint32_t address = reader->ReadAndSwap<uint32_t>();
  address &= 0x3FFFFFFF;
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0x7FF;
  uint32_t size_dwords = reader->ReadAndSwap<uint32_t>();
  size_dwords &= 0xFFF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  switch (type) {
    case 0:  // ALU
      index += 0x4000;
      break;
    case 1:  // FETCH
      index += 0x4800;
      break;
    case 2:  // BOOL
      index += 0x4900;
      break;
    case 3:  // LOOP
      index += 0x4908;
      break;
    case 4:  // REGISTERS
      index += 0x2000;
      break;
    default:
      assert_always();
      return true;
  }
  trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);
  for (uint32_t n = 0; n < size_dwords; n++, index++) {
    uint32_t data = xe::load_and_swap<uint32_t>(
        memory_->TranslatePhysical(address + n * 4));
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_SHADER_CONSTANTS(
    RingBuffer* reader, uint32_t packet, uint32_t count) {
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->ReadAndSwap<uint32_t>();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // load sequencer instruction memory (pointer-based)
  uint32_t addr_type = reader->ReadAndSwap<uint32_t>();
  auto shader_type = static_cast<xenos::ShaderType>(addr_type & 0x3);
  uint32_t addr = addr_type & ~0x3;
  uint32_t start_size = reader->ReadAndSwap<uint32_t>();
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  trace_writer_.WriteMemoryRead(CpuToGpu(addr), size_dwords * 4);
  auto shader =
      LoadShader(shader_type, addr, memory_->TranslatePhysical<uint32_t*>(addr),
                 size_dwords);
  switch (shader_type) {
    case xenos::ShaderType::kVertex:
      active_vertex_shader_ = shader;
      break;
    case xenos::ShaderType::kPixel:
      active_pixel_shader_ = shader;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD_IMMEDIATE(RingBuffer* reader,
                                                            uint32_t packet,
                                                            uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // load sequencer instruction memory (code embedded in packet)
  uint32_t dword0 = reader->ReadAndSwap<uint32_t>();
  uint32_t dword1 = reader->ReadAndSwap<uint32_t>();
  auto shader_type = static_cast<xenos::ShaderType>(dword0);
  uint32_t start_size = dword1;
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  assert_true(reader->read_count() >= size_dwords * 4);
  assert_true(count - 2 >= size_dwords);
  auto shader =
      LoadShader(shader_type, uint32_t(reader->read_ptr()),
                 reinterpret_cast<uint32_t*>(reader->read_ptr()), size_dwords);
  switch (shader_type) {
    case xenos::ShaderType::kVertex:
      active_vertex_shader_ = shader;
      break;
    case xenos::ShaderType::kPixel:
      active_pixel_shader_ = shader;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  reader->AdvanceRead(size_dwords * sizeof(uint32_t));
  return true;
}

bool CommandProcessor::ExecutePacketType3_INVALIDATE_STATE(RingBuffer* reader,
                                                           uint32_t packet,
                                                           uint32_t count) {
  // selective invalidation of state pointers
  /*uint32_t mask =*/reader->ReadAndSwap<uint32_t>();
  // driver_->InvalidateState(mask);
  return true;
}

bool CommandProcessor::ExecutePacketType3_VIZ_QUERY(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // begin/end initiator for viz query extent processing
  // https://www.google.com/patents/US20050195186
  assert_true(count == 1);

  uint32_t dword0 = reader->ReadAndSwap<uint32_t>();

  uint32_t id = dword0 & 0x3F;
  uint32_t end = dword0 & 0x100;
  if (!end) {
    // begin a new viz query @ id
    // On hardware this clears the internal state of the scan converter (which
    // is different to the register)
    WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, VIZQUERY_START);
    XELOGGPU("Begin viz query ID {:02X}", id);
  } else {
    // end the viz query
    WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, VIZQUERY_END);
    XELOGGPU("End viz query ID {:02X}", id);
    // The scan converter writes the internal result back to the register here.
    // We just fake it and say it was visible in case it is read back.
    if (id < 32) {
      register_file_->values[XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0] |= uint32_t(1)
                                                                     << id;
    } else {
      register_file_->values[XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1] |=
          uint32_t(1) << (id - 32);
    }
  }

  return true;
}

void CommandProcessor::InitializeTrace() {
  // Write the initial register values, to be loaded directly into the
  // RegisterFile since all registers, including those that may have side
  // effects on setting, will be saved.
  trace_writer_.WriteRegisters(0, register_file_->values,
                               RegisterFile::kRegisterCount, false);

  trace_writer_.WriteGammaRamp(gamma_ramp_256_entry_table(),
                               gamma_ramp_pwl_rgb(), gamma_ramp_rw_component_);
}

}  // namespace gpu
}  // namespace xe
