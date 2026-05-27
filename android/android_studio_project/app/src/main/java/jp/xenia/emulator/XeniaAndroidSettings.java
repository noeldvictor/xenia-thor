package jp.xenia.emulator;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;

public final class XeniaAndroidSettings {
    public static final String PREFS_NAME = "xenia_android_settings";

    public static final String KEY_PROFILE = "profile";
    public static final String KEY_GPU_DRIVER = "gpu_driver";
    public static final String KEY_INTERNAL_RESOLUTION = "internal_resolution";
    public static final String KEY_APU_DRIVER = "apu_driver";
    public static final String KEY_HID_DRIVER = "hid_driver";
    public static final String KEY_MUTE_AUDIO = "mute_audio";
    public static final String KEY_HIDE_OSD = "hide_osd";
    private static final String KEY_HIDE_OSD_DEFAULT_MIGRATED =
            "hide_osd_default_migrated_v2";
    public static final String KEY_SHOW_FPS = "show_fps";
    public static final String KEY_VULKAN_PERF_COUNTERS = "vulkan_perf_counters";
    public static final String KEY_VULKAN_PERF_COUNTERS_INTERVAL =
            "vulkan_perf_counters_interval";
    public static final String KEY_LAST_GAME_URI = "launcher_last_game_uri";
    public static final String KEY_LAST_GAME_TITLE = "launcher_last_game_title";
    public static final String KEY_LAST_RUN_TITLE = "launcher_last_run_title";
    public static final String KEY_LAST_RUN_TARGET = "launcher_last_run_target";
    public static final String KEY_LAST_RUN_STATE = "launcher_last_run_state";
    public static final String KEY_LAST_RUN_STARTED_AT_MS =
            "launcher_last_run_started_at_ms";
    public static final String KEY_LAST_RUN_FINISHED_AT_MS =
            "launcher_last_run_finished_at_ms";

    public static final String PROFILE_BALANCED = "thor_balanced";
    public static final String PROFILE_PERFORMANCE = "thor_performance";
    public static final String PROFILE_RESEARCH = "thor_research";
    public static final String PROFILE_CUSTOM = "custom";

    public static final String GPU_VULKAN = "vulkan";
    public static final String GPU_NULL = "null";
    public static final String RESOLUTION_480P = "480p";
    public static final String RESOLUTION_720P = "720p";
    public static final String RESOLUTION_1080P = "1080p";
    public static final String APU_NOP = "nop";
    public static final String APU_ANY = "any";
    public static final String HID_ANDROID = "android";
    public static final String HID_NOP = "nop";
    public static final String LAST_RUN_STATE_RUNNING = "running";
    public static final String LAST_RUN_STATE_EXITED_TO_MENU = "exited_to_menu";

    private static final String CPU_ARM64 = "arm64";
    private static final String EXTERNAL_STORAGE_PROVIDER =
            "com.android.externalstorage.documents";

    private XeniaAndroidSettings() {
    }

    public static SharedPreferences getPreferences(final Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }

    public static void ensureInitialized(final Context context) {
        final SharedPreferences preferences = getPreferences(context);
        if (!preferences.contains(KEY_PROFILE)) {
            writePreset(preferences.edit(), PROFILE_BALANCED)
                    .putBoolean(KEY_HIDE_OSD_DEFAULT_MIGRATED, true)
                    .apply();
            return;
        }
        if (!preferences.getBoolean(KEY_HIDE_OSD_DEFAULT_MIGRATED, false)) {
            final String profile = preferences.getString(KEY_PROFILE, PROFILE_BALANCED);
            final SharedPreferences.Editor editor = preferences.edit();
            if (!PROFILE_RESEARCH.equals(profile)) {
                editor.putBoolean(KEY_HIDE_OSD, true);
            }
            editor.putBoolean(KEY_HIDE_OSD_DEFAULT_MIGRATED, true).apply();
        }
    }

    public static Bundle createLaunchArguments(final Context context, final Uri target) {
        ensureInitialized(context);
        final SharedPreferences preferences = getPreferences(context);
        final Bundle launchArguments = new Bundle();
        launchArguments.putString("target", resolveLaunchTarget(context, target));
        launchArguments.putString("gpu", preferences.getString(KEY_GPU_DRIVER, GPU_VULKAN));
        launchArguments.putString(
                "kernel_display_resolution",
                preferences.getString(KEY_INTERNAL_RESOLUTION, RESOLUTION_720P));
        launchArguments.putString("cpu", CPU_ARM64);
        launchArguments.putString("apu", preferences.getString(KEY_APU_DRIVER, APU_NOP));
        launchArguments.putString("hid", preferences.getString(KEY_HID_DRIVER, HID_ANDROID));
        launchArguments.putBoolean("discord", false);
        if (preferences.getBoolean(KEY_MUTE_AUDIO, false)) {
            launchArguments.putBoolean("mute", true);
        }
        launchArguments.putBoolean(
                "android_hide_osd", preferences.getBoolean(KEY_HIDE_OSD, true));
        launchArguments.putBoolean(
                "android_show_fps", preferences.getBoolean(KEY_SHOW_FPS, true));
        if (preferences.getBoolean(KEY_VULKAN_PERF_COUNTERS, false)) {
            launchArguments.putBoolean("vulkan_trace_perf_counters", true);
            launchArguments.putInt(
                    "vulkan_trace_perf_counters_log_interval",
                    preferences.getInt(KEY_VULKAN_PERF_COUNTERS_INTERVAL, 60));
        }
        return launchArguments;
    }

    public static String resolveLaunchTarget(final Context context, final Uri target) {
        if (target == null) {
            return "";
        }
        if ("file".equalsIgnoreCase(target.getScheme())) {
            final String path = target.getPath();
            return path != null ? path : target.toString();
        }
        if (EXTERNAL_STORAGE_PROVIDER.equals(target.getAuthority())
                && DocumentsContract.isDocumentUri(context, target)) {
            final String documentId = DocumentsContract.getDocumentId(target);
            final int separator = documentId.indexOf(':');
            if (separator > 0 && separator + 1 < documentId.length()) {
                final String volume = documentId.substring(0, separator);
                final String relativePath = documentId.substring(separator + 1);
                if ("primary".equalsIgnoreCase(volume)) {
                    return "/storage/emulated/0/" + relativePath;
                }
                return "/storage/" + volume + "/" + relativePath;
            }
        }
        return target.toString();
    }

    public static void recordLaunchStarted(
            final Context context, final String title, final String target) {
        final long nowMs = System.currentTimeMillis();
        getPreferences(context).edit()
                .putString(KEY_LAST_RUN_TITLE, title != null ? title : "")
                .putString(KEY_LAST_RUN_TARGET, target != null ? target : "")
                .putString(KEY_LAST_RUN_STATE, LAST_RUN_STATE_RUNNING)
                .putLong(KEY_LAST_RUN_STARTED_AT_MS, nowMs)
                .remove(KEY_LAST_RUN_FINISHED_AT_MS)
                .commit();
    }

    public static void recordLaunchExitedToMenu(final Context context) {
        getPreferences(context).edit()
                .putString(KEY_LAST_RUN_STATE, LAST_RUN_STATE_EXITED_TO_MENU)
                .putLong(KEY_LAST_RUN_FINISHED_AT_MS, System.currentTimeMillis())
                .commit();
    }

    public static SharedPreferences.Editor writePreset(
            final SharedPreferences.Editor editor, final String profile) {
        editor.putString(KEY_PROFILE, profile);
        if (PROFILE_PERFORMANCE.equals(profile)) {
            editor.putString(KEY_GPU_DRIVER, GPU_VULKAN);
            editor.putString(KEY_INTERNAL_RESOLUTION, RESOLUTION_480P);
            editor.putString(KEY_APU_DRIVER, APU_NOP);
            editor.putString(KEY_HID_DRIVER, HID_ANDROID);
            editor.putBoolean(KEY_MUTE_AUDIO, true);
            editor.putBoolean(KEY_HIDE_OSD, true);
            editor.putBoolean(KEY_SHOW_FPS, true);
            editor.putBoolean(KEY_VULKAN_PERF_COUNTERS, false);
            editor.putInt(KEY_VULKAN_PERF_COUNTERS_INTERVAL, 60);
            return editor;
        }
        if (PROFILE_RESEARCH.equals(profile)) {
            editor.putString(KEY_GPU_DRIVER, GPU_VULKAN);
            editor.putString(KEY_INTERNAL_RESOLUTION, RESOLUTION_720P);
            editor.putString(KEY_APU_DRIVER, APU_NOP);
            editor.putString(KEY_HID_DRIVER, HID_ANDROID);
            editor.putBoolean(KEY_MUTE_AUDIO, false);
            editor.putBoolean(KEY_HIDE_OSD, false);
            editor.putBoolean(KEY_SHOW_FPS, true);
            editor.putBoolean(KEY_VULKAN_PERF_COUNTERS, true);
            editor.putInt(KEY_VULKAN_PERF_COUNTERS_INTERVAL, 60);
            return editor;
        }
        editor.putString(KEY_GPU_DRIVER, GPU_VULKAN);
        editor.putString(KEY_INTERNAL_RESOLUTION, RESOLUTION_720P);
        editor.putString(KEY_APU_DRIVER, APU_NOP);
        editor.putString(KEY_HID_DRIVER, HID_ANDROID);
        editor.putBoolean(KEY_MUTE_AUDIO, false);
        editor.putBoolean(KEY_HIDE_OSD, true);
        editor.putBoolean(KEY_SHOW_FPS, true);
        editor.putBoolean(KEY_VULKAN_PERF_COUNTERS, false);
        editor.putInt(KEY_VULKAN_PERF_COUNTERS_INTERVAL, 60);
        return editor;
    }
}
