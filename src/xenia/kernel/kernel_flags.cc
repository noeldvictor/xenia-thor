/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/kernel_flags.h"

DEFINE_bool(headless, false,
            "Don't display any UI, using defaults for prompts as needed.",
            "UI");
DEFINE_bool(log_high_frequency_kernel_calls, false,
            "Log kernel calls with the kHighFrequency tag.", "Kernel");
DEFINE_bool(kernel_call_log_skip_discarded, true,
            "Skip formatting a kernel-call trace line when the active log level "
            "would discard it (default Info drops the Debug-level call trace). "
            "PURE optimization: device-profiled ~26% of the Blue Dragon heavy "
            "frame was spent formatting kernel-call log strings that were then "
            "immediately thrown away (PrintKernelCall formats BEFORE AppendLogLine "
            "checks ShouldLog). Default on; set false to restore the old "
            "format-then-discard behavior for A/B measurement.",
            "Kernel");
DEFINE_bool(xboxkrnl_file_io_trace, false,
            "Trace NtCreateFile, NtReadFile, NtReadFileScatter, "
            "NtQueryFullAttributesFile, and NtQueryDirectoryFile calls.",
            "Kernel");
DEFINE_int32(xboxkrnl_file_io_trace_budget, 1024,
             "Maximum xboxkrnl_file_io_trace log lines to emit; negative means "
             "unlimited, zero suppresses them.",
             "Kernel");
DEFINE_int32(xboxkrnl_file_io_status_log_budget, 256,
             "Maximum non-success file I/O status log lines to emit; negative "
             "means unlimited, zero suppresses them.",
             "Kernel");
