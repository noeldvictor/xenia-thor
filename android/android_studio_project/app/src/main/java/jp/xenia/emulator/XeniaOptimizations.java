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
                        + "shader + alpha-test-discard + texture-fetch work (Qualcomm "
                        + "reports up to ~30% GPU-power savings on coarse passes). Unlike "
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

        ALL = Collections.unmodifiableList(list);
    }

    /** Apply every enabled optimization's cvars into the launch arguments. */
    public static void applyTo(
            final SharedPreferences prefs, final Bundle launchArguments) {
        for (final Optimization opt : ALL) {
            if (!opt.isEnabled(prefs)) {
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

    /** Number of optimizations currently enabled (for the UI "N of M active"). */
    public static int enabledCount(final SharedPreferences prefs) {
        int enabled = 0;
        for (final Optimization opt : ALL) {
            if (opt.isEnabled(prefs)) {
                enabled++;
            }
        }
        return enabled;
    }

    private XeniaOptimizations() {
    }
}
