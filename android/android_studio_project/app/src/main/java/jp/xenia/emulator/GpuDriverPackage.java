package jp.xenia.emulator;

import org.json.JSONObject;

import java.io.File;

/**
 * One installed custom Vulkan ICD (GPU driver) package, in the adrenotools
 * ADPKG format (a {@code meta.json} describing the package plus the main driver
 * {@code .so} and any private dependencies, see
 * {@code third_party/adrenotools/tools/ADPKG.md}).
 *
 * <p>The Thor ships the libadrenotools hook, so a user-imported Mesa Turnip (or
 * Qualcomm) driver can be loaded rootlessly at startup via the
 * {@code gpu_vulkan_driver*} cvars ({@code src/xenia/ui/vulkan/vulkan_instance.cc}).
 * {@link GpuDriverManager} installs these packages into the app's private files
 * dir and wires the selected one into the emulator launch arguments.
 */
public final class GpuDriverPackage {
    /** Storage slug = the directory name under {@code files/gpu_drivers/}. */
    public final String id;
    public final String name;
    public final String description;
    public final String author;
    /** "Mesa" (Turnip) or "Qualcomm" per the ADPKG schema. */
    public final String vendor;
    public final String driverVersion;
    public final String packageVersion;
    public final int minApi;
    /** Filename of the main ICD .so inside {@link #dir}. */
    public final String libraryName;
    /** Absolute install directory holding meta.json + the .so files. */
    public final File dir;

    GpuDriverPackage(
            final String id,
            final String name,
            final String description,
            final String author,
            final String vendor,
            final String driverVersion,
            final String packageVersion,
            final int minApi,
            final String libraryName,
            final File dir) {
        this.id = id;
        this.name = name;
        this.description = description;
        this.author = author;
        this.vendor = vendor;
        this.driverVersion = driverVersion;
        this.packageVersion = packageVersion;
        this.minApi = minApi;
        this.libraryName = libraryName;
        this.dir = dir;
    }

    public File libraryFile() {
        return new File(dir, libraryName);
    }

    /** True when the install dir + the main .so are present on disk. */
    public boolean isInstalledValid() {
        return dir != null && dir.isDirectory() && libraryFile().isFile();
    }

    /** Short one-line version label, e.g. "Mesa · 26.0.0". */
    public String displayVersion() {
        final StringBuilder builder = new StringBuilder();
        if (vendor != null && !vendor.isEmpty()) {
            builder.append(vendor);
        }
        if (driverVersion != null && !driverVersion.isEmpty()) {
            if (builder.length() > 0) {
                builder.append(" · ");
            }
            builder.append(driverVersion);
        }
        return builder.toString();
    }

    /**
     * Parse an ADPKG meta.json into a package descriptor.
     *
     * @throws IllegalArgumentException with a user-facing message if the package
     *     is not a usable driver (no {@code libraryName}).
     */
    static GpuDriverPackage fromMeta(
            final JSONObject meta, final String id, final File dir) {
        final String libraryName = meta.optString("libraryName", "").trim();
        if (libraryName.isEmpty()) {
            throw new IllegalArgumentException(
                    "meta.json is missing \"libraryName\" (the driver .so)");
        }
        return new GpuDriverPackage(
                id,
                firstNonEmpty(meta.optString("name", ""), "Unnamed driver"),
                meta.optString("description", ""),
                meta.optString("author", ""),
                meta.optString("vendor", ""),
                meta.optString("driverVersion", ""),
                meta.optString("packageVersion", ""),
                meta.optInt("minApi", 0),
                libraryName,
                dir);
    }

    private static String firstNonEmpty(final String value, final String fallback) {
        return value != null && !value.trim().isEmpty() ? value.trim() : fallback;
    }
}
