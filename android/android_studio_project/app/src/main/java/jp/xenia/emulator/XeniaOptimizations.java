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
                        + "New - validate before relying on it.",
                CATEGORY_CPU, false, false,
                new BoolCvar[]{new BoolCvar("ppc_rlwinm_shift_fastpath")}, null));

        list.add(new Optimization(
                "opt_algebraic_identities",
                "Algebraic identity simplification",
                "Folds redundant integer math (x+0, x*1, x<<0 ...) out of code.",
                "The recompiler sometimes emits redundant integer ops - adding "
                        + "zero, multiplying by one, shifting by zero. This folds "
                        + "those identities away before register allocation so they "
                        + "never reach the CPU. Integer-only (float identities are "
                        + "unsafe). New - validate before relying on it.",
                CATEGORY_CPU, false, false,
                new BoolCvar[]{new BoolCvar("hir_algebraic_identities")}, null));

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
                "opt_prime_core_router",
                "Prime-core thread router",
                "Runs the hottest threads on the 3.19 GHz Cortex-X3 prime core.",
                "The Snapdragon 8 Gen 2 has one 3.19 GHz Cortex-X3, four ~2.8 GHz mid "
                        + "cores and three 2.0 GHz efficiency cores. Left alone the OS can "
                        + "park the emulator's hot guest and GPU-command threads on a slow "
                        + "2.0 GHz core. This pins the GPU-command thread to the X3 and "
                        + "keeps the guest threads on the big cluster - up to a ~1.6x clock "
                        + "ratio on the single-threaded critical path. Scheduler hints "
                        + "only, with a safe fallback if the OS rejects them.",
                CATEGORY_THREADS, true, true,
                null,
                new IntCvar[]{
                        new IntCvar("thor_gpu_thread_affinity_cpu", 7),
                        new IntCvar("thor_guest_thread_affinity_mask", -1),
                }));

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
