/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/cpu_flags.h"

DEFINE_string(cpu, "any", "CPU backend [any, x64, arm64].", "CPU");

DEFINE_string(
    load_module_map, "",
    "Loads a .map for symbol names and to diff with the generated symbol "
    "database.",
    "CPU");

DEFINE_bool(disassemble_functions, false,
            "Disassemble functions during generation.", "CPU");
DEFINE_string(
    disassemble_function_filter, "",
    "Comma/semicolon/space separated guest function addresses or inclusive "
    "ranges to disassemble during generation without enabling global "
    "disassembly.",
    "CPU");

DEFINE_bool(trace_functions, false, "Generate tracing for function statistics.",
            "CPU");
DEFINE_bool(trace_function_coverage, false,
            "Generate tracing for function instruction coverage statistics.",
            "CPU");
DEFINE_bool(trace_function_references, false,
            "Generate tracing for function address references.", "CPU");
DEFINE_bool(trace_function_data, false,
            "Generate tracing for function result data.", "CPU");

DEFINE_bool(
    disable_global_lock, false,
    "Disables global lock usage in guest code. Does not affect host code.",
    "CPU");

DEFINE_bool(validate_hir, false,
            "Perform validation checks on the HIR during compilation.", "CPU");

DEFINE_string(
    cpu_perf_map_path, "",
    "Thor JIT profiling: when non-empty, the a64 backend appends a perf-style "
    "JIT symbol map line '<host_hex_start> <host_hex_size> guest_<guest_addr>' "
    "for every emitted guest function to this file. Pairs with a simpleperf "
    "--app capture so the anonymous JIT code-cache samples resolve to named "
    "guest functions (guest_82xxxxxx) instead of 'unknown'. Default empty = "
    "disabled (zero overhead). Point it at an app-writable path, e.g. the app's "
    "external files dir.",
    "CPU");

DEFINE_uint64(
    pvr, 0x710700,
    "Processor version and revision number.\nBits 0 to 15 are the version "
    "number.\nBits 16 to 31 are the revision number.\nNote: Some XEXs (such as "
    "mfgbootlauncher.xex) may check for a value that's less than 0x710700.",
    "CPU");

// Breakpoints:
DEFINE_uint64(break_on_instruction, 0,
              "int3 before the given guest address is executed.", "CPU");
DEFINE_int32(break_condition_gpr, -1, "GPR compared to", "CPU");
DEFINE_uint64(break_condition_value, 0, "value compared against", "CPU");
DEFINE_string(break_condition_op, "eq", "comparison operator", "CPU");
DEFINE_bool(break_condition_truncate, true, "truncate value to 32-bits", "CPU");

DEFINE_bool(break_on_debugbreak, true, "int3 on JITed __debugbreak requests.",
            "CPU");

// JIT symbol map for profiling: when on, every guest function placed in the
// code cache logs "JITSYM <host_exec_addr_hex> <code_size_hex>
// <guest_addr_hex>". simpleperf reports JIT'd guest code as anonymous host
// addresses (unknown[+...]); capture these JITSYM lines in the SAME run as a
// simpleperf profile, then offline-correlate a hot host address to its guest
// PPC function (Ghidra it). Default-off (one line per compiled function).
DEFINE_bool(cpu_emit_jit_perf_map, false,
            "Log a JITSYM host->guest map line for each placed guest function "
            "(for offline simpleperf JIT symbolization). Default off.",
            "CPU");

DEFINE_bool(arm64_jit_inline_audit, false,
            "Thor JIT inlining Unit 0 (read-only): at each DIRECT guest bl, "
            "classify the target by scanning its ucode and log an INLINE-CAND "
            "line (straight-line-leaf? instruction count). Measures the inlining "
            "opportunity before the splice is built. Changes no codegen. "
            "Default off.",
            "CPU");
DEFINE_bool(arm64_jit_inline_leaf, false,
            "Thor JIT inlining (build target, NOT YET ACTIVE): inline a direct "
            "bl to a small straight-line leaf guest function into the caller, "
            "eliminating the call + its context_barrier so the loop-invariant "
            "guest loads/CR stores promote across the (removed) call boundary - "
            "the SAFE alternative to cross-barrier elision (see the cross-barrier "
            "elision wall verdict). Default off.",
            "CPU");
DEFINE_bool(cpu_precompile_guest_functions, false,
            "Precompile the module's guest functions on background threads (the "
            "Thor's otherwise-idle cores) ahead of execution, eliminating the "
            "first-encounter JIT-compile stutter / compile-induced frame drops "
            "that otherwise block the executing guest thread. The compile path is "
            "thread-safe (EntryTable spins on STATUS_COMPILING, per-function "
            "lock, concurrent translator pool), so this races the executors "
            "safely. Exploits the spare cores (the goal's '~1 of 8 cores'). "
            "Default-off experimental.",
            "CPU");
DEFINE_int32(cpu_precompile_threads, 0,
             "Background precompiler thread count when cpu_precompile_guest_"
             "functions is on (0 = auto: hardware cores - 2, leaving cores for "
             "the executing guest threads).",
             "CPU");
