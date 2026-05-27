package jp.xenia.emulator;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.HashMap;
import java.util.Map;

public class ControllerMappingActivity extends Activity {
    private final Map<String, TextView> mBindingRows = new HashMap<>();
    private LinearLayout mBindingList;
    private ControllerPreviewView mPreviewView;
    private TextView mStatus;
    private TextView mLastInput;
    private TextView mSelectedAction;
    private String mListeningActionId;

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        XeniaAndroidSettings.ensureInitialized(this);

        final LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(24), dp(16), dp(24), dp(16));
        root.setBackgroundColor(getColor(R.color.xenia_surface));
        root.setFocusable(true);
        root.setFocusableInTouchMode(true);

        addHeader(root);
        addMainContent(root);
        addFooter(root);

        setContentView(root);
        root.requestFocus();
        refreshMappings();
    }

    @Override
    public boolean dispatchKeyEvent(final KeyEvent event) {
        if (event == null || event.getAction() != KeyEvent.ACTION_DOWN
                || event.getRepeatCount() != 0
                || !XeniaInputMapping.isBindableKeyCode(event.getKeyCode())) {
            return super.dispatchKeyEvent(event);
        }

        final String keyName = XeniaInputMapping.keyName(event.getKeyCode());
        mLastInput.setText("Last input: " + keyName);
        if (mListeningActionId != null) {
            final XeniaInputMapping.ButtonAction action =
                    XeniaInputMapping.findAction(mListeningActionId);
            XeniaInputMapping.setPhysicalKeyForAction(
                    this, mListeningActionId, event.getKeyCode());
            mListeningActionId = null;
            mStatus.setText(action != null
                    ? action.label + " mapped to " + keyName
                    : "Mapped to " + keyName);
            refreshMappings();
        }
        return true;
    }

    private void addHeader(final LinearLayout root) {
        final LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);

        final LinearLayout titleColumn = new LinearLayout(this);
        titleColumn.setOrientation(LinearLayout.VERTICAL);
        final TextView title = new TextView(this);
        title.setText(R.string.activity_label_controller_mapping);
        title.setTextColor(getColor(R.color.xenia_text));
        title.setTextSize(24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        titleColumn.addView(title, matchWrap());

        final TextView subtitle = new TextView(this);
        subtitle.setText(R.string.controller_mapping_subtitle);
        subtitle.setTextColor(getColor(R.color.xenia_text_secondary));
        subtitle.setTextSize(13);
        titleColumn.addView(subtitle, matchWrap());
        header.addView(titleColumn, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f));

        mStatus = new TextView(this);
        mStatus.setText(R.string.controller_mapping_ready);
        mStatus.setTextColor(getColor(R.color.xenia_green_soft));
        mStatus.setTextSize(14);
        mStatus.setTypeface(Typeface.DEFAULT_BOLD);
        mStatus.setGravity(Gravity.END);
        header.addView(mStatus, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f));

        root.addView(header, matchWrap());
    }

    private void addMainContent(final LinearLayout root) {
        final LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.HORIZONTAL);
        content.setGravity(Gravity.CENTER_VERTICAL);

        final LinearLayout previewPane = pane();
        addPreviewPane(previewPane);
        content.addView(previewPane, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1.15f));

        final LinearLayout listPane = pane();
        addListPane(listPane);
        final LinearLayout.LayoutParams listParams = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 0.85f);
        listParams.setMargins(dp(16), 0, 0, 0);
        content.addView(listPane, listParams);

        final LinearLayout.LayoutParams contentParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f);
        contentParams.setMargins(0, dp(14), 0, dp(12));
        root.addView(content, contentParams);
    }

    private void addPreviewPane(final LinearLayout pane) {
        pane.addView(sectionLabel("Tap a control"), matchWrap());

        mPreviewView = new ControllerPreviewView();
        mPreviewView.setOnActionSelected(actionId -> beginListening(actionId));
        final LinearLayout.LayoutParams previewParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f);
        previewParams.setMargins(0, dp(8), 0, 0);
        pane.addView(mPreviewView, previewParams);
    }

    private void addListPane(final LinearLayout pane) {
        pane.addView(sectionLabel("Selected"), matchWrap());
        mSelectedAction = valueText("None");
        pane.addView(mSelectedAction, matchWrapWithTopMargin(4));

        mLastInput = valueText(getString(R.string.controller_mapping_last_input_empty));
        pane.addView(mLastInput, matchWrapWithTopMargin(8));

        final TextView axes = valueText("Analog: LS X/Y   RS RX/RY   LT/RT analog   Hat X/Y");
        axes.setTextSize(12);
        pane.addView(axes, matchWrapWithTopMargin(2));

        pane.addView(sectionLabel("Button bindings"), matchWrapWithTopMargin(12));
        final ScrollView scrollView = new ScrollView(this);
        mBindingList = new LinearLayout(this);
        mBindingList.setOrientation(LinearLayout.VERTICAL);
        scrollView.addView(mBindingList);
        pane.addView(scrollView, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f));
    }

    private void addFooter(final LinearLayout root) {
        final LinearLayout footer = new LinearLayout(this);
        footer.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        footer.setOrientation(LinearLayout.HORIZONTAL);

        final Button reset = footerButton(R.string.controller_mapping_reset);
        reset.setOnClickListener(view -> {
            XeniaInputMapping.resetToDefaults(this);
            mListeningActionId = null;
            mStatus.setText(R.string.controller_mapping_defaults_restored);
            refreshMappings();
        });
        footer.addView(reset, wrapWithMargins(4));

        final Button done = footerButton(R.string.controller_mapping_done);
        done.setOnClickListener(view -> finish());
        footer.addView(done, wrapWithMargins(4));

        root.addView(footer, matchWrap());
    }

    private Button footerButton(final int textResId) {
        final Button button = new Button(this);
        button.setText(textResId);
        button.setAllCaps(false);
        button.setMinWidth(dp(120));
        return button;
    }

    private void beginListening(final String actionId) {
        mListeningActionId = actionId;
        final XeniaInputMapping.ButtonAction action = XeniaInputMapping.findAction(actionId);
        if (action != null) {
            mSelectedAction.setText(action.label + " -> " + action.xboxControl);
            mStatus.setText("Press a Thor button");
        }
        refreshMappings();
    }

    private void refreshMappings() {
        if (mPreviewView != null) {
            mPreviewView.invalidate();
        }

        mBindingList.removeAllViews();
        mBindingRows.clear();
        for (final XeniaInputMapping.ButtonAction action : XeniaInputMapping.getButtonActions()) {
            final TextView row = bindingRow(action);
            mBindingRows.put(action.id, row);
            mBindingList.addView(row, matchWrapWithTopMargin(4));
        }
    }

    private TextView bindingRow(final XeniaInputMapping.ButtonAction action) {
        final int keyCode = XeniaInputMapping.getPhysicalKeyCode(this, action);
        final boolean selected = action.id.equals(mListeningActionId);
        final TextView row = new TextView(this);
        row.setText(action.label + "   ->   " + XeniaInputMapping.keyName(keyCode));
        row.setTextColor(getColor(R.color.xenia_text));
        row.setTextSize(13);
        row.setTypeface(Typeface.DEFAULT_BOLD);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), 0, dp(14), 0);
        row.setMinHeight(dp(36));
        row.setBackground(makeBackground(selected, false));
        row.setOnClickListener(view -> beginListening(action.id));
        return row;
    }

    private LinearLayout pane() {
        final LinearLayout pane = new LinearLayout(this);
        pane.setOrientation(LinearLayout.VERTICAL);
        pane.setPadding(dp(16), dp(14), dp(16), dp(14));
        pane.setBackground(makePaneBackground());
        return pane;
    }

    private TextView sectionLabel(final String text) {
        final TextView label = new TextView(this);
        label.setText(text);
        label.setTextColor(getColor(R.color.xenia_green_soft));
        label.setTextSize(13);
        label.setTypeface(Typeface.DEFAULT_BOLD);
        return label;
    }

    private TextView valueText(final String text) {
        final TextView value = new TextView(this);
        value.setText(text);
        value.setTextColor(getColor(R.color.xenia_text_secondary));
        value.setTextSize(14);
        value.setTypeface(Typeface.DEFAULT_BOLD);
        value.setMinHeight(dp(32));
        value.setGravity(Gravity.CENTER_VERTICAL);
        return value;
    }

    private void addInfoRow(final LinearLayout root, final String left, final String right) {
        final TextView row = new TextView(this);
        row.setText(left + "   ->   " + right);
        row.setTextColor(getColor(R.color.xenia_text_secondary));
        row.setTextSize(12);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setMinHeight(dp(26));
        root.addView(row, matchWrap());
    }

    private GradientDrawable makePaneBackground() {
        final GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(getColor(R.color.xenia_surface_high));
        drawable.setStroke(dp(1), getColor(R.color.xenia_card_primary));
        drawable.setCornerRadius(dp(8));
        return drawable;
    }

    private GradientDrawable makeBackground(final boolean selected, final boolean preview) {
        final GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(getColor(selected ? R.color.xenia_green_dim : R.color.xenia_card));
        drawable.setStroke(dp(selected ? 2 : 1), getColor(
                selected ? R.color.xenia_green_soft : R.color.xenia_green));
        drawable.setCornerRadius(dp(preview ? 10 : 6));
        return drawable;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams matchWrapWithTopMargin(final int topMarginDp) {
        final LinearLayout.LayoutParams params = matchWrap();
        params.setMargins(0, dp(topMarginDp), 0, 0);
        return params;
    }

    private LinearLayout.LayoutParams wrapWithMargins(final int marginDp) {
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.setMargins(dp(marginDp), dp(marginDp), dp(marginDp), dp(marginDp));
        return params;
    }

    private int dp(final int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private String compactKeyName(final int keyCode) {
        String name = XeniaInputMapping.keyName(keyCode);
        if ("DPAD UP".equals(name)) {
            return "Up";
        }
        if ("DPAD DOWN".equals(name)) {
            return "Down";
        }
        if ("DPAD LEFT".equals(name)) {
            return "Left";
        }
        if ("DPAD RIGHT".equals(name)) {
            return "Right";
        }
        if ("THUMBL".equals(name)) {
            return "L3";
        }
        if ("THUMBR".equals(name)) {
            return "R3";
        }
        if ("MODE".equals(name)) {
            return "Guide";
        }
        if ("SELECT".equals(name)) {
            return "Back";
        }
        return name;
    }

    private String compactActionLabel(final String actionId, final String fallback) {
        if ("dpad_up".equals(actionId)) {
            return "U";
        }
        if ("dpad_down".equals(actionId)) {
            return "D";
        }
        if ("dpad_left".equals(actionId)) {
            return "L";
        }
        if ("dpad_right".equals(actionId)) {
            return "R";
        }
        if ("back".equals(actionId)) {
            return "Back";
        }
        if ("guide".equals(actionId)) {
            return "Guide";
        }
        if ("start".equals(actionId)) {
            return "Start";
        }
        return fallback;
    }

    private interface OnActionSelectedListener {
        void onActionSelected(String actionId);
    }

    private final class ControllerPreviewView extends View {
        private final Map<String, RectF> mHitRects = new HashMap<>();
        private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mText = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mSmallText = new Paint(Paint.ANTI_ALIAS_FLAG);
        private OnActionSelectedListener mListener;

        ControllerPreviewView() {
            super(ControllerMappingActivity.this);
            setFocusable(true);
            setClickable(true);
            mStroke.setStyle(Paint.Style.STROKE);
            mStroke.setStrokeWidth(dp(2));
            mText.setTextAlign(Paint.Align.CENTER);
            mText.setTypeface(Typeface.DEFAULT_BOLD);
            mText.setTextSize(dp(8));
            mSmallText.setTextAlign(Paint.Align.CENTER);
            mSmallText.setTypeface(Typeface.DEFAULT_BOLD);
            mSmallText.setTextSize(dp(5));
        }

        void setOnActionSelected(final OnActionSelectedListener listener) {
            mListener = listener;
        }

        @Override
        protected void onDraw(final Canvas canvas) {
            super.onDraw(canvas);
            mHitRects.clear();

            final float width = getWidth();
            final float height = getHeight();
            if (width <= 0 || height <= 0) {
                return;
            }

            final int card = getColor(R.color.xenia_card);
            final int cardPrimary = getColor(R.color.xenia_card_primary);
            final int textSecondary = getColor(R.color.xenia_text_secondary);

            mFill.setStyle(Paint.Style.FILL);
            mFill.setColor(card);
            final RectF body = rect(width, height, 0.08f, 0.18f, 0.92f, 0.88f);
            canvas.drawRoundRect(body, dp(32), dp(32), mFill);
            final RectF leftGrip = rect(width, height, 0.05f, 0.43f, 0.24f, 0.96f);
            final RectF rightGrip = rect(width, height, 0.76f, 0.43f, 0.95f, 0.96f);
            canvas.drawRoundRect(leftGrip, dp(42), dp(42), mFill);
            canvas.drawRoundRect(rightGrip, dp(42), dp(42), mFill);

            mStroke.setColor(cardPrimary);
            canvas.drawRoundRect(body, dp(32), dp(32), mStroke);
            canvas.drawRoundRect(leftGrip, dp(42), dp(42), mStroke);
            canvas.drawRoundRect(rightGrip, dp(42), dp(42), mStroke);

            drawPill(canvas, "lt", rect(width, height, 0.08f, 0.02f, 0.25f, 0.14f));
            drawPill(canvas, "lb", rect(width, height, 0.29f, 0.02f, 0.46f, 0.14f));
            drawPill(canvas, "rb", rect(width, height, 0.54f, 0.02f, 0.71f, 0.14f));
            drawPill(canvas, "rt", rect(width, height, 0.75f, 0.02f, 0.92f, 0.14f));

            drawCircleAction(canvas, "ls", width * 0.23f, height * 0.38f, min(width, height) * 0.095f);
            drawDpad(canvas, width * 0.24f, height * 0.67f, min(width, height) * 0.074f);

            drawCircleAction(canvas, "back", width * 0.39f, height * 0.46f, min(width, height) * 0.047f);
            drawCircleAction(canvas, "guide", width * 0.50f, height * 0.46f, min(width, height) * 0.055f);
            drawCircleAction(canvas, "start", width * 0.61f, height * 0.46f, min(width, height) * 0.047f);
            drawCircleAction(canvas, "rs", width * 0.58f, height * 0.72f, min(width, height) * 0.090f);

            final float faceRadius = min(width, height) * 0.052f;
            drawCircleAction(canvas, "y", width * 0.79f, height * 0.36f, faceRadius);
            drawCircleAction(canvas, "x", width * 0.72f, height * 0.50f, faceRadius);
            drawCircleAction(canvas, "b", width * 0.86f, height * 0.50f, faceRadius);
            drawCircleAction(canvas, "a", width * 0.79f, height * 0.64f, faceRadius);

            mSmallText.setColor(textSecondary);
            canvas.drawText("Fixed axes are shown in the bindings pane.",
                    width * 0.50f, height * 0.985f, mSmallText);
        }

        @Override
        public boolean onTouchEvent(final MotionEvent event) {
            if (event.getAction() != MotionEvent.ACTION_UP) {
                return true;
            }
            for (final Map.Entry<String, RectF> entry : mHitRects.entrySet()) {
                if (entry.getValue().contains(event.getX(), event.getY())) {
                    if (mListener != null) {
                        mListener.onActionSelected(entry.getKey());
                    }
                    return true;
                }
            }
            return true;
        }

        private void drawPill(final Canvas canvas, final String actionId, final RectF rect) {
            drawActionBackground(canvas, actionId, rect, dp(12));
            drawActionText(canvas, actionId, rect.centerX(), rect.centerY() - dp(2));
            mHitRects.put(actionId, new RectF(rect));
        }

        private void drawCircleAction(
                final Canvas canvas, final String actionId, final float cx, final float cy,
                final float radius) {
            final RectF rect = new RectF(cx - radius, cy - radius, cx + radius, cy + radius);
            drawActionBackground(canvas, actionId, rect, radius);
            drawActionText(canvas, actionId, cx, cy - dp(3));
            mHitRects.put(actionId, new RectF(rect));
        }

        private void drawDpad(
                final Canvas canvas, final float cx, final float cy, final float unit) {
            drawPill(canvas, "dpad_up",
                    new RectF(cx - unit * 0.55f, cy - unit * 2.15f,
                            cx + unit * 0.55f, cy - unit * 0.70f));
            drawPill(canvas, "dpad_down",
                    new RectF(cx - unit * 0.55f, cy + unit * 0.70f,
                            cx + unit * 0.55f, cy + unit * 2.15f));
            drawPill(canvas, "dpad_left",
                    new RectF(cx - unit * 2.15f, cy - unit * 0.55f,
                            cx - unit * 0.70f, cy + unit * 0.55f));
            drawPill(canvas, "dpad_right",
                    new RectF(cx + unit * 0.70f, cy - unit * 0.55f,
                            cx + unit * 2.15f, cy + unit * 0.55f));
        }

        private void drawActionBackground(
                final Canvas canvas, final String actionId, final RectF rect, final float radius) {
            final boolean selected = actionId.equals(mListeningActionId);
            mFill.setStyle(Paint.Style.FILL);
            mFill.setColor(getColor(selected ? R.color.xenia_green_dim : R.color.xenia_surface));
            canvas.drawRoundRect(rect, radius, radius, mFill);
            mStroke.setColor(getColor(selected ? R.color.xenia_green_soft : R.color.xenia_green));
            mStroke.setStrokeWidth(dp(selected ? 3 : 2));
            canvas.drawRoundRect(rect, radius, radius, mStroke);
        }

        private void drawActionText(
                final Canvas canvas, final String actionId, final float cx, final float cy) {
            final XeniaInputMapping.ButtonAction action = XeniaInputMapping.findAction(actionId);
            if (action == null) {
                return;
            }
            final int keyCode = XeniaInputMapping.getPhysicalKeyCode(
                    ControllerMappingActivity.this, action);
            mText.setColor(getColor(R.color.xenia_text));
            mSmallText.setColor(getColor(R.color.xenia_text_secondary));
            canvas.drawText(compactActionLabel(actionId, action.label), cx, cy, mText);
            canvas.drawText(compactKeyName(keyCode), cx, cy + dp(10), mSmallText);
        }

        private RectF rect(
                final float width, final float height, final float left, final float top,
                final float right, final float bottom) {
            return new RectF(width * left, height * top, width * right, height * bottom);
        }

        private float min(final float a, final float b) {
            return a < b ? a : b;
        }
    }
}
