package jp.xenia.emulator;

import android.app.Activity;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.util.Locale;

/**
 * Per-game performance optimizations: force any {@link XeniaOptimizations}
 * toggle ON or OFF for one specific title, or leave it on DEFAULT (inherit the
 * global Settings toggle). Mirrors the programmatic-UI idiom of
 * {@link GamePatchManagerActivity}.
 *
 * <p>The whole point of the optimization registry is that each win is a
 * stacking, explained toggle; this screen makes that per-game, because the best
 * set differs by title (e.g. the CPU-bound Blue Dragon field wants the guest
 * hot-thread on the prime core, while a GPU-bound scene wants the opposite).
 * Overrides are read at launch by {@link XeniaAndroidSettings} via
 * {@link XeniaOptimizations#applyTo(SharedPreferences, Bundle, String)}.
 */
public final class GameOptimizationsActivity extends Activity {
    public static final String EXTRA_TITLE_ID = "jp.xenia.emulator.OPT_TITLE_ID";
    public static final String EXTRA_TITLE_NAME = "jp.xenia.emulator.OPT_TITLE_NAME";

    private String mTitleId = "";
    private String mTitleName = "";
    private SharedPreferences mPrefs;
    private LinearLayout mListContainer;
    private TextView mSubtitle;

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        XeniaAndroidSettings.ensureInitialized(this);
        mPrefs = XeniaAndroidSettings.getPreferences(this);

        if (getIntent() != null) {
            mTitleId = safe(getIntent().getStringExtra(EXTRA_TITLE_ID))
                    .toUpperCase(Locale.US);
            mTitleName = safe(getIntent().getStringExtra(EXTRA_TITLE_NAME));
        }

        final ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(true);
        scrollView.setBackgroundColor(getColor(R.color.xenia_surface));

        final LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(18), dp(24), dp(18));
        scrollView.addView(root);

        addTitle(root);

        if (mTitleId.isEmpty()) {
            final TextView none = new TextView(this);
            none.setText("This game has no detected title ID, so per-game settings "
                    + "can't be saved for it. Use the global Performance "
                    + "optimizations in Settings instead.");
            none.setTextColor(getColor(R.color.xenia_text_secondary));
            none.setTextSize(13);
            root.addView(none, matchWrapWithTopMargin(16));
            setContentView(scrollView);
            return;
        }

        final TextView blurb = new TextView(this);
        blurb.setText("Set each optimization for THIS game. “Default” follows your "
                + "global setting; “On”/“Off” force it for this title only. Changes "
                + "apply the next time you launch the game. A few titles also have a "
                + "built-in profile that can override these.");
        blurb.setTextColor(getColor(R.color.xenia_text_secondary));
        blurb.setTextSize(12);
        root.addView(blurb, matchWrapWithTopMargin(10));

        final Button reset = new Button(this);
        reset.setText("Reset this game to defaults");
        reset.setAllCaps(false);
        reset.setOnClickListener(v -> {
            XeniaOptimizations.clearOverrides(mPrefs, mTitleId);
            rebuildList();
            updateSubtitle();
            Toast.makeText(this, "Reset " + displayName() + " to global defaults",
                    Toast.LENGTH_SHORT).show();
        });
        root.addView(reset, matchWrapWithTopMargin(12));

        mListContainer = new LinearLayout(this);
        mListContainer.setOrientation(LinearLayout.VERTICAL);
        root.addView(mListContainer, matchWrapWithTopMargin(8));

        setContentView(scrollView);
        rebuildList();
    }

    private void addTitle(final LinearLayout root) {
        final TextView title = new TextView(this);
        title.setText("Performance optimizations");
        title.setTextColor(getColor(R.color.xenia_text));
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        root.addView(title, matchWrap());

        mSubtitle = new TextView(this);
        mSubtitle.setTextColor(getColor(R.color.xenia_text_secondary));
        mSubtitle.setTextSize(13);
        final LinearLayout.LayoutParams params = matchWrap();
        params.setMargins(0, dp(4), 0, dp(2));
        root.addView(mSubtitle, params);
        updateSubtitle();
    }

    private void updateSubtitle() {
        if (mSubtitle == null) {
            return;
        }
        final String idPart = mTitleId.isEmpty() ? "" : "   ·   " + mTitleId;
        final int pinned = XeniaOptimizations.overrideCount(mPrefs, mTitleId);
        final String pinnedPart = pinned == 0
                ? "   ·   all default"
                : "   ·   " + pinned + " customized";
        mSubtitle.setText(displayName() + idPart + pinnedPart);
    }

    // ---- list -------------------------------------------------------------

    private void rebuildList() {
        if (mListContainer == null) {
            return;
        }
        mListContainer.removeAllViews();
        String currentCategory = null;
        for (final XeniaOptimizations.Optimization opt : XeniaOptimizations.ALL) {
            if (!opt.category.equals(currentCategory)) {
                currentCategory = opt.category;
                mListContainer.addView(
                        categoryHeader(currentCategory), matchWrapWithTopMargin(18));
            }
            addOptimizationRow(opt);
        }
    }

    private void addOptimizationRow(final XeniaOptimizations.Optimization opt) {
        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(0, dp(10), 0, dp(10));

        final TextView name = new TextView(this);
        name.setText(opt.title + (opt.recommended ? "   ★" : ""));
        name.setTextColor(getColor(R.color.xenia_text));
        name.setTextSize(15);
        name.setTypeface(Typeface.DEFAULT_BOLD);
        row.addView(name, matchWrap());

        final TextView summary = new TextView(this);
        summary.setText(opt.summary);
        summary.setTextColor(getColor(R.color.xenia_text_secondary));
        summary.setTextSize(12);
        row.addView(summary, matchWrapWithTopMargin(1));

        // Tri-state: Default (inherit global) / On / Off.
        final RadioGroup group = new RadioGroup(this);
        group.setOrientation(RadioGroup.HORIZONTAL);
        final String defaultLabel = "Default ("
                + (opt.isEnabled(mPrefs) ? "On" : "Off") + ")";
        final RadioButton rbDefault = makeRadio(defaultLabel);
        final RadioButton rbOn = makeRadio("On");
        final RadioButton rbOff = makeRadio("Off");
        rbDefault.setId(View.generateViewId());
        rbOn.setId(View.generateViewId());
        rbOff.setId(View.generateViewId());
        group.addView(rbDefault);
        group.addView(rbOn);
        group.addView(rbOff);

        switch (XeniaOptimizations.getOverride(mPrefs, mTitleId, opt.prefKey)) {
            case XeniaOptimizations.OVERRIDE_ON:
                group.check(rbOn.getId());
                break;
            case XeniaOptimizations.OVERRIDE_OFF:
                group.check(rbOff.getId());
                break;
            default:
                group.check(rbDefault.getId());
                break;
        }

        group.setOnCheckedChangeListener((g, checkedId) -> {
            final int state;
            if (checkedId == rbOn.getId()) {
                state = XeniaOptimizations.OVERRIDE_ON;
            } else if (checkedId == rbOff.getId()) {
                state = XeniaOptimizations.OVERRIDE_OFF;
            } else {
                state = XeniaOptimizations.OVERRIDE_DEFAULT;
            }
            XeniaOptimizations.setOverride(mPrefs, mTitleId, opt.prefKey, state);
            updateSubtitle();
        });
        row.addView(group, matchWrapWithTopMargin(4));

        mListContainer.addView(row, matchWrap());
    }

    private RadioButton makeRadio(final String text) {
        final RadioButton rb = new RadioButton(this);
        rb.setText(text);
        rb.setTextColor(getColor(R.color.xenia_text_secondary));
        rb.setTextSize(13);
        final LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        rb.setLayoutParams(lp);
        return rb;
    }

    private TextView categoryHeader(final String text) {
        final TextView header = new TextView(this);
        header.setText(text);
        header.setTextColor(getColor(R.color.xenia_green_soft));
        header.setTextSize(13);
        header.setTypeface(Typeface.DEFAULT_BOLD);
        return header;
    }

    private String displayName() {
        return mTitleName.isEmpty() ? "this game" : mTitleName;
    }

    private static String safe(final String value) {
        return value != null ? value.trim() : "";
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
