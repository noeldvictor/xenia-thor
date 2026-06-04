package jp.xenia.emulator;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Detects when the emulator process died abnormally last session (a game
 * crashing the whole app — native crash, signal, ANR, or OOM) and turns it into
 * a saved, shareable crash log plus a clear message in the launcher.
 *
 * <p>The launcher's existing last-run heuristic ({@code RUNNING} on restart =
 * died without a clean exit) catches THAT a crash happened; this fills in WHY,
 * using {@link ActivityManager#getHistoricalProcessExitReasons} (API 30+), which
 * is the only headless source that covers native crashes (with a tombstone
 * trace), not just Java exceptions.
 */
public final class CrashReporter {
    private static final String TAG = "XeniaCrash";
    private static final String CRASH_DIR = "crash_logs";
    private static final int TRACE_CHAR_LIMIT = 64 * 1024;
    private static final int KEEP_LOGS = 8;
    private static final int SCAN_EXITS = 12;

    private CrashReporter() {
    }

    /** A detected crash from the previous emulator session, for the UI. */
    public static final class CrashInfo {
        public final String title;
        public final String reason;
        public final String logPath;
        public final long timestampMs;

        CrashInfo(final String title, final String reason, final String logPath,
                final long timestampMs) {
            this.title = title;
            this.reason = reason;
            this.logPath = logPath;
            this.timestampMs = timestampMs;
        }
    }

    /**
     * Look for a fresh abnormal exit of the emulator (game) process; if found and
     * the last run was still flagged running, record it as an app crash with a
     * saved log so the launcher can surface the cause. Call before refreshing the
     * last-run status. No-op below API 30.
     */
    public static void reconcile(final Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return;
        }
        try {
            reconcileApi30(context);
        } catch (final Throwable t) {
            // Crash reporting must never itself crash the launcher.
            Log.w(TAG, "crash reconcile failed", t);
        }
    }

    private static void reconcileApi30(final Context context) {
        final ActivityManager am =
                (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null) {
            return;
        }
        final List<ApplicationExitInfo> exits = am.getHistoricalProcessExitReasons(
                context.getPackageName(), 0, SCAN_EXITS);
        if (exits == null || exits.isEmpty()) {
            return;
        }
        final SharedPreferences prefs = XeniaAndroidSettings.getPreferences(context);
        final long alreadyReported =
                prefs.getLong(XeniaAndroidSettings.KEY_LAST_CRASH_REPORTED_AT_MS, 0);
        // The game runs in the default process (== applicationId); the launcher
        // runs in ":launcher". Only surface crashes of the game process.
        final String emulatorProcess = context.getPackageName();

        ApplicationExitInfo newest = null;
        for (final ApplicationExitInfo info : exits) {
            if (!emulatorProcess.equals(info.getProcessName())) {
                continue;
            }
            if (info.getTimestamp() <= alreadyReported) {
                continue;
            }
            if (!isAbnormal(info.getReason())) {
                continue;
            }
            if (newest == null || info.getTimestamp() > newest.getTimestamp()) {
                newest = info;
            }
        }
        if (newest == null) {
            return;
        }

        final String title = prefs.getString(XeniaAndroidSettings.KEY_LAST_RUN_TITLE, "");
        final String reason = reasonText(newest);
        final String logPath = writeLog(context, title, newest, reason);

        final SharedPreferences.Editor editor = prefs.edit()
                .putLong(XeniaAndroidSettings.KEY_LAST_CRASH_REPORTED_AT_MS,
                        newest.getTimestamp())
                .putLong(XeniaAndroidSettings.KEY_LAST_CRASH_AT_MS, newest.getTimestamp())
                .putString(XeniaAndroidSettings.KEY_LAST_CRASH_REASON, reason);
        if (logPath != null) {
            editor.putString(XeniaAndroidSettings.KEY_LAST_CRASH_LOG_PATH, logPath);
        }
        // Promote a still-"running" last run to an explicit app crash so the
        // launcher shows the real cause instead of the vague "maybe crashed".
        if (XeniaAndroidSettings.LAST_RUN_STATE_RUNNING.equals(
                prefs.getString(XeniaAndroidSettings.KEY_LAST_RUN_STATE, ""))) {
            editor.putString(XeniaAndroidSettings.KEY_LAST_RUN_STATE,
                    XeniaAndroidSettings.LAST_RUN_STATE_APP_CRASH);
        }
        editor.apply();
        Log.w(TAG, "app crash last session: " + reason
                + (title.isEmpty() ? "" : " (" + title + ")"));
        pruneOldLogs(context);
    }

    /** The recorded crash from the previous session, or null. */
    public static CrashInfo getLastCrash(final Context context) {
        final SharedPreferences prefs = XeniaAndroidSettings.getPreferences(context);
        if (!XeniaAndroidSettings.LAST_RUN_STATE_APP_CRASH.equals(
                prefs.getString(XeniaAndroidSettings.KEY_LAST_RUN_STATE, ""))) {
            return null;
        }
        return new CrashInfo(
                prefs.getString(XeniaAndroidSettings.KEY_LAST_RUN_TITLE, ""),
                prefs.getString(XeniaAndroidSettings.KEY_LAST_CRASH_REASON, "Crashed"),
                prefs.getString(XeniaAndroidSettings.KEY_LAST_CRASH_LOG_PATH, ""),
                prefs.getLong(XeniaAndroidSettings.KEY_LAST_CRASH_AT_MS, 0));
    }

    /** Read a saved crash log file (for the details dialog). */
    public static String readLog(final String path) {
        if (path == null || path.isEmpty()) {
            return null;
        }
        try (InputStream in = new FileInputStream(path)) {
            final StringBuilder sb = new StringBuilder();
            final BufferedReader reader =
                    new BufferedReader(new InputStreamReader(in, "UTF-8"));
            final char[] buffer = new char[8192];
            int read;
            while ((read = reader.read(buffer)) != -1) {
                sb.append(buffer, 0, read);
            }
            return sb.toString();
        } catch (final Exception e) {
            return null;
        }
    }

    private static boolean isAbnormal(final int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_CRASH:         // Java/Kotlin uncaught exception
            case ApplicationExitInfo.REASON_CRASH_NATIVE:  // native crash (tombstone)
            case ApplicationExitInfo.REASON_SIGNALED:      // killed by a signal
            case ApplicationExitInfo.REASON_ANR:           // not responding
            case ApplicationExitInfo.REASON_LOW_MEMORY:    // out of memory
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE:
                return true;
            default:
                // EXIT_SELF, USER_REQUESTED, USER_STOPPED, DEPENDENCY_DIED,
                // PERMISSION_CHANGE, OTHER, UNKNOWN -> not a game crash.
                return false;
        }
    }

    private static String reasonText(final ApplicationExitInfo info) {
        final String base;
        switch (info.getReason()) {
            case ApplicationExitInfo.REASON_CRASH:
                base = "App crash (unhandled exception)";
                break;
            case ApplicationExitInfo.REASON_CRASH_NATIVE:
                base = "Native crash";
                break;
            case ApplicationExitInfo.REASON_SIGNALED:
                base = "Killed by signal";
                break;
            case ApplicationExitInfo.REASON_ANR:
                base = "Not responding (ANR)";
                break;
            case ApplicationExitInfo.REASON_LOW_MEMORY:
                base = "Out of memory";
                break;
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE:
                base = "Excessive resource use";
                break;
            default:
                base = "Abnormal exit";
                break;
        }
        final String description = info.getDescription();
        return description != null && !description.isEmpty()
                ? base + " — " + description : base;
    }

    private static String writeLog(
            final Context context,
            final String title,
            final ApplicationExitInfo info,
            final String reason) {
        try {
            final File dir = new File(context.getFilesDir(), CRASH_DIR);
            if (!dir.isDirectory() && !dir.mkdirs()) {
                return null;
            }
            final File file = new File(dir, "crash_" + info.getTimestamp() + ".txt");
            try (Writer writer = new OutputStreamWriter(
                    new FileOutputStream(file), "UTF-8")) {
                writer.write("Xenia-Thor crash report\n");
                writer.write("Game:        "
                        + (title.isEmpty() ? "(unknown)" : title) + "\n");
                writer.write("When:        " + formatTimestamp(info.getTimestamp()) + "\n");
                writer.write("Reason:      " + reason + "\n");
                writer.write("Process:     " + info.getProcessName()
                        + "  pid=" + info.getPid() + "\n");
                writer.write("Importance:  " + info.getImportance() + "\n");
                writer.write("App:         " + BuildConfig.APPLICATION_ID + "\n");
                writer.write("Device:      " + Build.MANUFACTURER + " " + Build.MODEL
                        + "  /  Android " + Build.VERSION.RELEASE
                        + " (API " + Build.VERSION.SDK_INT + ")\n");
                writer.write("\n--- trace / tombstone ---\n");
                final String trace = readTrace(info);
                writer.write(trace != null ? trace : "(no trace available)\n");
            }
            return file.getAbsolutePath();
        } catch (final Exception e) {
            Log.w(TAG, "failed to write crash log", e);
            return null;
        }
    }

    private static String readTrace(final ApplicationExitInfo info) {
        InputStream in = null;
        try {
            in = info.getTraceInputStream();
            if (in == null) {
                return null;
            }
            final StringBuilder sb = new StringBuilder();
            final BufferedReader reader = new BufferedReader(new InputStreamReader(in, "UTF-8"));
            final char[] buffer = new char[4096];
            int read;
            int total = 0;
            while ((read = reader.read(buffer)) != -1 && total < TRACE_CHAR_LIMIT) {
                sb.append(buffer, 0, read);
                total += read;
            }
            if (total >= TRACE_CHAR_LIMIT) {
                sb.append("\n…(trace truncated)…\n");
            }
            return sb.toString();
        } catch (final Exception e) {
            return null;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (final Exception ignored) {
                }
            }
        }
    }

    private static String formatTimestamp(final long millis) {
        try {
            return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date(millis));
        } catch (final Exception e) {
            return String.valueOf(millis);
        }
    }

    private static void pruneOldLogs(final Context context) {
        try {
            final File dir = new File(context.getFilesDir(), CRASH_DIR);
            final File[] files = dir.listFiles();
            if (files == null || files.length <= KEEP_LOGS) {
                return;
            }
            Arrays.sort(files, new Comparator<File>() {
                @Override
                public int compare(final File a, final File b) {
                    return Long.compare(b.lastModified(), a.lastModified());
                }
            });
            for (int i = KEEP_LOGS; i < files.length; ++i) {
                files[i].delete();
            }
        } catch (final Exception ignored) {
        }
    }
}
