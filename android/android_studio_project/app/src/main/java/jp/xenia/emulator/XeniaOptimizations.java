package jp.xenia.emulator;

import android.content.SharedPreferences;
import android.os.Bundle;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Single source of truth for the user-selectable performance optimizations.
 *
 * Every little optimization adds up. Each entry here is a perf win that the
 * native engine reads as a cvar; the Settings UI renders it as an explained
 * toggle ({@link SettingsActivity}), and {@link XeniaAndroidSettings} wires the
 * enabled entries into the emulator launch arguments. The user can stack the
 * toggles to see how the little advances compound into big advances.
 *
 * To add a new optimization: add ONE {@link Optimization} entry to {@link #ALL}
 * (it auto-appears in the UI and auto-wires its cvar) and allowlist the cvar in
 * {@link EmulatorActivity} so the launch extra is honored.
 */
public final class XeniaOptimizations {
    public static final String CATEGORY_CPU = "CPU · recompiler";
    public static final String CATEGORY_THREADS = "CPU · thread placement";
    public static final String CATEGORY_GPU = "GPU · Vulkan";

    // Per-game override tri-state: a title may force an optimization ON or OFF
    // for itself, or INHERIT the global toggle. Stored as a string under a
    // title-scoped pref key ("" / absent = inherit). This is how the user sets
    // every optimization per game from the UI; the launch path resolves the
    // effective value with {@link Optimization#isEnabledForGame}.
    public static final int OVERRIDE_DEFAULT = 0;
    public static final int OVERRIDE_ON = 1;
    public static final int OVERRIDE_OFF = 2;

    /** A boolean cvar set to true when the owning optimization is enabled. */
    static final class BoolCvar {
        final String name;

        BoolCvar(final String name) {
            this.name = name;
        }
    }

    /** An int cvar set to onValue when the owning optimization is enabled. */
    static final class IntCvar {
        final String name;
        final int onValue;

        IntCvar(final String name, final int onValue) {
            this.name = name;
            this.onValue = onValue;
        }
    }

    /** One toggleable, explained performance optimization. */
    public static final class Optimization {
        public final String prefKey;
        public final String title;
        public final String summary;
        public final String detail;
        public final String category;
        public final boolean defaultEnabled;
        public final boolean recommended;
        final BoolCvar[] boolCvars;
        final IntCvar[] intCvars;

        Optimization(
                final String prefKey,
                final String title,
                final String summary,
                final String detail,
                final String category,
                final boolean defaultEnabled,
                final boolean recommended,
                final BoolCvar[] boolCvars,
                final IntCvar[] intCvars) {
            this.prefKey = prefKey;
            this.title = title;
            this.summary = summary;
            this.detail = detail;
            this.category = category;
            this.defaultEnabled = defaultEnabled;
            this.recommended = recommended;
            this.boolCvars = boolCvars;
            this.intCvars = intCvars;
        }

        public boolean isEnabled(final SharedPreferences prefs) {
            return prefs.getBoolean(prefKey, defaultEnabled);
        }

        /**
         * Effective enabled state for a specific title: a per-game override
         * (ON/OFF) wins; otherwise inherit the global toggle. Empty titleId =
         * global (no per-game layer), so unprofiled launches are unchanged.
         */
        public boolean isEnabledForGame(
                final SharedPreferences prefs, final String titleId) {
            switch (getOverride(prefs, titleId, prefKey)) {
                case OVERRIDE_ON:
                    return true;
                case OVERRIDE_OFF:
                    return false;
                default:
                    return isEnabled(prefs);
            }
        }
    }

    public static final List<Optimization> ALL;

    static {
        final ArrayList<Optimization> list = new ArrayList<>();

        list.add(new Optimization(
                "opt_flat_membase",
                "Flat guest memory addressing",
                "Folds every guest load/store into one indexed CPU instruction.",
                "PowerPC loads and stores normally emit an extra address add before "
                        + "each access. This folds the guest address straight into the "
                        + "ARM64 indexed load/store ([base, reg]) so the hottest path in "
                        + "the recompiler does one instruction instead of two. "
                        + "Device-validated pixel-correct on Blue Dragon.",
                CATEGORY_CPU, true, true,
                new BoolCvar[]{new BoolCvar("arm64_use_flat_membase")}, null));

        list.add(new Optimization(
                "opt_rlwinm_shift",
                "Fast rotate/shift codegen",
                "Compiles common PowerPC rotate-and-mask ops to a single shift.",
                "The PowerPC rlwinm instruction (rotate-left then mask) is one of "
                        + "the most frequent ops in Xbox 360 code - array indexing, "
                        + "struct offsets, bit extracts. The recompiler normally emits "
                        + "a duplicate + 64-bit rotate + mask (5-6 host instructions) "
                        + "even when the op is really a plain shift. This recognizes the "
                        + "slwi/srwi shift forms and emits a single ARM shift instead. "
                        + "Device-validated correct on Blue Dragon.",
                CATEGORY_CPU, true, true,
                new BoolCvar[]{new BoolCvar("ppc_rlwinm_shift_fastpath")}, null));

        list.add(new Optimization(
                "opt_rlwinm_mask",
                "Fast bitfield-mask codegen",
                "Compiles PowerPC bitfield-mask ops (clrlwi/clrrwi/extract) to a single 32-bit AND.",
                "A second very common rlwinm form masks a bitfield without "
                        + "rotating - clearing the top or bottom bits of a word, or "
                        + "extracting a contiguous bitfield. The recompiler otherwise "
                        + "emits a 64-bit duplicate + rotate + mask; this collapses it "
                        + "to one ARM AND. Device-validated correct on Gears of War.",
                CATEGORY_CPU, true, true,
                new BoolCvar[]{new BoolCvar("ppc_rlwinm_mask_fastpath")}, null));

        list.add(new Optimization(
                "opt_rlwinm_general",
                "Fast general rotate-and-mask codegen",
                "Compiles rotate-then-mask bitfield extracts to a 32-bit rotate + AND.",
                "The general PowerPC rlwinm form (rotate the low word, then mask a "
                        + "non-wrapping field) is used for packed-field extraction. "
                        + "This emits one 32-bit rotate + AND instead of the 64-bit "
                        + "duplicate+rotate+mask (~3-4 fewer host instructions). "
                        + "Experimental: bit-exact for non-wrapping masks; wrapping "
                        + "masks safely use the original path.",
                CATEGORY_CPU, true, false,
                new BoolCvar[]{new BoolCvar("ppc_rlwinm_general_fastpath")}, null));

        list.add(new Optimization(
                "opt_cr_logical_self",
                "Fast condition-register idioms",
                "Simplifies common condition-register bit idioms (clear/set/copy/not).",
                "Compilers emit condition-register bit ops where the two source "
                        + "bits are the same (crclr/crset/crmove/crnot). The "
                        + "recompiler otherwise loads both bits and does a logical "
                        + "op; this recognizes the idiom and emits a single store. "
                        + "In-game validated (Burnout race A/B) + BD 3D pixel-correct.",
                CATEGORY_CPU, true, false,
                new BoolCvar[]{new BoolCvar("ppc_cr_logical_self_fastpath")}, null));

        list.add(new Optimization(
                "opt_vsplt_swizzle",
                "Fast vector splat codegen",
                "Compiles vector lane-splat (vspltw) to a single NEON dup.",
                "The PowerPC vspltw broadcasts one 32-bit lane of a vector to all "
                        + "four. The recompiler otherwise round-trips through a "
                        + "general-purpose register; this emits a single NEON dup, "
                        + "avoiding the vector->GP->vector stall. Experimental, "
                        + "pending in-game validation.",
                CATEGORY_CPU, true, false,
                new BoolCvar[]{new BoolCvar("ppc_vsplt_swizzle_fastpath")}, null));

        list.add(new Optimization(
                "opt_vand_self",
                "Fast self-AND vector codegen",
                "Folds vand/vandc of a register with itself to a copy or zero.",
                "When a PowerPC vector AND (vand) or AND-complement (vandc) uses "
                        + "the same register for both inputs, the result is just a "
                        + "copy or a zero. This recognizes that and drops the "
                        + "redundant load + logic, mirroring the shipped vor/vxor "
                        + "self-ops. Experimental, pending in-game validation.",
                CATEGORY_CPU, true, false,
                new BoolCvar[]{new BoolCvar("ppc_vand_self_fastpath")}, null));

        list.add(new Optimization(
                "opt_algebraic_identities",
                "Algebraic identity simplification",
                "Folds redundant integer math (x+0, x*1, x<<0 ...) out of code.",
                "The recompiler sometimes emits redundant integer ops - adding "
                        + "zero, multiplying by one, shifting by zero. This folds "
                        + "those identities away before register allocation so they "
                        + "never reach the CPU. Integer-only (float identities are "
                        + "unsafe). Device-validated correct on Blue Dragon.",
                CATEGORY_CPU, true, true,
                new BoolCvar[]{new BoolCvar("hir_algebraic_identities")}, null));

        list.add(new Optimization(
                "opt_known_bits_mask_fold",
                "Redundant mask elimination (known-bits)",
                "Drops AND masks a known-bits analysis proves are no-ops.",
                "Emulating 32-bit PowerPC ops and zero-extending loads on the "
                        + "64-bit ARM registers leaves redundant AND masks. A "
                        + "known-bits analysis proves when a mask keeps every bit a "
                        + "value could possibly have, and drops it - fewer host "
                        + "instructions in branchy integer code. Bit-exact; "
                        + "host-validated + device-regression-clean on Gears.",
                CATEGORY_CPU, true, true,
                new BoolCvar[]{new BoolCvar("hir_known_bits_mask_fold")}, null));

        list.add(new Optimization(
                "opt_const_range_fold",
                "Constant-zero range folding (known-bits)",
                "Folds ops that provably produce zero to a constant.",
                "Using the same known-bits analysis, folds an integer op to a "
                        + "constant 0 when its operands' possible bits prove the "
                        + "result cannot have any bit set - an AND with a "
                        + "non-overlapping mask, or a right shift that moves every "
                        + "possible bit out. Common in bitfield and shifted-load "
                        + "code. Bit-exact; host-validated + device-regression-"
                        + "clean on Gears.",
                CATEGORY_CPU, true, true,
                new BoolCvar[]{new BoolCvar("hir_const_range_fold")}, null));

        list.add(new Optimization(
                "opt_and_not_fold",
                "Fused AND-NOT codegen",
                "Compiles 'x AND (NOT y)' into a single ARM64 instruction.",
                "When recompiled code computes x AND (NOT y) - common after other "
                        + "passes expose a NOT feeding an AND - the recompiler normally "
                        + "emits a separate NOT then AND. This fuses them into one ARM64 "
                        + "BIC (bit-clear), one instruction instead of two. Bit-exact "
                        + "(AND(x, NOT(y)) equals AND_NOT(x, y)), proven by host unit "
                        + "tests; device-validated no-regression on Blue Dragon.",
                CATEGORY_CPU, true, true,
                new BoolCvar[]{new BoolCvar("hir_fold_and_not")}, null));

        // NOTE: the JIT leaf-inlining lever (cvar arm64_jit_inline_leaf) is NOT
        // offered as a user toggle - it is pixel-correct on Blue Dragon but
        // CRASHES Burnout Revenge in early boot (SIGABRT, decStrong over-release
        // in the present path; device-tested 2026-06-13). The straight-line-leaf
        // splice has a title-specific miscompile to root-cause before it can be a
        // safe option. The cvar stays in-tree (default-off) for investigation.

        list.add(new Optimization(
                "opt_present_refresh_capped",
                "Frame-rate limiter (cooler, less battery)",
                "Caps the frame rate so light/loading/menu screens don't render "
                        + "hundreds of fps and overheat the device.",
                "By default the emulator renders frames as fast as it can. On "
                        + "light/loading/menu/FMV screens that means HUNDREDS of fps "
                        + "with the GPU pegged at ~99%, overheating the handheld in "
                        + "seconds for no visible benefit (device-measured: Lost "
                        + "Odyssey's loading screen hit 943fps / 72.5C). Capping the "
                        + "display present alone (FIFO) does NOT help, because the "
                        + "guest keeps RENDERING every frame; this also paces the "
                        + "guest swap host-side to ~60fps so the GPU stops doing the "
                        + "wasted render work. Device-validated: the same LO loading "
                        + "screen dropped 943fps -> 61fps with GPU busy 99% -> 15%. "
                        + "Real gameplay is unaffected - Xbox 360 games are 30/60fps, "
                        + "at or below the cap, and the limiter never sleeps below it "
                        + "(Blue Dragon stays at its ~5.9fps baseline).",
                CATEGORY_GPU, true, true,
                new BoolCvar[]{new BoolCvar("vulkan_present_refresh_capped")},
                new IntCvar[]{new IntCvar("gpu_frame_limit_fps", 60)}));

        list.add(new Optimization(
                "opt_frame_gen",
                "Frame generation (smoother motion)",
                "Inserts a synthesized in-between frame so 30fps gameplay presents at ~60.",
                "Xbox 360 games like Blue Dragon run their game logic at a fixed 30fps "
                        + "that no emulator setting can raise (speeding the guest up just "
                        + "fast-forwards the game). Frame generation leaves the 30fps logic "
                        + "untouched and synthesizes an extra frame between each pair of real "
                        + "ones, so the screen presents at ~60 for smoother motion. The guest "
                        + "frame rate, game speed and save data are unchanged; this is "
                        + "perceived smoothness, not faster gameplay, and it does not reduce "
                        + "input latency. EXPERIMENTAL: the current synth frame is a 50% "
                        + "cross-fade of the two latest frames, so fast camera motion can show "
                        + "mild ghosting (a motion-warp upgrade is in progress). Best on "
                        + "GPU-light scenes (towns/menus) where the GPU has idle time for the "
                        + "extra frame; neutral-to-negative on the heaviest GPU-bound scenes. "
                        + "Default off.",
                CATEGORY_GPU, false, false,
                new BoolCvar[]{new BoolCvar("present_frame_extrapolation")},
                new IntCvar[]{new IntCvar("present_frame_gen_factor", 2)}));

        list.add(new Optimization(
                "opt_constants_arena",
                "Dynamic constant streaming",
                "Streams shader constants through one persistent GPU buffer.",
                "Instead of allocating and writing a fresh descriptor set for shader "
                        + "constants on every draw, constants stream through a persistent "
                        + "unified-memory ring that is bound once and selected per draw "
                        + "with a dynamic offset. Cut per-draw setup CPU (cpu_bind_us) "
                        + "by ~15-25% on Blue Dragon; device-validated pixel-correct.",
                CATEGORY_GPU, true, true,
                new BoolCvar[]{new BoolCvar("vulkan_dynamic_constants_arena")}, null));

        list.add(new Optimization(
                "opt_hoist_residency_lock",
                "Hoisted memory-residency lock",
                "Locks shared memory once per draw instead of once per buffer.",
                "Before each draw the emulator makes the guest's vertex and "
                        + "memory-export buffers resident, and each of those calls "
                        + "separately grabs and releases the shared-memory lock. This "
                        + "grabs the lock once around the whole batch (the lock is "
                        + "reentrant, so the result is identical) - fewer lock "
                        + "round-trips on the hottest per-draw path. Pure CPU hygiene; "
                        + "no rendering change.",
                CATEGORY_GPU, true, true,
                new BoolCvar[]{new BoolCvar("vulkan_hoist_request_range_lock")}, null));

        list.add(new Optimization(
                "opt_lazy_completion_polls",
                "Non-blocking GPU completion checks",
                "Stops fence-status checks from secretly waiting for the GPU.",
                "On the Turnip driver over Qualcomm's KGSL kernel, simply ASKING "
                        + "whether GPU work has finished (a fence status check) blocks "
                        + "until it actually finishes: the driver issues a zero-timeout "
                        + "kernel wait, and the kernel defines a zero timeout as "
                        + "wait-forever. The emulator checked eagerly at every frame "
                        + "start and every submission, so the CPU silently waited out a "
                        + "full GPU frame, every frame - CPU and GPU ran one-after-the-"
                        + "other instead of in parallel. This skips every check a frame "
                        + "doesn't strictly need: frames-in-flight rises 2 to 3 and the "
                        + "CPU and GPU truly overlap. Device-validated on Burnout "
                        + "Revenge: the frame-open wait fell 5.4 ms to 3 us and "
                        + "per-frame CPU draw cost fell 70%, pixel-correct, GPU time "
                        + "unchanged.",
                CATEGORY_GPU, true, true,
                new BoolCvar[]{new BoolCvar("vulkan_lazy_completion_polls")}, null));

        list.add(new Optimization(
                "opt_gate_rt_update",
                "Skip redundant render-target updates",
                "Reuses the render-target setup across draws that don't change it.",
                "Before every draw the emulator recomputes the render-target "
                        + "configuration (which color/depth buffers, their formats, the "
                        + "EDRAM layout). In a busy scene that runs thousands of times a "
                        + "frame although the targets change only a handful of times. "
                        + "This skips the recompute when the render-target config is "
                        + "byte-identical to the last draw AND the render pass is still "
                        + "open - a pass break, EDRAM transfer or frame boundary forces a "
                        + "real update - so the redundant work is removed with no "
                        + "rendering change. Device-validated: Burnout Revenge in-race "
                        + "5.9 -> 7.9 fps (+34%; the per-draw RT cost fell ~16ms -> ~2ms), "
                        + "pixel-correct; Blue Dragon pixel-correct and unchanged (it is "
                        + "GPU-bound, so the CPU saving is free headroom). Helps "
                        + "CPU-bound, draw-heavy scenes.",
                CATEGORY_GPU, true, true,
                new BoolCvar[]{new BoolCvar("vulkan_gate_rt_update")}, null));

        list.add(new Optimization(
                "opt_pipeline_cache",
                "Persistent shader pipeline cache",
                "Saves compiled GPU pipelines to disk so later launches skip the stutter.",
                "The first time a game reaches a new effect, the GPU driver must "
                        + "compile its shader pipeline, causing a brief hitch. This saves "
                        + "the driver's compiled pipeline cache to the app's storage and "
                        + "reloads it on the next launch, so those one-time hitches mostly "
                        + "disappear on later runs of the same game. Safe: the driver "
                        + "validates the cache and ignores it if it doesn't match (e.g. "
                        + "after a driver change). Smooths stutter; does not change the "
                        + "steady frame rate.",
                CATEGORY_GPU, true, true,
                new BoolCvar[]{new BoolCvar("vulkan_persistent_pipeline_cache")}, null));

        list.add(new Optimization(
                "opt_fp16_pixel",
                "FP16 pixel shaders (experimental)",
                "Runs pixel-shader math at half precision on the Adreno's native FP16.",
                "The Adreno 740 has dedicated FP16 (half-precision) shader math that "
                        + "our shaders don't use today. This marks pixel-shader float "
                        + "math as relaxed precision so the Turnip driver can run it at "
                        + "FP16, cutting shader-core and memory-bandwidth cost. It only "
                        + "touches pixel shaders, which never compute geometry position, "
                        + "so it can't break the 3D shape - but it can slightly shift "
                        + "colors or texture sharpness, so it's experimental and per-game. "
                        + "Helps shader/fill-heavy scenes, not the Blue Dragon binning "
                        + "bottleneck. Use the Turnip driver for the FP16 benefit.",
                CATEGORY_GPU, false, false,
                new BoolCvar[]{new BoolCvar("spirv_fp16_relaxed_pixel_alu")}, null));

        list.add(new Optimization(
                "opt_fp10_color_32bpp",
                "Compact HDR color buffer (32-bit, experimental)",
                "Stores the 7e3 HDR color render target at 32 bits instead of 64, "
                        + "halving its bandwidth.",
                "Xbox 360 games (Blue Dragon's dominant color format) render into a "
                        + "10-bit-per-channel floating HDR buffer (\"7e3\"). The emulator "
                        + "currently emulates that with a 64-bit-per-pixel half-float "
                        + "render target, which costs double the memory bandwidth - and on "
                        + "this handheld's shared memory, color-buffer bandwidth is part of "
                        + "the GPU cost. This stores it as a 32-bit 10-bit-per-channel "
                        + "buffer instead (half the bytes, still a full 10 bits per "
                        + "channel - sharper than the 8-bit diagnostic fallback). The "
                        + "tradeoff: it clamps the format's extended brightness range to "
                        + "[0,1], which is correct for ordinary (SDR) scenes but can clip "
                        + "very bright bloom/glow highlights - so it's experimental and "
                        + "per-game, and best left off for HDR-bloom-heavy titles. "
                        + "Device-measured on Blue Dragon's heavy field: ~10.5ms / ~8.5% "
                        + "off the GPU frame (123.0 -> 112.5 ms) at a matched scene, and "
                        + "visually correct on ordinary (SDR) scenes. The tradeoff is real "
                        + "though: bright HDR highlights (e.g. battle bloom) get clipped, "
                        + "so it stays off by default - a per-game SDR-vs-speed choice. No "
                        + "effect on titles that don't use the 7e3 format.",
                CATEGORY_GPU, false, false,
                new BoolCvar[]{new BoolCvar("gpu_fp10_color_as_unorm10")}, null));

        list.add(new Optimization(
                "opt_2101010_10bit",
                "Full 10-bit color buffer (quality, experimental)",
                "Stores the 10-bit color render target at full 10-bit precision instead "
                        + "of the 8-bit fallback - no speed cost, less banding.",
                "Some Xbox 360 games render into a 10-bit-per-channel color buffer "
                        + "(the non-float 2:10:10:10 format). The emulator currently "
                        + "emulates it with an 8-bit-per-channel buffer, silently throwing "
                        + "away 2 bits per channel (visible banding in smooth gradients "
                        + "like skies). This keeps the full 10 bits at the same 32-bit "
                        + "size, so it's a pure quality win with no bandwidth or speed "
                        + "cost. Only affects titles that use this format. Experimental, "
                        + "pending device validation.",
                CATEGORY_GPU, false, false,
                new BoolCvar[]{new BoolCvar("gpu_2101010_color_as_unorm10")}, null));

        // Binning-front-end levers. Blue Dragon's heavy scene is GPU-bound on the
        // Adreno binning stage (~1100-2180 tiny draws, ~263k verts/frame), which
        // bins per-vertex per-draw BEFORE culling - so the only lever is reducing
        // the draws/vertices SUBMITTED. These are alternative strategies; enable
        // ONE at a time and compare at a heavy scene. Experimental (the binning
        // floor is largely hardware; per-triangle culling measured a net loss, so
        // it is deliberately not offered).
        list.add(new Optimization(
                "opt_whole_draw_cull",
                "Whole-draw offscreen cull (broken)",
                "Skips draws judged off-screen - currently over-culls on Blue Dragon.",
                "Computes each draw's screen extent on the CPU and skips draws "
                        + "judged entirely off-screen so the GPU never bins them. "
                        + "KNOWN ISSUE (device-tested 2026-06-04): the affine extent "
                        + "test is too aggressive and OVER-CULLS on Blue Dragon - it "
                        + "blanks most of the scene (282 vs 263k vertices). Left OFF "
                        + "pending a fix to the extent test; not recommended to enable.",
                CATEGORY_GPU, false, false,
                new BoolCvar[]{
                        new BoolCvar("gpu_cull_compaction"),
                        new BoolCvar("gpu_whole_draw_only"),
                        new BoolCvar("gpu_cull_fast_replay"),
                        new BoolCvar("gpu_cull_fast_only"),
                }, null));

        list.add(new Optimization(
                "opt_draw_concat",
                "Draw concatenation",
                "Merges back-to-back same-state draws into one GPU draw call.",
                "Consecutive draws that share all GPU state get their index data "
                        + "appended into one transient index buffer and submitted as a "
                        + "SINGLE draw call, so the GPU pays its per-draw front-end cost "
                        + "(binning setup, context rolls) once per run instead of once "
                        + "per draw. Device-measured on Back to the Future's heavy menu: "
                        + "~180-195 fewer GPU draws per frame (~24% of the draw stream) "
                        + "for ~1ms (~2.4%) off the GPU frame, pixel-correct - about 5us "
                        + "of GPU time saved per eliminated draw. Biggest effect on "
                        + "draw-heavy scenes (Burnout races submit ~3400 draws/frame). "
                        + "Experimental until validated per-game; alternative to MDI "
                        + "batching - try one at a time and A/B at a heavy scene.",
                CATEGORY_GPU, false, false,
                new BoolCvar[]{new BoolCvar("vulkan_merge_draws_rewrite")}, null));

        list.add(new Optimization(
                "opt_mdi_batch",
                "Multi-draw indirect batching",
                "Submits many small same-state draws as one indirect command.",
                "Batches consecutive same-state draws into a single "
                        + "multiDrawIndirect call (each keeps its own index range, so "
                        + "strips batch too), cutting per-draw CPU + submission cost. "
                        + "Experimental; alternative to draw concatenation - try one "
                        + "at a time and A/B at a heavy scene.",
                CATEGORY_GPU, false, false,
                new BoolCvar[]{new BoolCvar("vulkan_merge_draws_indirect")}, null));

        list.add(new Optimization(
                "opt_foliage_thin",
                "Aggressive foliage reduction (performance mode)",
                "Thins alpha-test foliage/grass to cut GPU overdraw on heavy 3D scenes.",
                "Alpha-test foliage (grass, leaves, detail props) is often the "
                        + "single biggest GPU cost on heavy 3D scenes: its cut-out edges "
                        + "defeat the Adreno's hidden-surface optimization (LRZ), so "
                        + "every overlapping layer of foliage is fully shaded - massive "
                        + "overdraw. Device-measured on Blue Dragon's heavy scene: "
                        + "alpha-test foliage is ~43% of the entire GPU frame. This keeps "
                        + "1 of every 4 foliage draws and collapses the rest, trading "
                        + "foliage density for speed - Blue Dragon's scene went 865ms -> "
                        + "583ms per GPU frame (~1.48x, ~1.2 -> ~1.7 fps), foliage thinner "
                        + "but the scene intact. Correct and safe (it only reduces "
                        + "guest-drawn foliage geometry, never position/3D shape); the "
                        + "visual cost is sparser grass/detail. Helps alpha-test-heavy "
                        + "scenes; no effect where there is no foliage. Experimental, a "
                        + "per-game visual tradeoff.",
                CATEGORY_GPU, false, false,
                null,
                new IntCvar[]{new IntCvar("gpu_foliage_thin_factor", 4)}));

        list.add(new Optimization(
                "opt_blended_thin",
                "Reduce transparency effects (performance mode)",
                "Thins alpha-blended draws (fog/glow/shadows/particles) to cut GPU overdraw.",
                "Alpha-blended transparency (fog, soft shadows, glows, particles) "
                        + "cannot use the GPU's early-depth rejection, so overlapping "
                        + "transparent layers all get fully shaded - heavy overdraw. "
                        + "Device-measured on Blue Dragon's heavy scene: blended "
                        + "transparency is ~34% of the GPU frame. This keeps 1 of every "
                        + "2 blended draws and collapses the rest. Stacks with "
                        + "'Aggressive foliage reduction': both together took Blue "
                        + "Dragon's scene from 865ms to ~390ms per GPU frame (~2.2x, "
                        + "~1.2 -> ~2.5 fps) with the scene still intact. Riskier than "
                        + "foliage thinning - transparency is more load-bearing (fog/"
                        + "lighting mood), so it can visibly thin atmospheric effects. "
                        + "Correct/safe (only reduces blended geometry, never 3D shape); "
                        + "experimental, a per-game visual tradeoff.",
                CATEGORY_GPU, false, false,
                null,
                new IntCvar[]{new IntCvar("gpu_blended_thin_factor", 2)}));

        list.add(new Optimization(
                "opt_vrs_foliage",
                "Coarse-shade foliage (variable rate shading)",
                "Uses the GPU's hardware Variable Rate Shading to shade dense foliage "
                        + "at a lower rate, cutting per-pixel cost without removing geometry.",
                "The Adreno 740 supports hardware Variable Rate Shading "
                        + "(VK_KHR_fragment_shading_rate) - it can run one pixel-shader "
                        + "invocation per 2x2 block of pixels instead of per pixel. Xenia "
                        + "ships with it unused. This enables it for the overdraw-heavy "
                        + "foliage class (alpha-test grass/leaves + alpha-blended "
                        + "transparency), where the cut-out/transparent edges defeat the "
                        + "GPU's early-depth rejection and every overlapping layer is fully "
                        + "shaded. Coarse-shading that class cuts the per-covered-pixel "
                        + "shader + alpha-test-discard + texture-fetch work. Device-measured "
                        + "2026-06-20 (single-run scene-matched A/B): on Blue Dragon's "
                        + "GPU-bound foliage field, -17.8% GPU frame time on light foliage and "
                        + "-21.8% on the HEAVY foliage field (125.9->98.5ms) - the win scales "
                        + "with foliage density; plus -7.5% on the Burnout race (46.0->42.6ms). "
                        + "The fps gain lands on GPU-BOUND scenes (BD's field is the big one), "
                        + "and it's a GPU-time/power saving "
                        + "on scenes bound elsewhere (Qualcomm cites up to ~30% on coarse passes). Unlike "
                        + "'Aggressive foliage reduction' it keeps ALL the foliage - it "
                        + "just shades it more coarsely, so the tradeoff is slightly "
                        + "softer/blockier foliage edges, not sparser foliage. Stacks with "
                        + "the thinning toggles. Set to 2 for 2x2 (recommended) or 4 for "
                        + "4x4 (more aggressive). Only takes effect on devices that expose "
                        + "the extension (both Thor drivers do); inert otherwise. "
                        + "Experimental, a per-game visual tradeoff.",
                CATEGORY_GPU, false, false,
                null,
                new IntCvar[]{new IntCvar("gpu_vrs_foliage_rate", 2)}));

        list.add(new Optimization(
                "opt_prime_core_router",
                "Prime-core GPU-command priority",
                "Pins the GPU-command thread to the 3.19 GHz Cortex-X3 and raises "
                        + "its priority so the GPU never starves waiting on it.",
                "The Snapdragon 8 Gen 2 has one 3.19 GHz Cortex-X3, four ~2.8 GHz mid "
                        + "cores and three 2.0 GHz efficiency cores. Left alone the OS "
                        + "parks the emulator's hot threads on a slow 2.0 GHz core AND "
                        + "lets busy guest threads deschedule the GPU-command thread - so "
                        + "the GPU sits idle waiting for the next batch of work to be "
                        + "submitted. This (1) pins the GPU-command thread to the X3, (2) "
                        + "raises its scheduling priority (nice) so the OS keeps it running "
                        + "even when the guest threads are busy, and (3) keeps the guest "
                        + "threads on the big cluster. Device-validated 2026-06-07: Back "
                        + "to the Future's DeLorean scene went ~24 -> ~30 fps (its 30fps "
                        + "target), GPU frame time ~41.5 -> ~26 ms and GPU busy 66% -> 82% "
                        + "(the command thread was being descheduled; prioritizing it fed "
                        + "the GPU continuously). Raising priority does NOT take a core "
                        + "away from the guest, so it is safe for CPU-bound titles too. "
                        + "Scheduler hints only, with a safe fallback if the OS rejects them.",
                CATEGORY_THREADS, true, true,
                null,
                new IntCvar[]{
                        new IntCvar("thor_gpu_thread_affinity_cpu", 7),
                        new IntCvar("thor_guest_thread_affinity_mask", -1),
                        // nice -15: above the guest worker threads (-10) so the
                        // GPU-command thread wins scheduling and feeds the GPU,
                        // but BELOW the Android audio output thread (AudioTrack
                        // is nice -16) so it never preempts audio -> no glitches.
                        new IntCvar("gpu_cp_worker_nice", -15),
                }));

        list.add(new Optimization(
                "opt_hot_thread_prime",
                "Auto hot-thread on prime core",
                "Continuously keeps whatever thread is busiest on the 3.19 GHz "
                        + "Cortex-X3 - adapts per scene instead of guessing one thread.",
                "The 'Prime-core GPU-command priority' option above always gives "
                        + "the X3 to the GPU-command thread, which is right only when the "
                        + "GPU is the bottleneck. Many scenes are the opposite: the GPU "
                        + "sits idle while one CPU thread can't feed it fast enough - the "
                        + "\"CPU makes the GPU slow\" case. Device-measured on Blue "
                        + "Dragon's heavy field (2026-06-24): the GPU is idle ~98% and "
                        + "the GPU-command thread is blocked on the GPU, while a single "
                        + "guest game-logic thread runs at ~95% CPU with five cores idle - "
                        + "and that hot thread is NOT the command thread NOR the guest "
                        + "'main' thread (~14%), so any fixed pin guesses wrong. This adds "
                        + "a light monitor that samples which thread is actually busiest a "
                        + "few times a second and pins THAT one to the X3, releasing the "
                        + "previous one - so the real bottleneck always runs on the fastest "
                        + "core, whether it's the GPU-command thread (GPU-bound titles) or "
                        + "the hot guest thread (CPU-bound titles). It also frees the "
                        + "static command-thread pin so the two don't fight. Scheduling "
                        + "hint only, no rendering change. Experimental, pending device A/B.",
                CATEGORY_THREADS, false, false,
                null,
                new IntCvar[]{
                        new IntCvar("thor_hot_thread_prime_core", 7),
                        // Release the static command-thread X3 pin so the dynamic
                        // tracker is the sole owner of the prime core (it will pin
                        // the command thread there itself when it's the hottest).
                        new IntCvar("thor_gpu_thread_affinity_cpu", -1),
                }));

        list.add(new Optimization(
                "opt_adpf_perf_hints",
                "ADPF performance hints",
                "Tells Android which thread is frame-critical so it boosts the "
                        + "right core and saves power when there's slack.",
                "Android 12+ exposes the Dynamic Performance Framework (ADPF) "
                        + "Performance Hint API: the app reports how long the "
                        + "frame-critical GPU-command thread takes each frame versus the "
                        + "target frame budget, and the OS raises that core's clock when "
                        + "frames run long and lowers it (saving battery and heat) when "
                        + "there is slack - smarter than fixed core pinning, which Google "
                        + "says ADPF supersedes on Android 12+. Pure scheduling hint: it "
                        + "does not change rendering. No effect on ROMs without the API. "
                        + "Device-validated functional + safe on the Thor (2026-06-07); "
                        + "the scheduling benefit is largest on CPU-bound and loading scenes.",
                CATEGORY_THREADS, true, true,
                new BoolCvar[]{new BoolCvar("gpu_adpf_performance_hints")}, null));

        list.add(new Optimization(
                "opt_adpf_thermal",
                "ADPF thermal auto-throttle (experimental)",
                "Eases the frame rate down as the device heats up, before Android "
                        + "throttles it hard.",
                "Reads the OS thermal headroom each frame (ADPF Thermal API) and "
                        + "pre-emptively lowers the present cap (45/30/20 fps as the "
                        + "device approaches the throttling threshold) so the fanless "
                        + "handheld sheds heat gradually instead of the OS slamming the "
                        + "clocks down. Composes with the frame-rate limiter (takes the "
                        + "lower cap). No effect on ROMs without the API. Experimental.",
                CATEGORY_THREADS, false, false,
                new BoolCvar[]{new BoolCvar("gpu_adpf_thermal_throttle")}, null));

        list.add(new Optimization(
                "opt_object_handle_cache",
                "Lock-free object-handle cache (experimental)",
                "Skips the global kernel lock on repeated object-handle lookups.",
                "Every guest wait/handle/object operation (NtWaitForSingleObject, "
                        + "events, mutexes, threads, files) looks up its kernel object "
                        + "under a single global lock; on multi-threaded games that lock "
                        + "is contended ~20% of CPU time (profiled on Gears). This adds a "
                        + "small per-thread cache so repeated lookups of the same handle "
                        + "skip the lock entirely, kept correct by a table generation "
                        + "counter (any handle add/remove forces a re-lookup) and by "
                        + "holding a reference to each cached object. Helps CPU-bound "
                        + "multi-threaded titles; no effect on GPU-bound scenes. "
                        + "Experimental - validate per device.",
                CATEGORY_CPU, false, false,
                new BoolCvar[]{new BoolCvar("kernel_object_handle_cache")}, null));

        list.add(new Optimization(
                "opt_native_object_fast_path",
                "Lock-free event/wait resolve (experimental)",
                "Skips the global kernel lock when resolving an event/timer/"
                        + "semaphore that's already in use.",
                "Every KeSetEvent / KeWaitForSingleObject the game makes resolves "
                        + "its guest object to the host object under one global kernel "
                        + "lock - even though, after the first use, the object's handle "
                        + "is already recorded and never changes. On a multi-threaded "
                        + "title all the guest threads serialize through that lock on "
                        + "every event/wait, which is a large part of why the CPU can't "
                        + "feed the GPU. This skips the lock on the already-initialized "
                        + "path (the common case) and only locks on a brand-new object; "
                        + "any race safely falls back to the locked path. Pairs with the "
                        + "lock-free object-handle cache above to make a steady-state "
                        + "event/wait fully lock-free. Helps CPU-bound, thread-heavy "
                        + "titles; no effect on the GPU. Experimental - validate per "
                        + "device.",
                CATEGORY_CPU, false, false,
                new BoolCvar[]{new BoolCvar("kernel_native_object_fast_path")}, null));

        list.add(new Optimization(
                "opt_drop_redundant_atomic_barrier",
                "Drop redundant atomic barrier (experimental)",
                "Removes a duplicate memory fence after every guest atomic store.",
                "PowerPC store-conditional (the core of every lock, refcount and "
                        + "atomic the game uses) is compiled to an ARM64 acquire+release "
                        + "atomic, which already orders the store against both earlier and "
                        + "later memory accesses. The recompiler then emitted a SECOND "
                        + "full memory barrier right after it - redundant work on one of "
                        + "the hottest paths in multi-threaded game code. This drops the "
                        + "duplicate fence. It is memory-model-safe (the atomic itself "
                        + "still publishes the write); the much more common lightweight "
                        + "barrier (lwsync) is deliberately left untouched because it "
                        + "needs ordering ARM64's cheaper fences don't provide. Helps "
                        + "CPU-bound, lock-heavy titles; no GPU effect. Experimental - "
                        + "validate rendering per device.",
                CATEGORY_CPU, false, false,
                new BoolCvar[]{new BoolCvar("cpu_drop_redundant_atomic_release_barrier")},
                null));

        list.add(new Optimization(
                "opt_xma_skip_idle_lock",
                "Audio decoder lock fix",
                "Stops the audio decoder from spin-locking hundreds of idle voices "
                        + "every loop while music plays.",
                "The Xbox 360 has 320 hardware audio voices; the emulator's audio "
                        + "decoder worker scans ALL of them every loop and grabbed each "
                        + "voice's lock BEFORE checking whether it actually had audio to "
                        + "decode. While any sound plays the worker never sleeps, so it "
                        + "spin-locked the ~318 idle voices at full speed - device-"
                        + "profiled as ~13% of the ENTIRE CPU on Blue Dragon's field, the "
                        + "single biggest host-side hotspot, stealing cores from the "
                        + "game's own thread (which is what caps the frame rate). This "
                        + "checks each voice's enabled flag without the lock and only "
                        + "locks the few that have work. Output-identical (idle voices "
                        + "make no sound either way); helps every game with audio, most "
                        + "on CPU-bound titles. Recommended on.",
                CATEGORY_CPU, true, true,
                new BoolCvar[]{new BoolCvar("apu_xma_skip_idle_context_lock")},
                new IntCvar[]{new IntCvar("apu_xma_worker_poll_ms", 1)}));

        list.add(new Optimization(
                "opt_lockfree_check_global_lock",
                "Lock-free interrupt-state read",
                "Stops the guest's 'read interrupt state' step from taking a "
                        + "global lock just to read one number.",
                "Xbox 360 game code constantly runs tiny critical sections - it "
                        + "disables interrupts, does an atomic reserve / store-"
                        + "conditional, then re-enables (the mtmsr/lwarx/stwcx pattern "
                        + "behind every lock, refcount and atomic). The emulator "
                        + "serializes those through one global mutex. The catch: the "
                        + "'read MSR' (mfmsr) step that begins each section was taking a "
                        + "FULL mutex lock+unlock just to read whether the lock is held "
                        + "- and on the Snapdragon that lock/unlock is the single most "
                        + "expensive thing in Blue Dragon's CPU-bound field (a storm of "
                        + "16-bit compare-and-swap, swap and futex calls that tops the "
                        + "profile). mfmsr only needs to know if THIS thread is already "
                        + "in a critical section, which the lock owner's thread-id and "
                        + "depth - already published atomically by enter/leave - answer "
                        + "exactly, with no mutex at all. The real critical sections "
                        + "still serialize, so behavior is unchanged; this just deletes "
                        + "the wasteful read-side lock. Device-validated on Blue "
                        + "Dragon's field: renders identically and cuts the mutex cost "
                        + "~24% (the read no longer touches the lock at all) - about 1% "
                        + "of the frame there, more on lock-heavy CPU-bound titles "
                        + "(Burnout, Gears). Output-identical; no GPU effect. "
                        + "Recommended on.",
                CATEGORY_CPU, true, true,
                new BoolCvar[]{new BoolCvar("cpu_lockfree_check_global_lock")},
                null));

        ALL = Collections.unmodifiableList(list);
    }

    /** Apply every globally-enabled optimization's cvars (no per-game layer). */
    public static void applyTo(
            final SharedPreferences prefs, final Bundle launchArguments) {
        applyTo(prefs, launchArguments, "");
    }

    /**
     * Apply every optimization enabled FOR THIS TITLE into the launch arguments:
     * a per-game override (ON/OFF) wins over the global toggle. Empty titleId
     * falls back to the global toggles, so unprofiled launches are unchanged.
     */
    public static void applyTo(
            final SharedPreferences prefs,
            final Bundle launchArguments,
            final String titleId) {
        for (final Optimization opt : ALL) {
            if (!opt.isEnabledForGame(prefs, titleId)) {
                continue;
            }
            if (opt.boolCvars != null) {
                for (final BoolCvar cvar : opt.boolCvars) {
                    launchArguments.putBoolean(cvar.name, true);
                }
            }
            if (opt.intCvars != null) {
                for (final IntCvar cvar : opt.intCvars) {
                    launchArguments.putInt(cvar.name, cvar.onValue);
                }
            }
        }
    }

    /** Number of optimizations currently enabled globally (UI "N of M active"). */
    public static int enabledCount(final SharedPreferences prefs) {
        int enabled = 0;
        for (final Optimization opt : ALL) {
            if (opt.isEnabled(prefs)) {
                enabled++;
            }
        }
        return enabled;
    }

    /** Number of optimizations enabled for a specific title (per-game effective). */
    public static int enabledCountForGame(
            final SharedPreferences prefs, final String titleId) {
        int enabled = 0;
        for (final Optimization opt : ALL) {
            if (opt.isEnabledForGame(prefs, titleId)) {
                enabled++;
            }
        }
        return enabled;
    }

    // ---- Per-game overrides -------------------------------------------------

    /** SharedPreferences key holding a title's override for one optimization. */
    public static String perGameKey(final String titleId, final String prefKey) {
        return "game_opt::" + normalizeTitleId(titleId) + "::" + prefKey;
    }

    /** Current override for (title, optimization): DEFAULT / ON / OFF. */
    public static int getOverride(
            final SharedPreferences prefs,
            final String titleId,
            final String prefKey) {
        if (prefs == null || titleId == null || titleId.isEmpty()) {
            return OVERRIDE_DEFAULT;
        }
        final String value = prefs.getString(perGameKey(titleId, prefKey), "");
        if ("on".equals(value)) {
            return OVERRIDE_ON;
        }
        if ("off".equals(value)) {
            return OVERRIDE_OFF;
        }
        return OVERRIDE_DEFAULT;
    }

    /** Set (or clear, for DEFAULT) a title's override for one optimization. */
    public static void setOverride(
            final SharedPreferences prefs,
            final String titleId,
            final String prefKey,
            final int state) {
        if (prefs == null || titleId == null || titleId.isEmpty()) {
            return;
        }
        final SharedPreferences.Editor editor = prefs.edit();
        final String key = perGameKey(titleId, prefKey);
        if (state == OVERRIDE_ON) {
            editor.putString(key, "on");
        } else if (state == OVERRIDE_OFF) {
            editor.putString(key, "off");
        } else {
            editor.remove(key);
        }
        editor.apply();
    }

    /** How many optimizations this title has explicitly pinned (ON or OFF). */
    public static int overrideCount(
            final SharedPreferences prefs, final String titleId) {
        if (prefs == null || titleId == null || titleId.isEmpty()) {
            return 0;
        }
        int count = 0;
        for (final Optimization opt : ALL) {
            if (getOverride(prefs, titleId, opt.prefKey) != OVERRIDE_DEFAULT) {
                count++;
            }
        }
        return count;
    }

    /** Reset all of a title's per-game overrides back to inheriting the global. */
    public static void clearOverrides(
            final SharedPreferences prefs, final String titleId) {
        if (prefs == null || titleId == null || titleId.isEmpty()) {
            return;
        }
        final SharedPreferences.Editor editor = prefs.edit();
        for (final Optimization opt : ALL) {
            editor.remove(perGameKey(titleId, opt.prefKey));
        }
        editor.apply();
    }

    private static String normalizeTitleId(final String titleId) {
        return titleId == null ? "" : titleId.trim().toUpperCase(java.util.Locale.US);
    }

    private XeniaOptimizations() {
    }
}
