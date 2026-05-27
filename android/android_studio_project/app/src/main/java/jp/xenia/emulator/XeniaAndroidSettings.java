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
    public static final String KEY_APU_DRIVER = "apu_driver";
    public static final String KEY_HID_DRIVER = "hid_driver";
    public static final String KEY_MUTE_AUDIO = "mute_audio";
    public static final String KEY_HIDE_OSD = "hide_osd";
    public static final String KEY_VULKAN_PERF_COUNTERS = "vulkan_perf_counters";
    public static final String KEY_VULKAN_PERF_COUNTERS_INTERVAL =
            "vulkan_perf_counters_interval";

    public static final String PROFILE_BALANCED = "thor_balanced";
    public static final String PROFILE_PERFORMANCE = "thor_performance";
    public static final String PROFILE_RESEARCH = "thor_research";
    public static final String PROFILE_CUSTOM = "custom";

    public static final String GPU_VULKAN = "vulkan";
    public static final String GPU_NULL = "null";
    public static final String APU_NOP = "nop";
    public static final String APU_ANY = "any";
    public static final String HID_ANDROID = "android";
    public static final String HID_NOP = "nop";

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
            writePreset(preferences.edit(), PROFILE_BALANCED).apply();
        }
    }

    public static Bundle createLaunchArguments(final Context context, final Uri target) {
        ensureInitialized(context);
        final SharedPreferences preferences = getPreferences(context);
        final Bundle launchArguments = new Bundle();
        launchArguments.putString("target", resolveLaunchTarget(context, target));
        launchArguments.putString("gpu", preferences.getString(KEY_GPU_DRIVER, GPU_VULKAN));
        launchArguments.putString("cpu", CPU_ARM64);
        launchArguments.putString("apu", preferences.getString(KEY_APU_DRIVER, APU_NOP));
        launchArguments.putString("hid", preferences.getString(KEY_HID_DRIVER, HID_ANDROID));
        launchArguments.putBoolean("discord", false);
        if (preferences.getBoolean(KEY_MUTE_AUDIO, false)) {
            launchArguments.putBoolean("mute", true);
        }
        if (preferences.getBoolean(KEY_HIDE_OSD, false)) {
            launchArguments.putBoolean("android_hide_osd", true);
        }
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

    public static SharedPreferences.Editor writePreset(
            final SharedPreferences.Editor editor, final String profile) {
        editor.putString(KEY_PROFILE, profile);
        if (PROFILE_PERFORMANCE.equals(profile)) {
            editor.putString(KEY_GPU_DRIVER, GPU_VULKAN);
            editor.putString(KEY_APU_DRIVER, APU_NOP);
            editor.putString(KEY_HID_DRIVER, HID_ANDROID);
            editor.putBoolean(KEY_MUTE_AUDIO, true);
            editor.putBoolean(KEY_HIDE_OSD, true);
            editor.putBoolean(KEY_VULKAN_PERF_COUNTERS, false);
            editor.putInt(KEY_VULKAN_PERF_COUNTERS_INTERVAL, 60);
            return editor;
        }
        if (PROFILE_RESEARCH.equals(profile)) {
            editor.putString(KEY_GPU_DRIVER, GPU_VULKAN);
            editor.putString(KEY_APU_DRIVER, APU_NOP);
            editor.putString(KEY_HID_DRIVER, HID_ANDROID);
            editor.putBoolean(KEY_MUTE_AUDIO, false);
            editor.putBoolean(KEY_HIDE_OSD, false);
            editor.putBoolean(KEY_VULKAN_PERF_COUNTERS, true);
            editor.putInt(KEY_VULKAN_PERF_COUNTERS_INTERVAL, 60);
            return editor;
        }
        editor.putString(KEY_GPU_DRIVER, GPU_VULKAN);
        editor.putString(KEY_APU_DRIVER, APU_NOP);
        editor.putString(KEY_HID_DRIVER, HID_ANDROID);
        editor.putBoolean(KEY_MUTE_AUDIO, false);
        editor.putBoolean(KEY_HIDE_OSD, false);
        editor.putBoolean(KEY_VULKAN_PERF_COUNTERS, false);
        editor.putInt(KEY_VULKAN_PERF_COUNTERS_INTERVAL, 60);
        return editor;
    }
}
