package jp.xenia.emulator;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
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

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class EmulatorActivity extends WindowedAppActivity {
    private static final String TAG = "XeniaInput";
    private static final float AXIS_DEADZONE = 0.05f;
    private static final String ACTION_DEBUG_GAMEPAD_KEY =
            BuildConfig.APPLICATION_ID + ".DEBUG_GAMEPAD_KEY";
    private static final String EXTRA_DEBUG_GAMEPAD_KEY_CODE = "key_code";
    private static final String EXTRA_DEBUG_GAMEPAD_HOLD_MS = "hold_ms";
    private static final String EXTRA_DEBUG_GAMEPAD_ALREADY_MAPPED = "already_mapped";
    private static final String ACTION_SET_CVAR =
            BuildConfig.APPLICATION_ID + ".SET_CVAR";
    /** Title id of the game being launched, for per-game control remapping. */
    public static final String EXTRA_TITLE_ID = "jp.xenia.emulator.TITLE_ID";
    private static final String EXTRA_CVAR_NAME = "cvar_name";
    private static final String EXTRA_CVAR_VALUE = "cvar_value";
    private static final int DEBUG_GAMEPAD_DEVICE_ID = -1000;
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
    private long mFpsLastGuestSwapCount;
    private boolean mDebugGamepadReceiverRegistered;
    private final BroadcastReceiver mDebugGamepadReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(final Context context, final Intent intent) {
            if (!BuildConfig.DEBUG || intent == null) {
                return;
            }
            final String action = intent.getAction();
            if (ACTION_SET_CVAR.equals(action)) {
                final String cvarName = intent.getStringExtra(EXTRA_CVAR_NAME);
                final String cvarValue = intent.getStringExtra(EXTRA_CVAR_VALUE);
                if (cvarName != null && cvarValue != null) {
                    final boolean applied = nativeSetConfigVar(cvarName, cvarValue);
                    Log.i(TAG, "SET_CVAR " + cvarName + "=" + cvarValue
                            + (applied ? " applied" : " (unknown cvar)"));
                }
                return;
            }
            if (!ACTION_DEBUG_GAMEPAD_KEY.equals(action)) {
                return;
            }
            final int keyCode = intent.getIntExtra(
                    EXTRA_DEBUG_GAMEPAD_KEY_CODE, KeyEvent.KEYCODE_BUTTON_A);
            final boolean alreadyMapped = intent.getBooleanExtra(
                    EXTRA_DEBUG_GAMEPAD_ALREADY_MAPPED, true);
            final int mappedKeyCode = alreadyMapped
                    ? keyCode : XeniaInputMapping.mapAndroidKeyCode(
                            EmulatorActivity.this, keyCode);
            final int holdMs = Math.max(1, Math.min(2000,
                    intent.getIntExtra(EXTRA_DEBUG_GAMEPAD_HOLD_MS, 80)));
            injectDebugGamepadKey(mappedKeyCode, holdMs);
        }
    };
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

    private static native long nativeGetGuestSwapCount();

    private static native double nativeGetGuestTimeScalar();
    /** Set guest speed from the UI - the discoverable path to fast-forward. */
    private static native void nativeSetGuestTimeScalar(double scalar);
    /** Pause/resume the guest so the in-game menu is a REAL pause. */
    private static native void nativeSetEmulatorPaused(boolean paused);

    private static native boolean nativeSaveState(String path);

    private static native boolean nativeLoadState(String path);

    private static native boolean nativeSetConfigVar(String name, String value);

    // Newline-joined names of trainers actually loaded for the running title.
    private static native String nativeGetActiveTrainers();

    @Override
    protected String getWindowedAppIdentifier() {
        return "xenia";
    }

    @Override
    public void onNativeGuestCrash(final String classification, final String details) {
        XeniaAndroidSettings.recordLaunchGuestCrash(this, classification, details);
    }

    @Override
    protected void onCreate(final Bundle savedInstanceState) {
        final Intent intent = getIntent();
        if (intent != null && intent.getBundleExtra(EXTRA_CVARS) == null) {
            final Bundle launchArguments = new Bundle();
            copyStringExtra(intent, launchArguments, "target");
            copyStringExtra(intent, launchArguments, "gpu");
            copyStringExtra(intent, launchArguments, "kernel_display_resolution");
            copyStringExtra(intent, launchArguments, "cpu_hle_intercept_addrs");
            copyStringExtra(intent, launchArguments, "cpu_hle_binonce_addr");
            copyStringExtra(intent, launchArguments, "cpu_hle_tiling_replay_addr");
            copyStringExtra(intent, launchArguments, "cpu_hle_ring_writer_addr");
            copyStringExtra(intent, launchArguments, "cpu_hle_bin_once_begintiling_addr");
            copyStringExtra(intent, launchArguments, "cpu_d3d_hle_diag_draw_addr");
            copyBooleanExtra(intent, launchArguments, "cpu_d3d_hle_signatures");
            copyBooleanExtra(intent, launchArguments, "cpu_d3d_hle_diag_endtiling");
            copyIntExtra(intent, launchArguments, "cpu_watch_guest_write_page");
            copyStringExtra(intent, launchArguments, "cpu");
            copyBooleanExtra(intent, launchArguments, "cpu_backend_llvm");
            copyIntExtra(intent, launchArguments, "cpu_backend_llvm_opt");
            copyIntExtra(intent, launchArguments, "cpu_trace_resolve_caller");
            copyIntExtra(intent, launchArguments, "gpu_clamp_rt_framebuffer_height");
            copyIntExtra(intent, launchArguments, "gpu_clamp_rt_image_height");
            copyIntExtra(intent, launchArguments, "gpu_force_max_msaa_samples");
            copyStringExtra(intent, launchArguments, "cpu_backend_llvm_range_lo");
            copyStringExtra(intent, launchArguments, "cpu_backend_llvm_range_hi");
            copyBooleanExtra(intent, launchArguments, "cpu_backend_llvm_dump_ir");
            copyBooleanExtra(intent, launchArguments, "cpu_backend_llvm_dump_asm");
            copyIntExtra(intent, launchArguments, "cpu_backend_llvm_max_fns");
            copyStringExtra(intent, launchArguments, "cpu_backend_llvm_trace_addr");
            copyStringExtra(intent, launchArguments, "cpu_backend_llvm_skip_addrs");
            copyStringExtra(intent, launchArguments, "cpu_backend_llvm_skip_opcodes");
            copyBooleanExtra(intent, launchArguments, "cpu_backend_llvm_lower_vmaddfp");
            copyBooleanExtra(intent, launchArguments, "cpu_backend_llvm_context_residency");
            copyBooleanExtra(intent, launchArguments, "cpu_backend_llvm_residency_writeback");
            copyBooleanExtra(intent, launchArguments, "cpu_backend_llvm_residency_abi");
            copyBooleanExtra(intent, launchArguments, "cpu_backend_llvm_parallel_lowering");
            copyBooleanExtra(intent, launchArguments, "cpu_precompile_guest_functions");
            copyBooleanExtra(intent, launchArguments, "cpu_inline_saverest");
            copyBooleanExtra(intent, launchArguments, "cpu_aot_maximize");
            copyBooleanExtra(intent, launchArguments, "cpu_precompile_drain_frontier");
            copyBooleanExtra(intent, launchArguments, "cpu_precompile_scan_jump_tables");
            copyBooleanExtra(intent, launchArguments, "cpu_precompile_scan_pointer_tables");
            copyIntExtra(intent, launchArguments, "cpu_precompile_threads");
            copyIntExtra(intent, launchArguments, "cpu_precompile_budget_ms");
            copyStringExtra(intent, launchArguments, "cpu_perf_map_path");
            copyStringExtra(intent, launchArguments, "disassemble_function_filter");
            copyStringExtra(intent, launchArguments, "dump_shaders");
            copyStringExtra(intent, launchArguments, "apu");
            copyBooleanExtra(intent, launchArguments, "mute");
            copyIntExtra(intent, launchArguments, "log_level");
            copyIntExtra(intent, launchArguments, "user_language");
            copyBooleanExtra(intent, launchArguments, "xma_trace_context_state");
            copyBooleanExtra(intent, launchArguments, "xma_fast_silence");
            copyBooleanExtra(intent, launchArguments, "trainer_enable");
            copyBooleanExtra(intent, launchArguments, "trainer_run_entry");
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
            copyBooleanExtra(intent, launchArguments, "android_xam_keyboard_ime");
            copyBooleanExtra(intent, launchArguments, "break_on_debugbreak");
            copyBooleanExtra(intent, launchArguments, "mm_free_physical_nonbase_noop");
            copyBooleanExtra(intent, launchArguments, "guest_cpp_exception_dispatch");
            copyBooleanExtra(intent, launchArguments, "disassemble_functions");
            copyStringExtra(intent, launchArguments, "disassemble_function_filter");
            copyBooleanExtra(intent, launchArguments, "mount_cache");
            copyIntExtra(intent, launchArguments, "mmap_address_high");
            copyBooleanExtra(intent, launchArguments, "clear_memory_page_state");
            copyBooleanExtra(intent, launchArguments, "emit_inline_mmio_checks");
            copyBooleanExtra(intent, launchArguments, "arm64_use_flat_membase");
            copyBooleanExtra(intent, launchArguments, "ppc_rlwinm_shift_fastpath");
            copyBooleanExtra(intent, launchArguments, "ppc_rlwinm_mask_fastpath");
            copyBooleanExtra(intent, launchArguments, "ppc_cr_logical_self_fastpath");
            copyBooleanExtra(intent, launchArguments, "ppc_vsplt_swizzle_fastpath");
            copyBooleanExtra(intent, launchArguments, "ppc_vand_self_fastpath");
            copyBooleanExtra(intent, launchArguments, "ppc_rlwinm_general_fastpath");
            copyBooleanExtra(intent, launchArguments, "hir_known_bits_mask_fold");
            copyBooleanExtra(intent, launchArguments, "hir_const_range_fold");
            copyBooleanExtra(intent, launchArguments, "cpu_shared_function_fastpath");
            copyBooleanExtra(intent, launchArguments, "cpu_shared_function_harvest");
            copyBooleanExtra(intent, launchArguments, "cpu_lockfree_entry_lookup");
            copyBooleanExtra(intent, launchArguments, "arm64_jit_inline_extern_thunk");
            copyBooleanExtra(intent, launchArguments, "cpu_precompile_guest_functions");
            copyIntExtra(intent, launchArguments, "cpu_precompile_threads");
            copyIntExtra(intent, launchArguments, "cpu_precompile_budget_ms");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_object_cache");
            copyStringExtra(intent, launchArguments, "cpu_llvm_object_cache_path");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_object_cache_skip_lowering");
            // AOT object cache DEFAULT-ON (2026-07-08): the LLVM JIT compile is ~32%
            // of BD CPU cold (device-profiled this session); the object cache +
            // skip_lowering makes warm launches load the cached .o and skip the
            // IR-build/O2/codegen entirely (device-validated: 11034 .o written cold,
            // libLLVM 32%->~0% warm, BD renders correct). Point it at the app's
            // private files dir; an explicit intent extra still overrides. Safe: the
            // baked-host-pointer fns are nocache_, and the key = guest addr + a hash
            // of the guest code bytes (title/version/SMC-safe); a miss just compiles.
            if (!intent.hasExtra("cpu_llvm_object_cache")) {
                final java.io.File objcache =
                        new java.io.File(getFilesDir(), "objcache");
                objcache.mkdirs();
                launchArguments.putBoolean("cpu_llvm_object_cache", true);
                launchArguments.putString(
                        "cpu_llvm_object_cache_path", objcache.getAbsolutePath());
                launchArguments.putBoolean(
                        "cpu_llvm_object_cache_skip_lowering", true);
            }
            copyBooleanExtra(intent, launchArguments, "hir_algebraic_identities");
            copyBooleanExtra(intent, launchArguments, "hir_fold_and_not");
            // NOTE: permit_float_constant_evaluation is deliberately NOT
            // allowlisted. Setting it false makes constant-prop SKIP float XMM
            // folds, but the XMM backend asserts on a both-constant operand
            // (x64_op.h EmitCommutative/AssociativeBinaryXmmOp) -> a both-const
            // float ADD/MUL/etc. would crash. The engine cvar stays default-on
            // (inert, folds happen); do NOT make it settable until the XMM
            // emitters materialize BOTH constants. See port-roadmap memory.
            // Present-mode A/B (thermal vs latency): default selection prefers
            // IMMEDIATE (uncapped -> 568fps + GPU 98% + overheat on unthrottled
            // loading screens, device-observed on Lost Odyssey). Allowlist these
            // so a device A/B can force MAILBOX/FIFO and measure heat vs gameplay
            // perf before changing any default. No default behavior change.
            copyBooleanExtra(intent, launchArguments, "vulkan_allow_present_mode_immediate");
            copyBooleanExtra(intent, launchArguments, "vulkan_allow_present_mode_mailbox");
            copyBooleanExtra(intent, launchArguments, "vulkan_allow_present_mode_fifo_relaxed");
            copyBooleanExtra(intent, launchArguments, "vulkan_present_refresh_capped");
            // Save-state hooks for deterministic scene-reach A/B testing (gated,
            // default off): restore a state right after launch, and/or auto-save
            // once guest uptime hits a target. Exposes the engine save-state on
            // the Android path (no desktop F7/F8 keys).
            copyStringExtra(intent, launchArguments, "restore_state_path");
            copyStringExtra(intent, launchArguments, "save_state_path");
            copyIntExtra(intent, launchArguments, "save_state_at_guest_ms");
            copyBooleanExtra(intent, launchArguments, "gpu_hle_surface_trace");
            copyBooleanExtra(intent, launchArguments, "gpu_hle_surface_binonce");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_hle");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_renderer");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_skip_resolves");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_skip_transfers");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_copy_transfers");
            copyIntExtra(intent, launchArguments, "gpu_bd_native_rt_width");
            copyIntExtra(intent, launchArguments, "gpu_bd_native_stretch_width");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_whole_frame");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_aux_rt");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_tex_bind");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_drop_resolves");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_drop_transfers");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_drop_all_color_xfer");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_mainscene_redirect");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_depth_resolve");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_depth_xfer_census");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_drop_all_xfer");
            copyDoubleExtra(intent, launchArguments, "gpu_bd_native_depth_clear");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_drop_depth_downscale");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_depth_convert");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_depth_handoff");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_framegraph_depth");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_framegraph_depth_dump");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_framegraph_depth_shadow");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_patha_depth_snapshot");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_skip_foliage_shadows");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_diag_coverage");
            copyIntExtra(intent, launchArguments, "gpu_bd_native_aux_max_width");
            copyIntExtra(intent, launchArguments, "gpu_bd_native_color_lifetime_hle");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_field_convert");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_keep_scissor");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_field_decouple");
            copyDoubleExtra(intent, launchArguments, "gpu_bd_native_viewport_scale_x");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_force_samples1");
            copyBooleanExtra(intent, launchArguments, "vulkan_validation");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_native_hle_replace");
            copyIntExtra(intent, launchArguments, "gpu_bd_native_hle_decouple");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_hle_present_decoupled");
            copyIntExtra(intent, launchArguments, "gpu_bd_hle_drop_resolve");
            copyIntExtra(intent, launchArguments, "gpu_bd_full_native");
            copyIntExtra(intent, launchArguments, "gpu_bd_flatten_replay");
            copyBooleanExtra(intent, launchArguments, "gpu_interrupt_on_ring_idle");
            copyBooleanExtra(intent, launchArguments, "gpu_interrupt_on_swap");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_interrupts");
            copyBooleanExtra(intent, launchArguments, "gpu_log_interrupt_counts");
            copyBooleanExtra(intent, launchArguments, "gpu_watch_lo_render_gate");
            copyBooleanExtra(intent, launchArguments, "gpu_force_lo_render_latch");
            copyBooleanExtra(intent, launchArguments, "vd_hsio_training_succeeded_returns_zero");
            copyIntExtra(intent, launchArguments, "dump_guest_mem_at_ms");
            copyIntExtra(intent, launchArguments, "dump_guest_mem_base");
            copyIntExtra(intent, launchArguments, "dump_guest_mem_size_mb");
            copyStringExtra(intent, launchArguments, "dump_guest_mem_path");
            copyBooleanExtra(intent, launchArguments, "gpu_blue_dragon_kick_wait_token");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_swap");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_vd_swap");
            copyBooleanExtra(intent, launchArguments, "gpu_uma_direct_shared_memory");
            copyBooleanExtra(intent, launchArguments, "gpu_shared_memory_double_buffer");
            copyBooleanExtra(intent, launchArguments, "gpu_uma_serialize_before_write");
            copyBooleanExtra(intent, launchArguments, "gpu_uma_strong_coherency");
            copyBooleanExtra(intent, launchArguments, "gpu_adpf_performance_hints");
            copyBooleanExtra(intent, launchArguments, "gpu_adpf_thermal_throttle");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_edram_roaa");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_edram_atomic");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_shader_stats");
            copyIntExtra(intent, launchArguments, "gpu_edram_atomic_barrier_bytes");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_retro_depth_none");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_retro_color_dontcare");
            copyIntExtra(intent, launchArguments, "gpu_vulkan_retro_color_height_tolerance");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_bin_select");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_kick_lr");
            copyBooleanExtra(intent, launchArguments, "gpu_flatten_predicated_tiling");
            copyBooleanExtra(intent, launchArguments, "gpu_flatten_predicated_tiling_widen");
            copyBooleanExtra(intent, launchArguments, "gpu_depth_only_alpha_shader");
            copyBooleanExtra(intent, launchArguments, "kernel_video_widescreen");
            copyIntExtra(intent, launchArguments, "gpu_resolution_downscale_pct");
            copyBooleanExtra(intent, launchArguments, "gpu_diag_raster_ab");
            copyBooleanExtra(intent, launchArguments, "gpu_binonce_full_scissor");
            copyIntExtra(intent, launchArguments, "gpu_foliage_decimate_pct");
            copyBooleanExtra(intent, launchArguments, "gpu_diag_skip_alpha_test_draws");
            copyIntExtra(intent, launchArguments, "gpu_diag_skip_draws_min_indices");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_hybrid_postprocess");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_compute_postprocess_probe");
            copyBooleanExtra(intent, launchArguments, "kernel_object_handle_cache");
            copyBooleanExtra(intent, launchArguments, "kernel_native_object_fast_path");
            copyBooleanExtra(intent, launchArguments, "cpu_drop_redundant_atomic_release_barrier");
            copyBooleanExtra(intent, launchArguments, "ppc_cross_block_dead_flag_elim");
            copyBooleanExtra(intent, launchArguments, "ppc_cross_block_dead_flag_elim_audit");
            copyBooleanExtra(intent, launchArguments, "ppc_cross_block_dead_gpr_elim");
            copyBooleanExtra(intent, launchArguments, "ppc_cross_block_dead_gpr_elim_audit");
            copyBooleanExtra(intent, launchArguments, "ppc_cross_block_const_promotion");
            copyBooleanExtra(intent, launchArguments, "ppc_cross_block_const_promotion_audit");
            copyBooleanExtra(intent, launchArguments, "ppc_cross_block_value_promotion");
            copyBooleanExtra(intent, launchArguments, "apu_xma_skip_idle_context_lock");
            copyBooleanExtra(intent, launchArguments, "cpu_lockfree_check_global_lock");
            copyBooleanExtra(intent, launchArguments, "timer_queue_sleep_idle");
            copyIntExtra(intent, launchArguments, "timer_queue_idle_sleep_us");
            copyBooleanExtra(intent, launchArguments, "execute_unclipped_draw_vs_on_cpu");
            copyBooleanExtra(intent, launchArguments, "vulkan_sparse_shared_memory");
            copyBooleanExtra(intent, launchArguments, "vulkan_cache_texture_descriptors");
            copyBooleanExtra(intent, launchArguments, "vulkan_push_descriptors");
            copyBooleanExtra(intent, launchArguments, "vulkan_dynamic_constants_arena");
            copyBooleanExtra(intent, launchArguments, "vulkan_persistent_pipeline_cache");
            copyStringExtra(intent, launchArguments, "vulkan_pipeline_cache_path");
            copyBooleanExtra(intent, launchArguments, "vulkan_coalesce_edram_transfers");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_draw_outcomes_per_frame");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_pass_timestamps");
            copyBooleanExtra(intent, launchArguments, "gpu_binning_deinterleave_pos");
            copyBooleanExtra(intent, launchArguments, "gpu_hw_vertex_fetch");
            copyBooleanExtra(intent, launchArguments, "gpu_hw_vertex_fetch_force");
            copyBooleanExtra(intent, launchArguments, "gpu_native_render_path");
            copyBooleanExtra(intent, launchArguments, "gpu_native_render_targets");
            copyBooleanExtra(intent, launchArguments, "gpu_edram_passes_dont_care_safe");
            copyBooleanExtra(
                    intent, launchArguments, "gpu_use_vd_scaler_output_for_swap");
            copyBooleanExtra(
                    intent, launchArguments, "present_trace_guest_output_geometry");
            copyIntExtra(
                    intent, launchArguments, "present_trace_guest_output_geometry_budget");
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
            copyBooleanExtra(intent, launchArguments, "gpu_edram_passes_dont_care");
            copyBooleanExtra(intent, launchArguments, "gpu_skip_edram_transfers");
            copyBooleanExtra(intent, launchArguments, "a64_clock_spin_yield");
            copyIntExtra(intent, launchArguments, "a64_clock_spin_yield_stride");
            copyIntExtra(intent, launchArguments, "a64_clock_spin_yield_sleep_us");
            copyIntExtra(intent, launchArguments, "a64_clock_spin_yield_window_us");
            copyBooleanExtra(intent, launchArguments, "vulkan_dump_rt_full_barrier");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_dump_rt_image");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_dump_depth_image");
            copyBooleanExtra(intent, launchArguments, "spirv_no_invariant_color_output");
            copyBooleanExtra(intent, launchArguments, "spirv_no_invariant_position_output");
            copyBooleanExtra(intent, launchArguments, "gpu_fp16_shaders");
            copyBooleanExtra(intent, launchArguments, "spirv_debug_force_fullscreen_position");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_shared_memory_no_switch");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_float_constants_ssbo");
            copyBooleanExtra(intent, launchArguments, "spirv_debug_identity_ndc");
            copyBooleanExtra(intent, launchArguments, "spirv_pos_binning_passthrough");
            copyBooleanExtra(intent, launchArguments, "spirv_pos_collapse_only");
            copyBooleanExtra(intent, launchArguments, "gpu_force_tiny_draws");
            copyBooleanExtra(intent, launchArguments, "gpu_skip_bloom");
            copyBooleanExtra(intent, launchArguments, "gpu_present_fxaa");
            copyBooleanExtra(intent, launchArguments, "gpu_rt_as_texture");
            copyBooleanExtra(intent, launchArguments, "gpu_clamp_renderarea_to_scissor");
            copyIntExtra(intent, launchArguments, "gpu_max_rt_height");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_resolve_timing");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_skip_unused_depth_store");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_classify_img_sr_breaks");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_feedback_merge");
            copyBooleanExtra(intent, launchArguments, "gpu_collapse_alphatest_coverage");
            copyBooleanExtra(intent, launchArguments, "gpu_collapse_alphatest_depthonly");
            copyBooleanExtra(intent, launchArguments, "gpu_collapse_blended_coverage");
            copyBooleanExtra(intent, launchArguments, "gpu_collapse_opaque_coverage");
            copyIntExtra(intent, launchArguments, "gpu_blended_thin_factor");
            copyIntExtra(intent, launchArguments, "gpu_foliage_thin_factor");
            copyIntExtra(intent, launchArguments, "gpu_vrs_foliage_rate");
            copyBooleanExtra(intent, launchArguments, "gpu_vrs_all_draws");
            copyBooleanExtra(intent, launchArguments, "gpu_foliage_lrz_force_depth");
            copyBooleanExtra(intent, launchArguments, "gpu_foliage_lrz_feedback");
            copyBooleanExtra(intent, launchArguments, "gpu_foliage_force_early_z");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_rt_keep_ubwc");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_tex_keep_ubwc");
            copyBooleanExtra(intent, launchArguments, "gpu_fp10_color_as_unorm10");
            copyBooleanExtra(intent, launchArguments, "gpu_2101010_color_as_unorm10");
            copyBooleanExtra(intent, launchArguments, "present_frame_extrapolation");
            copyIntExtra(intent, launchArguments, "present_frame_gen_factor");
            copyBooleanExtra(intent, launchArguments, "present_frame_gen_motion_warp");
            copyBooleanExtra(intent, launchArguments, "gpu_opaque_depth_prepass");
            copyBooleanExtra(intent, launchArguments, "gpu_lrz_spike_depth_clear");
            copyIntExtra(intent, launchArguments, "gpu_fdm_foliage");
            copyIntExtra(intent, launchArguments, "draw_resolution_scale_x");
            copyIntExtra(intent, launchArguments, "draw_resolution_scale_y");
            copyBooleanExtra(intent, launchArguments, "gpu_force_no_color_write");
            copyBooleanExtra(intent, launchArguments, "vulkan_force_float_color_unorm");
            copyIntExtra(intent, launchArguments, "gpu_freeze_at_guest_ms");
            copyIntExtra(intent, launchArguments, "gpu_vrs_enable_after_guest_ms");
            copyBooleanExtra(intent, launchArguments, "gpu_freeze_ab_alternate_vrs");
            copyIntExtra(intent, launchArguments, "gpu_cull_max_per_frame");
            copyIntExtra(intent, launchArguments, "gpu_skip_draws_below_verts");
            copyIntExtra(intent, launchArguments, "gpu_merge_vf_index_stride_fix");
            copyBooleanExtra(intent, launchArguments, "vulkan_dynamic_state_cull_front");
            copyBooleanExtra(intent, launchArguments, "vulkan_dynamic_state_depth");
            copyBooleanExtra(intent, launchArguments, "vulkan_dynamic_state_stencil");
            copyBooleanExtra(intent, launchArguments, "vulkan_dynamic_state_topology");
            copyBooleanExtra(intent, launchArguments, "gpu_dynamic_blend_state");
            copyBooleanExtra(intent, launchArguments, "vulkan_merge_draws");
            // XenDroid-ported levers (2026-07-30): all default-off/neutral until
            // device-validated.
            copyIntExtra(intent, launchArguments, "vulkan_mid_frame_submission_draws");
            copyBooleanExtra(intent, launchArguments, "vulkan_fast_register_ranges");
            copyBooleanExtra(intent, launchArguments,
                    "vulkan_skip_redundant_fetch_constant_writes");
            copyBooleanExtra(intent, launchArguments, "vulkan_depth_unorm24");
            copyBooleanExtra(intent, launchArguments, "rt_cache_ownership_claim_memo");
            copyBooleanExtra(intent, launchArguments, "vulkan_direct_host_resolve");
            copyBooleanExtra(intent, launchArguments, "vulkan_cache_sampler_parameters");
            copyBooleanExtra(intent, launchArguments, "gpu_dynamic_polygon_mode");
            copyBooleanExtra(intent, launchArguments, "gpu_dynamic_depth_clamp");
            copyStringExtra(intent, launchArguments, "user_gamertag");
            copyStringExtra(intent, launchArguments, "disc_playlist");
            copyBooleanExtra(intent, launchArguments, "guest_scheduler");
            copyIntExtra(intent, launchArguments, "guest_scheduler_cpus");
            copyIntExtra(intent, launchArguments, "guest_scheduler_quantum_us");
            copyBooleanExtra(intent, launchArguments, "guest_scheduler_jit_safepoints");
            copyBooleanExtra(intent, launchArguments, "vulkan_merge_draws_rewrite");
            copyBooleanExtra(intent, launchArguments, "vulkan_merge_draws_rewrite_strips");
            copyBooleanExtra(intent, launchArguments, "gpu_vulkan_weak_external_subpass_deps");
            copyBooleanExtra(intent, launchArguments, "vulkan_merge_draws_indirect");
            copyBooleanExtra(intent, launchArguments, "vulkan_hoist_request_range_lock");
            copyBooleanExtra(intent, launchArguments, "vulkan_lazy_completion_polls");
            // Guest vblank pacing: vsync=false drops the 16ms vblank timer to
            // ~1ms - the A/B for the vsync-quantization hypothesis (every
            // title's frame interval is an exact 16.7ms multiple, B86i).
            copyBooleanExtra(intent, launchArguments, "vsync");
            copyBooleanExtra(intent, launchArguments, "vsync_on_swap");
            // Guest-wait localization probe (B86m): log blocking/high-frequency
            // kernel exports to identify what the render thread waits on
            // between swaps (the fixed ~22ms Burnout bubble).
            copyBooleanExtra(intent, launchArguments, "log_high_frequency_kernel_calls");
            copyBooleanExtra(intent, launchArguments, "vulkan_cache_vertex_residency");
            copyBooleanExtra(intent, launchArguments, "vulkan_gate_rt_update");
            copyBooleanExtra(intent, launchArguments, "gpu_trace_cullable_tris");
            copyBooleanExtra(intent, launchArguments, "gpu_cull_compaction");
            copyBooleanExtra(intent, launchArguments, "gpu_cull_replay_validate");
            copyBooleanExtra(intent, launchArguments, "gpu_cull_fast_replay");
            copyBooleanExtra(intent, launchArguments, "gpu_cull_fast_only");
            copyBooleanExtra(intent, launchArguments, "gpu_whole_draw_only");
            copyBooleanExtra(intent, launchArguments, "gpu_bd_sync_event_write_fences");
            copyBooleanExtra(intent, launchArguments, "vulkan_trace_shader_constants");
            copyStringExtra(
                    intent, launchArguments, "vulkan_trace_shader_constants_shader_filter");
            // Turnip custom-driver loading (libadrenotools). Default 'system'.
            copyStringExtra(intent, launchArguments, "gpu_vulkan_driver");
            copyStringExtra(intent, launchArguments, "gpu_vulkan_driver_path");
            copyStringExtra(intent, launchArguments, "gpu_vulkan_driver_lib");
            copyStringExtra(intent, launchArguments, "gpu_vulkan_driver_hooks_path");
            copyStringExtra(intent, launchArguments, "gpu_vulkan_driver_debug");
            copyStringExtra(intent, launchArguments, "gpu_vulkan_driver_ir3_debug");
            copyStringExtra(intent, launchArguments, "gpu_vulkan_driver_env");
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
            copyIntExtra(intent, launchArguments, "gpu_frame_limit_fps");
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
            copyIntExtra(intent, launchArguments, "spirv_debug_position_probe");
            copyIntExtra(intent, launchArguments, "thor_gpu_thread_affinity_cpu");
            copyIntExtra(intent, launchArguments, "thor_guest_thread_affinity_mask");
            copyIntExtra(intent, launchArguments, "thor_hot_thread_prime_core");
            copyIntExtra(intent, launchArguments, "thor_hot_thread_interval_ms");
            copyIntExtra(intent, launchArguments, "apu_xma_worker_poll_ms");
            copyIntExtra(intent, launchArguments, "gpu_cp_worker_nice");
            copyIntExtra(
                    intent, launchArguments, "vulkan_debug_pixel_shader_output_secondary_mode");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_interval");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_min_count");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_budget");
            copyStringExtra(intent, launchArguments, "arm64_compiled_call_trace_functions");
            copyStringExtra(intent, launchArguments, "arm64_compiled_call_trace_guest_tids");
            copyIntExtra(intent, launchArguments, "arm64_compiled_call_trace_after_ms");
            copyBooleanExtra(intent, launchArguments, "arm64_compiled_call_trace_full_regs");
            copyBooleanExtra(intent, launchArguments, "arm64_compiled_call_trace_returns");
            copyStringExtra(intent, launchArguments, "arm64_pc_operand_log_pcs");
            copyStringExtra(intent, launchArguments, "arm64_pc_operand_log_tids");
            copyIntExtra(intent, launchArguments, "arm64_pc_operand_log_budget");
            copyBooleanExtra(intent, launchArguments, "xboxkrnl_ntreadfile_force_complete");
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
            copyIntExtra(intent, launchArguments, "a64_max_stackpoints");
            // Canary-ported longjmp stack-sync retaddr disambiguation
            // (default-off; A/B on longjmp-heavy titles like Infinite
            // Undiscovery before flipping).
            copyBooleanExtra(intent, launchArguments, "a64_stack_sync_retaddr_match");
            // JIT host->guest symbol map for offline simpleperf symbolization
            // of the 43% guest-JIT cost (B86t/u). Default-off profiling aid.
            copyBooleanExtra(intent, launchArguments, "cpu_emit_jit_perf_map");
            copyBooleanExtra(intent, launchArguments, "kernel_call_log_skip_discarded");
            copyBooleanExtra(intent, launchArguments, "arm64_jit_inline_audit");
            copyBooleanExtra(intent, launchArguments, "arm64_jit_inline_leaf");
            copyBooleanExtra(intent, launchArguments, "arm64_context_promotion_gpr_crossblock");
            copyIntExtra(intent, launchArguments, "arm64_context_promotion_gpr_crossblock_mask");
            copyIntExtra(intent, launchArguments, "arm64_context_promotion_gpr_crossblock_function");
            copyBooleanExtra(intent, launchArguments, "arm64_context_promotion_gpr_crossblock_audit");
            copyBooleanExtra(intent, launchArguments, "arm64_context_promotion_gpr_crossblock_cond_branch_carry");
            copyBooleanExtra(intent, launchArguments, "arm64_register_cache_inherit");
            copyBooleanExtra(intent, launchArguments, "arm64_register_inheritance_audit");
            copyBooleanExtra(intent, launchArguments, "global_lock_owner_tracking");
            copyBooleanExtra(intent, launchArguments, "arm64_offset_memory_address_fastpath");
            copyBooleanExtra(intent, launchArguments, "arm64_vmx_dot_f32_fastpath");
            copyBooleanExtra(intent, launchArguments, "arm64_flagm_fastpath");
            copyBooleanExtra(intent, launchArguments, "arm64_permute_i32_zip_fastpath");
            // NZCV codegen units (B86p-s): compare->branch fusion + cmn for
            // negative compare immediates. Default-off, correctness-validated
            // x64+a64; allowlisted for device A/B (Burnout is now CPU-bound on
            // the guest Main XThread - B86t - so these codegen wins are its lever).
            copyBooleanExtra(intent, launchArguments, "arm64_single_compare_branch_fusion");
            copyBooleanExtra(intent, launchArguments, "arm64_cmp_negimm_cmn_fastpath");
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
            copyBooleanExtra(intent, launchArguments, "arm64_gears3_memcpy_fastpath");
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
                    intent, launchArguments, "arm64_fma_v128_fastpath");
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
                    intent,
                    launchArguments,
                    "arm64_context_promotion_gpr_livein_r1_preserve_call");
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
            copyBooleanExtra(intent, launchArguments, "thor_probe_a510_vector_units");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_target_features_native");
            copyBooleanExtra(intent, launchArguments, "a64_spin_hint_isb");
            copyIntExtra(intent, launchArguments, "kernel_spinlock_remote_spin_tries");
            copyBooleanExtra(intent, launchArguments, "kernel_spinlock_stats");
            copyBooleanExtra(intent, launchArguments, "a64_stackpoint_prolog_fastpath");
            copyBooleanExtra(intent, launchArguments, "a64_count_eor3_candidates");
            copyBooleanExtra(intent, launchArguments, "log_import_thunks");
            copyBooleanExtra(intent, launchArguments, "ppc_rlwinm_path_census");
            copyBooleanExtra(intent, launchArguments, "a64_v128_const_pool");
            copyBooleanExtra(intent, launchArguments, "a64_three_operand_shifts");
            copyBooleanExtra(intent, launchArguments, "a64_vmx_native_fmax_nan");
            copyBooleanExtra(intent, launchArguments, "a64_vmx_fp_no_operand_copy");
            copyBooleanExtra(intent, launchArguments, "a64_vmx_pressure_census");
            copyIntExtra(intent, launchArguments, "a64_spill_gprs_to_vector");
            copyBooleanExtra(intent, launchArguments, "a64_fpcr_switch_census");
            copyBooleanExtra(intent, launchArguments, "a64_fpcr_single_mode");
            copyBooleanExtra(intent, launchArguments, "a64_vmx_nan_fixup_branchless");
            copyBooleanExtra(intent, launchArguments, "a64_fold_cmp_immediates");
            // Counts eieio/sync translation sites - rule 4 before weakening
            // eieio to a store-only barrier (dmb ishst).
            copyBooleanExtra(intent, launchArguments, "cpu_ppc_barrier_census");
            // entry_delta is a64-only; this makes the guest visible under LLVM.
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_guest_entry_census");
            copyIntExtra(intent, launchArguments, "cpu_llvm_fallback_log_budget");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_callgraph_locality_census");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_lower_scalar_fma");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_vperm_tbx");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_vperm_tbl2_probe");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_vector_qload");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_lower_vsel");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_lower_is_true_v128");
            copyBooleanExtra(intent, launchArguments, "cpu_llvm_guest_call_clobber_barrier");
            copyBooleanExtra(intent, launchArguments, "cpu_backend_llvm_lower_vmaddfp");
            // 0=OS, 1=A510 littles, 2=cpu3-6. The AOT precompile is the startup
            // heat (261-340% CPU, 40C->68C) and is latency-insensitive.
            copyIntExtra(intent, launchArguments, "cpu_precompile_worker_core_policy");
            // Deschedules known guest busy-waits so the core can reach idle.
            // Semantics-preserving: the real guest body still runs.
            copyStringExtra(intent, launchArguments, "arm64_guest_spin_throttle_functions");
            copyIntExtra(intent, launchArguments, "arm64_guest_spin_throttle_stride");
            copyIntExtra(intent, launchArguments, "arm64_guest_spin_throttle_sleep_us");
            // Stops every guest signal waking every parked guest thread.
            // Default-off: a mistake here is a hang, not a wrong pixel.
            copyBooleanExtra(intent, launchArguments, "threading_per_object_condvar");
            // XenDroid 904374971 port - default-off pending the on-device check.
            copyBooleanExtra(intent, launchArguments, "vulkan_hoist_shmem_uploads");
            // Sizes whether hardware SHA-1 is worth building (rule 4: count first).
            copyBooleanExtra(intent, launchArguments, "xe_crypt_sha_census");
            copyBooleanExtra(intent, launchArguments, "arm64_offset_memory_address_fastpath");
            copyBooleanExtra(intent, launchArguments, "xam_input_trace");
            copyBooleanExtra(intent, launchArguments, "xboxkrnl_file_io_trace");
            copyIntExtra(intent, launchArguments, "xboxkrnl_file_io_trace_budget");
            copyIntExtra(intent, launchArguments, "gpu_vulkan_inpass_edram_transfers");
            copyBooleanExtra(intent, launchArguments, "xam_suppress_dirty_disc_error");
            copyBooleanExtra(intent, launchArguments, "xam_redirect_xui_font_cache");
            copyBooleanExtra(intent, launchArguments, "xam_auto_dismiss_message_boxes");
            copyBooleanExtra(intent, launchArguments, "xboxkrnl_thread_wait_trace");
            copyIntExtra(intent, launchArguments, "xboxkrnl_thread_wait_trace_budget");
            copyIntExtra(intent, launchArguments, "xboxkrnl_thread_wait_trace_after_ms");
            copyStringExtra(
                    intent, launchArguments, "xboxkrnl_thread_wait_trace_guest_tids");
            copyBooleanExtra(intent, launchArguments, "xboxkrnl_event_trace");
            copyIntExtra(intent, launchArguments, "xboxkrnl_event_trace_budget");
            copyStringExtra(intent, launchArguments, "xboxkrnl_event_trace_objects");
            copyBooleanExtra(intent, launchArguments, "xboxkrnl_physical_memory_audit");
            copyIntExtra(intent, launchArguments, "xboxkrnl_physical_memory_audit_budget");
            copyBooleanExtra(intent, launchArguments, "xboxkrnl_physical_suballocation_audit");
            copyIntExtra(intent, launchArguments, "xboxkrnl_physical_suballocation_audit_budget");
            copyBooleanExtra(intent, launchArguments, "xboxkrnl_reenter_audit");
            copyIntExtra(intent, launchArguments, "xboxkrnl_reenter_audit_budget");
            copyStringExtra(intent, launchArguments, "xboxkrnl_reenter_audit_guest_tids");
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

        // MUST run for EVERY launch, including one that already carries a cvars
        // bundle - and BEFORE super.onCreate(), which is where the native side
        // reads them. See ensureObjectCacheDefaults for why this is not inside
        // the block above.
        ensureObjectCacheDefaults(intent);

        super.onCreate(savedInstanceState);

        // Scope controller bindings to this title BEFORE the surface (and so any
        // input) exists. Null/absent = the global mapping, which is also what a
        // title with no per-game overrides falls back to action-by-action.
        XeniaInputMapping.setActiveTitleId(intent.getStringExtra(EXTRA_TITLE_ID));

        // Keep the panel awake for the whole session.
        //
        // Ported from XenDroid (4b416cd83). Gamepad input produces no touch
        // events, so Android's display timeout still fires mid-game and the
        // screen sleeps under you. That is worse here than it sounds: a sleeping
        // panel stops the activity, its SurfaceView loses the surface, and the
        // presenter then silently DROPS every guest frame while the emulator
        // keeps running - which reads as a hang and cost an entire debugging
        // session (see the black-screen note in CLAUDE.md).
        getWindow().addFlags(
                android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        setContentView(R.layout.activity_emulator);
        final WindowSurfaceView surfaceView = findViewById(R.id.emulator_surface_view);
        setWindowSurfaceView(surfaceView);
        if (surfaceView != null) {
            surfaceView.setFocusable(true);
            surfaceView.setFocusableInTouchMode(true);
            surfaceView.requestFocus();
        }
        final Bundle launchArguments = getLaunchArguments(intent);
        recordLaunchStarted(launchArguments);
        updateOsd(launchArguments);
        setupFpsOverlay(launchArguments);
        setupInGameMenu();
        registerDebugGamepadReceiver();
        // Draw the compile overlay IMMEDIATELY, before native init and before
        // anything can stall the UI thread.
        //
        // The watcher below is correct but reactive: it only shows the overlay
        // once a marker is tailed out of logcat, and by then the UI thread can
        // already be blocked in the paint path during precompile - so the post
        // never runs, nothing is drawn, and the user stares at black and then
        // gets "isn't responding". Painting it up front means the LAST thing
        // rendered before any stall is the compile screen, which is the whole
        // point of having one.
        //
        // Auto-dismissed shortly after if no AOT marker arrives, so a title
        // that does not precompile never shows a spurious overlay.
        showAotOverlay();
        mAotAutoHide.postDelayed(mAotAutoHideRunnable, 4000);
        startAotCompileWatcher();
        startUiThreadWatchdog();
    }

    // The AOT object cache turns a ~60s cold LLVM recompile into a warm load,
    // and it was silently OFF for every normal launch.
    //
    // The defaults block in onCreate is guarded by
    // `intent.getBundleExtra(EXTRA_CVARS) == null`, i.e. it only applies when
    // NOTHING supplied cvars. But LauncherActivity always attaches an
    // EXTRA_CVARS bundle when you start a game from the app, so that block -
    // and with it the object-cache defaults - was skipped on exactly the path
    // people actually play through. Headless `am start` runs, which carry no
    // bundle, DID get the cache, which is why the cache directory was full
    // while every real launch still recompiled from scratch.
    //
    // So this runs unconditionally and only fills in what is absent: an
    // explicit extra (or a GameProfile) still wins.
    private void ensureObjectCacheDefaults(final Intent intent) {
        if (intent == null) {
            return;
        }
        Bundle cvars = intent.getBundleExtra(EXTRA_CVARS);
        final boolean hadBundle = cvars != null;
        if (!hadBundle) {
            cvars = new Bundle();
        }
        if (cvars.containsKey("cpu_llvm_object_cache")
                || intent.hasExtra("cpu_llvm_object_cache")) {
            return;  // explicitly configured - leave it alone
        }
        final java.io.File objcache = new java.io.File(getFilesDir(), "objcache");
        objcache.mkdirs();
        cvars.putBoolean("cpu_llvm_object_cache", true);
        cvars.putString("cpu_llvm_object_cache_path", objcache.getAbsolutePath());
        cvars.putBoolean("cpu_llvm_object_cache_skip_lowering", true);
        intent.putExtra(EXTRA_CVARS, cvars);
    }

    // ---- AOT precompile progress overlay (RPCS3-style) -------------------
    // The native precompiler logs three markers ("load-window pre-warm on",
    // "AOT precompile progress: N / ~M functions", "pre-warmed N function(s)
    // in Xms"). This watcher tails our own process logcat for them and drives
    // a full-screen "Compiling" overlay - no JNI plumbing needed, and it only
    // ever appears when precompile actually runs.

    private android.widget.LinearLayout mAotOverlay;
    private TextView mAotProgressText;
    private android.widget.ProgressBar mAotProgressBar;
    private Thread mAotWatcherThread;
    private Process mAotWatcherProc;
    private volatile boolean mAotWatcherStop;

    // Posts to the main thread that BYPASS the Looper sync barrier.
    //
    // The watchdog caught the real cause of the missing compile overlay: while
    // "stalled", the main thread is idle in MessageQueue.nativePollOnce - not
    // blocked in native code, not holding a lock. ViewRootImpl inserts a Looper
    // SYNC BARRIER while it waits on a frame/surface, and a sync barrier blocks
    // normal messages while letting asynchronous ones through. runOnUiThread()
    // posts normal messages, so every overlay update queued behind the barrier
    // never ran - which is why the progress bar never appeared even though the
    // watcher and the markers were both working.
    //
    // Handler.createAsync marks its messages asynchronous, so they are
    // delivered regardless of the barrier. API 28+; below that, fall back to
    // flagging each Message by hand, which achieves the same thing.
    private android.os.Handler mAsyncMain;

    private android.os.Handler asyncMain() {
        if (mAsyncMain == null) {
            final android.os.Looper looper = android.os.Looper.getMainLooper();
            if (android.os.Build.VERSION.SDK_INT >= 28) {
                mAsyncMain = android.os.Handler.createAsync(looper);
            } else {
                mAsyncMain = new android.os.Handler(looper);
            }
        }
        return mAsyncMain;
    }

    private void postToUi(final Runnable r) {
        final android.os.Handler h = asyncMain();
        if (android.os.Build.VERSION.SDK_INT >= 28) {
            h.post(r);
            return;
        }
        final android.os.Message m = android.os.Message.obtain(h, r);
        m.setAsynchronous(true);
        h.sendMessage(m);
    }

    // ---- UI-thread watchdog -------------------------------------------------
    // Android shows "isn't responding" when the main thread does not service
    // input for ~5s. We keep hitting that during the AOT compile, and static
    // analysis has not pinned where main actually blocks - OnInitialize starts
    // the emulator on its own thread and returns, so it should be free.
    //
    // This settles it with evidence instead of inspection: a background thread
    // pings the main looper every second, and if a ping is not serviced within
    // 2s it logs the MAIN THREAD'S OWN STACK. Whatever native or Java frame is
    // holding it will be named. Cheap (one post per second) and permanently
    // useful, so it stays in rather than being a one-off probe.
    private Thread mUiWatchdogThread;
    private volatile boolean mUiWatchdogStop;
    private final java.util.concurrent.atomic.AtomicLong mUiWatchdogPongNs =
            new java.util.concurrent.atomic.AtomicLong(System.nanoTime());
    private final java.util.concurrent.atomic.AtomicLong mUiWatchdogAsyncPongNs =
            new java.util.concurrent.atomic.AtomicLong(System.nanoTime());

    // Per-title state file, so saving in one game cannot clobber another.
    private java.io.File getStateFile() {
        final String titleId = getIntent() != null
                ? getIntent().getStringExtra(EXTRA_TITLE_ID) : null;
        final java.io.File dir = new java.io.File(getFilesDir(), "states");
        dir.mkdirs();
        return new java.io.File(dir,
                (titleId != null && !titleId.isEmpty() ? titleId : "default") + ".sav");
    }

    private void startUiThreadWatchdog() {
        mUiWatchdogStop = false;
        final android.os.Handler main =
                new android.os.Handler(android.os.Looper.getMainLooper());
        final Thread mainThread = android.os.Looper.getMainLooper().getThread();
        mUiWatchdogThread = new Thread(() -> {
            boolean reported = false;
            while (!mUiWatchdogStop) {
                // Ping with BOTH a sync and an async message. A Looper sync
                // barrier blocks sync messages while letting async ones
                // through, so comparing the two tells us which situation we are
                // actually in: if async lands and sync does not, it is a
                // barrier and posting async is the right fix; if NEITHER lands,
                // the main thread is genuinely wedged and no amount of message
                // flagging will help.
                main.post(() -> mUiWatchdogPongNs.set(System.nanoTime()));
                postToUi(() -> mUiWatchdogAsyncPongNs.set(System.nanoTime()));
                try {
                    Thread.sleep(1000);
                } catch (final InterruptedException e) {
                    return;
                }
                final long stalledMs =
                        (System.nanoTime() - mUiWatchdogPongNs.get()) / 1000000L;
                final long asyncStalledMs =
                        (System.nanoTime() - mUiWatchdogAsyncPongNs.get()) / 1000000L;
                if (stalledMs > 2000) {
                    if (!reported) {
                        reported = true;
                        final StringBuilder sb = new StringBuilder();
                        sb.append("UI thread STALLED sync=").append(stalledMs)
                          .append("ms async=").append(asyncStalledMs)
                          .append("ms (async<sync => sync barrier, both stalled => "
                                  + "wedged) - main thread stack:");
                        for (final StackTraceElement f : mainThread.getStackTrace()) {
                            sb.append("\n    at ").append(f);
                        }
                        android.util.Log.w("xenia-uiwatchdog", sb.toString());
                    }
                } else {
                    reported = false;
                }
            }
        }, "xenia-ui-watchdog");
        mUiWatchdogThread.setDaemon(true);
        mUiWatchdogThread.start();
    }

    // Dismisses the eagerly-shown overlay when the title turns out not to
    // precompile. Cancelled by the first real AOT marker.
    private final android.os.Handler mAotAutoHide =
            new android.os.Handler(android.os.Looper.getMainLooper());
    private boolean mAotSawMarker;
    private final Runnable mAotAutoHideRunnable = () -> {
        if (!mAotSawMarker) {
            removeAotOverlay();
        }
    };

    private void startAotCompileWatcher() {
        mAotWatcherStop = false;
        mAotWatcherThread = new Thread(() -> {
            final Pattern progress = Pattern.compile(
                    "AOT precompile progress: (\\d+) / ~(\\d+) functions");
            final Pattern done = Pattern.compile(
                    "pre-warmed (\\d+) function\\(s\\) in (\\d+)ms");
            try {
                // -T 0: follow-only (a stale history line from a previous
                // session in a reused process must not drive the overlay).
                // Error stream merged so logcat can never block on a full
                // stderr pipe. The Process is held in a field: onDestroy
                // kills it, which is the ONLY way to unblock readLine().
                final ProcessBuilder pb = new ProcessBuilder(
                        "logcat", "--pid=" + android.os.Process.myPid(),
                        "-T", "0", "-s", "xenia:*");
                pb.redirectErrorStream(true);
                final Process proc = pb.start();
                mAotWatcherProc = proc;
                final BufferedReader reader = new BufferedReader(
                        new InputStreamReader(proc.getInputStream()));
                String line;
                while (!mAotWatcherStop && (line = reader.readLine()) != null) {
                    if (line.contains("load-window pre-warm on")) {
                        mAotSawMarker = true;
                        postToUi(this::showAotOverlay);
                        continue;
                    }
                    final Matcher m = progress.matcher(line);
                    if (m.find()) {
                        final int doneCount = Integer.parseInt(m.group(1));
                        final int total = Integer.parseInt(m.group(2));
                        mAotSawMarker = true;
                        postToUi(() -> updateAotOverlay(doneCount, total));
                        continue;
                    }
                    final Matcher d = done.matcher(line);
                    if (d.find()) {
                        final int total = Integer.parseInt(d.group(1));
                        final long ms = Long.parseLong(d.group(2));
                        // Do NOT stop watching: titles load multiple XEX
                        // modules and each runs its own precompile pass -
                        // hide now, re-show on the next pre-warm marker.
                        postToUi(() -> hideAotOverlay(total, ms));
                    }
                }
            } catch (final Exception ignored) {
                // Log tailing is best-effort; the game runs fine without the
                // overlay.
            } finally {
                final Process proc = mAotWatcherProc;
                if (proc != null) {
                    proc.destroy();
                }
            }
        }, "AotCompileWatcher");
        mAotWatcherThread.setDaemon(true);
        mAotWatcherThread.start();
    }

    private void showAotOverlay() {
        if (mAotOverlay != null || isFinishing() || isDestroyed()) {
            return;
        }
        final android.widget.LinearLayout overlay = new android.widget.LinearLayout(this);
        overlay.setOrientation(android.widget.LinearLayout.VERTICAL);
        overlay.setGravity(android.view.Gravity.CENTER);
        overlay.setBackgroundColor(0xF0101418);
        overlay.setClickable(true);

        final TextView title = new TextView(this);
        title.setText("Compiling game code…");
        title.setTextColor(0xFFFFFFFF);
        title.setTextSize(22);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        title.setGravity(android.view.Gravity.CENTER);
        overlay.addView(title);

        mAotProgressBar = new android.widget.ProgressBar(this, null,
                android.R.attr.progressBarStyleHorizontal);
        mAotProgressBar.setIndeterminate(false);
        mAotProgressBar.setMax(100);
        final android.widget.LinearLayout.LayoutParams barParams =
                new android.widget.LinearLayout.LayoutParams(
                        dpToPx(320), android.widget.LinearLayout.LayoutParams.WRAP_CONTENT);
        barParams.topMargin = dpToPx(18);
        barParams.gravity = android.view.Gravity.CENTER_HORIZONTAL;
        overlay.addView(mAotProgressBar, barParams);

        mAotProgressText = new TextView(this);
        mAotProgressText.setText("Starting…");
        mAotProgressText.setTextColor(0xFFB9C2CC);
        mAotProgressText.setTextSize(14);
        mAotProgressText.setGravity(android.view.Gravity.CENTER);
        final android.widget.LinearLayout.LayoutParams textParams =
                new android.widget.LinearLayout.LayoutParams(
                        android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                        android.widget.LinearLayout.LayoutParams.WRAP_CONTENT);
        textParams.topMargin = dpToPx(10);
        overlay.addView(mAotProgressText, textParams);

        final TextView note = new TextView(this);
        note.setText("Compiling the whole game ahead of time so gameplay "
                + "doesn't stutter. This runs once per launch and the game "
                + "starts automatically when it finishes." + "\n\n"
                + "Android may show \"Xenia isn't responding\" while this "
                + "runs — that is expected. Choose WAIT, not Close.");
        note.setTextColor(0xFF7C8894);
        note.setTextSize(12);
        note.setGravity(android.view.Gravity.CENTER);
        final android.widget.LinearLayout.LayoutParams noteParams =
                new android.widget.LinearLayout.LayoutParams(
                        dpToPx(340), android.widget.LinearLayout.LayoutParams.WRAP_CONTENT);
        noteParams.topMargin = dpToPx(16);
        noteParams.gravity = android.view.Gravity.CENTER_HORIZONTAL;
        overlay.addView(note, noteParams);

        addContentView(overlay, new android.view.ViewGroup.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        mAotOverlay = overlay;
    }

    // Cumulative across modules, because the native counters do NOT behave like
    // a single monotonic job: the estimated total GROWS while a module compiles
    // (6,665 -> 10,540 observed on Burnout) and both counters RESET when the
    // next XEX module starts. Feeding that straight to a progress bar makes it
    // jump backwards, which reads exactly like the hang we are trying to
    // reassure the user is not happening.
    private int mAotModuleIndex = 0;
    private int mAotLastDone = -1;
    private long mAotCumulativeDone = 0;

    private void updateAotOverlay(final int done, final int total) {
        if (mAotOverlay == null) {
            showAotOverlay();
        }
        // A drop in `done` means a new module's pass started.
        if (done < mAotLastDone) {
            mAotCumulativeDone += mAotLastDone;
            mAotModuleIndex++;
        }
        mAotLastDone = done;
        final long overall = mAotCumulativeDone + done;
        if (mAotProgressText != null) {
            mAotProgressText.setText(mAotModuleIndex == 0
                    ? String.format(Locale.US, "%,d functions compiled", overall)
                    : String.format(Locale.US, "%,d functions compiled  (module %d)",
                            overall, mAotModuleIndex + 1));
        }
        if (mAotProgressBar != null) {
            // Within-module percentage only, and never allowed to regress
            // while the estimate is still growing.
            if (total > 0) {
                final int pct = Math.min(100, (int) (done * 100L / total));
                mAotProgressBar.setProgress(Math.max(mAotProgressBar.getProgress(), pct));
            }
        }
    }

    // Tear the overlay down with no completion message - used when the title
    // turns out not to precompile at all, so the eagerly-shown overlay must
    // vanish without claiming it compiled anything.
    private void removeAotOverlay() {
        final android.widget.LinearLayout overlay = mAotOverlay;
        if (overlay == null) {
            return;
        }
        mAotOverlay = null;
        mAotProgressBar = null;
        mAotProgressText = null;
        mAotLastDone = -1;
        final android.view.ViewGroup parent =
                (android.view.ViewGroup) overlay.getParent();
        if (parent != null) {
            parent.removeView(overlay);
        }
    }

    private void hideAotOverlay(final int total, final long ms) {
        mAotAutoHide.removeCallbacks(mAotAutoHideRunnable);
        // Next module starts a fresh bar; the cumulative count keeps climbing.
        if (mAotProgressBar != null) {
            mAotProgressBar.setProgress(0);
        }
        mAotLastDone = -1;
        if (mAotOverlay != null) {
            if (mAotProgressText != null) {
                mAotProgressText.setText(String.format(Locale.US,
                        "Compiled %,d functions in %.1fs", total, ms / 1000.0));
            }
            final android.widget.LinearLayout overlay = mAotOverlay;
            mAotOverlay = null;
            mAotProgressBar = null;
            mAotProgressText = null;
            overlay.postDelayed(() -> {
                final android.view.ViewGroup parent =
                        (android.view.ViewGroup) overlay.getParent();
                if (parent != null) {
                    parent.removeView(overlay);
                }
            }, 900);
        }
    }

    private int dpToPx(final int dp) {
        return (int) (dp * getResources().getDisplayMetrics().density + 0.5f);
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
    protected void onDestroy() {
        mAotWatcherStop = true;
        if (mAotWatcherProc != null) {
            // Killing the logcat child is the only thing that unblocks the
            // watcher thread's readLine(); interrupt() cannot.
            mAotWatcherProc.destroy();
            mAotWatcherProc = null;
        }
        mAotWatcherThread = null;
        unregisterDebugGamepadReceiver();
        super.onDestroy();
    }

    @Override
    public boolean dispatchKeyEvent(final KeyEvent event) {
        // Back (= Select on the Thor) is BOTH the menu button and the modifier for
        // every gameplay hotkey, and the two live on completely different paths:
        // the menu opens HERE on KEYCODE_BACK, while the hotkeys are handled
        // natively in InputSystem::HandleHotkeys off X_INPUT. The native side
        // swallows Back from the GUEST, but it cannot suppress this Android key
        // event - so without the chord tracking below, every Back+<button> hotkey
        // would ALSO pop the menu the moment you let go of Back.
        //
        // That is not a new-feature problem: the pre-existing Select+R1
        // fast-forward has always had it. Fast-forwarding a cutscene opened the
        // pause menu, which is very plausibly what "the OSD pause is awful"
        // was describing.
        //
        // Rule: if any other gamepad button went down while Back was held, the
        // Back-up is the END OF A CHORD, not a menu request. A press of Back with
        // nothing else still toggles the menu, so nothing is lost.
        if (event != null && event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) {
                if (!mBackHeld) {
                    mBackChordUsed = false;
                }
                mBackHeld = true;
            } else if (event.getAction() == KeyEvent.ACTION_UP) {
                mBackHeld = false;
                if (mBackChordUsed) {
                    mBackChordUsed = false;
                } else {
                    toggleInGameMenu();
                }
            }
            return true;
        }
        // Note the ordering: this runs BEFORE the menu-visible branch on purpose,
        // so a chord is registered even if something else later consumes the key.
        if (event != null && mBackHeld && event.getAction() == KeyEvent.ACTION_DOWN
                && isGamepadKeyCode(event.getKeyCode())) {
            mBackChordUsed = true;
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

    private void registerDebugGamepadReceiver() {
        if (!BuildConfig.DEBUG || mDebugGamepadReceiverRegistered) {
            return;
        }
        final IntentFilter filter = new IntentFilter(ACTION_DEBUG_GAMEPAD_KEY);
        filter.addAction(ACTION_SET_CVAR);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(mDebugGamepadReceiver, filter, Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(mDebugGamepadReceiver, filter);
        }
        mDebugGamepadReceiverRegistered = true;
        Log.i(TAG, "debug-gamepad receiver registered action="
                + ACTION_DEBUG_GAMEPAD_KEY);
    }

    private void unregisterDebugGamepadReceiver() {
        if (!mDebugGamepadReceiverRegistered) {
            return;
        }
        unregisterReceiver(mDebugGamepadReceiver);
        mDebugGamepadReceiverRegistered = false;
    }

    private void injectDebugGamepadKey(final int mappedKeyCode, final int holdMs) {
        if (!isGamepadKeyCode(mappedKeyCode)) {
            Log.w(TAG, "debug-gamepad rejected non-bindable key="
                    + KeyEvent.keyCodeToString(mappedKeyCode));
            return;
        }
        updateLastInputSummary(mappedKeyCode, mappedKeyCode);
        nativeOnAndroidGamepadKey(mappedKeyCode, true, 0, DEBUG_GAMEPAD_DEVICE_ID);
        Log.i(TAG, "debug-gamepad "
                + KeyEvent.keyCodeToString(mappedKeyCode) + " down holdMs=" + holdMs);
        final View decorView = getWindow() != null ? getWindow().getDecorView() : null;
        if (decorView == null) {
            nativeOnAndroidGamepadKey(mappedKeyCode, false, 0, DEBUG_GAMEPAD_DEVICE_ID);
            Log.i(TAG, "debug-gamepad "
                    + KeyEvent.keyCodeToString(mappedKeyCode) + " up");
            return;
        }
        decorView.postDelayed(() -> {
            nativeOnAndroidGamepadKey(mappedKeyCode, false, 0, DEBUG_GAMEPAD_DEVICE_ID);
            Log.i(TAG, "debug-gamepad "
                    + KeyEvent.keyCodeToString(mappedKeyCode) + " up");
        }, holdMs);
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

    private void recordLaunchStarted(final Bundle launchArguments) {
        if (launchArguments == null) {
            return;
        }
        final String target = launchArguments.getString("target", "");
        if (target == null || target.isEmpty()) {
            return;
        }
        XeniaAndroidSettings.recordLaunchStarted(
                this, getDisplayNameForTarget(target), target);
    }

    // Hand the native hotkey layer the two things it cannot work out for itself:
    // WHERE the quick-state file lives (per-title, and the path is an Android
    // app-private directory) and WHAT multiplier the user last chose.
    //
    // Deliberately NOT done through the `--ez/--es` allowlist: that whole block
    // is gated on `getBundleExtra(EXTRA_CVARS) == null`, which is false for every
    // GUI launch, so an allowlisted extra would silently never apply on the path
    // real users take. nativeSetConfigVar runs unconditionally.
    private void applyHotkeyConfig() {
        try {
            nativeSetConfigVar("hotkey_state_path", getStateFile().getAbsolutePath());
        } catch (Throwable t) {
            Log.w(TAG, "could not set hotkey_state_path", t);
        }
        // Frame gen is remembered per-device and re-applied at launch, so the
        // in-game toggle is not lost the moment the title restarts.
        mFrameGenEnabled = getPreferences(MODE_PRIVATE).getBoolean(PREF_FRAME_GEN, false);
        if (mFrameGenEnabled) {
            try {
                nativeSetConfigVar("present_frame_extrapolation", "true");
            } catch (Throwable t) {
                Log.w(TAG, "could not restore present_frame_extrapolation", t);
            }
        }
        final float saved = getPreferences(MODE_PRIVATE).getFloat(PREF_SPEED_SCALAR, 0f);
        if (saved >= 1.25f && saved <= 8.0f) {
            try {
                nativeSetConfigVar("hotkey_speed_scalar",
                        String.format(Locale.US, "%.2f", saved));
            } catch (Throwable t) {
                Log.w(TAG, "could not restore hotkey_speed_scalar", t);
            }
        }
    }

    private void setupInGameMenu() {
        applyHotkeyConfig();
        mInGameMenu = findViewById(R.id.emulator_in_game_menu);
        mInGameMenuShowFps = findViewById(R.id.emulator_menu_show_fps);
        mInGameMenuInputStatus = findViewById(R.id.emulator_menu_input_status);
        mInGameMenuControllerHelp = findViewById(R.id.emulator_menu_controller_help);

        final Button resumeButton = findViewById(R.id.emulator_menu_resume);
        if (resumeButton != null) {
            resumeButton.setOnClickListener(view -> hideInGameMenu());
        }

        // Discoverable speed control. The Select+R1 chord already worked and the
        // FPS badge already showed "2.00x", but nothing on screen revealed it, so
        // a user who did not know the chord could not find the feature.
        //
        // The button CYCLES rather than toggles, because a toggle can only ever
        // reach one speed and the useful multiplier is title-dependent - 2x is
        // right for battles, 4x for overworld travel, 0.5x for a section you keep
        // failing. Cycling exposes all of them without a submenu.
        final Button fastForwardButton = findViewById(R.id.emulator_menu_fastforward);
        if (fastForwardButton != null) {
            fastForwardButton.setOnClickListener(view -> {
                // Read the CURRENT scalar rather than tracking a local flag, so the
                // button agrees with the hotkey, a game profile, or the config -
                // Clock stays the single source of truth.
                final double next = nextSpeedInCycle(nativeGetGuestTimeScalar());
                nativeSetGuestTimeScalar(next);
                // Keep the CHORD in sync with the button: whatever fast speed was
                // picked here becomes what Select+R1 toggles to, so the two
                // controls cannot disagree about what "fast-forward" means.
                if (next > 1.01) {
                    nativeSetConfigVar("hotkey_speed_scalar",
                            String.format(Locale.US, "%.2f", next));
                    getPreferences(MODE_PRIVATE).edit()
                            .putFloat(PREF_SPEED_SCALAR, (float) next).apply();
                }
                refreshFastForwardButton();
            });
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

        final TextView stateStatus = findViewById(R.id.emulator_menu_state_status);
        final Button saveStateButton = findViewById(R.id.emulator_menu_save_state);
        final Button loadStateButton = findViewById(R.id.emulator_menu_load_state);
        if (stateStatus != null) {
            stateStatus.setText(getStateFile().exists()
                    ? getString(R.string.emulator_menu_state_loaded_hint,
                            android.text.format.DateFormat.getTimeFormat(this)
                                    .format(new java.util.Date(getStateFile().lastModified())))
                    : getString(R.string.emulator_menu_state_none));
        }
        if (saveStateButton != null) {
            saveStateButton.setOnClickListener(v -> {
                // Synchronous and pauses the guest internally; on this hardware it
                // is well under a second, and doing it off-thread would race the
                // pause/resume the native side performs.
                final boolean ok = nativeSaveState(getStateFile().getAbsolutePath());
                if (stateStatus != null) {
                    stateStatus.setText(getString(ok
                            ? R.string.emulator_menu_state_saved
                            : R.string.emulator_menu_state_failed));
                }
            });
        }
        if (loadStateButton != null) {
            loadStateButton.setOnClickListener(v -> {
                if (!getStateFile().exists()) {
                    if (stateStatus != null) {
                        stateStatus.setText(getString(R.string.emulator_menu_state_none));
                    }
                    return;
                }
                final boolean ok = nativeLoadState(getStateFile().getAbsolutePath());
                if (stateStatus != null) {
                    stateStatus.setText(getString(ok
                            ? R.string.emulator_menu_state_loaded
                            : R.string.emulator_menu_state_failed));
                }
                if (ok) {
                    hideInGameMenu();
                }
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
        // REALLY pause the guest. Until 2026-08-09 this method only changed
        // visibility, so the game kept running - and kept burning CPU, power and
        // thermal budget - behind the menu. That is what made the overlay feel
        // cheap next to rpcs3, where opening it stops emulation.
        nativeSetEmulatorPaused(true);
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
        // Resume BEFORE hiding, so no frame is presented while the guest is still
        // suspended.
        nativeSetEmulatorPaused(false);
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
        XeniaAndroidSettings.recordLaunchExitedToMenu(this);
        final Intent launcherIntent = new Intent(this, LauncherActivity.class);
        launcherIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(launcherIntent);
        finish();
    }

    // The speeds the OSD button cycles through. 8x is deliberately NOT here even
    // though the cvar allows it: past ~4x the audio mixer cannot keep up and the
    // guest's own frame pacing dominates, so it feels broken rather than fast.
    // 0.5x is last so the common case (1x -> 2x) is one tap.
    // Back-as-hotkey-modifier tracking; see dispatchKeyEvent.
    private boolean mBackHeld = false;
    private boolean mBackChordUsed = false;

    private static final double[] SPEED_CYCLE = {1.0, 2.0, 3.0, 4.0, 0.5};
    private static final String PREF_SPEED_SCALAR = "in_game_speed_scalar";
    private static final String PREF_FRAME_GEN = "in_game_frame_gen";
    private boolean mFrameGenEnabled = false;

    private static double nextSpeedInCycle(double current) {
        // Match on approximate equality - the scalar round-trips through a double
        // cvar and a string, so exact comparison would always fall through to the
        // first entry and the cycle would never advance past 2x.
        for (int i = 0; i < SPEED_CYCLE.length; i++) {
            if (Math.abs(current - SPEED_CYCLE[i]) < 0.01) {
                return SPEED_CYCLE[(i + 1) % SPEED_CYCLE.length];
            }
        }
        // An unrecognised scalar (a title profile, a config value) is a real
        // setting, not an error - step to normal speed rather than silently
        // adopting it into the cycle.
        return 1.0;
    }

    // Label reflects the LIVE scalar, so the menu never disagrees with the badge
    // or with what the Select+R1 chord last did.
    private void refreshFastForwardButton() {
        final Button ff = findViewById(R.id.emulator_menu_fastforward);
        if (ff == null) {
            return;
        }
        final double scalar = nativeGetGuestTimeScalar();
        final double next = nextSpeedInCycle(scalar);
        final String label = Math.abs(scalar - 1.0) < 0.01
                ? "Normal speed"
                : (scalar < 1.0
                        ? String.format(Locale.US, "Slow motion %.2gx", scalar)
                        : String.format(Locale.US, "Fast-forward %.3gx", scalar));
        ff.setText(String.format(Locale.US, "Speed: %s → tap for %s", label,
                Math.abs(next - 1.0) < 0.01
                        ? "1x"
                        : String.format(Locale.US, "%.3gx", next)));
    }

    // Frame generation is a PRESENTATION feature, not a speed one, and the menu
    // says so - it raises presented smoothness on a guest locked to a fixed rate
    // (Blue Dragon is 30Hz) without making game logic run faster. Conflating the
    // two is how "frame gen" gets mistaken for emulation speed.
    //
    // The backend already existed (present_frame_extrapolation + the synth tick
    // thread in presenter.cc); it was only reachable from Settings, which needs a
    // relaunch to matter. This toggles it live, in-game, which is the only way to
    // judge whether the added latency is worth the smoothness on THIS scene.
    private void refreshFrameGenToggle() {
        final CheckBox fg = findViewById(R.id.emulator_menu_frame_gen);
        if (fg == null) {
            return;
        }
        mUpdatingMenuControls = true;
        fg.setChecked(mFrameGenEnabled);
        mUpdatingMenuControls = false;
        if (fg.getTag() == null) {
            fg.setTag(Boolean.TRUE);
            fg.setOnCheckedChangeListener((btn, checked) -> {
                if (mUpdatingMenuControls) {
                    return;
                }
                mFrameGenEnabled = checked;
                nativeSetConfigVar("present_frame_extrapolation",
                        checked ? "true" : "false");
                getPreferences(MODE_PRIVATE).edit()
                        .putBoolean(PREF_FRAME_GEN, checked).apply();
            });
        }
    }

    // A running trainer is otherwise INVISIBLE - it patches guest memory and
    // leaves no on-screen trace - so a crash or a graphical fault caused by one
    // looks exactly like an emulator bug. Naming them here is a debugging aid
    // first and a status line second.
    private void refreshTrainerNote() {
        final TextView tv = findViewById(R.id.emulator_menu_trainers);
        if (tv == null) {
            return;
        }
        String joined = "";
        try {
            joined = nativeGetActiveTrainers();
        } catch (Throwable t) {
            Log.w(TAG, "could not read active trainers", t);
        }
        if (joined == null || joined.isEmpty()) {
            tv.setText(getString(R.string.emulator_menu_trainers_none));
            return;
        }
        final String[] names = joined.split("\n");
        tv.setText(getString(R.string.emulator_menu_trainers_active,
                names.length, android.text.TextUtils.join(", ", names)));
    }

    private void refreshInGameMenu() {
        refreshFastForwardButton();
        refreshFrameGenToggle();
        refreshTrainerNote();
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
        mFpsLastGuestSwapCount = 0;
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
            mFpsLastGuestSwapCount = nativeGetGuestSwapCount();
            mFpsOverlay.setText(R.string.emulator_fps_initial);
            return;
        }
        final long elapsedNs = nowNs - mFpsWindowStartNs;
        if (elapsedNs < 500000000L) {
            return;
        }
        final long guestSwapCount = nativeGetGuestSwapCount();
        final long guestSwapDelta = Math.max(0, guestSwapCount - mFpsLastGuestSwapCount);
        final double fps = (guestSwapDelta * 1000000000.0) / elapsedNs;
        // Append the fast-forward multiplier when guest time is scaled (Back + RB).
        // Read from the native Clock rather than tracking it here, so the badge is
        // right regardless of what set the scalar.
        final double timeScalar = nativeGetGuestTimeScalar();
        if (timeScalar > 1.01) {
            mFpsOverlay.setText(String.format(Locale.US, "%.1f FPS   %.3gx", fps, timeScalar));
        } else {
            mFpsOverlay.setText(String.format(Locale.US, "%.1f FPS", fps));
        }
        mFpsWindowStartNs = nowNs;
        mFpsLastGuestSwapCount = guestSwapCount;
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
