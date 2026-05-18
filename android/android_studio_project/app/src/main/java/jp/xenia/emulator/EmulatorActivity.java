package jp.xenia.emulator;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.TextView;

public class EmulatorActivity extends WindowedAppActivity {
    @Override
    protected String getWindowedAppIdentifier() {
        return "xenia";
    }

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        final Intent intent = getIntent();
        if (intent != null && intent.getBundleExtra(EXTRA_CVARS) == null) {
            final Bundle launchArguments = new Bundle();
            copyStringExtra(intent, launchArguments, "target");
            copyStringExtra(intent, launchArguments, "gpu");
            copyStringExtra(intent, launchArguments, "cpu");
            copyStringExtra(intent, launchArguments, "apu");
            copyStringExtra(intent, launchArguments, "hid");
            copyBooleanExtra(intent, launchArguments, "arm64_enable_mini_jit");
            copyBooleanExtra(intent, launchArguments, "disassemble_functions");
            copyBooleanExtra(intent, launchArguments, "mount_cache");
            copyStringExtra(intent, launchArguments, "arm64_mini_jit_blacklist");
            copyStringExtra(
                    intent, launchArguments, "arm64_force_interpreter_guest_ranges");
            if (intent.hasExtra("discord")) {
                launchArguments.putBoolean(
                        "discord", intent.getBooleanExtra("discord", false));
            }
            if (!launchArguments.isEmpty()) {
                intent.putExtra(EXTRA_CVARS, launchArguments);
            }
        }

        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_emulator);
        setWindowSurfaceView(findViewById(R.id.emulator_surface_view));
        updateOsd(getLaunchArguments(intent));
    }

    private static void copyStringExtra(
            final Intent intent, final Bundle launchArguments, final String name) {
        final String value = intent.getStringExtra(name);
        if (value != null && !value.isEmpty()) {
            launchArguments.putString(name, value);
        }
    }

    private static void copyBooleanExtra(
            final Intent intent, final Bundle launchArguments, final String name) {
        if (intent.hasExtra(name)) {
            launchArguments.putBoolean(name, intent.getBooleanExtra(name, false));
        }
    }

    private Bundle getLaunchArguments(final Intent intent) {
        return intent != null ? intent.getBundleExtra(EXTRA_CVARS) : null;
    }

    private void updateOsd(final Bundle launchArguments) {
        final TextView titleView = findViewById(R.id.emulator_osd_title);
        final TextView subtitleView = findViewById(R.id.emulator_osd_subtitle);
        if (titleView == null || subtitleView == null) {
            return;
        }

        String target = null;
        if (launchArguments != null) {
            target = launchArguments.getString("target");
        }
        if (target == null || target.isEmpty()) {
            titleView.setText(R.string.osd_default_title);
            subtitleView.setText(R.string.osd_app_title);
            return;
        }

        titleView.setText(getDisplayNameForTarget(target));
        subtitleView.setText(target);
    }

    private static String getDisplayNameForTarget(final String target) {
        final Uri uri = Uri.parse(target);
        String displayName = uri.getLastPathSegment();
        if (displayName == null || displayName.isEmpty()) {
            final int lastSeparator = target.lastIndexOf('/');
            displayName = lastSeparator >= 0 && lastSeparator + 1 < target.length()
                    ? target.substring(lastSeparator + 1)
                    : target;
        }
        return displayName;
    }
}
