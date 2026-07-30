package jp.xenia.emulator;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Content manager: install DLC and Title Update packages (LIVE / PIRS / CON
 * files) into the emulator's content root, list what's installed, and delete
 * packages. The XenDroid-parity feature for the launcher UI - the extraction
 * itself is the pure-Java {@link ContentInstaller} (no native library load),
 * writing the exact folder layout the native {@code xam::ContentManager}
 * reads at {@code files/content/<TITLEID>/<CONTENTTYPE>/<package_name>/}.
 *
 * <p>Optionally filtered to one title via {@link #EXTRA_TITLE_ID} (the
 * launcher's per-game menu); without it, shows content for every title.
 */
public final class ContentManagerActivity extends Activity {
    public static final String EXTRA_TITLE_ID = "jp.xenia.emulator.CONTENT_TITLE_ID";
    public static final String EXTRA_TITLE_NAME = "jp.xenia.emulator.CONTENT_TITLE_NAME";

    private static final int REQUEST_PICK_PACKAGE = 1;

    private String mTitleId = "";
    private String mTitleName = "";
    private LinearLayout mListContainer;
    private TextView mStatus;
    private boolean mBusy;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        final Intent intent = getIntent();
        if (intent != null) {
            mTitleId = safe(intent.getStringExtra(EXTRA_TITLE_ID)).toUpperCase(Locale.US);
            mTitleName = safe(intent.getStringExtra(EXTRA_TITLE_NAME));
        }

        final ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(true);

        final LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(18), dp(24), dp(18));
        scrollView.addView(root);

        final TextView title = new TextView(this);
        title.setTextSize(22);
        title.setText(mTitleId.isEmpty()
                ? "Content manager"
                : "Content - " + (mTitleName.isEmpty() ? mTitleId : mTitleName));
        root.addView(title);

        final TextView subtitle = new TextView(this);
        subtitle.setTextSize(13);
        subtitle.setText(
                "Install DLC and Title Update packages (LIVE/PIRS/CON files). "
                        + "Installed content is read by games at launch.");
        subtitle.setPadding(0, dp(4), 0, dp(12));
        root.addView(subtitle);

        final Button installButton = new Button(this);
        installButton.setText("Install content package…");
        installButton.setOnClickListener(v -> pickPackage());
        root.addView(installButton);

        mStatus = new TextView(this);
        mStatus.setTextSize(13);
        mStatus.setPadding(0, dp(8), 0, dp(8));
        root.addView(mStatus);

        mListContainer = new LinearLayout(this);
        mListContainer.setOrientation(LinearLayout.VERTICAL);
        root.addView(mListContainer);

        setContentView(scrollView);
        refreshList();
    }

    private File contentRoot() {
        // Matches xenia_main.cc: content_root = storage_root/content where
        // storage_root = getFilesDir() on Android.
        return new File(getFilesDir(), "content");
    }

    private void pickPackage() {
        if (mBusy) {
            return;
        }
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, REQUEST_PICK_PACKAGE);
        } catch (final Exception e) {
            Toast.makeText(this, "No file picker available", Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(final int requestCode, final int resultCode,
                                    final Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICK_PACKAGE || resultCode != RESULT_OK
                || data == null || data.getData() == null) {
            return;
        }
        installFromUri(data.getData());
    }

    private void installFromUri(final Uri uri) {
        mBusy = true;
        mStatus.setText("Installing…");
        new Thread(() -> {
            String result;
            try {
                // SAF content must be a real file for random access - copy to
                // cache first (packages are typically a few MB to a few
                // hundred MB).
                final File temp = new File(getCacheDir(), "content_install.tmp");
                copyUriToFile(uri, temp);
                final ContentInstaller installer = new ContentInstaller(temp);
                try {
                    final ContentInstaller.PackageInfo info = installer.info();
                    if (!mTitleId.isEmpty()
                            && !info.titleIdHex().equalsIgnoreCase(mTitleId)) {
                        throw new IOException("Package is for title "
                                + info.titleIdHex() + ", not " + mTitleId);
                    }
                    final String packageName = packageNameFromUri(uri);
                    final File dest = new File(contentRoot(),
                            info.titleIdHex() + File.separator
                                    + info.contentTypeHex() + File.separator
                                    + packageName);
                    if (dest.exists()) {
                        ContentInstaller.deleteRecursively(dest);
                    }
                    //noinspection ResultOfMethodCallIgnored
                    dest.mkdirs();
                    final int files = installer.extractTo(dest);
                    result = "Installed \"" + displayOrName(info, packageName)
                            + "\" (" + info.contentTypeName() + ", " + files
                            + " file" + (files == 1 ? "" : "s") + ") for title "
                            + info.titleIdHex();
                } finally {
                    installer.close();
                    //noinspection ResultOfMethodCallIgnored
                    temp.delete();
                }
            } catch (final Exception e) {
                result = "Install failed: " + e.getMessage();
            }
            final String finalResult = result;
            mMainHandler.post(() -> {
                mBusy = false;
                mStatus.setText(finalResult);
                refreshList();
            });
        }, "ContentInstall").start();
    }

    private static String displayOrName(final ContentInstaller.PackageInfo info,
                                        final String fallback) {
        return info.displayName == null || info.displayName.isEmpty()
                ? fallback : info.displayName;
    }

    private String packageNameFromUri(final Uri uri) {
        String name = uri.getLastPathSegment();
        if (name == null) {
            name = "package";
        }
        final int slash = name.lastIndexOf('/');
        if (slash >= 0) {
            name = name.substring(slash + 1);
        }
        final int colon = name.lastIndexOf(':');
        if (colon >= 0) {
            name = name.substring(colon + 1);
        }
        // The native ContentManager uses the folder name as the package file
        // name; keep it filesystem-safe.
        name = name.replaceAll("[^A-Za-z0-9._ -]", "_");
        return name.isEmpty() ? "package" : name;
    }

    private void copyUriToFile(final Uri uri, final File dest) throws IOException {
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(dest)) {
            if (in == null) {
                throw new IOException("Could not open the selected file");
            }
            final byte[] buffer = new byte[1 << 16];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        }
    }

    private static final class InstalledPackage {
        String titleId;
        String contentType;
        File dir;
    }

    private void refreshList() {
        mListContainer.removeAllViews();
        final List<InstalledPackage> installed = new ArrayList<>();
        final File root = contentRoot();
        final File[] titles = root.listFiles();
        if (titles != null) {
            for (final File titleDir : titles) {
                if (!titleDir.isDirectory()) {
                    continue;
                }
                if (!mTitleId.isEmpty()
                        && !titleDir.getName().equalsIgnoreCase(mTitleId)) {
                    continue;
                }
                final File[] types = titleDir.listFiles();
                if (types == null) {
                    continue;
                }
                for (final File typeDir : types) {
                    if (!typeDir.isDirectory()) {
                        continue;
                    }
                    final File[] packages = typeDir.listFiles();
                    if (packages == null) {
                        continue;
                    }
                    for (final File packageDir : packages) {
                        final InstalledPackage p = new InstalledPackage();
                        p.titleId = titleDir.getName();
                        p.contentType = typeDir.getName();
                        p.dir = packageDir;
                        installed.add(p);
                    }
                }
            }
        }

        if (installed.isEmpty()) {
            final TextView empty = new TextView(this);
            empty.setTextSize(13);
            empty.setText("No content installed"
                    + (mTitleId.isEmpty() ? "." : " for this title."));
            mListContainer.addView(empty);
            return;
        }

        for (final InstalledPackage p : installed) {
            final LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setPadding(0, dp(6), 0, dp(6));

            final TextView label = new TextView(this);
            label.setTextSize(14);
            label.setText(p.dir.getName() + "\n" + p.titleId + " · "
                    + contentTypeName(p.contentType));
            final LinearLayout.LayoutParams labelParams =
                    new LinearLayout.LayoutParams(0,
                            LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
            row.addView(label, labelParams);

            final Button delete = new Button(this);
            delete.setText("Delete");
            delete.setOnClickListener(v -> confirmDelete(p));
            row.addView(delete);

            mListContainer.addView(row);
        }
    }

    private static String contentTypeName(final String hex) {
        try {
            final long value = Long.parseLong(hex, 16);
            if (value == 0x2) return "DLC";
            if (value == 0x1) return "Saved game";
            if (value == 0xB0000) return "Title update";
            if (value == 0x7000) return "Installer";
            if (value == 0x80000) return "Profile";
        } catch (final NumberFormatException ignored) {
        }
        return "Type " + hex;
    }

    private void confirmDelete(final InstalledPackage p) {
        new AlertDialog.Builder(this)
                .setTitle("Delete content?")
                .setMessage(p.dir.getName() + " (" + p.titleId + ")")
                .setPositiveButton("Delete", (dialog, which) -> {
                    ContentInstaller.deleteRecursively(p.dir);
                    refreshList();
                })
                .setNegativeButton("Cancel", null)
                .show();
    }

    private static String safe(final String value) {
        return value == null ? "" : value;
    }

    private int dp(final int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
