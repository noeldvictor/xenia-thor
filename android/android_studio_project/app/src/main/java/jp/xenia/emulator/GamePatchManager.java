package jp.xenia.emulator;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * Downloads, parses, toggles and stores Xbox 360 game patches (xenia-canary
 * {@code .patch.toml} files) for the native patcher.
 *
 * <p>Patch files live in {@code files/patches/}, which is exactly the
 * {@code storage_root/patches} directory the engine's {@code PatchDB} scans at
 * title launch (Android storage_root == getFilesDir(), see
 * {@code src/xenia/app/xenia_main.cc}). Each file is matched to a running title
 * by {@code title_id} + build hash and applied by
 * {@code src/xenia/patcher/patcher.cc}.
 *
 * <p>Patches are sourced from the community repo
 * <a href="https://github.com/xenia-canary/game-patches">xenia-canary/game-patches</a>
 * (performance fixes — 60 FPS, disable blur/SSAO — and cheats — infinite
 * lives/money/ammo). Users can also drop hand-authored files here (see the
 * {@code xenia-thor-ghidra-game-patch} skill).
 *
 * <p>Rather than carry a full TOML parser, this edits the file by lines: it
 * finds each {@code [[patch]]} block, reads its {@code name}/{@code desc}/
 * {@code is_enabled}, and rewrites just the {@code is_enabled} line when a
 * patch is toggled. The native side does the real (cpptoml) parse.
 */
public final class GamePatchManager {
    public static final String PATCHES_DIRNAME = "patches";
    public static final String REPO_HUMAN_URL =
            "https://github.com/xenia-canary/game-patches/tree/main/patches";

    private static final String CONTENTS_API_URL =
            "https://api.github.com/repos/xenia-canary/game-patches/contents/patches";
    private static final int MAX_LISTING_BYTES = 8 * 1024 * 1024;
    private static final int MAX_PATCH_BYTES = 2 * 1024 * 1024;
    private static final Pattern TITLE_ID_PATTERN = Pattern.compile("[0-9A-Fa-f]{8}");

    private GamePatchManager() {
    }

    /** One {@code [[patch]]} block within a file. */
    public static final class Patch {
        public final String name;
        public final String description;
        public final String author;
        public boolean enabled;
        /** Line index of the {@code is_enabled} line, or -1 if absent. */
        final int enabledLineIndex;
        /** Line index of the owning {@code [[patch]]} header line. */
        final int headerLineIndex;

        Patch(final String name, final String description, final String author,
                final boolean enabled, final int enabledLineIndex,
                final int headerLineIndex) {
            this.name = name;
            this.description = description;
            this.author = author;
            this.enabled = enabled;
            this.enabledLineIndex = enabledLineIndex;
            this.headerLineIndex = headerLineIndex;
        }
    }

    /** A parsed {@code .patch.toml} file and the patches it contains. */
    public static final class PatchFile {
        public final File file;
        public final String titleId;
        public final String titleName;
        public final List<Patch> patches;

        PatchFile(final File file, final String titleId, final String titleName,
                final List<Patch> patches) {
            this.file = file;
            this.titleId = titleId;
            this.titleName = titleName;
            this.patches = patches;
        }
    }

    // ---- storage ----------------------------------------------------------

    public static File patchesDir(final Context context) {
        return new File(context.getFilesDir(), PATCHES_DIRNAME);
    }

    /** All locally-present patch files whose title_id matches, parsed. */
    public static List<PatchFile> listForTitle(
            final Context context, final String titleId) {
        final ArrayList<PatchFile> out = new ArrayList<>();
        final String wanted = normalizeTitleId(titleId);
        final File[] files = patchesDir(context).listFiles();
        if (files == null || wanted == null) {
            return out;
        }
        for (final File file : files) {
            final String name = file.getName().toLowerCase(Locale.US);
            if (!file.isFile() || !name.endsWith(".patch.toml")
                    || !name.startsWith(wanted.toLowerCase(Locale.US))) {
                continue;
            }
            try {
                out.add(parse(file));
            } catch (final IOException ignored) {
                // Skip unreadable files.
            }
        }
        return out;
    }

    // ---- download ---------------------------------------------------------

    /**
     * Download every {@code .patch.toml} for {@code titleId} from the community
     * repo into {@code files/patches/}. Runs synchronously (call off the UI
     * thread). Returns the number of patch files written.
     *
     * @throws IOException on a network/HTTP failure or if no patch exists.
     */
    public static int downloadForTitle(final Context context, final String titleId)
            throws IOException {
        final String wanted = normalizeTitleId(titleId);
        if (wanted == null) {
            throw new IllegalArgumentException("This game has no usable title ID.");
        }
        final JSONArray listing = fetchListing();
        final File dir = patchesDir(context);
        if (!dir.exists() && !dir.mkdirs()) {
            throw new IOException("Could not create the patches folder.");
        }
        int written = 0;
        final String prefix = wanted.toLowerCase(Locale.US);
        for (int i = 0; i < listing.length(); ++i) {
            final JSONObject entry = listing.optJSONObject(i);
            if (entry == null || !"file".equals(entry.optString("type"))) {
                continue;
            }
            final String name = entry.optString("name", "");
            final String lower = name.toLowerCase(Locale.US);
            if (!lower.endsWith(".patch.toml") || !lower.startsWith(prefix)) {
                continue;
            }
            final String downloadUrl = entry.optString("download_url", "");
            if (downloadUrl.isEmpty()) {
                continue;
            }
            final byte[] body = httpGet(downloadUrl, MAX_PATCH_BYTES);
            final File dest = new File(dir, sanitizeFileName(name));
            try (OutputStream os = new FileOutputStream(dest)) {
                os.write(body);
            }
            written++;
        }
        if (written == 0) {
            throw new IOException(
                    "No community patches were found for title " + wanted + ".");
        }
        return written;
    }

    private static JSONArray fetchListing() throws IOException {
        final byte[] body = httpGet(CONTENTS_API_URL, MAX_LISTING_BYTES);
        try {
            return new JSONArray(new String(body, StandardCharsets.UTF_8));
        } catch (final org.json.JSONException e) {
            throw new IOException("Unexpected response from the patch repository.");
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
                "Accept", "application/vnd.github+json, text/plain, */*");
        try {
            connection.connect();
            final int code = connection.getResponseCode();
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

    // ---- parse / toggle ---------------------------------------------------

    /** Parse a {@code .patch.toml} into its title + {@code [[patch]]} blocks. */
    public static PatchFile parse(final File file) throws IOException {
        final List<String> lines = readLines(file);
        String titleId = "";
        String titleName = "";
        final ArrayList<Patch> patches = new ArrayList<>();

        int headerIndex = -1;
        String name = "";
        String desc = "";
        String author = "";
        boolean enabled = false;
        int enabledIndex = -1;
        boolean inPatch = false;

        for (int i = 0; i < lines.size(); ++i) {
            final String trimmed = lines.get(i).trim();
            if (trimmed.equals("[[patch]]")) {
                if (inPatch) {
                    patches.add(new Patch(name, desc, author, enabled,
                            enabledIndex, headerIndex));
                }
                inPatch = true;
                headerIndex = i;
                name = "";
                desc = "";
                author = "";
                enabled = false;
                enabledIndex = -1;
                continue;
            }
            if (!inPatch) {
                if (trimmed.startsWith("title_id")) {
                    titleId = stringValue(trimmed);
                } else if (trimmed.startsWith("title_name")) {
                    titleName = stringValue(trimmed);
                }
                continue;
            }
            // Inside a [[patch]] block (data sub-tables like [[patch.be32]] are
            // ignored for the header fields).
            if (trimmed.startsWith("name") && trimmed.contains("=")
                    && name.isEmpty()) {
                name = stringValue(trimmed);
            } else if (trimmed.startsWith("desc") && trimmed.contains("=")
                    && desc.isEmpty()) {
                desc = stringValue(trimmed);
            } else if (trimmed.startsWith("author") && trimmed.contains("=")
                    && author.isEmpty()) {
                author = stringValue(trimmed);
            } else if (trimmed.startsWith("is_enabled") && enabledIndex == -1) {
                enabled = trimmed.toLowerCase(Locale.US).contains("true");
                enabledIndex = i;
            }
        }
        if (inPatch) {
            patches.add(new Patch(name, desc, author, enabled, enabledIndex,
                    headerIndex));
        }
        return new PatchFile(file, titleId, titleName, patches);
    }

    /**
     * Toggle one patch's {@code is_enabled} flag and persist the file. Returns a
     * refreshed {@link PatchFile} so line indices stay valid.
     */
    public static PatchFile setEnabled(final PatchFile patchFile, final int patchIndex,
            final boolean enabled) throws IOException {
        if (patchIndex < 0 || patchIndex >= patchFile.patches.size()) {
            throw new IOException("No such patch.");
        }
        final Patch patch = patchFile.patches.get(patchIndex);
        final List<String> lines = readLines(patchFile.file);
        final String flag = "is_enabled = " + (enabled ? "true" : "false");
        if (patch.enabledLineIndex >= 0 && patch.enabledLineIndex < lines.size()) {
            lines.set(patch.enabledLineIndex, indentOf(
                    lines.get(patch.enabledLineIndex)) + flag);
        } else if (patch.headerLineIndex >= 0
                && patch.headerLineIndex < lines.size()) {
            // No is_enabled line existed; insert one under the header.
            lines.add(patch.headerLineIndex + 1, flag);
        } else {
            throw new IOException("Could not locate the patch in the file.");
        }
        writeLines(patchFile.file, lines);
        return parse(patchFile.file);
    }

    public static void delete(final PatchFile patchFile) {
        if (patchFile != null && patchFile.file != null) {
            patchFile.file.delete();
        }
    }

    // ---- helpers ----------------------------------------------------------

    private static String stringValue(final String line) {
        final int first = line.indexOf('"');
        final int last = line.lastIndexOf('"');
        if (first >= 0 && last > first) {
            return line.substring(first + 1, last);
        }
        return "";
    }

    private static String indentOf(final String line) {
        int i = 0;
        while (i < line.length()
                && (line.charAt(i) == ' ' || line.charAt(i) == '\t')) {
            i++;
        }
        return line.substring(0, i);
    }

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
        return cleaned.isEmpty() ? "patch.patch.toml" : cleaned;
    }

    private static List<String> readLines(final File file) throws IOException {
        final ArrayList<String> lines = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(new java.io.FileInputStream(file),
                        StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                lines.add(line);
            }
        }
        return lines;
    }

    private static void writeLines(final File file, final List<String> lines)
            throws IOException {
        try (OutputStream os = new FileOutputStream(file)) {
            final StringBuilder sb = new StringBuilder();
            for (final String line : lines) {
                sb.append(line).append('\n');
            }
            os.write(sb.toString().getBytes(StandardCharsets.UTF_8));
        }
    }
}
