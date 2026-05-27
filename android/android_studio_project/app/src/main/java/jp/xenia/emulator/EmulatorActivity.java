package jp.xenia.emulator;

import android.content.Intent;
import android.util.Log;
import android.view.InputDevice;
import android.view.Choreographer;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.TextView;

import org.jetbrains.annotations.Nullable;

import java.util.Locale;

public class EmulatorActivity extends WindowedAppActivity {
    private static final String TAG = "XeniaInput";
    private static final float AXIS_DEADZONE = 0.05f;
    private static int sGamepadLogBudget = 24;

    private View mInGameMenu;
    private TextView mFpsOverlay;
    private CheckBox mInGameMenuShowFps;
    private TextView mInGameMenuInputStatus;
    private TextView mInGameMenuControllerHelp;
    private String mLastInputSummary;
    private boolean mShowFps;
    private boolean mUpdatingMenuControls;
    private boolean mRefreshFpsFromPreferencesOnResume;
    private boolean mFpsCallbackScheduled;
    private long mFpsWindowStartNs;
    private int mFpsFrameCount;
    private final Choreographer.FrameCallback mFpsFrameCallback =
            new Choreographer.FrameCallback() {
                @Override
                public void doFrame(final long frameTimeNanos) {
                    if (!mFpsCallbackScheduled) {
                        return;
                    }
                    updateFpsCounter(frameTimeNanos);
                    Choreographer.getInstance().postFrameCallback(this);
                }
            };

    private static native void nativeOnAndroidGamepadKey(
            int keyCode, boolean pressed, int repeatCount, int deviceId);

    private static native void nativeOnAndroidGamepadMotion(
            int deviceId,
            float leftX,
            float leftY,
            float rightX,
            float rightY,
            float leftTrigger,
            float rightTrigger,
            float hatX,
            float hatY);

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
            copyBooleanExtra(intent, launchArguments, "mute");
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
            copyBooleanExtra(intent, launchArguments, "android_show_fps");
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
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_perf_counters");
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
            copyIntExtra(
                    intent, launchArguments, "vulkan_trace_perf_counters_log_interval");
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
            copyBooleanExtra(intent, launchArguments, "a64_inline_fpr_helpers");
            copyBooleanExtra(intent, launchArguments, "a64_inline_vmx_helpers");
            copyBooleanExtra(
                    intent, launchArguments, "a64_inline_ppc_thread_field_leaf_helpers");
            copyBooleanExtra(intent, launchArguments, "a64_inline_kernel_high_frequency_exports");
            copyBooleanExtra(intent, launchArguments, "a64_inline_kernel_spinlock_exports");
            copyBooleanExtra(intent, launchArguments, "a64_lse_kernel_lock_fastpaths");
            copyBooleanExtra(
                    intent, launchArguments, "a64_enable_host_guest_stack_synchronization");
            copyBooleanExtra(intent, launchArguments, "arm64_offset_memory_address_fastpath");
            copyBooleanExtra(intent, launchArguments, "arm64_vmx_dot_f32_fastpath");
            copyBooleanExtra(intent, launchArguments, "arm64_permute_i32_zip_fastpath");
            copyBooleanExtra(intent, launchArguments, "a64_rtl_enter_free_first");
            copyBooleanExtra(intent, launchArguments, "a64_inline_rtl_leave_final_unlock");
            copyBooleanExtra(intent, launchArguments, "a64_rtl_leave_fastpath_audit");
            copyBooleanExtra(intent, launchArguments, "a64_inline_kf_lower_irql");
            copyBooleanExtra(intent, launchArguments, "a64_inline_kf_lower_irql_apc_guard");
            copyBooleanExtra(intent, launchArguments, "a64_kf_lower_irql_apc_guard_audit");
            copyIntExtra(
                    intent,
                    launchArguments,
                    "a64_kf_lower_irql_apc_guard_native_poll_interval");
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
            copyIntExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_draw_wait_fastpath_native_yield_stride");
            copyIntExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_draw_wait_fastpath_native_sleep_us");
            copyIntExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_draw_wait_fastpath_timeout_ms");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_draw_wait_inline_in_caller");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_draw_wait_caller_profile");
            copyIntExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_draw_wait_caller_profile_stride");
            copyIntExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_draw_wait_caller_profile_budget");
            copyBooleanExtra(intent, launchArguments, "arm64_blue_dragon_memcpy_fastpath");
            copyBooleanExtra(intent, launchArguments, "arm64_blue_dragon_stricmp_fastpath");
            copyBooleanExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_stricmp_deferred_cr_fastpath");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_stricmp_return_profile");
            copyIntExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_stricmp_return_profile_stride");
            copyIntExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_stricmp_return_profile_budget");
            copyBooleanExtra(intent, launchArguments, "arm64_blue_dragon_jump_table_fastpath");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_jump_table_inline_in_caller");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_vmx_copy_loop_fastpath");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_word_copy_loop_fastpath");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_stvewx_stack_lane_fastpath");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_stvewx_stack_lane_audit");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_mul_add_v128_fastpath");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_mul_add_v128_audit");
            copyBooleanExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_call_boundary_state_audit");
            copyBooleanExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_call_boundary_state_suppress_dead_stores");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_f1_carrier_audit");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_f1_carrier_fastpath");
            copyBooleanExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_state_carrier_design_audit");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_blue_dragon_edge_variant_audit");
            copyBooleanExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_edge_payload_storage_audit");
            copyBooleanExtra(
                    intent,
                    launchArguments,
                    "arm64_blue_dragon_fpscr_cfg_writeback_audit");
            copyBooleanExtra(intent, launchArguments, "arm64_add_sub_imm_audit");
            copyIntExtra(intent, launchArguments, "arm64_add_sub_imm_audit_function");
            copyIntExtra(intent, launchArguments, "arm64_add_sub_imm_audit_budget");
            copyBooleanExtra(intent, launchArguments, "arm64_add_i64_wrapped_imm_fastpath");
            copyIntExtra(
                    intent, launchArguments, "arm64_add_i64_wrapped_imm_fastpath_function");
            copyBooleanExtra(intent, launchArguments, "arm64_immediate_lowering_audit");
            copyIntExtra(intent, launchArguments, "arm64_immediate_lowering_audit_function");
            copyIntExtra(intent, launchArguments, "arm64_immediate_lowering_audit_budget");
            copyBooleanExtra(intent, launchArguments, "arm64_context_value_cache");
            copyBooleanExtra(intent, launchArguments, "arm64_context_value_cache_fallthrough");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_context_value_cache_preserve_barrier");
            copyBooleanExtra(intent, launchArguments, "arm64_context_pinned_gpr_r1");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_context_pinned_gpr_r1_fallthrough");
            copyIntExtra(intent, launchArguments, "arm64_context_pinned_gpr_r1_function");
            copyBooleanExtra(intent, launchArguments, "arm64_context_pinned_gpr_r1_audit");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_context_promotion_gpr_local_slots");
            copyIntExtra(
                    intent, launchArguments, "arm64_context_promotion_gpr_local_slots_function");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_context_promotion_gpr_local_slots_audit");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_context_promotion_gpr_livein_r1");
            copyIntExtra(
                    intent, launchArguments, "arm64_context_promotion_gpr_livein_r1_function");
            copyBooleanExtra(
                    intent,
                    launchArguments,
                    "arm64_context_promotion_gpr_livein_r1_preserve_barrier");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_context_promotion_gpr_livein_r1_audit");
            copyBooleanExtra(intent, launchArguments, "arm64_guest_state_register_cache_audit");
            copyIntExtra(
                    intent, launchArguments, "arm64_guest_state_register_cache_audit_function");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_guest_state_register_cache_residual_audit");
            copyIntExtra(
                    intent,
                    launchArguments,
                    "arm64_guest_state_register_cache_residual_audit_function");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_guest_state_nonclosed_cache_audit");
            copyIntExtra(
                    intent,
                    launchArguments,
                    "arm64_guest_state_nonclosed_cache_audit_function");
            copyBooleanExtra(intent, launchArguments, "arm64_register_allocation_audit");
            copyIntExtra(intent, launchArguments, "arm64_register_allocation_audit_function");
            copyBooleanExtra(intent, launchArguments, "arm64_guest_call_fast_entry_audit");
            copyIntExtra(intent, launchArguments, "arm64_guest_call_fast_entry_audit_function");
            copyIntExtra(intent, launchArguments, "arm64_guest_call_fast_entry_audit_budget");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_guest_call_fast_entry_stub_skeleton");
            copyBooleanExtra(intent, launchArguments, "arm64_guest_stack_arg_handoff_audit");
            copyIntExtra(
                    intent, launchArguments, "arm64_guest_stack_arg_handoff_audit_function");
            copyIntExtra(
                    intent, launchArguments, "arm64_guest_stack_arg_handoff_audit_budget");
            copyBooleanExtra(
                    intent, launchArguments, "arm64_cr_compare_branch_across_context_barrier");
            copyBooleanExtra(intent, launchArguments, "arm64_cr_store_elide_for_fused_branch");
            copyIntExtra(
                    intent, launchArguments, "arm64_cr_store_elide_for_fused_branch_function");
            copyBooleanExtra(intent, launchArguments, "arm64_context_traffic_audit");
            copyIntExtra(intent, launchArguments, "arm64_context_traffic_audit_function");
            copyIntExtra(intent, launchArguments, "arm64_context_traffic_audit_budget");
            copyStringExtra(intent, launchArguments, "arm64_speed_profile_body_time_filter");
            copyStringExtra(
                    intent, launchArguments, "arm64_speed_profile_entry_exit_time_filter");
            copyIntExtra(intent, launchArguments, "arm64_speed_profile_body_time_after_ms");
            copyStringExtra(intent, launchArguments, "arm64_speed_profile_block_filter");
            copyBooleanExtra(intent, launchArguments, "arm64_speed_profile_block_body_time");
            copyStringExtra(intent, launchArguments, "arm64_speed_profile_call_edge_filter");
            copyBooleanExtra(intent, launchArguments, "arm64_speed_profile_call_edge_audit_only");
            copyBooleanExtra(intent, launchArguments, "arm64_speed_profile_thread_snapshot");
            copyBooleanExtra(intent, launchArguments, "arm64_speed_profile_thread_snapshot_on_idle");
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
        final WindowSurfaceView surfaceView = findViewById(R.id.emulator_surface_view);
        setWindowSurfaceView(surfaceView);
        if (surfaceView != null) {
            surfaceView.setFocusable(true);
            surfaceView.setFocusableInTouchMode(true);
            surfaceView.requestFocus();
        }
        updateOsd(getLaunchArguments(intent));
        setupFpsOverlay(getLaunchArguments(intent));
        setupInGameMenu();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mRefreshFpsFromPreferencesOnResume) {
            mRefreshFpsFromPreferencesOnResume = false;
            setShowFps(XeniaAndroidSettings.getPreferences(this).getBoolean(
                    XeniaAndroidSettings.KEY_SHOW_FPS, true));
            refreshInGameMenu();
        }
        startFpsTickerIfNeeded();
    }

    @Override
    protected void onPause() {
        stopFpsTicker();
        super.onPause();
    }

    @Override
    public boolean dispatchKeyEvent(final KeyEvent event) {
        if (event != null && event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_UP) {
                toggleInGameMenu();
            }
            return true;
        }
        if (isInGameMenuVisible()) {
            if (event != null && event.getAction() == KeyEvent.ACTION_DOWN
                    && isGamepadKeyCode(event.getKeyCode())) {
                updateLastInputSummary(
                        event.getKeyCode(),
                        XeniaInputMapping.mapAndroidKeyCode(this, event.getKeyCode()));
                refreshInGameMenu();
            }
            if (event != null && event.getAction() == KeyEvent.ACTION_UP
                    && isControllerActivateKey(event.getKeyCode())) {
                final View focused = getCurrentFocus();
                if (focused != null && focused.isEnabled()) {
                    focused.performClick();
                    return true;
                }
            }
            return super.dispatchKeyEvent(event);
        }
        if (handleGamepadKeyEvent(event)) {
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(final MotionEvent event) {
        if (isInGameMenuVisible()) {
            return super.dispatchGenericMotionEvent(event);
        }
        if (handleGamepadMotionEvent(event)) {
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public void onBackPressed() {
        toggleInGameMenu();
    }

    private static boolean isGamepadSource(final int source) {
        return ((source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD)
                || ((source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
                || ((source & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD);
    }

    private static boolean isGamepadDevice(@Nullable final InputDevice device) {
        if (device == null) {
            return false;
        }
        final int sources = device.getSources();
        if (isGamepadSource(sources)) {
            return true;
        }
        final String name = device.getName();
        return name != null && name.toLowerCase().contains("odin controller");
    }

    private static void logGamepadEvent(
            final String kind, final InputDevice device, final int source, final String detail) {
        if (sGamepadLogBudget <= 0) {
            return;
        }
        sGamepadLogBudget--;
        final String name = device != null ? device.getName() : "<none>";
        final int deviceId = device != null ? device.getId() : -1;
        Log.i(TAG, kind + " device=" + deviceId + " name=\"" + name
                + "\" source=0x" + Integer.toHexString(source) + " " + detail);
    }

    private static boolean isGamepadKeyCode(final int keyCode) {
        return XeniaInputMapping.isBindableKeyCode(keyCode);
    }

    private static boolean isControllerActivateKey(final int keyCode) {
        return keyCode == KeyEvent.KEYCODE_BUTTON_A
                || keyCode == KeyEvent.KEYCODE_BUTTON_START
                || keyCode == KeyEvent.KEYCODE_DPAD_CENTER
                || keyCode == KeyEvent.KEYCODE_ENTER
                || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER;
    }

    private boolean handleGamepadKeyEvent(final KeyEvent event) {
        if (event == null || !isGamepadKeyCode(event.getKeyCode())) {
            return false;
        }
        final int action = event.getAction();
        if (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) {
            return false;
        }
        final InputDevice device = event.getDevice();
        final int mappedKeyCode = XeniaInputMapping.mapAndroidKeyCode(this, event.getKeyCode());
        updateLastInputSummary(event.getKeyCode(), mappedKeyCode);
        nativeOnAndroidGamepadKey(
                mappedKeyCode,
                action == KeyEvent.ACTION_DOWN,
                event.getRepeatCount(),
                event.getDeviceId());
        logGamepadEvent("key", device, event.getSource(),
                KeyEvent.keyCodeToString(event.getKeyCode()) + "->"
                        + KeyEvent.keyCodeToString(mappedKeyCode) + " "
                        + (action == KeyEvent.ACTION_DOWN ? "down" : "up"));
        return true;
    }

    private boolean handleGamepadMotionEvent(final MotionEvent event) {
        if (event == null || event.getActionMasked() != MotionEvent.ACTION_MOVE
                || (!isGamepadSource(event.getSource()) && !isGamepadDevice(event.getDevice()))) {
            return false;
        }

        final float leftX = getCenteredAxis(event, MotionEvent.AXIS_X);
        final float leftY = getCenteredAxis(event, MotionEvent.AXIS_Y);
        final float axisZ = getCenteredAxis(event, MotionEvent.AXIS_Z);
        final float axisRz = getCenteredAxis(event, MotionEvent.AXIS_RZ);
        final float axisRx = getCenteredAxis(event, MotionEvent.AXIS_RX);
        final float axisRy = getCenteredAxis(event, MotionEvent.AXIS_RY);

        float rightX = axisRx;
        float rightY = axisRy;
        if (rightX == 0.0f && rightY == 0.0f
                && (axisZ != 0.0f || axisRz != 0.0f
                        || !hasAxis(event, MotionEvent.AXIS_RX))) {
            rightX = axisZ;
            rightY = axisRz;
        }

        float leftTrigger = getTriggerAxis(event, MotionEvent.AXIS_LTRIGGER);
        float rightTrigger = getTriggerAxis(event, MotionEvent.AXIS_RTRIGGER);
        if (leftTrigger == 0.0f && rightTrigger == 0.0f) {
            leftTrigger = getTriggerAxis(event, MotionEvent.AXIS_BRAKE);
            rightTrigger = getTriggerAxis(event, MotionEvent.AXIS_GAS);
        }
        if (leftTrigger == 0.0f && rightTrigger == 0.0f
                && hasAxis(event, MotionEvent.AXIS_RX)
                && (hasAxis(event, MotionEvent.AXIS_Z)
                        || hasAxis(event, MotionEvent.AXIS_RZ))) {
            leftTrigger = getTriggerAxis(event, MotionEvent.AXIS_Z);
            rightTrigger = getTriggerAxis(event, MotionEvent.AXIS_RZ);
        }

        nativeOnAndroidGamepadMotion(
                event.getDeviceId(),
                leftX,
                leftY,
                rightX,
                rightY,
                leftTrigger,
                rightTrigger,
                getCenteredAxis(event, MotionEvent.AXIS_HAT_X),
                getCenteredAxis(event, MotionEvent.AXIS_HAT_Y));
        logGamepadEvent("motion", event.getDevice(), event.getSource(),
                "lx=" + leftX + " ly=" + leftY + " rx=" + rightX + " ry=" + rightY
                        + " lt=" + leftTrigger + " rt=" + rightTrigger);
        return true;
    }

    private static boolean hasAxis(final MotionEvent event, final int axis) {
        return getMotionRange(event, axis) != null;
    }

    private static InputDevice.MotionRange getMotionRange(
            final MotionEvent event, final int axis) {
        final InputDevice device = event.getDevice();
        if (device == null) {
            return null;
        }
        InputDevice.MotionRange range = device.getMotionRange(axis, event.getSource());
        if (range == null) {
            range = device.getMotionRange(axis, InputDevice.SOURCE_JOYSTICK);
        }
        if (range == null) {
            range = device.getMotionRange(axis, InputDevice.SOURCE_GAMEPAD);
        }
        if (range == null) {
            range = device.getMotionRange(axis);
        }
        return range;
    }

    private static float getCenteredAxis(final MotionEvent event, final int axis) {
        final InputDevice.MotionRange range = getMotionRange(event, axis);
        final float flat = range != null ? Math.max(range.getFlat(), AXIS_DEADZONE) : AXIS_DEADZONE;
        final float value = event.getAxisValue(axis);
        return Math.abs(value) > flat ? clamp(value, -1.0f, 1.0f) : 0.0f;
    }

    private static float getTriggerAxis(final MotionEvent event, final int axis) {
        final InputDevice.MotionRange range = getMotionRange(event, axis);
        if (range == null) {
            return 0.0f;
        }
        final float flat = Math.max(range.getFlat(), AXIS_DEADZONE);
        float value = event.getAxisValue(axis);
        if (Math.abs(value) <= flat) {
            return 0.0f;
        }
        if (range.getMin() < 0.0f) {
            value = (value + 1.0f) * 0.5f;
        }
        return clamp(value, 0.0f, 1.0f);
    }

    private static float clamp(final float value, final float min, final float max) {
        return Math.max(min, Math.min(max, value));
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

    private void setupInGameMenu() {
        mInGameMenu = findViewById(R.id.emulator_in_game_menu);
        mInGameMenuShowFps = findViewById(R.id.emulator_menu_show_fps);
        mInGameMenuInputStatus = findViewById(R.id.emulator_menu_input_status);
        mInGameMenuControllerHelp = findViewById(R.id.emulator_menu_controller_help);

        final Button resumeButton = findViewById(R.id.emulator_menu_resume);
        if (resumeButton != null) {
            resumeButton.setOnClickListener(view -> hideInGameMenu());
        }

        final Button controllerButton = findViewById(R.id.emulator_menu_controller_mapping);
        if (controllerButton != null) {
            controllerButton.setOnClickListener(view ->
                    startActivity(new Intent(this, ControllerMappingActivity.class)));
        }

        final Button settingsButton = findViewById(R.id.emulator_menu_settings);
        if (settingsButton != null) {
            settingsButton.setOnClickListener(view -> {
                mRefreshFpsFromPreferencesOnResume = true;
                startActivity(new Intent(this, SettingsActivity.class));
            });
        }

        final Button exitButton = findViewById(R.id.emulator_menu_exit_game);
        if (exitButton != null) {
            exitButton.setOnClickListener(view -> exitGameToLauncher());
        }

        if (mInGameMenuShowFps != null) {
            mInGameMenuShowFps.setOnCheckedChangeListener((buttonView, checked) -> {
                if (mUpdatingMenuControls) {
                    return;
                }
                XeniaAndroidSettings.getPreferences(this)
                        .edit()
                        .putBoolean(XeniaAndroidSettings.KEY_SHOW_FPS, checked)
                        .apply();
                setShowFps(checked);
            });
        }

        refreshInGameMenu();
    }

    private boolean isInGameMenuVisible() {
        return mInGameMenu != null && mInGameMenu.getVisibility() == View.VISIBLE;
    }

    private void toggleInGameMenu() {
        if (isInGameMenuVisible()) {
            hideInGameMenu();
            return;
        }
        showInGameMenu();
    }

    private void showInGameMenu() {
        if (mInGameMenu == null) {
            return;
        }
        refreshInGameMenu();
        mInGameMenu.setVisibility(View.VISIBLE);
        mInGameMenu.requestFocus();
        final Button resumeButton = findViewById(R.id.emulator_menu_resume);
        if (resumeButton != null) {
            resumeButton.requestFocus();
        }
    }

    private void hideInGameMenu() {
        if (mInGameMenu == null) {
            return;
        }
        mInGameMenu.setVisibility(View.GONE);
        final WindowSurfaceView surfaceView = findViewById(R.id.emulator_surface_view);
        if (surfaceView != null) {
            surfaceView.requestFocus();
        }
        enterImmersiveMode();
    }

    private void exitGameToLauncher() {
        if (mInGameMenu != null) {
            mInGameMenu.setVisibility(View.GONE);
        }
        final Intent launcherIntent = new Intent(this, LauncherActivity.class);
        launcherIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(launcherIntent);
        finish();
    }

    private void refreshInGameMenu() {
        if (mInGameMenuShowFps != null) {
            mUpdatingMenuControls = true;
            mInGameMenuShowFps.setChecked(mShowFps);
            mUpdatingMenuControls = false;
        }
        if (mInGameMenuInputStatus != null) {
            final String input = mLastInputSummary != null
                    ? mLastInputSummary
                    : getString(R.string.emulator_menu_no_input);
            mInGameMenuInputStatus.setText(getString(R.string.emulator_menu_status, input));
        }
        if (mInGameMenuControllerHelp != null) {
            mInGameMenuControllerHelp.setText(buildControllerHelpText());
        }
    }

    private String buildControllerHelpText() {
        final StringBuilder text = new StringBuilder();
        for (final XeniaInputMapping.ButtonAction action :
                XeniaInputMapping.getButtonActions()) {
            if (text.length() > 0) {
                text.append('\n');
            }
            text.append(action.label)
                    .append("  ->  ")
                    .append(physicalLabelForAction(action));
        }
        return text.toString();
    }

    private String physicalLabelForAction(final XeniaInputMapping.ButtonAction action) {
        String label = XeniaInputMapping.keyName(
                XeniaInputMapping.getPhysicalKeyCode(this, action));
        if ("back".equals(action.id) && !label.equals(XeniaInputMapping.keyName(
                KeyEvent.KEYCODE_MENU))) {
            label += " / " + XeniaInputMapping.keyName(KeyEvent.KEYCODE_MENU);
        }
        return label;
    }

    private void updateLastInputSummary(final int physicalKeyCode, final int mappedKeyCode) {
        mLastInputSummary = getString(
                R.string.emulator_menu_input_format,
                XeniaInputMapping.keyName(physicalKeyCode),
                XeniaInputMapping.keyName(mappedKeyCode));
    }

    private void setupFpsOverlay(final Bundle launchArguments) {
        mFpsOverlay = findViewById(R.id.emulator_fps_overlay);
        final boolean showFps = launchArguments != null
                ? launchArguments.getBoolean("android_show_fps", true)
                : XeniaAndroidSettings.getPreferences(this).getBoolean(
                        XeniaAndroidSettings.KEY_SHOW_FPS, true);
        setShowFps(showFps);
    }

    private void setShowFps(final boolean showFps) {
        mShowFps = showFps;
        mFpsWindowStartNs = 0;
        mFpsFrameCount = 0;
        if (mFpsOverlay == null) {
            return;
        }
        mFpsOverlay.setVisibility(showFps ? View.VISIBLE : View.GONE);
        if (showFps) {
            mFpsOverlay.setText(R.string.emulator_fps_initial);
            startFpsTickerIfNeeded();
        } else {
            stopFpsTicker();
        }
    }

    private void startFpsTickerIfNeeded() {
        if (!mShowFps || mFpsCallbackScheduled) {
            return;
        }
        mFpsCallbackScheduled = true;
        Choreographer.getInstance().postFrameCallback(mFpsFrameCallback);
    }

    private void stopFpsTicker() {
        mFpsCallbackScheduled = false;
    }

    private void updateFpsCounter(final long nowNs) {
        if (!mShowFps || mFpsOverlay == null) {
            return;
        }
        if (mFpsWindowStartNs == 0) {
            mFpsWindowStartNs = nowNs;
            mFpsFrameCount = 0;
        }
        mFpsFrameCount++;
        final long elapsedNs = nowNs - mFpsWindowStartNs;
        if (elapsedNs < 500000000L) {
            return;
        }
        final double fps = (mFpsFrameCount * 1000000000.0) / elapsedNs;
        mFpsOverlay.setText(String.format(Locale.US, "%.1f FPS", fps));
        mFpsWindowStartNs = nowNs;
        mFpsFrameCount = 0;
    }

    private void updateOsd(final Bundle launchArguments) {
        final View topBar = findViewById(R.id.emulator_osd_top_bar);
        final TextView titleView = findViewById(R.id.emulator_osd_title);
        final TextView subtitleView = findViewById(R.id.emulator_osd_subtitle);
        final TextView warningView = findViewById(R.id.emulator_osd_warning);
        if (topBar != null) {
            topBar.setVisibility(View.GONE);
        }
        if (warningView != null) {
            warningView.setVisibility(View.GONE);
        }
        if (launchArguments == null) {
            return;
        }
        if (launchArguments.getBoolean("android_hide_osd", true)) {
            return;
        }
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
