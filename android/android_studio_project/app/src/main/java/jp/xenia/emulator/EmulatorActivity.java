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
            copyBooleanExtra(intent, launchArguments, "disassemble_functions");
            copyStringExtra(intent, launchArguments, "disassemble_function_filter");
            copyBooleanExtra(intent, launchArguments, "mount_cache");
            copyIntExtra(intent, launchArguments, "mmap_address_high");
            copyBooleanExtra(intent, launchArguments, "clear_memory_page_state");
            copyBooleanExtra(intent, launchArguments, "emit_inline_mmio_checks");
            copyBooleanExtra(intent, launchArguments, "gpu_interrupt_on_ring_idle");
            copyBooleanExtra(intent, launchArguments, "gpu_blue_dragon_kick_wait_token");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_swap");
            copyBooleanExtra(
                    intent, launchArguments, "gpu_early_primary_read_pointer_writeback");
            copyIntExtra(intent, launchArguments, "gpu_blue_dragon_kick_wait_token_budget");
            copyIntExtra(intent, launchArguments, "gpu_trace_packet_budget");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_interval");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_min_count");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_budget");
            copyStringExtra(intent, launchArguments, "arm64_compiled_call_trace_functions");
            copyStringExtra(intent, launchArguments, "arm64_compiled_call_trace_guest_tids");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_after_ms");
            copyBooleanExtra(intent, launchArguments, "arm64_blue_dragon_draw_wait_probe");
            copyStringExtra(intent, launchArguments, "arm64_guest_store_watch");
            copyIntExtra(intent, launchArguments, "arm64_guest_store_watch_budget");
            copyBooleanExtra(intent, launchArguments, "xboxkrnl_thread_wait_trace");
            copyIntExtra(intent, launchArguments, "xboxkrnl_thread_wait_trace_budget");
            copyIntExtra(intent, launchArguments, "xboxkrnl_thread_wait_trace_after_ms");
            copyStringExtra(
                    intent, launchArguments, "xboxkrnl_thread_wait_trace_guest_tids");
            copyBooleanExtra(intent, launchArguments, "xboxkrnl_event_trace");
            copyIntExtra(intent, launchArguments, "xboxkrnl_event_trace_budget");
            copyStringExtra(intent, launchArguments, "xboxkrnl_event_trace_objects");
            copyBooleanExtra(
                    intent, launchArguments, "xboxkrnl_ignore_guest_debug_breakpoints");
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

    private static void copyIntExtra(
            final Intent intent, final Bundle launchArguments, final String name) {
        if (intent.hasExtra(name)) {
            launchArguments.putInt(name, intent.getIntExtra(name, 0));
        }
    }

    private Bundle getLaunchArguments(final Intent intent) {
        return intent != null ? intent.getBundleExtra(EXTRA_CVARS) : null;
    }

    private void updateOsd(final Bundle launchArguments) {
        final TextView titleView = findViewById(R.id.emulator_osd_title);
        final TextView subtitleView = findViewById(R.id.emulator_osd_subtitle);
        final TextView warningView = findViewById(R.id.emulator_osd_warning);
        if (titleView == null || subtitleView == null || warningView == null) {
            return;
        }

        String target = null;
        if (launchArguments != null) {
            target = launchArguments.getString("target");
        }
        warningView.setText(R.string.osd_a64_research_active);
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
