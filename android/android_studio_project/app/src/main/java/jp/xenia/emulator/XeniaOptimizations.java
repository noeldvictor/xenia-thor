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
    public static final String CATEGORY_CHEATS = "Cheats · trainers";

    /** Pref key of the trainer master toggle (also used by the trainer screen). */
    public static final String PREF_KEY_TRAINERS = "opt_trainers";

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





        // ---- Coarse shading (VRS). Two ALTERNATIVES, not two features. ----
        // Both write gpu_vrs_foliage_rate, and applyTo runs in list order, so
        // if a user enables both the PERFORMANCE one wins (it is listed second).
        // That is stated in its description rather than hidden.
        list.add(new Optimization(
                "opt_vrs_balanced",
                "Smoother transparencies (balanced)",
                "Speeds up smoke, foliage and other see-through effects by "
                        + "shading them at half detail. Recommended.",
                "Games like Blue Dragon stack a lot of see-through layers - "
                        + "grass, smoke, glows, water - on top of each other. Every "
                        + "layer has to be drawn over the ones behind it, and that "
                        + "is usually what makes these games run slowly.\n\n"
                        + "This shades those see-through layers in 2x1 blocks "
                        + "instead of pixel by pixel, so the GPU does about half the "
                        + "work on them. Solid things - characters, buildings, "
                        + "ground, menus and text - are NOT affected and stay sharp.\n\n"
                        + "Measured on Blue Dragon on this device: 15.3 -> 20.4 fps "
                        + "(+33%). Cost: foliage looks slightly softer if you look "
                        + "closely.\n\n"
                        + "If you want more speed and do not mind a blurrier "
                        + "picture, use the Performance option instead.",
                // ⭐ DEFAULT-ON since 2026-08-17. It shipped OFF, which meant BD ran at
                // 15.5-15.7 fps in heavy scenes while 21.1 was one toggle away and
                // nobody was toggling it - measured same-session, same build, heavy
                // buckets n=248-356: no VRS 15.5/15.7, VRS 2x1 21.1 (+35%).
                // The cost is the one a user already described from the panel:
                // "slightly softer foliage". Solid geometry, text and UI are
                // untouched because the gate is the blend/alpha-test state.
                // Still a QUALITY TRADE, so it stays a toggle and honours the
                // per-game override - this changes the DEFAULT, not the choice.
                CATEGORY_GPU, true, true,
                null,
                new IntCvar[]{new IntCvar("gpu_vrs_foliage_rate", 1)}));

        list.add(new Optimization(
                "opt_vrs_performance",
                "Smoother transparencies (performance)",
                "Same idea, twice as coarse. Faster, but visibly blurrier.",
                "The stronger version of the balanced option: see-through "
                        + "layers are shaded in 2x2 blocks instead of 2x1, so the GPU "
                        + "does about a quarter of the work on them.\n\n"
                        + "Measured on Blue Dragon on this device: 15.3 -> 25.0 fps "
                        + "(+63%), against +33% for balanced.\n\n"
                        + "The trade is real and you will see it: transparent "
                        + "effects look noticeably blurry, not just soft. Try "
                        + "balanced first and only move here if you need the extra "
                        + "frames.\n\n"
                        + "If both this and the balanced option are turned on, this "
                        + "one takes effect.",
                CATEGORY_GPU, false, false,
                null,
                new IntCvar[]{new IntCvar("gpu_vrs_foliage_rate", 2)}));

        // Listed AFTER the other two so that applyTo (which runs in list order)
        // lets this one win if a user enables several - it is the best-measured
        // of the three.
        list.add(new Optimization(
                "opt_vrs_heavy_scenes",
                "Smoother transparencies (heavy scenes only)",
                "Half detail on see-through layers, and quarter detail only in "
                        + "the busiest scenes. Best speed for the quality.",
                "The other two options treat every part of the frame the same. "
                        + "This one does not.\n\n"
                        + "Most of what the console draws is cheap - menus, text, "
                        + "the HUD, simple backgrounds. In Blue Dragon, 61 of the 74 "
                        + "drawing steps in a frame contain a single object. Two of "
                        + "them contain around 890 stacked see-through layers, and "
                        + "those two are 81% of all the GPU time.\n\n"
                        + "So this option only drops to quarter detail once a "
                        + "drawing step is already piling up layers. Menus, text and "
                        + "simple scenes keep the finer detail; the heavy scenes get "
                        + "the extra speed where you cannot easily see it.\n\n"
                        + "Measured on Blue Dragon on this device: 21.1 -> 24.5 fps "
                        + "(+16%) on top of the balanced option, in the heaviest "
                        + "scenes.\n\n"
                        + "NOT YET EYE-CHECKED: the speed is measured, but nobody "
                        + "has confirmed on-screen that it looks better than the "
                        + "Performance option. If see-through effects shimmer while "
                        + "the camera moves, turn it off and tell us.",
                CATEGORY_GPU, false, false,
                null,
                new IntCvar[]{new IntCvar("gpu_vrs_foliage_rate", 1),
                              new IntCvar("gpu_vrs_heavy_pass_rate", 2),
                              new IntCvar("gpu_vrs_heavy_pass_draws", 16)}));















        // NOTE: the JIT leaf-inlining lever (cvar arm64_jit_inline_leaf) is NOT
        // offered as a user toggle - it is pixel-correct on Blue Dragon but
        // CRASHES Burnout Revenge in early boot (SIGABRT, decStrong over-release
        // in the present path; device-tested 2026-06-13). The straight-line-leaf
        // splice has a title-specific miscompile to root-cause before it can be a
        // safe option. The cvar stays in-tree (default-off) for investigation.


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









        // Binning-front-end levers. Blue Dragon's heavy scene is GPU-bound on the
        // Adreno binning stage (~1100-2180 tiny draws, ~263k verts/frame), which
        // bins per-vertex per-draw BEFORE culling - so the only lever is reducing
        // the draws/vertices SUBMITTED. These are alternative strategies; enable
        // ONE at a time and compare at a heavy scene. Experimental (the binning
        // floor is largely hardware; per-triangle culling measured a net loss, so
        // it is deliberately not offered).
















        list.add(new Optimization(
                PREF_KEY_TRAINERS,
                "Xbox 360 trainers (cheats)",
                "Load Aurora-style .xex trainers for the launched game and run "
                        + "them (infinite health/ammo/money + the in-game menu).",
                "Aurora/RGH consoles run \"trainers\" - small Xbox 360 .xex programs "
                        + "that hook the running game to apply cheats and draw an on-"
                        + "screen menu (usually START+BACK). This loads any trainer you "
                        + "have downloaded for the title (the per-game \"Trainers\" "
                        + "screen) from the app's trainers/ folder, matched by title id, "
                        + "and runs its entry so it installs its hooks. A trainer is just "
                        + "guest code the emulator already runs - measured import "
                        + "coverage on xenia's kernel is ~89-100%. OFF by default: it "
                        + "runs third-party guest code and is build-specific (a trainer "
                        + "must match the game's exact title update, or the game can "
                        + "misbehave). Download a matching trainer first, then enable "
                        + "this per-game.",
                CATEGORY_CHEATS, false, false,
                new BoolCvar[]{new BoolCvar("trainer_enable"),
                        new BoolCvar("trainer_run_entry")}, null));

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
