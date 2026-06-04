package jp.xenia.emulator;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import java.util.List;
import java.util.Locale;

/**
 * Per-game patch manager: download the community {@code .patch.toml} for this
 * title from xenia-canary/game-patches, then flip individual patches on/off with
 * a switch. Mirrors the programmatic-UI idiom of {@link GpuDriverManagerActivity}.
 *
 * <p>Patches come in two flavours and both are surfaced the same way: performance
 * fixes (60 FPS, disable blur/SSAO/LoD) and cheats (infinite lives/money/ammo).
 * Toggles are written straight to {@code files/patches/} and read by the native
 * patcher ({@code src/xenia/patcher/}) the next time the game launches.
 */
public final class GamePatchManagerActivity extends Activity {
    public static final String EXTRA_TITLE_ID = "jp.xenia.emulator.PATCH_TITLE_ID";
    public static final String EXTRA_TITLE_NAME = "jp.xenia.emulator.PATCH_TITLE_NAME";

    private String mTitleId = "";
    private String mTitleName = "";
    private LinearLayout mListContainer;
    private TextView mStatus;
    private boolean mBusy;

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        XeniaAndroidSettings.ensureInitialized(this);

        final Intent intent = getIntent();
        if (intent != null) {
            mTitleId = safe(intent.getStringExtra(EXTRA_TITLE_ID)).toUpperCase(Locale.US);
            mTitleName = safe(intent.getStringExtra(EXTRA_TITLE_NAME));
        }

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

        addActions(root);

        root.addView(header("Patches"), matchWrapWithTopMargin(18));
        final TextView blurb = new TextView(this);
        blurb.setText("Turn individual patches on or off. Changes apply the next time you "
                + "launch this game. Performance patches (60 FPS, disable blur/SSAO) and "
                + "cheats (infinite lives/money) appear here together.");
        blurb.setTextColor(getColor(R.color.xenia_text_secondary));
        blurb.setTextSize(12);
        root.addView(blurb, matchWrapWithTopMargin(2));

        mListContainer = new LinearLayout(this);
        mListContainer.setOrientation(LinearLayout.VERTICAL);
        root.addView(mListContainer, matchWrapWithTopMargin(6));

        addFooter(root);

        setContentView(scrollView);
        rebuildList();
    }

    private void addTitle(final LinearLayout root) {
        final TextView title = new TextView(this);
        title.setText("Game patches");
        title.setTextColor(getColor(R.color.xenia_text));
        title.setTextSize(24);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        root.addView(title, matchWrap());

        final TextView subtitle = new TextView(this);
        final String name = mTitleName.isEmpty() ? "this game" : mTitleName;
        subtitle.setText(name + (mTitleId.isEmpty() ? "" : "   ·   " + mTitleId));
        subtitle.setTextColor(getColor(R.color.xenia_text_secondary));
        subtitle.setTextSize(13);
        final LinearLayout.LayoutParams params = matchWrap();
        params.setMargins(0, dp(4), 0, dp(6));
        root.addView(subtitle, params);
    }

    private void addActions(final LinearLayout root) {
        final Button download = new Button(this);
        download.setText("Download patches for this game");
        download.setAllCaps(false);
        download.setOnClickListener(view -> downloadAsync());
        root.addView(download, matchWrapWithTopMargin(12));

        final Button browse = new Button(this);
        browse.setText("Browse the patch repository ↗");
        browse.setAllCaps(false);
        browse.setOnClickListener(view -> openUrl(GamePatchManager.REPO_HUMAN_URL));
        root.addView(browse, matchWrapWithTopMargin(4));
    }

    private void addFooter(final LinearLayout root) {
        final TextView credit = new TextView(this);
        credit.setText("Patches from the community xenia-canary/game-patches project. You can "
                + "also hand-author patches (e.g. a dirty-disc bypass) — see the Ghidra "
                + "game-patch workflow in the repo. Files live in the app's patches/ folder.");
        credit.setTextColor(getColor(R.color.xenia_text_secondary));
        credit.setTextSize(11);
        root.addView(credit, matchWrapWithTopMargin(20));
    }

    // ---- list -------------------------------------------------------------

    private void rebuildList() {
        if (mListContainer == null) {
            return;
        }
        mListContainer.removeAllViews();

        final List<GamePatchManager.PatchFile> files =
                GamePatchManager.listForTitle(this, mTitleId);
        int patchCount = 0;
        for (final GamePatchManager.PatchFile file : files) {
            patchCount += file.patches.size();
        }

        if (files.isEmpty() || patchCount == 0) {
            final TextView none = new TextView(this);
            none.setText(files.isEmpty()
                    ? "No patches downloaded yet — tap “Download patches for this "
                            + "game” above to fetch them from the community repo."
                    : "The downloaded patch file has no toggleable patches.");
            none.setTextColor(getColor(R.color.xenia_text_secondary));
            none.setTextSize(12);
            mListContainer.addView(none, matchWrapWithTopMargin(8));
            return;
        }

        for (final GamePatchManager.PatchFile file : files) {
            if (files.size() > 1) {
                final TextView fileHeader = new TextView(this);
                final String label = file.titleName.isEmpty()
                        ? file.file.getName() : file.titleName;
                fileHeader.setText(label);
                fileHeader.setTextColor(getColor(R.color.xenia_green_soft));
                fileHeader.setTextSize(12);
                fileHeader.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
                mListContainer.addView(fileHeader, matchWrapWithTopMargin(12));
            }
            for (int i = 0; i < file.patches.size(); ++i) {
                addPatchRow(file, i);
            }
        }
    }

    private void addPatchRow(final GamePatchManager.PatchFile file, final int index) {
        final GamePatchManager.Patch patch = file.patches.get(index);

        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, dp(8), 0, dp(8));

        final LinearLayout textColumn = new LinearLayout(this);
        textColumn.setOrientation(LinearLayout.VERTICAL);
        row.addView(textColumn, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        final TextView nameView = new TextView(this);
        nameView.setText(patch.name.isEmpty() ? "(unnamed patch)" : patch.name);
        nameView.setTextColor(getColor(R.color.xenia_text));
        nameView.setTextSize(15);
        nameView.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        textColumn.addView(nameView, matchWrap());

        final StringBuilder detail = new StringBuilder();
        if (!patch.description.isEmpty()) {
            detail.append(patch.description);
        }
        if (!patch.author.isEmpty()) {
            detail.append(detail.length() > 0 ? "\n" : "").append("by ").append(patch.author);
        }
        if (detail.length() > 0) {
            final TextView detailView = new TextView(this);
            detailView.setText(detail.toString());
            detailView.setTextColor(getColor(R.color.xenia_text_secondary));
            detailView.setTextSize(12);
            textColumn.addView(detailView, matchWrapWithTopMargin(1));
        }

        final Switch toggle = new Switch(this);
        toggle.setChecked(patch.enabled);
        toggle.setOnClickListener(view -> {
            final boolean wanted = toggle.isChecked();
            if (mBusy) {
                toggle.setChecked(patch.enabled);
                return;
            }
            setEnabledAsync(file, index, wanted, toggle);
        });
        row.addView(toggle, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        mListContainer.addView(row, matchWrap());
    }

    // ---- async work -------------------------------------------------------

    private void downloadAsync() {
        if (mBusy) {
            return;
        }
        if (mTitleId.isEmpty()) {
            setStatus("This game has no title ID, so patches can't be matched.");
            return;
        }
        mBusy = true;
        setStatus("Downloading patches for " + mTitleId + "…");
        new Thread(() -> {
            String message;
            boolean ok = false;
            try {
                final int count = GamePatchManager.downloadForTitle(this, mTitleId);
                message = "Downloaded " + count + " patch file"
                        + (count == 1 ? "" : "s") + " — toggle the ones you want below.";
                ok = true;
            } catch (final Exception e) {
                message = e.getMessage() != null ? e.getMessage() : "Download failed.";
            }
            final String finalMessage = message;
            final boolean finalOk = ok;
            runOnUiThread(() -> {
                mBusy = false;
                setStatus(finalMessage);
                rebuildList();
                Toast.makeText(this, finalMessage,
                        finalOk ? Toast.LENGTH_SHORT : Toast.LENGTH_LONG).show();
            });
        }, "patch-download").start();
    }

    private void setEnabledAsync(final GamePatchManager.PatchFile file, final int index,
            final boolean enabled, final Switch toggle) {
        mBusy = true;
        new Thread(() -> {
            String message = null;
            boolean ok = false;
            try {
                GamePatchManager.setEnabled(file, index, enabled);
                ok = true;
            } catch (final Exception e) {
                message = e.getMessage() != null ? e.getMessage() : "Could not save the change.";
            }
            final boolean finalOk = ok;
            final String finalMessage = message;
            runOnUiThread(() -> {
                mBusy = false;
                if (finalOk) {
                    setStatus((enabled ? "Enabled" : "Disabled") + " “"
                            + file.patches.get(index).name + "” — applies next launch.");
                } else {
                    toggle.setChecked(!enabled);
                    setStatus(finalMessage);
                }
                rebuildList();
            });
        }, "patch-toggle").start();
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

    private static String safe(final String value) {
        return value != null ? value.trim() : "";
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
