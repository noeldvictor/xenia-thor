package jp.xenia.emulator;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.SharedPreferences;
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
 * Per-game trainer manager: download the community Aurora {@code .xex} trainers
 * for this title, flip the trainer feature on/off for the game, and delete
 * trainers you don't want. Mirrors {@link GamePatchManagerActivity}.
 *
 * <p>Trainers are downloaded into {@code files/trainers/} by {@link TrainerManager}
 * and loaded at launch by the native {@code Emulator::LoadTrainersForTitle}. The
 * master on/off is the {@link XeniaOptimizations#PREF_KEY_TRAINERS} toggle (it
 * sets the {@code trainer_enable} + {@code trainer_run_entry} cvars); this screen
 * exposes it as a per-game override so you can arm trainers for one title only.
 */
public final class TrainerManagerActivity extends Activity {
    public static final String EXTRA_TITLE_ID = "jp.xenia.emulator.TRAINER_TITLE_ID";
    public static final String EXTRA_TITLE_NAME = "jp.xenia.emulator.TRAINER_TITLE_NAME";

    private String mTitleId = "";
    private String mTitleName = "";
    private SharedPreferences mPrefs;
    private LinearLayout mListContainer;
    private TextView mStatus;
    private boolean mBusy;

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        XeniaAndroidSettings.ensureInitialized(this);
        mPrefs = XeniaAndroidSettings.getPreferences(this);

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

        addMasterToggle(root);
        addActions(root);

        root.addView(header("Downloaded trainers"), matchWrapWithTopMargin(18));
        final TextView blurb = new TextView(this);
        blurb.setText("Trainers are small Xbox 360 programs that hook the running game "
                + "to apply cheats (infinite health/ammo/money) and show an in-game "
                + "menu, usually opened with START+BACK. They are build-specific — a "
                + "trainer must match this game's title update, or the game may "
                + "misbehave. Enable the toggle above once you've downloaded one.");
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
        title.setText("Trainers");
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

    private void addMasterToggle(final LinearLayout root) {
        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, dp(10), 0, dp(4));

        final LinearLayout textColumn = new LinearLayout(this);
        textColumn.setOrientation(LinearLayout.VERTICAL);
        row.addView(textColumn, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        final TextView label = new TextView(this);
        label.setText("Enable trainers for this game");
        label.setTextColor(getColor(R.color.xenia_text));
        label.setTextSize(15);
        label.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        textColumn.addView(label, matchWrap());

        final TextView note = new TextView(this);
        note.setText("Loads and runs the trainers below when this game launches.");
        note.setTextColor(getColor(R.color.xenia_text_secondary));
        note.setTextSize(12);
        textColumn.addView(note, matchWrapWithTopMargin(1));

        final Switch toggle = new Switch(this);
        toggle.setEnabled(!mTitleId.isEmpty());
        toggle.setChecked(isTrainersEnabledForGame());
        toggle.setOnClickListener(view -> {
            if (mTitleId.isEmpty()) {
                return;
            }
            XeniaOptimizations.setOverride(mPrefs, mTitleId,
                    XeniaOptimizations.PREF_KEY_TRAINERS,
                    toggle.isChecked() ? XeniaOptimizations.OVERRIDE_ON
                            : XeniaOptimizations.OVERRIDE_OFF);
            setStatus(toggle.isChecked()
                    ? "Trainers armed for this game — they load next launch."
                    : "Trainers disabled for this game.");
        });
        row.addView(toggle, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        root.addView(row, matchWrapWithTopMargin(12));
    }

    private boolean isTrainersEnabledForGame() {
        return XeniaOptimizations.getOverride(mPrefs, mTitleId,
                XeniaOptimizations.PREF_KEY_TRAINERS) == XeniaOptimizations.OVERRIDE_ON;
    }

    private void addActions(final LinearLayout root) {
        final Button download = new Button(this);
        download.setText("Download trainers for this game");
        download.setAllCaps(false);
        download.setOnClickListener(view -> downloadAsync());
        root.addView(download, matchWrapWithTopMargin(12));

        final Button browse = new Button(this);
        browse.setText("Browse the trainer repository ↗");
        browse.setAllCaps(false);
        browse.setOnClickListener(view -> openUrl(TrainerManager.REPO_HUMAN_URL));
        root.addView(browse, matchWrapWithTopMargin(4));
    }

    private void addFooter(final LinearLayout root) {
        final TextView credit = new TextView(this);
        credit.setText("Trainers from the community Aurora JTAG/RGH trainer packs. Files "
                + "live in the app's trainers/ folder, matched to a game by title id. "
                + "Trainers run third-party guest code — only load ones you trust.");
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

        final List<TrainerManager.Trainer> trainers =
                TrainerManager.listForTitle(this, mTitleId);

        if (trainers.isEmpty()) {
            final TextView none = new TextView(this);
            none.setText("No trainers downloaded yet — tap “Download trainers for this "
                    + "game” above to fetch them from the community repo.");
            none.setTextColor(getColor(R.color.xenia_text_secondary));
            none.setTextSize(12);
            mListContainer.addView(none, matchWrapWithTopMargin(8));
            return;
        }

        for (final TrainerManager.Trainer trainer : trainers) {
            addTrainerRow(trainer);
        }
    }

    private void addTrainerRow(final TrainerManager.Trainer trainer) {
        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, dp(8), 0, dp(8));

        final TextView nameView = new TextView(this);
        nameView.setText(trainer.displayName);
        nameView.setTextColor(getColor(R.color.xenia_text));
        nameView.setTextSize(15);
        row.addView(nameView, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        final Button delete = new Button(this);
        delete.setText("Delete");
        delete.setAllCaps(false);
        delete.setOnClickListener(view -> {
            if (mBusy) {
                return;
            }
            TrainerManager.delete(trainer);
            setStatus("Deleted “" + trainer.displayName + "”.");
            rebuildList();
        });
        row.addView(delete, new LinearLayout.LayoutParams(
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
            setStatus("This game has no title ID, so trainers can't be matched.");
            return;
        }
        mBusy = true;
        setStatus("Downloading trainers for " + mTitleId + "…");
        new Thread(() -> {
            String message;
            boolean ok = false;
            try {
                final int count = TrainerManager.downloadForTitle(this, mTitleId);
                message = "Downloaded " + count + " trainer"
                        + (count == 1 ? "" : "s")
                        + " — enable the toggle above to use them.";
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
        }, "trainer-download").start();
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
