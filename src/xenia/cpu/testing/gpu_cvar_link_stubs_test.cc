/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Host-link stubs for the cpu-tests (xenia-cpu-tests) target -- NOT a real
// test (no TEST_CASE; named *_test.cc only so the premake test_suite glob
// includes it).
//
// Commit eb45ad643 ("Add optional VdSwap/GPU presenter tracing") made the
// KERNEL (xboxkrnl_video.cc / xboxkrnl_memory.cc) reference these GPU-lib
// cvars. The cpu-tests executable links xenia-kernel but NOT xenia-gpu (the
// harness is CPU/HIR only), so those references are unresolved (LNK2001)
// whenever the kernel is recompiled -- which broke the host test harness for
// any kernel-touching change (e.g. the guest C++ exception-dispatch build).
//
// The real definitions live in src/xenia/gpu/gpu_flags.cc; these cpu-tests-only
// definitions just satisfy the kernel's externs. This is ODR-safe because the
// cpu-tests link does NOT include xenia-gpu, and the full app does NOT compile
// this file. Proper long-term fix: give the test_suite premake target an
// xenia-gpu dependency, or stop the kernel referencing gpu-lib cvars directly.

#include "xenia/base/cvar.h"

DEFINE_bool(gpu_trace_swap, false,
            "(cpu-tests link stub; real definition in gpu_flags.cc)", "GPU");
DEFINE_bool(gpu_trace_vd_swap, false,
            "(cpu-tests link stub; real definition in gpu_flags.cc)", "GPU");
DEFINE_bool(gpu_use_vd_scaler_output_for_swap, false,
            "(cpu-tests link stub; real definition in gpu_flags.cc)", "GPU");
