package jp.xenia.emulator;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
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
            copyIntExtra(intent, launchArguments, "log_level");
            copyBooleanExtra(intent, launchArguments, "xma_trace_context_state");
            copyBooleanExtra(intent, launchArguments, "xma_fast_silence");
            copyStringExtra(intent, launchArguments, "hid");
            copyStringExtra(intent, launchArguments, "dump_shaders");
            copyStringExtra(intent, launchArguments, "render_target_path_vulkan");
            copyDoubleExtra(intent, launchArguments, "time_scalar");
            copyBooleanExtra(intent, launchArguments, "hid_nop_connected");
            copyStringExtra(intent, launchArguments, "hid_nop_buttons");
            copyStringExtra(intent, launchArguments, "hid_nop_button_sequence");
            copyIntExtra(intent, launchArguments, "hid_nop_buttons_delay_ms");
            copyIntExtra(intent, launchArguments, "hid_nop_buttons_hold_ms");
            copyBooleanExtra(intent, launchArguments, "android_hide_osd");
            copyBooleanExtra(intent, launchArguments, "break_on_debugbreak");
            copyBooleanExtra(intent, launchArguments, "disassemble_functions");
            copyStringExtra(intent, launchArguments, "disassemble_function_filter");
            copyBooleanExtra(intent, launchArguments, "mount_cache");
            copyIntExtra(intent, launchArguments, "mmap_address_high");
            copyBooleanExtra(intent, launchArguments, "clear_memory_page_state");
            copyBooleanExtra(intent, launchArguments, "emit_inline_mmio_checks");
            copyBooleanExtra(intent, launchArguments, "gpu_interrupt_on_ring_idle");
            copyBooleanExtra(intent, launchArguments, "gpu_interrupt_on_swap");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_interrupts");
            copyBooleanExtra(intent, launchArguments, "gpu_blue_dragon_kick_wait_token");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_swap");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_texture_cache_actions");
            copyIntExtra(intent, launchArguments, "gpu_unknown_register_log_budget");
            copyIntExtra(
                    intent, launchArguments, "xboxkrnl_nt_create_file_fail_log_budget");
            copyBooleanExtra(
                    intent, launchArguments, "gpu_trace_swap_frontbuffer_checksum");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_swap_render_targets");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_resolve");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_trace_resolve_checksum");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_trace_edram_checksum");
            copyBooleanExtra(intent, launchArguments, "vulkan_readback_resolve");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_copy_state");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_draw_state");
            copyStringExtra(
                    intent, launchArguments, "vulkan_trace_draw_shader_filter");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_pipeline_creation");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_shader_constants");
            copyStringExtra(
                    intent, launchArguments, "vulkan_trace_shader_constants_shader_filter");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_trace_texture_source_checksum");
            copyStringExtra(
                    intent, launchArguments, "vulkan_trace_texture_source_shader_filter");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_trace_vertex_fetch_checksum");
            copyStringExtra(
                    intent, launchArguments, "vulkan_trace_vertex_fetch_shader_filter");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_trace_swap_shared_memory_checksum");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_present_recent_resolve_on_swap");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_present_scored_resolve_on_swap");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_present_scored_resolve_reject_clear_like");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_present_forced_resolve_on_swap");
            copyBooleanExtra(intent, launchArguments, "vulkan_debug_solid_guest_output");
            copyStringExtra(
                    intent, launchArguments, "vulkan_debug_pixel_shader_output_filter");
            copyStringExtra(
                    intent, launchArguments, "vulkan_debug_pixel_shader_output_secondary_filter");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_force_2101010_rgba8_fallback");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_force_signed_2101010_unorm_fallback");
            copyBooleanExtra(
                    intent, launchArguments, "vulkan_debug_texture_fetch_disable_exp_adjust");
            copyBooleanExtra(
                    intent, launchArguments, "gpu_early_primary_read_pointer_writeback");
            copyIntExtra(intent, launchArguments, "gpu_blue_dragon_kick_wait_token_budget");
            copyIntExtra(intent, launchArguments, "gpu_trace_interrupts_budget");
            copyIntExtra(intent, launchArguments, "gpu_trace_packet_budget");
            copyIntExtra(
                    intent, launchArguments, "gpu_trace_swap_frontbuffer_checksum_budget");
            copyIntExtra(intent, launchArguments, "gpu_trace_swap_render_targets_budget");
            copyIntExtra(intent, launchArguments, "vulkan_trace_resolve_budget");
            copyIntExtra(
                    intent, launchArguments, "vulkan_trace_resolve_checksum_budget");
            copyIntExtra(
                    intent, launchArguments, "vulkan_trace_edram_checksum_budget");
            copyIntExtra(intent, launchArguments, "vulkan_trace_copy_state_budget");
            copyIntExtra(intent, launchArguments, "vulkan_trace_draw_state_budget");
            copyIntExtra(intent, launchArguments, "vulkan_trace_shader_constants_budget");
            copyIntExtra(
                    intent, launchArguments, "vulkan_trace_texture_source_checksum_budget");
            copyIntExtra(
                    intent, launchArguments, "vulkan_trace_vertex_fetch_checksum_budget");
            copyIntExtra(
                    intent, launchArguments, "vulkan_trace_swap_shared_memory_checksum_budget");
            copyIntExtra(intent, launchArguments, "vulkan_present_scored_resolve_min_width");
            copyIntExtra(intent, launchArguments, "vulkan_present_scored_resolve_min_height");
            copyIntExtra(intent, launchArguments, "vulkan_present_scored_resolve_budget");
            copyIntExtra(intent, launchArguments, "vulkan_present_scored_resolve_required_format");
            copyIntExtra(intent, launchArguments, "vulkan_present_forced_resolve_address");
            copyIntExtra(intent, launchArguments, "vulkan_present_forced_resolve_length");
            copyIntExtra(intent, launchArguments, "vulkan_present_forced_resolve_width");
            copyIntExtra(intent, launchArguments, "vulkan_present_forced_resolve_height");
            copyIntExtra(intent, launchArguments, "vulkan_present_forced_resolve_pitch");
            copyIntExtra(intent, launchArguments, "vulkan_present_forced_resolve_format");
            copyIntExtra(intent, launchArguments, "vulkan_debug_pixel_shader_output_mode");
            copyIntExtra(
                    intent, launchArguments, "vulkan_debug_pixel_shader_output_secondary_mode");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_interval");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_min_count");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_budget");
            copyStringExtra(intent, launchArguments, "arm64_compiled_call_trace_functions");
            copyStringExtra(intent, launchArguments, "arm64_compiled_call_trace_guest_tids");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_after_ms");
            copyIntExtra(intent, launchArguments, "arm64_speed_profile_interval_ms");
            copyIntExtra(intent, launchArguments, "arm64_speed_profile_top_functions");
            copyIntExtra(intent, launchArguments, "arm64_speed_profile_min_delta");
            copyBooleanExtra(intent, launchArguments, "a64_inline_gprlr_helpers");
            copyBooleanExtra(
                    intent, launchArguments, "a64_inline_ppc_thread_field_leaf_helpers");
            copyBooleanExtra(intent, launchArguments, "arm64_blue_dragon_draw_wait_probe");
            copyIntExtra(
                    intent, launchArguments, "arm64_blue_dragon_draw_wait_probe_stride");
            copyIntExtra(
                    intent, launchArguments, "arm64_blue_dragon_draw_wait_inline_tick_step");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_draw_wait_fastpath");
            copyBooleanExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_draw_wait_fastpath_host_counter_time");
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

    private static void copyDoubleExtra(
            final Intent intent, final Bundle launchArguments, final String name) {
        if (!intent.hasExtra(name)) {
            return;
        }
        final Bundle extras = intent.getExtras();
        if (extras == null) {
            return;
        }
        final Object value = extras.get(name);
        if (value instanceof Number) {
            launchArguments.putDouble(name, ((Number) value).doubleValue());
            return;
        }
        if (value instanceof String) {
            try {
                launchArguments.putDouble(name, Double.parseDouble((String) value));
            } catch (final NumberFormatException ignored) {
            }
        }
    }

    private Bundle getLaunchArguments(final Intent intent) {
        return intent != null ? intent.getBundleExtra(EXTRA_CVARS) : null;
    }

    private void updateOsd(final Bundle launchArguments) {
        final View topBar = findViewById(R.id.emulator_osd_top_bar);
        final TextView titleView = findViewById(R.id.emulator_osd_title);
        final TextView subtitleView = findViewById(R.id.emulator_osd_subtitle);
        final TextView warningView = findViewById(R.id.emulator_osd_warning);
        if (launchArguments != null
                && launchArguments.getBoolean("android_hide_osd", false)) {
            if (topBar != null) {
                topBar.setVisibility(View.GONE);
            }
            if (warningView != null) {
                warningView.setVisibility(View.GONE);
            }
            return;
        }
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
