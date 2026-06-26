package jp.xenia.emulator;

import android.os.Bundle;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Built-in per-game "best known options" profiles, keyed by the 8-hex-digit
 * upper-case Xbox 360 title-id (the same key used for save dirs + patch files).
 *
 * <p>Each profile is a small set of emulator cvar overrides that we have
 * device-validated to fix or unblock a specific game. They are applied into the
 * launch {@link Bundle} (the intent-extra channel) which is the ONLY layer that
 * beats the device's persisted {@code files/xenia.config.toml} - a compiled C++
 * default or a {@code config/<id>.config.toml} would be silently overridden by
 * that file (the long-standing "phantom config" gotcha). They are applied AFTER
 * the global {@link XeniaOptimizations} so a per-game win overrides the global
 * default, but BEFORE the user's adb/debug extras so explicit intent still wins.
 *
 * <p>Strictly a no-op for any title that is not in the table, so unprofiled
 * games launch byte-identically to before (zero regression).
 */
public final class GameProfiles {
    private GameProfiles() {}

    /** A single cvar override - a tagged union of the Bundle put-types. */
    private static final class Cvar {
        final String name;
        final Object value; // Boolean | Integer | String
        final String why;

        Cvar(final String name, final Object value, final String why) {
            this.name = name;
            this.value = value;
            this.why = why;
        }
    }

    private static final class Profile {
        final String displayName;
        final List<Cvar> cvars = new ArrayList<>();

        Profile(final String displayName) {
            this.displayName = displayName;
        }

        Profile add(final String name, final Object value, final String why) {
            cvars.add(new Cvar(name, value, why));
            return this;
        }
    }

    private static final Map<String, Profile> PROFILES = new HashMap<>();

    static {
        // Blue Dragon (4D5307DF): UMA-direct shared memory present-hangs the
        // title (black screen, VdSwap=0, GPU 0%) even with the smart-sync
        // guards on. Device-validated 2026-05-31. Keep it off for this title.
        PROFILES.put("4D5307DF", new Profile("Blue Dragon")
                .add("gpu_uma_direct_shared_memory", Boolean.FALSE,
                        "UMA-direct present-hangs Blue Dragon; keep it off")
                .add("xboxkrnl_ntreadfile_force_complete", Boolean.TRUE,
                        "BD reads its pack files (item_rec.ipk etc.) async; its "
                        + "async-completion path never consumes the (already-read) "
                        + "result, so it polls STATUS_PENDING forever -> white-screen "
                        + "boot stall (the same 'false dirty-disc' async race as "
                        + "Banjo). Forcing synchronous completion routes BD to its "
                        + "working sync-consume path -> it boots reliably to the 3D "
                        + "field. Device-validated 2026-06-20 (reached rendered=2250 / "
                        + "263k-vert foliage field; STATUS_PENDING polls 0).")
                .add("gpu_vrs_foliage_rate", Integer.valueOf(2),
                        "Adreno hardware VRS 2x2 coarse-shades BD's overdraw-heavy "
                        + "foliage + blended transparency. Device-validated 2026-06-20: "
                        + "-21.8% GPU frame time on the heavy foliage field "
                        + "(125.9->98.5ms, scene-matched A/B via the single-run "
                        + "alternation validator); BD is GPU-bound so this lands as real "
                        + "fps (~5.9->7.9 in the field). Slightly softer foliage edges, "
                        + "visually clean. Default-on for this GPU-bound priority title.")
                .add("kernel_object_handle_cache", Boolean.TRUE,
                        "BD's heavy field is actually CPU/LOCK-bound, not GPU-bound "
                        + "(device-profiled 2026-06-23: GPU idle ~98%, busy 0%, turnip "
                        + "driver 0.25%; the CPU bottleneck is global_critical_region "
                        + "contention from ObjectTable handle lookups). The lock-free "
                        + "per-thread handle cache clears it: live +8% fps (13.3->14.4 "
                        + "VdSwap/s), stacking on the default-on kernel-call-logging fix "
                        + "(kernel_call_log_skip_discarded) for ~+25% from the 11.5 "
                        + "baseline. Correctness-verified (generation counter + per-entry "
                        + "ref, no use-after-free). Targeted: helps lock-bound BD, "
                        + "net-flat on low-contention titles so it stays per-title here.")
                .add("gpu_frame_limit_fps", Integer.valueOf(30),
                        "Blue Dragon is a 30fps-native Xbox 360 title. Cap the host "
                        + "frame limiter at 30 so light/menu scenes hold a steady 30 "
                        + "(instead of oscillating up to 60) and the guest loop is "
                        + "paced - throttling CPU+GPU work cuts heat/power, which also "
                        + "extends the thermal budget for the GPU-heavy foliage field. "
                        + "30 is the target, not a regression: the heavy field is below "
                        + "the cap regardless; the cap only smooths the scenes that "
                        + "exceed it.")
                .add("kernel_display_resolution", "720p",
                        "Render Blue Dragon at 720p by default. BD's heavy field is "
                        + "CPU/lock-bound, NOT fragment/fill-bound (device-profiled "
                        + "2026-06-23: GPU idle ~98% on the heavy field; resolution "
                        + "barely moves its frame time), so the higher resolution is "
                        + "near-free here while giving a much sharper image than 480p. "
                        + "The 30fps cap above and the full optimization stack keep it "
                        + "paced; validated on-device at 720p."));

        // Burnout Revenge (454107DC): UMA-direct present-hangs the EA/EAHD/CRRW
        // intro movie chain (VdSwap freezes). Off runs the movies through to a
        // live menu ~55-63fps. Device-validated (A/B, docs 20260530).
        PROFILES.put("454107DC", new Profile("Burnout Revenge")
                .add("gpu_uma_direct_shared_memory", Boolean.FALSE,
                        "UMA-direct present-hangs the intro movies; keep it off")
                .add("gpu_frame_limit_fps", Integer.valueOf(60),
                        "Burnout Revenge is a 60fps-native Xbox 360 title - cap the "
                        + "host limiter at 60 (its native ceiling). Device-validated "
                        + "2026-06-25: with the full global optimization stack on "
                        + "(gate_rt_update, dynamic_constants_arena, lazy_completion_"
                        + "polls + the CPU fast-paths), the race runs 46.2fps (VdSwap "
                        + "462/10s, 0 faults, pixel-correct) - up from a confounded "
                        + "14.83 measured with a PARTIAL stack. cpu_issuedraw collapsed "
                        + "43.8->14.9ms (cpu_rt 10->1ms via gate_rt_update), gpu_frame "
                        + "45->23.6ms. Solidly in the 30-60 target.")
                .add("gpu_cp_worker_nice", Integer.valueOf(-15),
                        "Raise the GPU-command (CP) thread above the guest workers so "
                        + "it isn't descheduled under contention - Burnout's Main thread "
                        + "busy-waits on the GPU ring read-pointer, so a continuously-fed "
                        + "CP shortens that bubble. Part of the validated 46.2fps stack.")
                .add("gpu_vrs_foliage_rate", Integer.valueOf(2),
                        "Adreno hardware VRS 2x2 coarse-shades Burnout's overdraw-heavy "
                        + "blended draws. Device-validated -46% earlier; part of the "
                        + "46.2fps full-stack measurement. Keeps all geometry (fragment-"
                        + "shading coarsen only, not polygon reduction)."));

        // Infinite Undiscovery (535107DB): the default 65536 a64 stackpoints
        // array overflows ~37s in (Overflowed stackpoints! -> SIGABRT) from a
        // longjmp stackpoint leak; 262144 (4x) reaches the title/difficulty
        // menu. Device-validated; regression-checked safe on Blue Dragon.
        PROFILES.put("535107DB", new Profile("Infinite Undiscovery")
                .add("a64_max_stackpoints", Integer.valueOf(262144),
                        "Needs a larger guest-stack array or it crashes ~37s in"));

        // MagnaCarta 2 (4E4D080B): the a64 longjmp stackpoint array overflows
        // even the global 262144 (Overflowed stackpoints! -> SIGABRT crash at
        // boot via the play button). The leak is BOUNDED - 1048576 (4x) keeps it
        // under the cap through boot. Device-validated 2026-06-06: at 1048576 the
        // title boots to in-game (a story-dialogue scene, ~30fps OSD, VdSwap
        // flowing) instead of crashing. Per-game so only this title pays the
        // larger per-thread stackpoint allocation.
        PROFILES.put("4E4D080B", new Profile("MagnaCarta 2")
                .add("a64_max_stackpoints", Integer.valueOf(1048576),
                        "Needs a larger guest-stack array or it crashes at boot"));

        // Back to the Future: The Game (443607D6): GPU-bound DeLorean gameplay
        // ran ~24fps (gpu_frame ~41.5ms) NOT because of the GPU work itself but
        // because the guest threads descheduled the GPU-command (CP) thread, so
        // the GPU starved ~16ms/frame waiting on command submission (busy 66%).
        // FIX: raise the CP thread's scheduling priority (nice -19) so the OS
        // keeps it running under guest-thread contention -> the GPU is fed
        // continuously -> gpu_frame ~26ms, BTTF reaches its 30fps target (busy
        // 82%, now frame-paced not GPU-starved). Device-validated 2026-06-07:
        // gpu_cp_worker_nice=-19 gave gpu_frame ~25.7ms / VdSwap 30-31/s / busy
        // 82% at the DeLorean scene (cvar-confirmed "worker nice set to -19"),
        // reproducing the earlier X3-affinity result (two runs ~25.4/26.1ms) but
        // WITHOUT stealing a core from the guest -> safe even for CPU-bound
        // titles. (Supersedes the per-game X3 affinity cvars: same win, safer
        // mechanism, no core-steal.)
        PROFILES.put("443607D6", new Profile("Back to the Future: The Game")
                .add("gpu_cp_worker_nice", Integer.valueOf(-15),
                        "Raise the GPU-command thread's priority above the guest workers (-10) so it isn't descheduled -> GPU fed -> 30fps; -15 stays below the audio thread (-16) to avoid audio glitches")
                .add("gpu_vrs_foliage_rate", Integer.valueOf(2),
                        "Adreno hardware VRS 2x2 coarse-shades BTTF's overdraw-heavy "
                        + "blended-transparency draws (its VRS-eligible class; this "
                        + "title renders alphatest=0, all transparency is blended). "
                        + "Device-validated 2026-06-20: -16.7% to -19.9% GPU frame time "
                        + "on transparency-heavy gameplay (37.0->30.8ms, scene-matched "
                        + "A/B by blended-draw count, 1633 samples). BTTF is GPU-bound so "
                        + "this stacks with the CP-priority fix toward its 30fps target. "
                        + "Mild transparency-edge softening, visually clean."));

        // Gears of War (4D5307D5): a 30fps-native UE3 title, heavily multi-threaded
        // and LOCK-bound (object-handle lookups under the global kernel lock were
        // device-profiled at ~20% of CPU on Gears). The global default-on stack
        // already serves it well: the lock-free object-handle cache + native-object
        // fast-path clear the lock contention, and the Turnip fence fix (lazy
        // completion polls) overlaps CPU/GPU (menu device-measured ~18->29.6fps).
        // Profile adds the native 30fps cap so it paces cleanly.
        PROFILES.put("4D5307D5", new Profile("Gears of War")
                .add("gpu_frame_limit_fps", Integer.valueOf(30),
                        "Gears of War is a 30fps-native Xbox 360 title - cap the host "
                        + "limiter at 30 (its native ceiling) so it paces cleanly. The "
                        + "global default-on stack (lock-free object-handle cache + "
                        + "native-object fast-path for its global-lock contention, plus "
                        + "the Turnip fence fix) supplies the speed."));

        // Lost Odyssey (4D5307FA): 30fps-native JRPG. KNOWN BOOT BLOCKER - it can
        // hang on a stuck LOADING screen (a file-not-found IO stall, device-observed
        // 2026-05-30; NOT fixed by xboxkrnl_ntreadfile_force_complete, that was ruled
        // out for LO specifically). The loading screen also pegged 943fps/72.5C
        // before the frame cap. Profile sets the native 30 cap (also caps the
        // loading-screen heat); the boot stall is separate, unresolved RE work.
        PROFILES.put("4D5307FA", new Profile("Lost Odyssey")
                .add("gpu_frame_limit_fps", Integer.valueOf(30),
                        "Lost Odyssey is a 30fps-native Xbox 360 title - cap at 30 (its "
                        + "native ceiling; also tames the loading-screen overheat, "
                        + "device-measured 943fps/72.5C uncapped -> ~61fps). NOTE: LO has "
                        + "a separate unresolved boot blocker (stuck loading-screen IO "
                        + "stall) tracked outside the profile."));

        // Banjo-Kazooie: Nuts & Bolts (4D5307ED): 30fps-native. KNOWN BOOT BLOCKER -
        // a guest-side false "dirty disc" verification (deep RE; the current patch
        // sites are wrong-target, per CLAUDE.md). Profile sets the native 30 cap for
        // when it runs; booting it past the dirty-disc check is separate RE work.
        PROFILES.put("4D5307ED", new Profile("Banjo-Kazooie: Nuts & Bolts")
                .add("gpu_frame_limit_fps", Integer.valueOf(30),
                        "Banjo-Kazooie: Nuts & Bolts is a 30fps-native Xbox 360 title - "
                        + "cap at 30 (its native ceiling).")
                .add("xam_redirect_xui_font_cache", Boolean.TRUE,
                        "BOOT FIX (device-validated 2026-06-26): Banjo creates its XUI "
                        + "font cache (xuifontcachefont/meta) relative to the READ-ONLY "
                        + "game disc then polls for it - the create fails, the files "
                        + "never appear, and the UI never initializes (black screen, "
                        + "VdSwap frozen at ~267). Redirecting those basenames to the "
                        + "writable cache: device lets create+poll+read succeed: Banjo "
                        + "boots past it and renders (VdSwap 267->3016, ~30fps). The "
                        + "dirty-disc was already fixed in code (XctdCompressionInformation "
                        + "-> INVALID_PARAMETER); this font-cache redirect was the "
                        + "remaining boot blocker.")
                .add("xam_suppress_dirty_disc_error", Boolean.TRUE,
                        "BOOT FIX #2 (device-validated 2026-06-26): even with the "
                        + "XFileSectorInformation kernel fix (which cut the content-lookup "
                        + "failures from hundreds to ~31), Banjo still trips ONE residual "
                        + "FALSE dirty-disc check and calls XamShowDirtyDiscErrorUI, which "
                        + "shows xenia's 'Disc Read Error' popup and exit(1)s the title. "
                        + "Banjo's content is actually fine here (complete 7.3GB ISO, the "
                        + "same mmap path serves Burnout/Gears, reads succeed) - the check "
                        + "is a false positive (likely an a64 hash mismatch in Banjo's own "
                        + "verify). Suppressing the error UI lets the title continue past "
                        + "the false check: Banjo boots past the popup to a clean RARE "
                        + "splash (VdSwap flowing, no exit). Pairs with the "
                        + "XFileSectorInformation fix + font-cache redirect."));
    }

    private static String normalize(final String titleId) {
        if (titleId == null) {
            return "";
        }
        return titleId.trim().toUpperCase(Locale.US);
    }

    /** True if a built-in profile exists for this title-id. */
    public static boolean hasProfile(final String titleId) {
        return PROFILES.containsKey(normalize(titleId));
    }

    /** Display name of the built-in profile, or {@code null} if none. */
    public static String profileName(final String titleId) {
        final Profile profile = PROFILES.get(normalize(titleId));
        return profile != null ? profile.displayName : null;
    }

    /** Number of tuned settings in a title's profile (0 if none). */
    public static int settingCount(final String titleId) {
        final Profile profile = PROFILES.get(normalize(titleId));
        return profile != null ? profile.cvars.size() : 0;
    }

    /**
     * A human-readable, multi-line description of a title's profile (each tuned
     * setting + the reason), or {@code null} if the title has no profile. Used
     * by the UI to explain what the profile changes.
     */
    public static String describe(final String titleId) {
        final Profile profile = PROFILES.get(normalize(titleId));
        if (profile == null) {
            return null;
        }
        final StringBuilder sb = new StringBuilder();
        for (final Cvar cvar : profile.cvars) {
            if (sb.length() > 0) {
                sb.append('\n');
            }
            sb.append("• ").append(cvar.name).append(" = ").append(cvar.value)
                    .append(" — ").append(cvar.why);
        }
        return sb.toString();
    }

    /**
     * Apply the built-in profile's cvar overrides into the launch Bundle.
     * No-op if the title has no profile or the bundle is null.
     */
    public static void applyTo(final String titleId, final Bundle bundle) {
        final Profile profile = PROFILES.get(normalize(titleId));
        if (profile == null || bundle == null) {
            return;
        }
        for (final Cvar cvar : profile.cvars) {
            if (cvar.value instanceof Boolean) {
                bundle.putBoolean(cvar.name, (Boolean) cvar.value);
            } else if (cvar.value instanceof Integer) {
                bundle.putInt(cvar.name, (Integer) cvar.value);
            } else if (cvar.value instanceof String) {
                bundle.putString(cvar.name, (String) cvar.value);
            }
        }
    }
}
