package jp.xenia.emulator;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public final class XeniaCoverArt {
    public static final String DEFAULT_COVER_DATABASE_URL =
            "https://raw.githubusercontent.com/xenia-manager/x360db/main/games.json";

    private static final long COVER_DATABASE_CACHE_MS = 7L * 24L * 60L * 60L * 1000L;
    private static final int CONNECT_TIMEOUT_MS = 7000;
    private static final int READ_TIMEOUT_MS = 10000;
    private static final Pattern TITLE_ID_PATTERN =
            Pattern.compile("(?i)(?:^|[^0-9a-f])([0-9a-f]{8})(?:$|[^0-9a-f])");
    private static final ExecutorService COVER_EXECUTOR =
            Executors.newFixedThreadPool(2);
    private static final Object ACTIVE_LOCK = new Object();
    private static final Set<String> ACTIVE_LOOKUPS = new HashSet<>();

    private XeniaCoverArt() {
    }

    public static void loadInto(
            final Context context,
            final ImageView coverImage,
            final TextView placeholder,
            final String title,
            final String path) {
        if (context == null || coverImage == null) {
            return;
        }
        final Context appContext = context.getApplicationContext();
        final String lookupKey = buildLookupKey(title, path);
        coverImage.setTag(lookupKey);

        final Bitmap cached = decodeBitmap(getCoverFile(appContext, lookupKey));
        if (cached != null) {
            applyCover(coverImage, placeholder, lookupKey, cached);
            return;
        }
        coverImage.setVisibility(View.GONE);
        if (placeholder != null) {
            placeholder.setVisibility(View.VISIBLE);
        }

        synchronized (ACTIVE_LOCK) {
            if (ACTIVE_LOOKUPS.contains(lookupKey)) {
                return;
            }
            ACTIVE_LOOKUPS.add(lookupKey);
        }

        COVER_EXECUTOR.execute(new Runnable() {
            @Override
            public void run() {
                try {
                    final String coverUrl = findCoverUrl(appContext, title, path);
                    if (coverUrl == null || coverUrl.isEmpty()) {
                        return;
                    }
                    final Bitmap bitmap = getOrDownloadCover(appContext, lookupKey, coverUrl);
                    if (bitmap != null) {
                        coverImage.post(new Runnable() {
                            @Override
                            public void run() {
                                applyCover(coverImage, placeholder, lookupKey, bitmap);
                            }
                        });
                    }
                } finally {
                    synchronized (ACTIVE_LOCK) {
                        ACTIVE_LOOKUPS.remove(lookupKey);
                    }
                }
            }
        });
    }

    public static String findCachedTitleId(
            final Context context,
            final String title,
            final String path) {
        final String titleId = findTitleId(title + " " + path);
        if (!titleId.isEmpty()) {
            return titleId;
        }
        if (context == null) {
            return "";
        }
        final File databaseFile =
                new File(context.getCacheDir(), "x360db-games.json");
        if (!databaseFile.isFile()) {
            return "";
        }
        try {
            return findTitleIdInDatabase(readText(databaseFile), title, path);
        } catch (final IOException | JSONException ignored) {
            return "";
        }
    }

    private static void applyCover(
            final ImageView coverImage,
            final TextView placeholder,
            final String lookupKey,
            final Bitmap bitmap) {
        final Object tag = coverImage.getTag();
        if (!(tag instanceof String) || !lookupKey.equals(tag)) {
            return;
        }
        coverImage.setImageBitmap(bitmap);
        coverImage.setVisibility(View.VISIBLE);
        if (placeholder != null) {
            placeholder.setVisibility(View.GONE);
        }
    }

    private static Bitmap getOrDownloadCover(
            final Context context,
            final String lookupKey,
            final String coverUrl) {
        final File coverFile = getCoverFile(context, lookupKey);
        final Bitmap cached = decodeBitmap(coverFile);
        if (cached != null) {
            return cached;
        }
        try {
            downloadToFile(coverUrl, coverFile);
        } catch (final IOException ignored) {
            return null;
        }
        return decodeBitmap(coverFile);
    }

    private static String findCoverUrl(
            final Context context,
            final String title,
            final String path) {
        try {
            final String json = readCoverDatabase(context);
            final String titleId = findTitleId(title + " " + path);
            final String normalizedTitle = normalizeTitle(title);
            String fuzzyMatch = "";
            final JSONArray games = new JSONArray(json);
            for (int i = 0; i < games.length(); ++i) {
                final JSONObject game = games.optJSONObject(i);
                if (game == null) {
                    continue;
                }
                final String cover = sanitizeCoverUrl(game.optString("boxart", ""));
                if (cover.isEmpty()) {
                    continue;
                }
                final String gameId = game.optString("id", "").toUpperCase(Locale.US);
                if (!titleId.isEmpty() && titleId.equals(gameId)) {
                    return cover;
                }
                if (matchesAlternativeId(game, titleId)) {
                    return cover;
                }
                final String gameTitle = normalizeTitle(game.optString("title", ""));
                if (!normalizedTitle.isEmpty() && normalizedTitle.equals(gameTitle)) {
                    return cover;
                }
                if (fuzzyMatch.isEmpty()
                        && normalizedTitle.length() >= 6
                        && gameTitle.length() >= 6
                        && (normalizedTitle.contains(gameTitle)
                        || gameTitle.contains(normalizedTitle))) {
                    fuzzyMatch = cover;
                }
            }
            return fuzzyMatch;
        } catch (final IOException | JSONException ignored) {
            return "";
        }
    }

    private static String findTitleIdInDatabase(
            final String json,
            final String title,
            final String path) throws JSONException {
        final String normalizedTitle = normalizeTitle(title);
        String fuzzyMatch = "";
        final JSONArray games = new JSONArray(json);
        for (int i = 0; i < games.length(); ++i) {
            final JSONObject game = games.optJSONObject(i);
            if (game == null) {
                continue;
            }
            final String gameId = game.optString("id", "").toUpperCase(Locale.US);
            if (gameId.isEmpty()) {
                continue;
            }
            final String gameTitle = normalizeTitle(game.optString("title", ""));
            if (!normalizedTitle.isEmpty() && normalizedTitle.equals(gameTitle)) {
                return gameId;
            }
            if (fuzzyMatch.isEmpty()
                    && normalizedTitle.length() >= 6
                    && gameTitle.length() >= 6
                    && (normalizedTitle.contains(gameTitle)
                    || gameTitle.contains(normalizedTitle))) {
                fuzzyMatch = gameId;
            }
        }
        return fuzzyMatch;
    }

    private static boolean matchesAlternativeId(
            final JSONObject game,
            final String titleId) {
        if (titleId == null || titleId.isEmpty()) {
            return false;
        }
        final JSONArray alternatives = game.optJSONArray("alternative_id");
        if (alternatives == null) {
            return false;
        }
        for (int i = 0; i < alternatives.length(); ++i) {
            if (titleId.equals(alternatives.optString(i, "").toUpperCase(Locale.US))) {
                return true;
            }
        }
        return false;
    }

    private static String readCoverDatabase(final Context context) throws IOException {
        final File databaseFile = new File(context.getCacheDir(), "x360db-games.json");
        final long nowMs = System.currentTimeMillis();
        if (databaseFile.isFile()
                && nowMs - databaseFile.lastModified() < COVER_DATABASE_CACHE_MS) {
            return readText(databaseFile);
        }
        try {
            downloadToFile(DEFAULT_COVER_DATABASE_URL, databaseFile);
        } catch (final IOException exception) {
            if (databaseFile.isFile()) {
                return readText(databaseFile);
            }
            throw exception;
        }
        return readText(databaseFile);
    }

    private static String readText(final File file) throws IOException {
        final InputStream input = new java.io.FileInputStream(file);
        try {
            final ByteArrayOutputStream output = new ByteArrayOutputStream();
            final byte[] buffer = new byte[16 * 1024];
            while (true) {
                final int read = input.read(buffer);
                if (read < 0) {
                    break;
                }
                output.write(buffer, 0, read);
            }
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        } finally {
            input.close();
        }
    }

    private static void downloadToFile(
            final String urlText,
            final File outFile) throws IOException {
        final File parent = outFile.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Unable to create " + parent);
        }
        final HttpURLConnection connection =
                (HttpURLConnection) new URL(urlText).openConnection();
        connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
        connection.setReadTimeout(READ_TIMEOUT_MS);
        connection.setInstanceFollowRedirects(true);
        connection.setRequestProperty("User-Agent", "XeniaThorAndroid");
        try {
            final int responseCode = connection.getResponseCode();
            if (responseCode < 200 || responseCode >= 300) {
                throw new IOException("HTTP " + responseCode);
            }
            final InputStream input = connection.getInputStream();
            try {
                final FileOutputStream output = new FileOutputStream(outFile);
                try {
                    final byte[] buffer = new byte[16 * 1024];
                    while (true) {
                        final int read = input.read(buffer);
                        if (read < 0) {
                            break;
                        }
                        output.write(buffer, 0, read);
                    }
                } finally {
                    output.close();
                }
            } finally {
                input.close();
            }
        } finally {
            connection.disconnect();
        }
    }

    private static Bitmap decodeBitmap(final File file) {
        if (file == null || !file.isFile() || file.length() <= 0) {
            return null;
        }
        return BitmapFactory.decodeFile(file.getAbsolutePath());
    }

    private static File getCoverFile(final Context context, final String lookupKey) {
        return new File(new File(context.getCacheDir(), "cover-art"),
                stableFileName(lookupKey) + ".jpg");
    }

    private static String buildLookupKey(final String title, final String path) {
        final String titleId = findTitleId(title + " " + path);
        if (!titleId.isEmpty()) {
            return "id-" + titleId;
        }
        final String normalizedTitle = normalizeTitle(title);
        return !normalizedTitle.isEmpty() ? normalizedTitle : normalizeTitle(path);
    }

    private static String findTitleId(final String text) {
        if (text == null || text.isEmpty()) {
            return "";
        }
        final Matcher matcher = TITLE_ID_PATTERN.matcher(text);
        if (matcher.find()) {
            return matcher.group(1).toUpperCase(Locale.US);
        }
        return "";
    }

    private static String normalizeTitle(final String text) {
        if (text == null || text.isEmpty()) {
            return "";
        }
        String value = text;
        final int slash = Math.max(value.lastIndexOf('/'), value.lastIndexOf('\\'));
        if (slash >= 0 && slash + 1 < value.length()) {
            value = value.substring(slash + 1);
        }
        final int dot = value.lastIndexOf('.');
        if (dot > 0) {
            value = value.substring(0, dot);
        }
        value = value.replaceAll("\\[[^\\]]*\\]", " ");
        value = value.replaceAll("\\([^\\)]*\\)", " ");
        value = value.replace('_', ' ');
        value = value.replace('-', ' ');
        value = value.toLowerCase(Locale.US);
        value = value.replaceAll("[^a-z0-9]+", " ").trim();
        value = value.replaceAll("\\s+", " ");
        return value;
    }

    private static String sanitizeCoverUrl(final String rawUrl) {
        if (rawUrl == null) {
            return "";
        }
        return rawUrl.trim();
    }

    private static String stableFileName(final String key) {
        try {
            final MessageDigest digest = MessageDigest.getInstance("SHA-1");
            final byte[] bytes = digest.digest(key.getBytes(StandardCharsets.UTF_8));
            final StringBuilder builder = new StringBuilder();
            for (final byte value : bytes) {
                builder.append(String.format(Locale.US, "%02x", value & 0xFF));
            }
            return builder.toString();
        } catch (final NoSuchAlgorithmException ignored) {
            return Integer.toHexString(key.hashCode());
        }
    }
}
