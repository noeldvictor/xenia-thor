package jp.xenia.emulator;

import android.content.Context;
import android.os.Bundle;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Install / select / apply custom Vulkan ICD (GPU driver) packages for the Thor.
 *
 * <p>Packages are adrenotools ADPKG zips (meta.json + the driver .so). They are
 * installed into {@code files/gpu_drivers/<id>/}; the selected package is wired
 * into the emulator launch arguments by {@link #applyToLaunch}, which sets the
 * {@code gpu_vulkan_driver*} cvars the native loader reads
 * ({@code src/xenia/ui/vulkan/vulkan_instance.cc}). A bad/missing package can
 * never brick a launch — the native loader falls back to the system driver.
 */
public final class GpuDriverManager {
    public static final String DRIVERS_DIRNAME = "gpu_drivers";
    private static final String META_NAME = "meta.json";
    /** Empty selection = the stock Qualcomm proprietary driver (no override). */
    public static final String SYSTEM_ID = "";

    private static final int MAX_PACKAGE_BYTES = 64 * 1024 * 1024;
    private static final int SCAN_DEPTH = 3;

    private GpuDriverManager() {
    }

    /** A curated, one-tap recommended driver (verified download URLs). */
    public static final class Recommended {
        public final String title;
        public final String summary;
        public final String url;
        public final boolean recommended;

        Recommended(
                final String title,
                final String summary,
                final String url,
                final boolean recommended) {
            this.title = title;
            this.summary = summary;
            this.url = url;
            this.recommended = recommended;
        }
    }

    /**
     * Curated Turnip builds for the AYN Thor's Adreno 740 (Snapdragon 8 Gen 2),
     * from the community-standard K11MCH1/AdrenoToolsDrivers releases. Mesa Turnip
     * is open-source and usually faster + more correct than the stock Qualcomm
     * driver for emulation on this GPU.
     */
    public static final List<Recommended> RECOMMENDED;
    public static final String RELEASES_PAGE_URL =
            "https://github.com/K11MCH1/AdrenoToolsDrivers/releases";

    static {
        final ArrayList<Recommended> list = new ArrayList<>();
        list.add(new Recommended(
                "Turnip v26.0.0 R8",
                "Latest stable Mesa Turnip for Adreno 7xx — the recommended default. "
                        + "Open-source Vulkan driver; generally the best all-round pick "
                        + "for Blue Dragon and the other priority titles.",
                "https://github.com/K11MCH1/AdrenoToolsDrivers/releases/download/"
                        + "v26.0.0-rc08/Turnip_v26.0.0_R8.zip",
                true));
        list.add(new Recommended(
                "Turnip v26.0.0 R8 — Sysmem",
                "Same build forced to direct (sysmem) rendering, bypassing GMEM tile "
                        + "binning. Experimental: useful for A/B-ing the tile-binning cost "
                        + "on Blue Dragon's heavy scene. May be slower on most content.",
                "https://github.com/K11MCH1/AdrenoToolsDrivers/releases/download/"
                        + "v26.0.0-rc08/Turnip_v26.0.0_R8_Sysmem.zip",
                false));
        list.add(new Recommended(
                "Turnip v26.0.0 R7",
                "Proven community-favourite build for the Adreno 7xx series. Fall back "
                        + "here if a newer build regresses on a particular game.",
                "https://github.com/K11MCH1/AdrenoToolsDrivers/releases/download/"
                        + "v26.0.0-rc07/Turnip_v26.0.0_R7.zip",
                false));
        RECOMMENDED = Collections.unmodifiableList(list);
    }

    // ---- storage ----------------------------------------------------------

    public static File driversDir(final Context context) {
        return new File(context.getFilesDir(), DRIVERS_DIRNAME);
    }

    /** All valid installed packages, sorted by display name. */
    public static List<GpuDriverPackage> listInstalled(final Context context) {
        final ArrayList<GpuDriverPackage> out = new ArrayList<>();
        final File[] dirs = driversDir(context).listFiles();
        if (dirs != null) {
            for (final File dir : dirs) {
                if (!dir.isDirectory()) {
                    continue;
                }
                final GpuDriverPackage pkg = tryLoad(dir);
                if (pkg != null) {
                    out.add(pkg);
                }
            }
        }
        Collections.sort(out, new Comparator<GpuDriverPackage>() {
            @Override
            public int compare(final GpuDriverPackage a, final GpuDriverPackage b) {
                return a.name.toLowerCase(Locale.US).compareTo(b.name.toLowerCase(Locale.US));
            }
        });
        return out;
    }

    static GpuDriverPackage tryLoad(final File dir) {
        final File meta = new File(dir, META_NAME);
        if (!meta.isFile()) {
            return null;
        }
        try {
            final GpuDriverPackage pkg = GpuDriverPackage.fromMeta(
                    new JSONObject(readFile(meta)), dir.getName(), dir);
            return pkg.isInstalledValid() ? pkg : null;
        } catch (final Exception ignored) {
            return null;
        }
    }

    // ---- selection --------------------------------------------------------

    public static String getSelectedId(final Context context) {
        return XeniaAndroidSettings.getPreferences(context)
                .getString(XeniaAndroidSettings.KEY_GPU_DRIVER_PACKAGE, SYSTEM_ID);
    }

    public static void setSelectedId(final Context context, final String id) {
        XeniaAndroidSettings.getPreferences(context).edit()
                .putString(
                        XeniaAndroidSettings.KEY_GPU_DRIVER_PACKAGE,
                        id != null ? id : SYSTEM_ID)
                .apply();
    }

    /** The selected package, or null when the system driver is selected/invalid. */
    public static GpuDriverPackage getSelected(final Context context) {
        final String id = getSelectedId(context);
        if (id == null || id.isEmpty()) {
            return null;
        }
        return tryLoad(new File(driversDir(context), id));
    }

    public static void delete(final Context context, final String id) {
        if (id == null || id.isEmpty()) {
            return;
        }
        deleteRecursive(new File(driversDir(context), id));
        if (id.equals(getSelectedId(context))) {
            setSelectedId(context, SYSTEM_ID);
        }
    }

    /**
     * Set the libadrenotools launch cvars for the selected custom driver. No-op
     * (leaves {@code gpu_vulkan_driver} at its "system" default) when the system
     * driver is selected; self-heals a stale selection (deleted/corrupt package)
     * back to the system driver.
     */
    public static void applyToLaunch(final Context context, final Bundle launchArguments) {
        final GpuDriverPackage pkg = getSelected(context);
        if (pkg == null || !pkg.isInstalledValid()) {
            if (getSelectedId(context) != null && !getSelectedId(context).isEmpty()) {
                setSelectedId(context, SYSTEM_ID);
            }
            return;
        }
        final String hooks = context.getApplicationInfo().nativeLibraryDir;
        // Native loader treats "turnip" as "load this custom ICD via adrenotools"
        // (works for any vendor); trailing slash required by adrenotools' path join.
        launchArguments.putString("gpu_vulkan_driver", "turnip");
        launchArguments.putString(
                "gpu_vulkan_driver_path", pkg.dir.getAbsolutePath() + File.separator);
        launchArguments.putString("gpu_vulkan_driver_lib", pkg.libraryName);
        if (hooks != null && !hooks.isEmpty()) {
            launchArguments.putString("gpu_vulkan_driver_hooks_path", hooks);
        }
    }

    // ---- install ----------------------------------------------------------

    /** Download an ADPKG zip over http(s) and install it. Runs synchronously. */
    public static GpuDriverPackage installFromUrl(
            final Context context, final String urlString) throws IOException {
        final String trimmed = urlString != null ? urlString.trim() : "";
        if (trimmed.isEmpty()) {
            throw new IllegalArgumentException("Enter a driver download URL");
        }
        final URL url = new URL(trimmed);
        final String protocol = url.getProtocol();
        if (!"https".equalsIgnoreCase(protocol) && !"http".equalsIgnoreCase(protocol)) {
            throw new IllegalArgumentException("URL must start with http:// or https://");
        }
        final HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        connection.setInstanceFollowRedirects(true);
        connection.setConnectTimeout(20000);
        connection.setReadTimeout(60000);
        connection.setRequestProperty("User-Agent", "xenia-thor");
        connection.setRequestProperty("Accept", "application/octet-stream, application/zip, */*");
        try {
            connection.connect();
            final int code = connection.getResponseCode();
            if (code / 100 != 2) {
                throw new IOException("Download failed (HTTP " + code + ")");
            }
            try (InputStream in = connection.getInputStream()) {
                return installFromZip(context, in);
            }
        } finally {
            connection.disconnect();
        }
    }

    /**
     * Install an ADPKG zip read from {@code in}. Unzips to a temp dir, validates
     * meta.json + the named .so, then moves it to {@code files/gpu_drivers/<id>/}.
     *
     * @throws IllegalArgumentException with a user-facing message if the zip is
     *     not a usable driver package.
     */
    public static GpuDriverPackage installFromZip(
            final Context context, final InputStream in) throws IOException {
        final File tmp = new File(
                context.getCacheDir(), "driver_import_" + System.nanoTime());
        deleteRecursive(tmp);
        if (!tmp.mkdirs()) {
            throw new IOException("Could not create a temp directory");
        }
        try {
            unzipInto(in, tmp);
            final File metaFile = findMeta(tmp, SCAN_DEPTH);
            if (metaFile == null) {
                throw new IllegalArgumentException(
                        "Not a driver package (no meta.json inside the zip)");
            }
            final File pkgRoot = metaFile.getParentFile();
            final JSONObject meta;
            try {
                meta = new JSONObject(readFile(metaFile));
            } catch (final org.json.JSONException e) {
                throw new IllegalArgumentException("meta.json is not valid JSON");
            }
            final String libraryName = meta.optString("libraryName", "").trim();
            if (libraryName.isEmpty()) {
                throw new IllegalArgumentException(
                        "meta.json is missing \"libraryName\"");
            }
            if (!new File(pkgRoot, libraryName).isFile()) {
                throw new IllegalArgumentException(
                        "Driver library \"" + libraryName + "\" is missing from the package");
            }
            final String id = makeId(meta);
            final File dest = new File(driversDir(context), id);
            deleteRecursive(dest);
            final File parent = dest.getParentFile();
            if (parent != null) {
                parent.mkdirs();
            }
            copyDir(pkgRoot, dest);
            final GpuDriverPackage pkg = GpuDriverPackage.fromMeta(meta, id, dest);
            if (!pkg.isInstalledValid()) {
                deleteRecursive(dest);
                throw new IOException("Driver did not install cleanly");
            }
            return pkg;
        } finally {
            deleteRecursive(tmp);
        }
    }

    // ---- helpers ----------------------------------------------------------

    private static void unzipInto(final InputStream in, final File destDir)
            throws IOException {
        final String destCanonical = destDir.getCanonicalPath() + File.separator;
        boolean any = false;
        long totalBytes = 0;
        try (ZipInputStream zis = new ZipInputStream(new BufferedInputStream(in))) {
            ZipEntry entry;
            while ((entry = zis.getNextEntry()) != null) {
                final String name = entry.getName();
                if (name == null || entry.isDirectory()) {
                    continue;
                }
                final File out = new File(destDir, name);
                // Reject zip-slip path traversal.
                if (!(out.getCanonicalPath() + "").startsWith(destCanonical)) {
                    throw new IOException("Unsafe entry in zip: " + name);
                }
                final File outParent = out.getParentFile();
                if (outParent != null) {
                    outParent.mkdirs();
                }
                try (OutputStream os = new FileOutputStream(out)) {
                    final byte[] buffer = new byte[8192];
                    int read;
                    while ((read = zis.read(buffer)) != -1) {
                        totalBytes += read;
                        if (totalBytes > MAX_PACKAGE_BYTES) {
                            throw new IOException("Driver package is too large");
                        }
                        os.write(buffer, 0, read);
                    }
                }
                any = true;
            }
        }
        if (!any) {
            throw new IllegalArgumentException("The zip is empty");
        }
    }

    private static File findMeta(final File dir, final int depth) {
        final File direct = new File(dir, META_NAME);
        if (direct.isFile()) {
            return direct;
        }
        if (depth <= 0) {
            return null;
        }
        final File[] children = dir.listFiles();
        if (children == null) {
            return null;
        }
        for (final File child : children) {
            if (child.isDirectory()) {
                final File found = findMeta(child, depth - 1);
                if (found != null) {
                    return found;
                }
            }
        }
        return null;
    }

    private static String makeId(final JSONObject meta) {
        final String base = (meta.optString("name", "driver") + "-"
                + meta.optString("driverVersion", "") + "-"
                + meta.optString("packageVersion", ""));
        final StringBuilder slug = new StringBuilder();
        for (int i = 0; i < base.length() && slug.length() < 64; ++i) {
            final char c = Character.toLowerCase(base.charAt(i));
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                slug.append(c);
            } else if (c == '.' || c == '-' || c == '_') {
                slug.append(c);
            } else if (slug.length() > 0 && slug.charAt(slug.length() - 1) != '-') {
                slug.append('-');
            }
        }
        String id = slug.toString().replaceAll("(^[-_.]+)|([-_.]+$)", "");
        if (id.isEmpty()) {
            id = "driver";
        }
        return id;
    }

    private static String readFile(final File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            final ByteArrayOutputStream out = new ByteArrayOutputStream();
            final byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            return out.toString("UTF-8");
        }
    }

    private static void copyDir(final File src, final File dest) throws IOException {
        if (!dest.exists() && !dest.mkdirs()) {
            throw new IOException("Could not create " + dest.getName());
        }
        final File[] children = src.listFiles();
        if (children == null) {
            return;
        }
        for (final File child : children) {
            final File target = new File(dest, child.getName());
            if (child.isDirectory()) {
                copyDir(child, target);
            } else {
                copyFile(child, target);
            }
        }
    }

    private static void copyFile(final File src, final File dest) throws IOException {
        try (InputStream in = new FileInputStream(src);
                OutputStream out = new FileOutputStream(dest)) {
            final byte[] buffer = new byte[8192];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        }
    }

    private static void deleteRecursive(final File file) {
        if (file == null || !file.exists()) {
            return;
        }
        if (file.isDirectory()) {
            final File[] children = file.listFiles();
            if (children != null) {
                for (final File child : children) {
                    deleteRecursive(child);
                }
            }
        }
        // Best-effort; a leftover temp file is harmless.
        file.delete();
    }
}
