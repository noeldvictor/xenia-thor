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
                        "UMA-direct present-hangs Blue Dragon; keep it off"));

        // Burnout Revenge (454107DC): UMA-direct present-hangs the EA/EAHD/CRRW
        // intro movie chain (VdSwap freezes). Off runs the movies through to a
        // live menu ~55-63fps. Device-validated (A/B, docs 20260530).
        PROFILES.put("454107DC", new Profile("Burnout Revenge")
                .add("gpu_uma_direct_shared_memory", Boolean.FALSE,
                        "UMA-direct present-hangs the intro movies; keep it off"));

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
                        "Raise the GPU-command thread's priority above the guest workers (-10) so it isn't descheduled -> GPU fed -> 30fps; -15 stays below the audio thread (-16) to avoid audio glitches"));
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
