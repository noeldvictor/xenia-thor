package jp.xenia.emulator;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.InputStream;
import java.util.List;
import java.util.concurrent.Callable;

/**
 * Custom GPU (Vulkan ICD) driver manager: pick the active driver, import an
 * adrenotools driver .zip from a file or URL, or one-tap a curated Turnip build
 * recommended for the Thor's Adreno 740. Mirrors the programmatic-UI idiom of
 * {@link SettingsActivity}. The selected driver is wired into launches by
 * {@link GpuDriverManager#applyToLaunch}.
 */
public final class GpuDriverManagerActivity extends Activity {
    private static final int REQ_IMPORT_FILE = 4011;

    private LinearLayout mListContainer;
    private TextView mStatus;
    private EditText mUrlInput;
    private boolean mBusy;

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        XeniaAndroidSettings.ensureInitialized(this);

        final ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(true);
        scrollView.setBackgroundColor(getColor(R.color.xenia_surface));

        final LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(18), dp(24), dp(18));
        scrollView.addView(root);

        addTitle(root);

        mStatus = new TextView(this);
        mStatus.setTextColor(getColor(R.color.xenia_green_soft));
        mStatus.setTextSize(12);
        mStatus.setVisibility(View.GONE);
        root.addView(mStatus, matchWrapWithTopMargin(8));

        root.addView(
                header("Active driver"), matchWrapWithTopMargin(14));
        final TextView activeBlurb = new TextView(this);
        activeBlurb.setText("Which Vulkan driver the emulator loads. The open-source "
                + "Turnip driver is usually faster and more correct than the stock "
                + "Qualcomm one on this GPU — but if a game breaks, switch back to the "
                + "system driver. A bad driver can never brick a launch (it falls back "
                + "to the system driver automatically).");
        activeBlurb.setTextColor(getColor(R.color.xenia_text_secondary));
        activeBlurb.setTextSize(12);
        root.addView(activeBlurb, matchWrapWithTopMargin(2));

        mListContainer = new LinearLayout(this);
        mListContainer.setOrientation(LinearLayout.VERTICAL);
        root.addView(mListContainer, matchWrapWithTopMargin(6));

        addInstallSection(root);
        addRecommendedSection(root);

        setContentView(scrollView);
        rebuildList();
    }

    @Override
    protected void onResume() {
        super.onResume();
        rebuildList();
    }

    private void addTitle(final LinearLayout root) {
        final TextView title = new TextView(this);
        title.setText("GPU drivers");
        title.setTextColor(getColor(R.color.xenia_text));
        title.setTextSize(24);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        root.addView(title, matchWrap());

        final TextView subtitle = new TextView(this);
        subtitle.setText("Download and switch custom Vulkan drivers for the Thor (Adreno 740).");
        subtitle.setTextColor(getColor(R.color.xenia_text_secondary));
        subtitle.setTextSize(13);
        final LinearLayout.LayoutParams params = matchWrap();
        params.setMargins(0, dp(4), 0, dp(6));
        root.addView(subtitle, params);
    }

    // ---- active-driver list ----------------------------------------------

    private void rebuildList() {
        if (mListContainer == null) {
            return;
        }
        mListContainer.removeAllViews();
        final String selectedId = GpuDriverManager.getSelectedId(this);

        addDriverRow(
                "System driver (Qualcomm)",
                "Stock proprietary Adreno driver shipped with the device.",
                GpuDriverManager.SYSTEM_ID,
                selectedId == null || selectedId.isEmpty(),
                false);

        final List<GpuDriverPackage> installed = GpuDriverManager.listInstalled(this);
        for (final GpuDriverPackage pkg : installed) {
            final StringBuilder detail = new StringBuilder();
            if (!pkg.displayVersion().isEmpty()) {
                detail.append(pkg.displayVersion());
            }
            if (!pkg.author.isEmpty()) {
                detail.append(detail.length() > 0 ? "  ·  " : "").append("by ").append(pkg.author);
            }
            if (!pkg.description.isEmpty()) {
                detail.append('\n').append(pkg.description);
            }
            addDriverRow(
                    pkg.name,
                    detail.toString(),
                    pkg.id,
                    pkg.id.equals(selectedId),
                    true);
        }

        if (installed.isEmpty()) {
            final TextView none = new TextView(this);
            none.setText("No custom drivers installed yet — import one below.");
            none.setTextColor(getColor(R.color.xenia_text_secondary));
            none.setTextSize(12);
            mListContainer.addView(none, matchWrapWithTopMargin(8));
        }
    }

    private void addDriverRow(
            final String name,
            final String detail,
            final String id,
            final boolean active,
            final boolean deletable) {
        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, dp(6), 0, dp(6));

        final LinearLayout textColumn = new LinearLayout(this);
        textColumn.setOrientation(LinearLayout.VERTICAL);
        final LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        row.addView(textColumn, textParams);

        final TextView titleView = new TextView(this);
        titleView.setText((active ? "●  " : "○  ") + name);
        titleView.setTextColor(getColor(active ? R.color.xenia_green_soft : R.color.xenia_text));
        titleView.setTextSize(15);
        titleView.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        textColumn.addView(titleView, matchWrap());

        if (detail != null && !detail.isEmpty()) {
            final TextView detailView = new TextView(this);
            detailView.setText(detail);
            detailView.setTextColor(getColor(R.color.xenia_text_secondary));
            detailView.setTextSize(12);
            final LinearLayout.LayoutParams detailParams = matchWrap();
            detailParams.setMargins(dp(22), dp(1), 0, 0);
            textColumn.addView(detailView, detailParams);
        }

        textColumn.setOnClickListener(view -> {
            if (mBusy) {
                return;
            }
            GpuDriverManager.setSelectedId(this, id);
            rebuildList();
            setStatus(active ? "" : "Active driver: " + name + " — applies next launch.");
        });

        if (deletable) {
            final Button delete = new Button(this);
            delete.setText("Delete");
            delete.setAllCaps(false);
            delete.setOnClickListener(view -> {
                if (mBusy) {
                    return;
                }
                GpuDriverManager.delete(this, id);
                rebuildList();
                setStatus("Deleted " + name + ".");
            });
            row.addView(delete, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT));
        }

        mListContainer.addView(row, matchWrap());
    }

    // ---- install (file / url) --------------------------------------------

    private void addInstallSection(final LinearLayout root) {
        root.addView(header("Add a driver"), matchWrapWithTopMargin(20));

        final Button importFile = new Button(this);
        importFile.setText("Import driver .zip from a file");
        importFile.setAllCaps(false);
        importFile.setOnClickListener(view -> openFilePicker());
        root.addView(importFile, matchWrapWithTopMargin(6));

        final TextView urlLabel = new TextView(this);
        urlLabel.setText("…or paste a direct driver .zip URL:");
        urlLabel.setTextColor(getColor(R.color.xenia_text_secondary));
        urlLabel.setTextSize(12);
        root.addView(urlLabel, matchWrapWithTopMargin(10));

        mUrlInput = new EditText(this);
        mUrlInput.setHint("https://…/driver.zip");
        mUrlInput.setTextColor(getColor(R.color.xenia_text));
        mUrlInput.setHintTextColor(getColor(R.color.xenia_text_secondary));
        mUrlInput.setTextSize(13);
        mUrlInput.setSingleLine(true);
        root.addView(mUrlInput, matchWrapWithTopMargin(2));

        final Button download = new Button(this);
        download.setText("Download & install");
        download.setAllCaps(false);
        download.setOnClickListener(view -> {
            final String url = mUrlInput.getText() != null
                    ? mUrlInput.getText().toString().trim() : "";
            if (url.isEmpty()) {
                setStatus("Enter a driver download URL first.");
                return;
            }
            installAsync("Downloading driver…", () -> GpuDriverManager.installFromUrl(this, url));
        });
        root.addView(download, matchWrapWithTopMargin(4));
    }

    private void addRecommendedSection(final LinearLayout root) {
        root.addView(
                header("Recommended for Adreno 740"), matchWrapWithTopMargin(20));

        for (final GpuDriverManager.Recommended rec : GpuDriverManager.RECOMMENDED) {
            final TextView title = new TextView(this);
            title.setText(rec.recommended ? rec.title + "  ★" : rec.title);
            title.setTextColor(getColor(R.color.xenia_text));
            title.setTextSize(15);
            title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            root.addView(title, matchWrapWithTopMargin(10));

            final TextView summary = new TextView(this);
            summary.setText(rec.summary);
            summary.setTextColor(getColor(R.color.xenia_text_secondary));
            summary.setTextSize(12);
            root.addView(summary, matchWrapWithTopMargin(1));

            final Button download = new Button(this);
            download.setText("Download & install");
            download.setAllCaps(false);
            download.setOnClickListener(view ->
                    installAsync("Downloading " + rec.title + "…",
                            () -> GpuDriverManager.installFromUrl(this, rec.url)));
            root.addView(download, matchWrapWithTopMargin(4));
        }

        final Button browse = new Button(this);
        browse.setText("Browse all builds ↗");
        browse.setAllCaps(false);
        browse.setOnClickListener(view -> openUrl(GpuDriverManager.RELEASES_PAGE_URL));
        root.addView(browse, matchWrapWithTopMargin(14));

        final TextView credit = new TextView(this);
        credit.setText("Recommended builds from the community K11MCH1/AdrenoToolsDrivers "
                + "project. Turnip is the Mesa open-source Vulkan driver for Adreno.");
        credit.setTextColor(getColor(R.color.xenia_text_secondary));
        credit.setTextSize(11);
        root.addView(credit, matchWrapWithTopMargin(6));
    }

    private void openFilePicker() {
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, REQ_IMPORT_FILE);
        } catch (final ActivityNotFoundException e) {
            setStatus("No file picker available on this device.");
        }
    }

    @Override
    protected void onActivityResult(
            final int requestCode, final int resultCode, final Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_IMPORT_FILE || resultCode != RESULT_OK || data == null) {
            return;
        }
        final Uri uri = data.getData();
        if (uri == null) {
            return;
        }
        installAsync("Installing driver…", () -> {
            try (InputStream in = getContentResolver().openInputStream(uri)) {
                if (in == null) {
                    throw new java.io.IOException("Could not open the selected file");
                }
                return GpuDriverManager.installFromZip(this, in);
            }
        });
    }

    /** Run a driver-install task off the UI thread, then refresh + report. */
    private void installAsync(
            final String workingMessage, final Callable<GpuDriverPackage> task) {
        if (mBusy) {
            return;
        }
        mBusy = true;
        setStatus(workingMessage);
        new Thread(() -> {
            String message;
            boolean ok = false;
            try {
                final GpuDriverPackage pkg = task.call();
                // Auto-select a freshly installed driver so it takes effect next launch.
                GpuDriverManager.setSelectedId(this, pkg.id);
                message = "Installed " + pkg.name + " — selected, applies next launch.";
                ok = true;
            } catch (final IllegalArgumentException e) {
                message = e.getMessage() != null ? e.getMessage() : "Invalid driver package";
            } catch (final Exception e) {
                message = e.getMessage() != null ? e.getMessage() : "Install failed";
            }
            final String finalMessage = message;
            final boolean finalOk = ok;
            runOnUiThread(() -> {
                mBusy = false;
                setStatus(finalMessage);
                rebuildList();
                Toast.makeText(
                        this,
                        finalOk ? "Driver installed" : finalMessage,
                        finalOk ? Toast.LENGTH_SHORT : Toast.LENGTH_LONG).show();
            });
        }, "gpu-driver-install").start();
    }

    private void openUrl(final String url) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (final ActivityNotFoundException e) {
            setStatus("No browser available; URL: " + url);
        }
    }

    private void setStatus(final String message) {
        if (mStatus == null) {
            return;
        }
        if (message == null || message.isEmpty()) {
            mStatus.setVisibility(View.GONE);
            mStatus.setText("");
        } else {
            mStatus.setVisibility(View.VISIBLE);
            mStatus.setText(message);
        }
    }

    private TextView header(final String text) {
        final TextView title = new TextView(this);
        title.setText(text);
        title.setTextColor(getColor(R.color.xenia_green_soft));
        title.setTextSize(13);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        return title;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams matchWrapWithTopMargin(final int topMarginDp) {
        final LinearLayout.LayoutParams params = matchWrap();
        params.setMargins(0, dp(topMarginDp), 0, 0);
        return params;
    }

    private int dp(final int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
