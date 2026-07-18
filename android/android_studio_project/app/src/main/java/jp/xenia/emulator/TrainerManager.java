package jp.xenia.emulator;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * Downloads and stores Aurora-style Xbox 360 {@code .xex} trainers for the
 * native trainer loader, the same way {@link GamePatchManager} handles
 * {@code .patch.toml} files.
 *
 * <p>Trainer files live in {@code files/trainers/}, which is exactly the
 * {@code storage_root/trainers} directory the engine scans at title launch
 * (Android storage_root == getFilesDir(), see {@code src/xenia/app/xenia_main.cc}).
 * A trainer is matched to a running title by an 8-hex-digit title-id filename
 * prefix and loaded by {@code Emulator::LoadTrainersForTitle} ->
 * {@code KernelState::LoadTrainerModule}. Master toggles are the
 * {@code trainer_enable} / {@code trainer_run_entry} cvars.
 *
 * <p>Trainers are sourced from the community repo
 * <a href="https://github.com/StonedModder/Aurora-JTAG-RGH-Trainer-Pack-Repository">
 * StonedModder/Aurora-JTAG-RGH-Trainer-Pack-Repository</a>, which stores each
 * title's trainers under {@code Aurora/user/Trainers/<TITLEID>/*.xex}. Because
 * the loader matches on the filename prefix, downloads are saved locally as
 * {@code <TITLEID>_<original name>.xex}.
 */
public final class TrainerManager {
    public static final String TRAINERS_DIRNAME = "trainers";
    public static final String REPO_HUMAN_URL =
            "https://github.com/StonedModder/Aurora-JTAG-RGH-Trainer-Pack-Repository";

    // GitHub contents API for a single title's trainer folder. The repo folder
    // names are upper-case 8-hex title ids (e.g. .../Trainers/354807D1).
    private static final String CONTENTS_API_BASE =
            "https://api.github.com/repos/StonedModder/"
            + "Aurora-JTAG-RGH-Trainer-Pack-Repository/contents/"
            + "Aurora/user/Trainers/";
    private static final int MAX_TRAINER_BYTES = 16 * 1024 * 1024;
    private static final Pattern TITLE_ID_PATTERN = Pattern.compile("[0-9A-Fa-f]{8}");

    private TrainerManager() {
    }

    /** A locally-present trainer file for a title. */
    public static final class Trainer {
        public final File file;
        public final String displayName;

        Trainer(final File file, final String displayName) {
            this.file = file;
            this.displayName = displayName;
        }
    }

    // ---- storage ----------------------------------------------------------

    public static File trainersDir(final Context context) {
        return new File(context.getFilesDir(), TRAINERS_DIRNAME);
    }

    /** All locally-present trainers whose filename prefix matches the title. */
    public static List<Trainer> listForTitle(
            final Context context, final String titleId) {
        final ArrayList<Trainer> out = new ArrayList<>();
        final String wanted = normalizeTitleId(titleId);
        final File[] files = trainersDir(context).listFiles();
        if (files == null || wanted == null) {
            return out;
        }
        final String prefix = wanted.toLowerCase(Locale.US);
        for (final File file : files) {
            final String name = file.getName().toLowerCase(Locale.US);
            if (!file.isFile() || !name.endsWith(".xex")
                    || !name.startsWith(prefix)) {
                continue;
            }
            out.add(new Trainer(file, displayNameOf(file.getName(), wanted)));
        }
        return out;
    }

    // ---- download ---------------------------------------------------------

    /**
     * Download every {@code .xex} trainer for {@code titleId} from the community
     * repo into {@code files/trainers/}. Runs synchronously (call off the UI
     * thread). Returns the number of trainer files written.
     *
     * @throws IOException on a network/HTTP failure or if no trainer exists.
     */
    public static int downloadForTitle(final Context context, final String titleId)
            throws IOException {
        final String wanted = normalizeTitleId(titleId);
        if (wanted == null) {
            throw new IllegalArgumentException("This game has no usable title ID.");
        }
        final JSONArray listing = fetchTitleListing(wanted);
        final File dir = trainersDir(context);
        if (!dir.exists() && !dir.mkdirs()) {
            throw new IOException("Could not create the trainers folder.");
        }
        int written = 0;
        for (int i = 0; i < listing.length(); ++i) {
            final JSONObject entry = listing.optJSONObject(i);
            if (entry == null || !"file".equals(entry.optString("type"))) {
                continue;
            }
            final String name = entry.optString("name", "");
            if (!name.toLowerCase(Locale.US).endsWith(".xex")) {
                continue;
            }
            final String downloadUrl = entry.optString("download_url", "");
            if (downloadUrl.isEmpty()) {
                continue;
            }
            final byte[] body = httpGet(downloadUrl, MAX_TRAINER_BYTES);
            // Save with the title-id prefix the loader matches on.
            final File dest = new File(dir,
                    wanted + "_" + sanitizeFileName(name));
            try (OutputStream os = new FileOutputStream(dest)) {
                os.write(body);
            }
            written++;
        }
        if (written == 0) {
            throw new IOException(
                    "No community trainers were found for title " + wanted + ".");
        }
        return written;
    }

    public static void delete(final Trainer trainer) {
        if (trainer != null && trainer.file != null) {
            trainer.file.delete();
        }
    }

    private static JSONArray fetchTitleListing(final String wantedTitleId)
            throws IOException {
        final byte[] body;
        try {
            body = httpGet(CONTENTS_API_BASE + wantedTitleId, MAX_TRAINER_BYTES);
        } catch (final NotFoundException e) {
            // GitHub returns 404 for a title folder that doesn't exist.
            throw new IOException(
                    "No community trainers were found for title "
                    + wantedTitleId + ".");
        }
        try {
            return new JSONArray(new String(body, StandardCharsets.UTF_8));
        } catch (final org.json.JSONException e) {
            throw new IOException(
                    "Unexpected response from the trainer repository.");
        }
    }

    /** Distinguishes an HTTP 404 (no trainers for this title) from other errors. */
    private static final class NotFoundException extends IOException {
        NotFoundException(final String message) {
            super(message);
        }
    }

    private static byte[] httpGet(final String urlString, final int maxBytes)
            throws IOException {
        final URL url = new URL(urlString);
        final HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setInstanceFollowRedirects(true);
        connection.setConnectTimeout(20000);
        connection.setReadTimeout(60000);
        connection.setRequestProperty("User-Agent", "xenia-thor");
        connection.setRequestProperty(
                "Accept", "application/vnd.github+json, application/octet-stream, */*");
        try {
            connection.connect();
            final int code = connection.getResponseCode();
            if (code == 404) {
                throw new NotFoundException("Not found (HTTP 404).");
            }
            if (code == 403) {
                throw new IOException(
                        "GitHub rate limit reached — try again in a few minutes.");
            }
            if (code / 100 != 2) {
                throw new IOException("Download failed (HTTP " + code + ").");
            }
            try (InputStream in = connection.getInputStream()) {
                final ByteArrayOutputStream out = new ByteArrayOutputStream();
                final byte[] buffer = new byte[16384];
                int read;
                int total = 0;
                while ((read = in.read(buffer)) != -1) {
                    total += read;
                    if (total > maxBytes) {
                        throw new IOException("Download was unexpectedly large.");
                    }
                    out.write(buffer, 0, read);
                }
                return out.toByteArray();
            }
        } finally {
            connection.disconnect();
        }
    }

    // ---- helpers ----------------------------------------------------------

    private static String normalizeTitleId(final String titleId) {
        if (titleId == null) {
            return null;
        }
        final String trimmed = titleId.trim();
        if (!TITLE_ID_PATTERN.matcher(trimmed).matches()) {
            return null;
        }
        return trimmed.toUpperCase(Locale.US);
    }

    /** Strip the "<TITLEID>_" prefix and ".xex" suffix for display. */
    private static String displayNameOf(final String fileName, final String titleId) {
        String name = fileName;
        final String prefix = titleId + "_";
        if (name.regionMatches(true, 0, prefix, 0, prefix.length())) {
            name = name.substring(prefix.length());
        }
        if (name.toLowerCase(Locale.US).endsWith(".xex")) {
            name = name.substring(0, name.length() - 4);
        }
        return name.isEmpty() ? fileName : name;
    }

    private static String sanitizeFileName(final String name) {
        final StringBuilder sb = new StringBuilder();
        for (int i = 0; i < name.length(); ++i) {
            final char c = name.charAt(i);
            if (c == '/' || c == '\\' || c == ':' || c == 0) {
                sb.append('_');
            } else {
                sb.append(c);
            }
        }
        final String cleaned = sb.toString();
        return cleaned.isEmpty() ? "trainer.xex" : cleaned;
    }
}
