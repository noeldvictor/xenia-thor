package jp.xenia.emulator;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

public class SettingsActivity extends Activity {
    private RadioGroup mProfileGroup;
    private RadioGroup mGpuGroup;
    private RadioGroup mAudioGroup;
    private RadioGroup mInputGroup;
    private CheckBox mMuteAudio;
    private CheckBox mHideOsd;
    private CheckBox mShowFps;
    private CheckBox mVulkanCounters;
    private boolean mUpdatingControls;

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
        mProfileGroup = addRadioGroup(root, R.string.settings_profile, new String[][] {
                {getString(R.string.settings_profile_balanced),
                        XeniaAndroidSettings.PROFILE_BALANCED},
                {getString(R.string.settings_profile_performance),
                        XeniaAndroidSettings.PROFILE_PERFORMANCE},
                {getString(R.string.settings_profile_research),
                        XeniaAndroidSettings.PROFILE_RESEARCH},
                {getString(R.string.settings_profile_custom),
                        XeniaAndroidSettings.PROFILE_CUSTOM},
        });
        mGpuGroup = addRadioGroup(root, R.string.settings_gpu, new String[][] {
                {getString(R.string.settings_gpu_vulkan), XeniaAndroidSettings.GPU_VULKAN},
                {getString(R.string.settings_gpu_null), XeniaAndroidSettings.GPU_NULL},
        });
        mAudioGroup = addRadioGroup(root, R.string.settings_audio, new String[][] {
                {getString(R.string.settings_audio_disabled), XeniaAndroidSettings.APU_NOP},
                {getString(R.string.settings_audio_auto), XeniaAndroidSettings.APU_ANY},
        });
        mInputGroup = addRadioGroup(root, R.string.settings_input, new String[][] {
                {getString(R.string.settings_input_android), XeniaAndroidSettings.HID_ANDROID},
                {getString(R.string.settings_input_none), XeniaAndroidSettings.HID_NOP},
        });
        addControllerMappingButton(root);

        addToggles(root);
        addButtons(root);
        setContentView(scrollView);

        loadFromPreferences();
        wireChangeListeners();
    }

    private void addTitle(final LinearLayout root) {
        final TextView title = new TextView(this);
        title.setText(R.string.activity_label_settings);
        title.setTextColor(getColor(R.color.xenia_text));
        title.setTextSize(24);
        title.setGravity(Gravity.START);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        root.addView(title, matchWrap());

        final TextView subtitle = new TextView(this);
        subtitle.setText(R.string.settings_subtitle);
        subtitle.setTextColor(getColor(R.color.xenia_text_secondary));
        subtitle.setTextSize(13);
        final LinearLayout.LayoutParams params = matchWrap();
        params.setMargins(0, dp(4), 0, dp(14));
        root.addView(subtitle, params);
    }

    private RadioGroup addRadioGroup(
            final LinearLayout root, final int titleResId, final String[][] options) {
        final TextView title = sectionTitle(titleResId);
        root.addView(title, matchWrapWithTopMargin(12));

        final RadioGroup group = new RadioGroup(this);
        group.setOrientation(RadioGroup.VERTICAL);
        for (final String[] option : options) {
            final RadioButton button = new RadioButton(this);
            button.setId(View.generateViewId());
            button.setTag(option[1]);
            button.setText(option[0]);
            button.setTextColor(getColor(R.color.xenia_text));
            button.setTextSize(15);
            group.addView(button, matchWrap());
        }
        root.addView(group, matchWrap());
        return group;
    }

    private void addControllerMappingButton(final LinearLayout root) {
        final Button button = new Button(this);
        button.setText(R.string.settings_controller_mapping);
        button.setAllCaps(false);
        button.setOnClickListener(view ->
                startActivity(new Intent(this, ControllerMappingActivity.class)));
        root.addView(button, matchWrapWithTopMargin(8));
    }

    private void addToggles(final LinearLayout root) {
        root.addView(sectionTitle(R.string.settings_runtime), matchWrapWithTopMargin(12));
        mMuteAudio = addCheckBox(root, R.string.settings_mute_audio);
        mHideOsd = addCheckBox(root, R.string.settings_hide_osd);
        mShowFps = addCheckBox(root, R.string.settings_show_fps);
        mVulkanCounters = addCheckBox(root, R.string.settings_vulkan_counters);
    }

    private CheckBox addCheckBox(final LinearLayout root, final int textResId) {
        final CheckBox checkBox = new CheckBox(this);
        checkBox.setText(textResId);
        checkBox.setTextColor(getColor(R.color.xenia_text));
        checkBox.setTextSize(15);
        root.addView(checkBox, matchWrap());
        return checkBox;
    }

    private void addButtons(final LinearLayout root) {
        final LinearLayout buttons = new LinearLayout(this);
        buttons.setGravity(Gravity.END);
        buttons.setOrientation(LinearLayout.HORIZONTAL);

        final Button reset = new Button(this);
        reset.setText(R.string.settings_reset);
        reset.setOnClickListener(view -> {
            XeniaAndroidSettings.writePreset(
                    XeniaAndroidSettings.getPreferences(this).edit(),
                    XeniaAndroidSettings.PROFILE_BALANCED).apply();
            loadFromPreferences();
            Toast.makeText(this, R.string.settings_reset_saved, Toast.LENGTH_SHORT).show();
        });
        buttons.addView(reset, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        final Button save = new Button(this);
        save.setText(R.string.settings_save);
        save.setOnClickListener(view -> {
            saveToPreferences();
            Toast.makeText(this, R.string.settings_saved, Toast.LENGTH_SHORT).show();
        });
        final LinearLayout.LayoutParams saveParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        saveParams.setMargins(dp(10), 0, 0, 0);
        buttons.addView(save, saveParams);

        root.addView(buttons, matchWrapWithTopMargin(18));
    }

    private TextView sectionTitle(final int titleResId) {
        final TextView title = new TextView(this);
        title.setText(titleResId);
        title.setTextColor(getColor(R.color.xenia_green_soft));
        title.setTextSize(13);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        return title;
    }

    private void wireChangeListeners() {
        mProfileGroup.setOnCheckedChangeListener((group, checkedId) -> {
            if (mUpdatingControls) {
                return;
            }
            final String profile = getCheckedValue(group);
            if (!XeniaAndroidSettings.PROFILE_CUSTOM.equals(profile)) {
                applyPresetToControls(profile);
            }
        });
        final RadioGroup.OnCheckedChangeListener customListener =
                (group, checkedId) -> markCustom();
        mGpuGroup.setOnCheckedChangeListener(customListener);
        mAudioGroup.setOnCheckedChangeListener(customListener);
        mInputGroup.setOnCheckedChangeListener(customListener);
        final CompoundButton.OnCheckedChangeListener customToggleListener =
                (button, checked) -> markCustom();
        mMuteAudio.setOnCheckedChangeListener(customToggleListener);
        mHideOsd.setOnCheckedChangeListener(customToggleListener);
        mVulkanCounters.setOnCheckedChangeListener(customToggleListener);
    }

    private void loadFromPreferences() {
        final SharedPreferences preferences = XeniaAndroidSettings.getPreferences(this);
        mUpdatingControls = true;
        selectByValue(mProfileGroup, preferences.getString(
                XeniaAndroidSettings.KEY_PROFILE, XeniaAndroidSettings.PROFILE_BALANCED));
        selectByValue(mGpuGroup, preferences.getString(
                XeniaAndroidSettings.KEY_GPU_DRIVER, XeniaAndroidSettings.GPU_VULKAN));
        selectByValue(mAudioGroup, preferences.getString(
                XeniaAndroidSettings.KEY_APU_DRIVER, XeniaAndroidSettings.APU_NOP));
        selectByValue(mInputGroup, preferences.getString(
                XeniaAndroidSettings.KEY_HID_DRIVER, XeniaAndroidSettings.HID_ANDROID));
        mMuteAudio.setChecked(preferences.getBoolean(
                XeniaAndroidSettings.KEY_MUTE_AUDIO, false));
        mHideOsd.setChecked(preferences.getBoolean(XeniaAndroidSettings.KEY_HIDE_OSD, true));
        mShowFps.setChecked(preferences.getBoolean(XeniaAndroidSettings.KEY_SHOW_FPS, true));
        mVulkanCounters.setChecked(preferences.getBoolean(
                XeniaAndroidSettings.KEY_VULKAN_PERF_COUNTERS, false));
        mUpdatingControls = false;
    }

    private void saveToPreferences() {
        final SharedPreferences.Editor editor = XeniaAndroidSettings.getPreferences(this).edit();
        editor.putString(XeniaAndroidSettings.KEY_PROFILE, getCheckedValue(mProfileGroup));
        editor.putString(XeniaAndroidSettings.KEY_GPU_DRIVER, getCheckedValue(mGpuGroup));
        editor.putString(XeniaAndroidSettings.KEY_APU_DRIVER, getCheckedValue(mAudioGroup));
        editor.putString(XeniaAndroidSettings.KEY_HID_DRIVER, getCheckedValue(mInputGroup));
        editor.putBoolean(XeniaAndroidSettings.KEY_MUTE_AUDIO, mMuteAudio.isChecked());
        editor.putBoolean(XeniaAndroidSettings.KEY_HIDE_OSD, mHideOsd.isChecked());
        editor.putBoolean(XeniaAndroidSettings.KEY_SHOW_FPS, mShowFps.isChecked());
        editor.putBoolean(
                XeniaAndroidSettings.KEY_VULKAN_PERF_COUNTERS, mVulkanCounters.isChecked());
        editor.putInt(XeniaAndroidSettings.KEY_VULKAN_PERF_COUNTERS_INTERVAL, 60);
        editor.apply();
    }

    private void applyPresetToControls(final String profile) {
        mUpdatingControls = true;
        if (XeniaAndroidSettings.PROFILE_PERFORMANCE.equals(profile)) {
            selectByValue(mGpuGroup, XeniaAndroidSettings.GPU_VULKAN);
            selectByValue(mAudioGroup, XeniaAndroidSettings.APU_NOP);
            selectByValue(mInputGroup, XeniaAndroidSettings.HID_ANDROID);
            mMuteAudio.setChecked(true);
            mHideOsd.setChecked(true);
            mShowFps.setChecked(true);
            mVulkanCounters.setChecked(false);
        } else if (XeniaAndroidSettings.PROFILE_RESEARCH.equals(profile)) {
            selectByValue(mGpuGroup, XeniaAndroidSettings.GPU_VULKAN);
            selectByValue(mAudioGroup, XeniaAndroidSettings.APU_NOP);
            selectByValue(mInputGroup, XeniaAndroidSettings.HID_ANDROID);
            mMuteAudio.setChecked(false);
            mHideOsd.setChecked(false);
            mShowFps.setChecked(true);
            mVulkanCounters.setChecked(true);
        } else {
            selectByValue(mGpuGroup, XeniaAndroidSettings.GPU_VULKAN);
            selectByValue(mAudioGroup, XeniaAndroidSettings.APU_NOP);
            selectByValue(mInputGroup, XeniaAndroidSettings.HID_ANDROID);
            mMuteAudio.setChecked(false);
            mHideOsd.setChecked(true);
            mShowFps.setChecked(true);
            mVulkanCounters.setChecked(false);
        }
        mUpdatingControls = false;
    }

    private void markCustom() {
        if (mUpdatingControls) {
            return;
        }
        mUpdatingControls = true;
        selectByValue(mProfileGroup, XeniaAndroidSettings.PROFILE_CUSTOM);
        mUpdatingControls = false;
    }

    private static void selectByValue(final RadioGroup group, final String value) {
        for (int i = 0; i < group.getChildCount(); ++i) {
            final View child = group.getChildAt(i);
            if (value.equals(child.getTag())) {
                group.check(child.getId());
                return;
            }
        }
    }

    private static String getCheckedValue(final RadioGroup group) {
        final View child = group.findViewById(group.getCheckedRadioButtonId());
        final Object value = child != null ? child.getTag() : null;
        return value instanceof String ? (String) value : "";
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

    private int dp(final int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
