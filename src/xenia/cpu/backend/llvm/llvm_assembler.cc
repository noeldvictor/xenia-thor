/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/llvm/llvm_assembler.h"

#include "xenia/base/cvar.h"
#include "xenia/base/exception_handler.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_arm64.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/llvm/llvm_backend.h"
#include "xenia/cpu/backend/llvm/llvm_jit_context.h"
#include "xenia/cpu/backend/llvm/llvm_object_cache.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/mmio_handler.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
#include "xenia/memory.h"

#ifndef XE_LLVM_BACKEND_ENABLED
#define XE_LLVM_BACKEND_ENABLED 0
#endif

#if XE_LLVM_BACKEND_ENABLED
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/Cloning.h"

// Mirror of llvm_backend.cc's tag helper - both construction sites must produce
// the SAME directory or the assembler and the JIT would disagree about where
// objects live.
static std::string LlvmLoweringStampTagAsm() {
  const char* s = xe::cpu::backend::llvm_backend::LlvmLoweringBuildStamp();
  uint32_t h = 2166136261u;
  for (const char* p = s; *p; ++p) {
    h ^= static_cast<uint8_t>(*p);
    h *= 16777619u;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%08X", h);
  return std::string(buf);
}

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

// Compile stamp of THIS translation unit - the one that owns every lowering.
// Declared in llvm_object_cache.h; see that header for why the cache directory
// is keyed on it rather than on a hand-maintained version constant.
const char* LlvmLoweringBuildStamp() { return __DATE__ " " __TIME__; }
}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe
#endif  // XE_LLVM_BACKEND_ENABLED

DEFINE_bool(
    cpu_llvm_guest_call_clobber_barrier, false,
    "Tell LLVM the TRUTH about what an a64 guest callee clobbers. "
    "xe_llvm_guest_call is a plain C call, so LLVM assumes AAPCS and keeps "
    "values live across it in x19-x28 / v8-v15 - but a64 code clobbers x22-x28 "
    "and the FULL q8-q15, and AAPCS only ever preserved the LOW 64 bits of "
    "v8-v15. A 128-bit value parked there returns with its top half destroyed: "
    "that is the vmaddfp miscompile (1,019 of 1,022 LLVM fallbacks come from "
    "disabling that lowering to dodge it, including BD hottest fn 0x824694A0). "
    "This emits an empty inline-asm barrier with the real clobber list after "
    "every guest call. UNVALIDATED: it constrains the allocator and may force "
    "extra spills - A/B it, and verify PIXELS (BD field) not just no-crash.",
    "CPU");

DEFINE_bool(
    cpu_llvm_callgraph_locality_census, false,
    "Bucket |callee - caller| for every DIRECT guest call during AOT. This is "
    "the go/no-go measurement for multi-function LLVM modules "
    "(docs/research/20260808-multi-function-llvm-modules-design.md): that design "
    "only pays if call targets are close enough to share a module, so calls "
    "become DIRECT and the inliner can finally fire. Tight distribution = "
    "clusterable, worth building. Mostly >=512K = targets scatter, no call "
    "becomes direct, and the design is DEAD for one cheap run. Aggregated into "
    "5 buckets and printed every 4096 calls - NOT one line per edge, which "
    "would be ~90k lines and evict the log.",
    "CPU");

DEFINE_bool(
    cpu_llvm_lower_scalar_fma, false,
    "Lower SCALAR f32/f64 MUL_ADD/MUL_SUB in the LLVM backend instead of "
    "falling back to a64. Recovers ~830 functions (LLVM fallbacks 1,022 -> 194, "
    "device-measured) which then also keep LLVM register residency. DEFAULT OFF "
    "since 2026-08-09: a rendering regression (guest + GPU threads running, 57C, "
    "BLACK screen = degenerate geometry) appeared in the window this landed in, "
    "and this is the only always-on float-semantics change in it. SUSPECTED "
    "CAUSE: a64 runs scalar FMA under ChangeFpcrMode(Fpu) because PPC scalar FP "
    "needs FPCR.FZ CLEAR; this lowering does not manage FPCR, so under VMX mode "
    "(FZ set) denormals flush and results differ. The qemu differential compared "
    "a C model of the SEQUENCE and never modelled the FPCR MODE, so its 32/32 "
    "PASS does not cover this. "
    "UPDATE 2026-08-09: the FPCR HALF IS NOW FIXED AT ITS SOURCE. The leak was "
    "traced by reading the emitter: the a64 epilog did NOT restore FPU mode "
    "before ret, and a64 only got away with it because its CALLERS re-establish "
    "the mode after every call (Call() -> ForgetFpcrMode()) while LLVM, which "
    "never touches FPCR, does not. So an LLVM fn calling an a64 fn that ended in "
    "VMX mode continued with FZ set. The a64 epilog now restores FPU mode when "
    "the function ever entered VMX mode, which makes 'FPCR is in FPU mode at "
    "every guest function boundary' a global invariant. NOTE the bug was never "
    "specific to this lowering - FADD/FMUL/FDIV/FSQRT were equally exposed; this "
    "lowering just put more scalar FP on the LLVM path. "
    "STILL DEFAULT OFF: the remaining gate is the PIXEL CHECK, which needs the "
    "device. Do not flip this on the strength of the FPCR fix alone.",
    "CPU");

DEFINE_bool(
    cpu_llvm_vperm_tbx, false,
    "VPERM (INT8): emit tbl1 + TBX1 instead of tbl1 + tbl1 + ORR, dropping the "
    "OR. 3 uOPs -> 2 on the FP/ASIMD pipe, which is only 2 WIDE on the A710/"
    "A715 mid cores - and LLVM is the shipping backend, so this is the path "
    "most VPERMs take (a64 emits a real two-table tbl = 1 uOP). "
    "WHY IT WORKS: the current form relies on TBL ZEROING out-of-range lanes so "
    "the OR reconstructs a two-table permute; TBX instead PRESERVES the "
    "destination out-of-range, which is the same reconstruction with the OR "
    "folded in. Lanes indexing the low table compute remap-16, which wraps as "
    "u8 to 240..255 and is therefore out of range for TBX, preserving the tbl1 "
    "result. "
    "IMPORTANTLY this keeps the SINGLE-table form, so it carries NONE of the "
    "consecutive-register-pair constraint that makes aarch64.neon.tbl2 crash "
    "our AsmPrinter (see the comment at the emit site). Same win direction, "
    "none of the allocator risk. "
    "VALIDATED DEVICE-FREE: tools/qemu/vperm_tbx_vs_tbl_or.c, 8/8 PASS "
    "including an EXHAUSTIVE 32-index x 16-lane sweep (512 cases), all "
    "bit-identical to both the current form and a C reference. "
    "DEVICE-VALIDATED FOR STABILITY 2026-08-09 (Blue Dragon, Turnip, full AOT): "
    "title reached, 0 faults / SIGTRAP / Scudo, guest threads advancing. "
    "CRITICALLY the run passed cpu_llvm_object_cache=false so the lever ACTUALLY "
    "RAN - LLVMobjload=0 with 37,722 LLVM lowerings. The objcache key "
    "(o<opt>r<res>w<wb>a<abi>) does NOT include this cvar, so a warm cache would "
    "have served pre-existing objects and the A/B would have measured nothing. "
    "STILL DEFAULT OFF: what remains is a PIXEL check, and it needs a human - "
    "screencap does not reliably capture this SurfaceView (it returned 14,881 "
    "bytes, the 'got nothing' size, on a title known to render). A wrong byte "
    "permute is wrong pixels, not a crash, so stability is not sufficient.",
    "CPU");

DEFINE_bool(
    cpu_llvm_batch_lane_calls, true,
    "vrsqrtefp / vlogefp / vexptefp: make ONE host call for the whole vector "
    "instead of FOUR (extract lane, call, insert lane, repeated 4x). That "
    "per-lane form is the 'scalarized vector operation' shape - each lane paid "
    "a full guest->host transition to do work on 32 bits. "
    "THE SAME DEFECT WAS ALREADY FIXED ON THE a64 SIDE for vrsqrtefp (an "
    "emit-time lane loop containing a blr, batched to one call) and that fix "
    "left the LLVM path untouched - LLVM being the SHIPPING backend, the "
    "version that mattered most kept paying 4x. The a64 census measured 192+ "
    "emission sites, dominated by the vector form. "
    "SEMANTICS UNCHANGED BY CONSTRUCTION: same per-lane helper, same order, "
    "in-place writeback through an entry-block alloca. vrsqrte is pure integer "
    "table math (no FPCR); log2/exp2 run in host FPCR exactly as before. "
    "DEFAULT ON: unlike a register-allocation or float-semantics change, this "
    "cannot alter results - it only changes how many times the identical "
    "function is called. Set false to A/B the call overhead.",
    "CPU");

DEFINE_bool(
    cpu_llvm_vmx_fmax_nan, false,
    "vmaxfp / vminfp in the LLVM backend: use llvm.maximum/llvm.minimum "
    "(propagate NaN, lower to FMAX/FMIN) instead of llvm.maxnum/llvm.minnum "
    "(IEEE maxNum - return the NUMBER when one operand is NaN, lower to "
    "FMAXNM). "
    "THE LLVM BACKEND STILL HAS THE EXACT BUG THE a64 BACKEND WAS FIXED FOR. "
    "PPC vmaxfp PROPAGATES NaN - PEM 3.2.5.1, verbatim: 'if the element in "
    "register vA is a NaN then the result is that NaN, else if the element in "
    "register vB is a NaN then the result is that NaN'. ARM FMAX propagates "
    "and therefore matches PPC; FMAXNM does not. That is precisely why "
    "a64_vmx_native_fmax_nan was flipped DEFAULT TRUE after "
    "tools/qemu/fmax_nan_differential.c showed ARM fmax matching PPC in all 8 "
    "cases including (QNaN1,QNaN2) and (SNaN,num). "
    "So today a64 returns the NaN and LLVM returns the number for the same "
    "guest instruction - a semantic divergence on the SHIPPING backend, with "
    "the primary sources already in-repo. "
    "DEFAULT OFF only because it changes float results and needs a pixel check; "
    "the expected direction is LLVM converging onto the validated a64 "
    "behaviour. Measure separately from cpu_llvm_vmx_float_flush - two distinct "
    "divergences that happen to live in the same lowering.",
    "CPU");

DEFINE_bool(
    cpu_llvm_vmx_float_flush, false,
    "Software-flush denormals around VMX float ADD/SUB (vaddfp / vsubfp) in the "
    "LLVM backend, matching a64. "
    "THE BUG THIS FIXES: PPC VMX flushes denormals (VSCR.NJ). a64 gets that "
    "from HARDWARE - it wraps VMX float arithmetic in EmitWithVmxFpcr, setting "
    "FPCR.FZ (a64_seq_vector.cc:254). The LLVM backend NEVER SETS FPCR, and its "
    "VECTOR_ADD/VECTOR_SUB FLOAT32 path is a bare fadd/fsub with no software "
    "flush either - so on the SHIPPING backend vaddfp/vsubfp do not flush "
    "denormals and diverge from a64. This is a live correctness divergence "
    "between our two backends on two very common VMX instructions, not a "
    "hypothetical. "
    "Note the vmaddfp lowering in this same file ALREADY software-flushes both "
    "ends (VmxFlushDenorm) and is therefore FPCR-independent and correct - so "
    "the machinery exists and add/sub simply never got it. "
    "SUSPECT FOR THE bd-llvm-postload-3d-cyan-bug: that bug is a float-semantics "
    "fault which appears when vmaddfp is lowered ALONGSIDE OTHER VECTOR OPS in "
    "one function, and unflushed denormals in colour/lighting math is exactly "
    "the shape of a wrong-colour result. NOT PROVEN - retest vmaddfp with this "
    "on. "
    "DEFAULT OFF only because it changes float results and cannot be pixel-"
    "checked without the device; it makes LLVM match the validated a64 "
    "reference, so the expected outcome is FEWER wrong pixels, not more.",
    "CPU");

DEFINE_string(
    cpu_llvm_target_cpu, "",
    "Scheduling model the LLVM JIT compiles guest code for, e.g. "
    "'cortex-a710', 'cortex-a715', 'cortex-x3'. Empty = leave whatever "
    "JITTargetMachineBuilder::detectHost() picked. "
    "WHY THIS EXISTS: detectHost() calls setCPU(sys::getHostCPUName()), and on "
    "AArch64 that is derived from /proc/cpuinfo MIDR. This SoC is big.LITTLE "
    "and cpu0-2 are Cortex-A510 LITTLE cores (thor_topology.h), so the detected "
    "CPU is very likely 'cortex-a510' - while manual review #4 deliberately "
    "moved every guest thread onto the BIG cluster (guest 0 -> X3, guest 1-5 -> "
    "cpu3-6). If so, the backend that compiles ~80% of guest code is scheduling "
    "for the wrong microarchitecture, and the two differ enormously: the A510 "
    "shares a VPU across the complex (A510 SWOG 4.8) while the X3 has FOUR "
    "FP/ASIMD pipes and the A710/A715 have two (Table 2-1). "
    "This is the JIT-side twin of the host -mtune gap already fixed in "
    "premake5.lua - and note the JIT can target cores clang 14 CANNOT: the "
    "vendored libLLVM is 20.1.8, so cortex-a715 and cortex-x3 are both valid "
    "here even though the NDK compiler rejects them. "
    "The effective CPU is logged once at init as 'LLVMtargetcpu', so ONE launch "
    "shows what detectHost actually chose. Recommended value if it reports a "
    "little core: cortex-a710, matching the host build's -mtune and the "
    "lowest-common-denominator big core. "
    "DEFAULT EMPTY (no behaviour change) pending that one log line.",
    "CPU");

// FNV-1a of cpu_llvm_target_cpu, folded into the object-cache key. target-cpu
// selects the SCHEDULING MODEL, so it changes emitted machine code and a warm
// hit under a different value would serve code scheduled for another core.
// Hashed rather than inlined because it is a free-form string.
static uint32_t LlvmTargetCpuKeyHash() {
  uint32_t h = 2166136261u;
  for (char c : cvars::cpu_llvm_target_cpu) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  return h;
}

DEFINE_bool(
    cpu_llvm_vector_qload, false,
    "Emit ONE q-load/q-store for guest VEC128 memory access instead of FOUR "
    "volatile 32-bit words plus three insertelements. "
    "WHY THIS MIGHT BE FREE: the a64 backend ALREADY emits a single q-load for "
    "the identical operation (a64_seq_memory.cc:574) and ships that way every "
    "day without crashing. The 4-word split exists in the LLVM path only, "
    "justified by a comment about the access-violation handler being unable to "
    "decode a faulting q-load - but MMIOHandler::EmulateWatchedStore, the "
    "consumer that needs the decode, is called ONLY from x64_backend.cc, and "
    "ZERO x64_ sources compile into the Android APK. The a64 watch-page handler "
    "is separately gated behind cpu_watch_guest_write_page, a default-off "
    "RE/debug tool. So the two backends disagree about a supposedly shared "
    "constraint and the permissive one is the one that ships. "
    "WHAT IT COSTS TODAY: 4 LDR + 3 lane inserts vs 1 LDR, on a FP/ASIMD pipe "
    "that is only 2 wide on the A710/A715 mid cores, plus the 3-cycle dispatch "
    "stall the A710 SWOG §4.2 charges for a quad-word source previously written "
    "as single words. On the SHIPPING backend, for every guest vector access. "
    "THE RESIDUAL RISK, stated honestly: MMIO is live on ARM64 "
    "(MMIOHandler::Install, memory.cc:249) and TryDecodeLoadStore only decodes "
    "32-bit LDR/STR, so a guest VECTOR access landing on an MMIO page would be "
    "undecodable. Whether that can happen is the open question - but it is the "
    "SAME risk a64 already takes, not a new one. "
    "DEFAULT OFF: one launch decides it. Stable -> flip the default and the 4x "
    "cost is gone for free. Hangs -> the comment is right and the side-table "
    "design in CLAUDE.md review #10 is needed after all.",
    "CPU");

DEFINE_bool(
    cpu_llvm_vperm_tbl2_probe, false,
    "DIAGNOSTIC, NOT AN OPTIMISATION - EXPECT IT TO CRASH. Emits the two-table "
    "aarch64.neon.tbl2 for VPERM, which this tree records as crashing the "
    "AsmPrinter in a wild-pointer re-fault storm. Purpose: observe the failure "
    "MODE instead of inferring it. Upstream RPCS3 hit the identical "
    "consecutive-register-pair wall on SPU SHFB and got past it by catching "
    "LLVM's compile failure and retrying that one function with the "
    "single-table form (3 fallbacks per 10,000 blocks, keeping an 8% win). "
    "Whether that ports depends on which failure we have, and the two are "
    "indistinguishable from outside: a clean report_fatal_error now appears in "
    "logcat as 'LLVMfatal: ...' via the handler installed in llvm_backend.cc, "
    "whereas memory corruption kills the process with nothing logged. Our "
    "libLLVM DOES contain the 'ran out of registers' diagnostic (verified by "
    "grepping the stripped .so), so the clean path exists in the binary - which "
    "is why this is worth exactly one launch. Never ship enabled.",
    "CPU");

DEFINE_uint32(
    cpu_llvm_fallback_log_budget, 120,
    "How many LLVMfallback lines to log before going quiet. Each line names a "
    "guest function LLVM could NOT lower and the opcode that forced it to the "
    "a64 backend, so a full-boot histogram is the list of opcodes worth "
    "implementing next - those functions lose LLVM AND its register residency. "
    "Was a hardcoded 120: a census then reports exactly 120 and LOOKS like a "
    "real count. Raise it (e.g. 100000) for a census run.",
    "CPU");

DEFINE_bool(
    cpu_llvm_guest_entry_census, false,
    "Count guest-function entries through LLVM-COMPILED code, so a CPU A/B run "
    "in the SHIPPING config (cpu_backend_llvm=true) can see the guest at all. "
    "The a64 profiler's entry_delta is emitted by A64Emitter and therefore "
    "counts ONLY a64-compiled functions - device-proven, same title and scene, "
    "one flag: LLVM on = peak 14.1M/5s, LLVM off = peak 130.6M/5s (9.3x). Every "
    "lever A/B'd with LLVM on was scored on ~11% of guest execution. Emits one "
    "relaxed atomic increment per guest function entry, so it is NOT free - "
    "measurement only, never ship it on.",
    "CPU");

DECLARE_bool(cpu_llvm_no_runtime_compiles);
DECLARE_int32(cpu_backend_llvm_opt);
DECLARE_string(cpu_backend_llvm_range_lo);
DECLARE_string(cpu_backend_llvm_range_hi);
DECLARE_bool(cpu_backend_llvm_dump_ir);
DECLARE_int32(cpu_backend_llvm_max_fns);
DECLARE_string(cpu_backend_llvm_trace_addr);
DECLARE_string(cpu_backend_llvm_skip_opcodes);
DECLARE_bool(cpu_backend_llvm_lower_vmaddfp);
DECLARE_bool(cpu_backend_llvm_dump_asm);
DECLARE_string(cpu_backend_llvm_skip_addrs);
DECLARE_bool(cpu_backend_llvm_context_residency);
DECLARE_bool(cpu_backend_llvm_residency_writeback);
DECLARE_bool(cpu_backend_llvm_residency_abi);
DECLARE_bool(cpu_llvm_object_cache);
DECLARE_string(cpu_llvm_object_cache_path);
DECLARE_bool(cpu_llvm_object_cache_skip_lowering);
DECLARE_bool(cpu_backend_llvm_parallel_lowering);


DEFINE_bool(
    cpu_llvm_target_features_native, false,
    "Tell LLVM which ARM features this CPU actually HAS, instead of only which "
    "ones to avoid. Our target-features string is otherwise entirely negative "
    "(reserve-x20/x21 plus the SVE disables), and with no positive features and "
    "no target CPU, LLVM targets generic armv8-a - so it will never emit UDOT/"
    "SDOT, EOR3/BCAX or LSE atomics even though the Thor has all of them. Only "
    "features confirmed present via HWCAP are added, and only ones with exact "
    "integer/atomic/bitwise semantics; FP16/BF16 are deliberately NOT enabled "
    "(see the standing rule that they are heuristics-only, never guest FP32). "
    "The SVE disables are always kept - executing SVE SIGILLs on this device. "
    "HELD DEFAULT-OFF 2026-08-06: a Burnout crash that same day was traced to "
    "LLVM-emitted code writing x20 - the RESERVED guest-context register - so "
    "changing LLVM's instruction selection while an LLVM codegen bug is under "
    "investigation would only confuse the bisect. Re-evaluate once the reserved-"
    "register violation is understood. "
    "Same class of miss as RPCS3's ARM feature-detection fix - they were "
    "gating FMA on the CPU NAME containing 'cortex', which silently excluded "
    "every Qualcomm core. DEFAULT ON 2026-08-06: leaving it off means every "
    "function the LLVM backend compiles targets generic armv8-a, which is the "
    "whole title on a GUI launch since opt_llvm_backend is defaultEnabled. The "
    "features added are exact-semantics only, so nothing here can change a "
    "result - it only lets LLVM pick better encodings for work it already "
    "does.",
    "CPU");

namespace {

// Built once: the per-function target-features attribute. Always carries the
// x20/x21 reservation (guest ctx/membase) and the SVE/SME disables - executing
// an SVE instruction SIGILLs on the Thor (device-confirmed 2026-06-27, an
// llvm.memset lowered to SVE). With cpu_llvm_target_features_native, appends
// the HWCAP-detected features so LLVM can use what the CPU really has.
const std::string& GetLlvmTargetFeatures() {
  static const std::string features = []() -> std::string {
    std::string out =
        "+reserve-x20,+reserve-x21,-sve,-sve2,-sve2-bitperm,-sme,-sme2";
    if (!cvars::cpu_llvm_target_features_native) {
      return out;
    }
#if XE_ARCH_ARM64
    const uint64_t flags = xe::arm64::GetFeatureFlags();
    // Exact-semantics only: integer, atomic and bitwise. Nothing here can
    // change floating-point results.
    const std::pair<uint64_t, const char*> kMap[] = {
        {xe::arm64::kA64EmitLSE, ",+lse"},
        {xe::arm64::kA64EmitLSE2, ",+lse2"},
        {xe::arm64::kA64EmitLRCPC, ",+rcpc"},
        {xe::arm64::kA64EmitDotProd, ",+dotprod"},
        {xe::arm64::kA64EmitFlagM, ",+flagm"},
        {xe::arm64::kA64EmitJSCVT, ",+jsconv"},
        {xe::arm64::kA64EmitFCMA, ",+complxnum"},
    };
    // +sha3 is the NEON win, not a crypto one: FEAT_SHA3 also brings the
    // three-input bitwise ops EOR3 and BCAX (and RAX1/XAR). LLVM fuses
    // (a^b)^c into one EOR3 and (a ^ (b & ~c)) into one BCAX, which is exactly
    // the shape VMX bitwise chains lower to - so this collapses two NEON
    // instructions into one across all vector code the LLVM backend emits,
    // with no hand-written peepholes. Pure bitwise: bit-exact by construction.
    // Gated on the same HWCAP 'sha3' the Thor reports.
    if (xe::arm64::GetFeatureFlags() & xe::arm64::kA64EmitSHA3) {
      out += ",+sha3";
    }
    for (const auto& entry : kMap) {
      if ((flags & entry.first) == entry.first) {
        out += entry.second;
      }
    }
    XELOGI("LLVMAssembler: target-features = {}", out);
#endif  // XE_ARCH_ARM64
    return out;
  }();
  return features;
}

}  // namespace

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

// Total guest-function entries through LLVM-COMPILED code.
//
// WHY THIS EXISTS (2026-08-08): the a64 profiler's entry counter is emitted by
// A64Emitter (a64_emitter.cc:4104) into a per-A64Function slot, so it counts
// ONLY a64-compiled functions. Under the shipping config (cpu_backend_llvm=true)
// the LLVM-compiled majority was invisible, and `entry_delta` reported the small
// a64 FALLBACK slice. Device-proven on one flag, same title and scene:
//     cpu_backend_llvm true  -> peak  14.1M / 5s
//     cpu_backend_llvm false -> peak 130.6M / 5s   (9.3x)
// So every CPU lever A/B'd in the shipping config was scored on ~11% of guest
// execution, which is why so many read FLAT.
//
// A per-function counter would mean moving profile_entry_count_ down to
// GuestFunction AND retyping the A64Function*-keyed speed_profile_functions_
// registry. An A/B does not need per-function attribution - it needs a complete
// TOTAL - so this is one global counter instead of that surgery.
std::atomic<uint64_t> g_llvm_guest_entry_count{0};

uint64_t LlvmGuestEntryCount() {
  return g_llvm_guest_entry_count.load(std::memory_order_relaxed);
}


using namespace xe::cpu::hir;

LLVMAssembler::LLVMAssembler(LLVMBackend* backend)
    : Assembler(backend), llvm_backend_(backend) {}

LLVMAssembler::~LLVMAssembler() = default;

bool LLVMAssembler::Initialize() {
  if (!Assembler::Initialize()) {
    return false;
  }
  // The fallback is a plain a64 assembler bound to the same backend (which IS-A
  // A64Backend). Qualified call to skip LLVMBackend's CreateAssembler override.
  fallback_ = llvm_backend_->a64::A64Backend::CreateAssembler();
  if (!fallback_ || !fallback_->Initialize()) {
    XELOGE("LLVMAssembler: failed to create a64 fallback assembler");
    return false;
  }
  return true;
}

void LLVMAssembler::Reset() { Assembler::Reset(); }

#if XE_LLVM_BACKEND_ENABLED

namespace {

// Per-function HIR -> LLVM IR lowering. Direct-context model (milestone 1):
// LOAD/STORE_CONTEXT lower to loads/stores on the real PPCContext pointer; the
// LLVM optimizer (GVN/SROA) gives cross-block residency. Returns false on any
// opcode it does not handle, so the caller falls back to a64.
class Lowerer {
 public:
  Lowerer(llvm::LLVMContext& ctx, llvm::Module* mod, llvm::Function* fn,
          uint32_t guest_addr)
      : ctx_(ctx), mod_(mod), fn_(fn), b_(ctx), guest_addr_(guest_addr) {}

  bool Run(HIRBuilder* builder);

  // True if Run() baked any run-specific host pointer into the code (CALL_EXTERN
  // target / MMIO ptrs) - the function's machine code is then NOT reusable across
  // launches and must not be persisted to the AOT object cache.
  bool baked_host_pointer() const { return baked_host_pointer_; }

 private:
  llvm::Type* T(TypeName t) {
    switch (t) {
      case INT8_TYPE:
        return llvm::Type::getInt8Ty(ctx_);
      case INT16_TYPE:
        return llvm::Type::getInt16Ty(ctx_);
      case INT32_TYPE:
        return llvm::Type::getInt32Ty(ctx_);
      case INT64_TYPE:
        return llvm::Type::getInt64Ty(ctx_);
      case FLOAT32_TYPE:
        return llvm::Type::getFloatTy(ctx_);
      case FLOAT64_TYPE:
        return llvm::Type::getDoubleTy(ctx_);
      case VEC128_TYPE:
        // 128-bit vector. <4 x i32> is the canonical carrier; lane-agnostic ops
        // (vand/vor/vxor/vnot, copies) are correct on it and lower to NEON.
        // Lane-typed arith, vsel, and vector mem+byteswap still fall back (the
        // type-mismatched cases are guarded in their handlers below).
        return llvm::VectorType::get(llvm::Type::getInt32Ty(ctx_), 4, false);
      default:
        return nullptr;
    }
  }

  bool IsVec(llvm::Value* v) { return v && v->getType()->isVectorTy(); }

  // Lane vector type for a VMX part_type; the <4 x i32> VEC128 carrier is
  // bitcast to/from this for lane-typed ops (element-wise ops match a64
  // bit-for-bit regardless of lane order, since both work the same 128 bits).
  llvm::Type* LaneVecTy(TypeName pt) {
    switch (pt) {
      case INT8_TYPE:
        return llvm::VectorType::get(llvm::Type::getInt8Ty(ctx_), 16, false);
      case INT16_TYPE:
        return llvm::VectorType::get(llvm::Type::getInt16Ty(ctx_), 8, false);
      case INT32_TYPE:
        return llvm::VectorType::get(llvm::Type::getInt32Ty(ctx_), 4, false);
      case FLOAT32_TYPE:
        return llvm::VectorType::get(llvm::Type::getFloatTy(ctx_), 4, false);
      default:
        return nullptr;
    }
  }

  // VMX float32x4 denormal flush on a <4 x i32> bit-pattern vector: per lane a
  // denormal (exp==0, mantissa!=0) becomes SIGNED zero, exactly as a64's
  // FlushDenormals_V128. Predicate: ((val<<1) - 1) <u 0x00FFFFFF; flushed value
  // = val & 0x80000000 (keep sign). Zeros (val<<1==0 -> 0xFFFFFFFF) and normals
  // are untouched. Branchless so it matches the a64 lane-for-lane.
  llvm::Value* VmxFlushDenorm(llvm::Value* iv) {
    auto* vty = iv->getType();  // <4 x i32>
    auto* one = llvm::ConstantInt::get(vty, 1);
    auto* shifted = b_.CreateShl(iv, one);
    auto* m1 = b_.CreateSub(shifted, one);
    auto* is_den =
        b_.CreateICmpULT(m1, llvm::ConstantInt::get(vty, 0x00FFFFFF));
    auto* sign = b_.CreateAnd(iv, llvm::ConstantInt::get(vty, 0x80000000));
    return b_.CreateSelect(is_den, sign, iv);
  }
  // PPC NaN propagation fixup on a <4 x i32> FP result, given the flushed source
  // bit-patterns in PRIORITY order (srcs[0] highest). Matches a64's
  // FixupVmxNan_V128*: where the result lane is NaN, take the first NaN source
  // (quieted: | 0x00400000); if no source is NaN it's a generated NaN -> the PPC
  // default 0xFFC00000. NaN predicate: (val<<1) >u 0xFF000000 (excludes +/-inf).
  llvm::Value* VmxNanFixup(llvm::Value* res,
                           std::initializer_list<llvm::Value*> srcs) {
    auto* vty = res->getType();  // <4 x i32>
    auto* one = llvm::ConstantInt::get(vty, 1);
    auto* nan_thr = llvm::ConstantInt::get(vty, 0xFF000000);
    auto* qbit = llvm::ConstantInt::get(vty, 0x00400000);
    auto is_nan = [&](llvm::Value* v) {
      return b_.CreateICmpUGT(b_.CreateShl(v, one), nan_thr);
    };
    llvm::Value* pick = llvm::ConstantInt::get(vty, 0xFFC00000);
    // Fold from lowest priority up so the highest-priority NaN source wins.
    for (auto it = std::rbegin(srcs); it != std::rend(srcs); ++it) {
      pick = b_.CreateSelect(is_nan(*it), b_.CreateOr(*it, qbit), pick);
    }
    return b_.CreateSelect(is_nan(res), pick, res);
  }

  // Apply a u32->u32 runtime helper to each of the 4 i32 lanes of a VEC128 and
  // return the rebuilt <4 x i32> carrier (vrsqrtefp/vlogefp/vexptefp). The
  // helper is a pure function (no x20/x21 dependency; AAPCS-callee-saved).
  llvm::Value* EmitVecLaneCall(const char* name, llvm::Value* vec) {
    auto* i32 = b_.getInt32Ty();
    auto* lt = LaneVecTy(INT32_TYPE);
    if (cvars::cpu_llvm_batch_lane_calls) {
      // ONE call for the whole vector instead of four.
      //
      // The old form below is the "scalarized vector operation" shape - extract
      // lane, call, insert lane, x4 - so every vrsqrtefp / vlogefp / vexptefp
      // paid FOUR guest->host transitions to do work on 32 bits at a time.
      // Exactly the defect already fixed on the a64 side for vrsqrtefp, which
      // left the SHIPPING LLVM path still paying 4x.
      //
      // Pass the vector by pointer through an ENTRY-block alloca (allocated
      // once, so a loop body does not grow the stack) and let the helper loop
      // internally. Semantics are unchanged by construction: same per-lane
      // function, same order, in-place writeback.
      std::string vname(name);
      const size_t suffix = vname.rfind("_lane");
      if (suffix != std::string::npos) {
        vname.replace(suffix, 5, "_vec");
        auto* slot = EntryAlloca(lt);
        b_.CreateStore(b_.CreateBitCast(vec, lt), slot);
        auto callee = mod_->getOrInsertFunction(
            vname, llvm::FunctionType::get(b_.getVoidTy(),
                                           {slot->getType()}, false));
        b_.CreateCall(callee, {slot});
        return b_.CreateBitCast(b_.CreateLoad(lt, slot), T(VEC128_TYPE));
      }
    }
    auto callee = mod_->getOrInsertFunction(
        name, llvm::FunctionType::get(i32, {i32}, false));
    auto* xv = b_.CreateBitCast(vec, lt);
    llvm::Value* r = llvm::PoisonValue::get(lt);
    for (int lane = 0; lane < 4; lane++) {
      auto* c = b_.CreateCall(
          callee, {b_.CreateExtractElement(xv, b_.getInt32(lane))});
      r = b_.CreateInsertElement(r, c, b_.getInt32(lane));
    }
    return b_.CreateBitCast(r, T(VEC128_TYPE));
  }

  // A scratch alloca placed in the function ENTRY block (allocated once, so it
  // doesn't grow the stack if the using instruction is in a loop). For passing a
  // VEC128 by pointer to a C runtime helper (e.g. xe_llvm_unpack).
  llvm::AllocaInst* EntryAlloca(llvm::Type* ty) {
    llvm::IRBuilder<> eb(&fn_->getEntryBlock(), fn_->getEntryBlock().begin());
    return eb.CreateAlloca(ty);
  }

  // Store a VEC128 as FOUR volatile 32-bit stores (base+0/4/8/12). Each is a
  // single decodable STR for the access-violation handler (a q-store that faults
  // on a GPU write-watch / MMIO page can't be decoded -> BD hangs); volatile
  // stops LLVM re-merging them into a q-store. Mirrors the 4-load vector LOAD.
  void StoreVec128AsWords(llvm::Value* val, llvm::Value* base) {
    if (cvars::cpu_llvm_vector_qload) {
      // ONE q-store, matching the a64 backend. See cpu_llvm_vector_qload.
      b_.CreateStore(val, base, /*isVolatile=*/true);
      return;
    }
    auto* v = b_.CreateBitCast(val, LaneVecTy(INT32_TYPE));
    for (int k = 0; k < 4; k++) {
      auto* p = b_.CreateGEP(b_.getInt8Ty(), base, b_.getInt64(4 * k));
      b_.CreateStore(b_.CreateExtractElement(v, b_.getInt32(k)), p,
                     /*isVolatile=*/true);
    }
  }

  // Emit a call to the runtime guest-call helper (resolves the target guest
  // function + invokes it). x20/x21 are AAPCS callee-saved across this C call.
  void EmitGuestCall(llvm::Value* target_i32) {
    auto* i32 = llvm::Type::getInt32Ty(ctx_);
    // xe_llvm_guest_call(ctx, target, ret_addr): resolve + invoke via the helper
    // (C dispatch + host_to_guest_thunk). ctx (x20) lets it skip the per-call
    // thread_local lookup. A non-tail INLINE-CACHE (resolve -> direct machine_code
    // call) was tried TWICE (with and without TCK_NoTail): qemu byte-correct
    // (2624 assertions) but CRASHES BD at opt=2 on device.
    // ROOT CAUSE (derived 2026-06-28 from the call ABI, the ~13% dispatch lever):
    // a guest fn is ENTERED with x0 = its GUEST return address and "returns" by
    // BRANCHING to x0 through the dispatcher (see xe_llvm_resolve_function: "x0 =
    // guest return address, the a64 guest->guest ABI") - it does NOT do a host RET.
    // host_to_guest_thunk works because it passes x0 = a sentinel HOST return stub,
    // so the callee's branch-to-x0 lands back in host code right after the call. A
    // plain LLVM CreateCall provides no such stub: the callee branches to the guest
    // ret_addr, re-dispatching the caller's continuation in a fresh frame while THIS
    // frame is abandoned -> the opt=2 crash. (Tail calls work precisely because x0 =
    // the CALLER's own guest ret addr, which already has a valid stub up the chain.)
    // ⚠️ CORRECTED 2026-07-24 (gpt-5.6-sol consult, tmp/sol_trampoline_out.txt).
    // The "abandoned frame / needs a return trampoline" story above is NOT the root
    // cause, and a blockaddress + guest->host-map registration is NOT the fix. The
    // RETURN path already works: an a64 callee recognizes guest LR == its entry x0
    // and does a HOST ret, which lands right after the host call.
    // THE REAL ROOT CAUSE IS AN ABI MISMATCH. A raw CreateCall to an a64 guest
    // entry violates that entry's register contract: the a64 backend expects x19 =
    // its backend context (LLVM does not reserve x19), and a64 code clobbers
    // x22-x28 and the full q8-q15 - whereas AAPCS only guarantees the LOW 64 bits
    // of v8-v15 are preserved. At opt=2 LLVM allocates exactly those registers for
    // values live across the call, so the callee silently destroys them: an
    // immediate fault in the a64 prologue / PushStackpoint when x19 is garbage, or
    // delayed guest-state corruption otherwise. That is the opt=2 BD crash.
    // FIX DIRECTION (do this instead): (1) reserve x19 alongside the existing
    // x20/x21 reservations, (2) call a64 guest entries through a small AAPCS
    // ADAPTER thunk emitted in the a64 runtime code cache (host_to_guest_thunk is
    // an acceptable bring-up oracle), calling raw machine code ONLY for entries
    // known to be LLVM/AAPCS, and (3) cache the resolved entry per call site as a
    // single atomic pointer to an immutable {guest_address, entry} record, revalidated
    // against the target (the self-validating pattern at llvm_backend.cc:239) -
    // never as separate atomic target/pointer fields, which can observe target A
    // with pointer B. Keep CALL_TAIL off this path in the first patch (a retained
    // adapter frame across a musttail edge grows unboundedly in a guest tail loop).
    // Until then the helper path stays. Bisection tooling if revisited:
    // cpu_backend_llvm_max_fns / _dump_ir, and _dump_asm - this bug lives in
    // REGISTER ALLOCATION, so the IR alone cannot show it.
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                        {b_.getPtrTy(), i32, i32}, false);
    auto callee = mod_->getOrInsertFunction("xe_llvm_guest_call", fty);
    auto* ret_addr = b_.CreateTrunc(
        b_.CreateLoad(b_.getInt64Ty(), next_call_ret_addr_), i32);
    if (writeback_) WriteBackCtxRegs();  // flush guest regs -> ctx for the callee
    b_.CreateCall(callee, {ctx_ptr_, target_i32, ret_addr});
    EmitGuestCallClobberBarrier();
    if (residency_) ReloadCtxRegs();  // callee may have changed guest state
  }

  // THE a64-CALLEE ABI LIE, AND THE FIX.
  //
  // xe_llvm_guest_call is declared as a PLAIN C FUNCTION, so LLVM applies
  // standard AAPCS to the call site: it believes the callee preserves x19-x28
  // and the low 64 bits of v8-v15, and it will happily keep values live across
  // the call in exactly those registers. But the a64 guest code reached through
  // that helper CLOBBERS x22-x28 and the FULL q8-q15 (llvm_assembler.cc ~:407),
  // and AAPCS64 only ever promised the BOTTOM 64 BITS of v8-v15 anyway
  // (docs/reference/arm/aapcs64-callee-saved-notes.md).
  //
  // So a 128-bit value live across a guest call can be parked in v8-v15 and come
  // back with its TOP HALF DESTROYED. That is the long-standing vmaddfp
  // miscompile: it only appears "when lowered together with other vector ops in
  // one function" because that is when register pressure pushes a vector into
  // v8-v15 in the first place - which is why the IR is byte-correct under qemu
  // and the bug lives purely in register allocation.
  //
  // An empty inline-asm barrier with the real clobber list tells LLVM the truth,
  // so it stops allocating those registers for values that must survive the call.
  // MEASURED COST: it constrains the allocator across every guest call, so it can
  // force extra spills - that is the price of correctness here, and it must be
  // A/B'd, not assumed free.
  void EmitGuestCallClobberBarrier() {
    if (!cvars::cpu_llvm_guest_call_clobber_barrier) {
      return;
    }
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_), false);
    // Full vector regs (not d8-d15) - the upper halves are the ones being lost.
    std::string clobbers =
        "~{v8},~{v9},~{v10},~{v11},~{v12},~{v13},~{v14},~{v15}"
        ",~{x22},~{x23},~{x24},~{x25},~{x26},~{x27},~{x28}";
    auto* ia = llvm::InlineAsm::get(fty, "", clobbers, /*hasSideEffects=*/true);
    b_.CreateCall(ia);
  }

  // Guest CALL_EXTERN: call the extern HANDLER (C++) via xe_llvm_call_extern,
  // NOT the guest thunk address (calling the address re-enters the `sc; bclr`
  // thunk -> infinite recursion). The symbol Function* is baked in as a constant
  // (stable across the run); the helper dispatches to handler/extern_handler.
  void EmitCallExtern(xe::cpu::Function* fn) {
    // Pass ctx (x20) so the helper derives context/thread_state from it instead
    // of a per-call thread_local ThreadState::Get() TLS lookup.
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                        {b_.getPtrTy(), b_.getPtrTy()}, false);
    auto callee = mod_->getOrInsertFunction("xe_llvm_call_extern", fty);
    auto* sym_ptr = b_.CreateIntToPtr(
        b_.getInt64(reinterpret_cast<uint64_t>(fn)), b_.getPtrTy());
    if (writeback_) WriteBackCtxRegs();  // flush guest regs -> ctx for the handler
    b_.CreateCall(callee, {ctx_ptr_, sym_ptr});
    if (residency_) ReloadCtxRegs();  // handler may have changed guest state
    baked_host_pointer_ = true;  // run-specific fn ptr -> not AOT-cacheable
  }

  // A guest TAIL call (b/bctr in tail position): resolve the target host entry
  // and `musttail`-jump to it, REUSING this frame (vs nesting a host frame per
  // call, which overflowed the host stack on a guest tail-call loop = the device
  // signal storm). Passes THIS function's return address (x0) straight through,
  // matching a64's CALL_TAIL (`ldr x0,[GUEST_RET_ADDR]; br x9`). Emits the call
  // + a `ret` terminator. Returns false if the target can't be wired for a
  // guaranteed tail call (caller falls back to a64 for this function).
  bool EmitGuestTailCall(llvm::Value* target_i32) {
    auto* i32 = llvm::Type::getInt32Ty(ctx_);
    auto* i64 = llvm::Type::getInt64Ty(ctx_);
    auto* voidTy = llvm::Type::getVoidTy(ctx_);
    // void* xe_llvm_resolve_function(void* ctx, uint32_t target) - ctx (x20) lets
    // the helper skip the per-call thread_local ThreadState lookup.
    auto* rfty =
        llvm::FunctionType::get(b_.getPtrTy(), {b_.getPtrTy(), i32}, false);
    auto resolve = mod_->getOrInsertFunction("xe_llvm_resolve_function", rfty);
    auto* host = b_.CreateCall(resolve, {ctx_ptr_, target_i32});
    // Callee ABI == this function's: void(i64 guest_return_address).
    auto* callee_ty = llvm::FunctionType::get(voidTy, {i64}, false);
    auto* my_ret = b_.CreateLoad(i64, my_ret_addr_);
    // Write-through mode: the context is already current. Write-back mode: flush
    // the alloca-only guest regs to the context so the tail-callee (and ultimately
    // the original caller) reads current values via x20. musttail permits
    // instructions BEFORE the call - only the ret must immediately follow it.
    if (writeback_) WriteBackCtxRegs();
    auto* call = b_.CreateCall(callee_ty, host, {my_ret});
    call->setTailCallKind(llvm::CallInst::TCK_MustTail);
    b_.CreateRetVoid();
    return true;
  }

  bool IsInt(TypeName t) { return t <= INT64_TYPE; }
  bool IsFloat(TypeName t) { return t == FLOAT32_TYPE || t == FLOAT64_TYPE; }

  // VMX float32x4 binary op on VEC128 operands, mirroring a64's
  // EmitVmxFpBinOp_V128 (a64_seq_util.h:851): flush denormal inputs, operate,
  // PPC NaN fixup, flush the result. Used by the V128 forms of ADD/SUB/MUL,
  // every one of which a64 implements as a FLOAT op.
  llvm::Value* VmxFpBin(llvm::Value* a, llvm::Value* c,
                        llvm::Instruction::BinaryOps fop) {
    auto* i32x4 = T(VEC128_TYPE);
    auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
    auto* ai = b_.CreateBitCast(a, i32x4);
    auto* ci = b_.CreateBitCast(c, i32x4);
    if (cvars::cpu_llvm_vmx_float_flush) {
      ai = VmxFlushDenorm(ai);
      ci = VmxFlushDenorm(ci);
    }
    auto* r = b_.CreateBinOp(fop, b_.CreateBitCast(ai, f32x4),
                             b_.CreateBitCast(ci, f32x4));
    auto* ri = VmxNanFixup(b_.CreateBitCast(r, i32x4), {ai, ci});
    if (cvars::cpu_llvm_vmx_float_flush) {
      ri = VmxFlushDenorm(ri);
    }
    return b_.CreateBitCast(ri, T(VEC128_TYPE));
  }

  // Resolve an HIR value to an LLVM value (constant materialized inline, else
  // looked up from the def map).
  llvm::Value* V(Value* v) {
    if (!v) return nullptr;
    if (v->IsConstant()) {
      switch (v->type) {
        case INT8_TYPE:
          return b_.getInt8(uint8_t(v->constant.i8));
        case INT16_TYPE:
          return b_.getInt16(uint16_t(v->constant.i16));
        case INT32_TYPE:
          return b_.getInt32(uint32_t(v->constant.i32));
        case INT64_TYPE:
          return b_.getInt64(uint64_t(v->constant.i64));
        case FLOAT32_TYPE:
          return llvm::ConstantFP::get(b_.getFloatTy(), v->constant.f32);
        case FLOAT64_TYPE:
          return llvm::ConstantFP::get(b_.getDoubleTy(), v->constant.f64);
        case VEC128_TYPE: {
          // Materialize a 128-bit constant as the <4 x i32> carrier (element k =
          // word k = vec128_t.u32[k], matching the a64 LoadV128Const byte order).
          uint32_t w[4] = {v->constant.v128.u32[0], v->constant.v128.u32[1],
                           v->constant.v128.u32[2], v->constant.v128.u32[3]};
          return llvm::ConstantDataVector::get(ctx_,
                                               llvm::ArrayRef<uint32_t>(w, 4));
        }
        default:
          return nullptr;
      }
    }
    auto it = values_.find(v->ordinal);
    return it == values_.end() ? nullptr : it->second;
  }

  void Def(Value* dest, llvm::Value* lv) { values_[dest->ordinal] = lv; }

  // Host pointer into the guest context (x20 + offset).
  llvm::Value* CtxPtr(uint64_t offset) {
    return b_.CreateGEP(b_.getInt8Ty(), ctx_ptr_, b_.getInt64(offset));
  }
  // Host pointer into guest memory (x21 + ea, ea zero-extended to 64-bit).
  // IMPORTANT: every guest LOAD/STORE through this pointer is emitted VOLATILE
  // (see OPCODE_LOAD/STORE below). Guest memory accesses can fault into xenia's
  // access-violation handler (MMIO ranges + GPU write-watches). For MMIO the
  // handler hand-decodes the faulting ARM64 instruction and only recognizes a
  // SINGLE 32-bit LDR/STR (the exact form the a64 JIT emits, mmio_handler.cc
  // TryDecodeLoadStore). LLVM's optimizer, left unconstrained, merges / pairs
  // (LDP/STP) / widens (64-bit) / vectorizes / reorders these accesses, so the
  // handler can't decode them -> the fault is never resolved -> the instruction
  // re-faults forever (the device "signal storm" = 0 fps). Volatile forbids all
  // of that, keeping each guest access a single decodable LDR/STR with its byte-
  // swap REV adjacent. This does NOT cost the residency win: context/local slots
  // (LOAD/STORE_CONTEXT, LOAD/STORE_LOCAL) stay non-volatile and are still
  // promoted to SSA registers; only true guest-memory traffic is pinned.
  llvm::Value* MemPtr(llvm::Value* ea) {
    // The guest effective address is 32-bit (Xbox 360 has a 4GB guest space;
    // PPC EAs wrap at 32 bits). CRITICAL: reduce ea to 32 bits and ZERO-extend
    // before adding membase. HIR constants for guest addresses >= 0x80000000
    // (very common - e.g. 0x82751C6C) arrive SIGN-extended to i64
    // (0xFFFFFFFF82751C6C); adding that to membase via GEP subtracts instead of
    // adds (membase - 0x7D8AE394) -> a wild host address below membase -> reads
    // garbage. Truncate-then-zext makes membase + 0x0000000082751C6C, matching
    // a64's 32-bit address computation (and correctly wraps 32-bit overflow).
    auto* ea32 = b_.CreateTrunc(b_.CreateZExtOrTrunc(ea, b_.getInt64Ty()),
                                b_.getInt32Ty());
    auto* ea64 = b_.CreateZExt(ea32, b_.getInt64Ty());
    return b_.CreateGEP(b_.getInt8Ty(), membase_, ea64);
  }
  llvm::Value* MaybeByteSwap(llvm::Value* v, llvm::Type* ty, uint16_t flags) {
    if (!(flags & LOAD_STORE_BYTE_SWAP)) return v;
    if (ty->isVectorTy()) {
      // VEC128 = rev32 (bswap each 32-bit lane).
      return b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, v);
    }
    if (ty->isFloatingPointTy()) {
      auto* it = ty->isFloatTy() ? b_.getInt32Ty() : b_.getInt64Ty();
      auto* iv = b_.CreateBitCast(v, it);
      iv = b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, iv);
      return b_.CreateBitCast(iv, ty);
    }
    if (ty->getIntegerBitWidth() <= 8) return v;
    return b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, v);
  }
  // i1 truth test of an HIR boolean/scalar value.
  llvm::Value* Truth(llvm::Value* v) {
    if (v->getType()->isFloatingPointTy()) {
      return b_.CreateFCmpUNE(v, llvm::ConstantFP::get(v->getType(), 0.0));
    }
    return b_.CreateICmpNE(v, llvm::ConstantInt::get(v->getType(), 0));
  }

  llvm::BasicBlock* BlockFor(Block* hb) { return block_map_[hb->ordinal]; }

  bool LowerInstr(Instr* i);

  llvm::LLVMContext& ctx_;
  llvm::Module* mod_;
  llvm::Function* fn_;
  llvm::IRBuilder<> b_;
  uint32_t guest_addr_ = 0;
  bool trace_this_ = false;
  // Set when this function bakes a RUN-SPECIFIC host pointer as an immediate
  // (CALL_EXTERN's target Function*, MMIO range/callback ptrs). Such code is NOT
  // portable across process launches (ASLR moves the pointer), so LowerAndJit
  // must exclude the function from the AOT object cache (a cached .o would carry
  // the previous run's address -> wild call). See baked_host_pointer().
  bool baked_host_pointer_ = false;
  void EmitTrace(uint32_t tag) {
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                        {b_.getInt32Ty()}, false);
    auto callee = mod_->getOrInsertFunction("xe_llvm_trace_entry", fty);
    b_.CreateCall(callee, {b_.getInt32(tag)});
  }
  // Guest TRAP -> call the xe_llvm_trap runtime helper with the trap-type flags.
  void EmitTrapCall(uint32_t flags) {
    auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                        {b_.getInt32Ty()}, false);
    auto callee = mod_->getOrInsertFunction("xe_llvm_trap", fty);
    b_.CreateCall(callee, {b_.getInt32(flags)});
  }
  llvm::Value* ctx_ptr_ = nullptr;
  llvm::Value* membase_ = nullptr;
  // a64 guest-call ABI: x0 = this function's guest return address (saved at
  // entry into my_ret_addr_). next_call_ret_addr_ is the return address that
  // SET_RETURN_ADDRESS stashes for the NEXT guest call. A CALL_INDIRECT whose
  // target == my_ret_addr_ is a guest `blr` RETURN, lowered to `ret` (not a
  // forward call) - otherwise the host stack never unwinds and overflows (the
  // device signal-storm root cause).
  llvm::AllocaInst* my_ret_addr_ = nullptr;
  llvm::AllocaInst* next_call_ret_addr_ = nullptr;
  std::unordered_map<uint32_t, llvm::Value*> values_;
  std::unordered_map<uint32_t, llvm::AllocaInst*> locals_;
  std::unordered_map<uint16_t, llvm::BasicBlock*> block_map_;

  // Guest-register residency (cpu_backend_llvm_context_residency): mirror
  // LOAD/STORE_CONTEXT through entry-block allocas (mem2reg -> host registers)
  // instead of ctx+offset memory, with write-back/reload at call barriers.
  bool residency_ = false;
  bool writeback_ = false;  // alloca-only stores + flush at barriers (RPCS3-class)
  bool abi_residency_ = false;  // skip reloading ABI callee-saved regs after calls

  // Xbox 360 PPC ABI callee-saved (non-volatile) context offsets. Matched to the
  // register SAVE/RESTORE helpers the toolchain emits (xex_module FindSaveRest):
  //   GPR r14-r31         (__savegprlr_14),
  //   FPR f14-f31         (__savefpr_14),
  //   VMX v14-v31         (__savevmx_14) AND v64-v127 (__savevmx_64, VMX128).
  // A callee that modifies any of these saves+restores it (via those helpers), so
  // after a guest call the pre-call alloca mirrors are still correct - reloading
  // them is redundant AND forces LLVM to spill them across the call (the residency
  // trap). Skipping the reload keeps them in host callee-saved registers across
  // the call = cross-function residency. NOT the standard AltiVec v20-v31: the
  // 360's VMX128 non-volatile set is wider (v14-v31 + v64-v127).
  static bool IsAbiNonVolatileOffset(uint64_t offset) {
    using Ctx = xe::cpu::ppc::PPCContext;
    constexpr uint64_t kR = offsetof(Ctx, r);   // uint64_t r[32]
    constexpr uint64_t kF = offsetof(Ctx, f);   // double   f[32]
    constexpr uint64_t kV = offsetof(Ctx, v);   // vec128_t v[128]
    if (offset >= kR + 14 * 8 && offset < kR + 32 * 8) return true;    // r14-r31
    if (offset >= kF + 14 * 8 && offset < kF + 32 * 8) return true;    // f14-f31
    if (offset >= kV + 14 * 16 && offset < kV + 32 * 16) return true;  // v14-v31
    if (offset >= kV + 64 * 16 && offset < kV + 128 * 16) return true;  // v64-v127
    return false;
  }
  std::unordered_map<uint64_t, llvm::AllocaInst*> ctx_regs_;  // offset -> alloca
  std::unordered_map<uint64_t, llvm::Type*> ctx_reg_ty_;      // offset -> type
  std::unordered_set<uint64_t> ctx_stored_;    // offsets written via alloca-only
  std::unordered_set<uint64_t> ctx_poisoned_;  // mixed-type: kept write-through

  // Get (lazily create) the alloca mirroring guest-context offset `offset`. The
  // alloca + its init-load-from-ctx are inserted right after the entry x20/x21
  // setup (so ctx_ptr_ dominates the init, and the alloca is in the entry block
  // for mem2reg). Returns nullptr if a prior access used a different type (the
  // offset then stays on the direct-ctx path - byte-exact, just not promoted).
  llvm::AllocaInst* GetCtxReg(uint64_t offset, llvm::Type* ty) {
    auto it = ctx_regs_.find(offset);
    if (it != ctx_regs_.end()) {
      return ctx_reg_ty_[offset] == ty ? it->second : nullptr;
    }
    auto* anchor = llvm::cast<llvm::Instruction>(membase_)->getNextNode();
    llvm::IRBuilder<> eb(anchor);
    auto* slot = eb.CreateAlloca(ty, nullptr, "gr");
    auto* p = eb.CreateGEP(eb.getInt8Ty(), ctx_ptr_, eb.getInt64(offset));
    eb.CreateStore(eb.CreateLoad(ty, p), slot);
    ctx_regs_[offset] = slot;
    ctx_reg_ty_[offset] = ty;
    return slot;
  }
  // Reload all mirrored regs from the context (after a call: the callee may have
  // changed guest state). The mirrors then match the post-call context.
  void ReloadCtxRegs() {
    // Compile-time instrumentation: tally how many ctx-reload instructions the
    // ABI-aware residency ELIMINATES vs. the total it would otherwise emit at
    // call sites, so a run through the LLVM backend (device / qemu) quantifies
    // the lever's static code reduction precisely instead of estimating it.
    static std::atomic<uint64_t> s_skipped{0};
    static std::atomic<uint64_t> s_total{0};
    uint64_t local_skip = 0, local_tot = 0;
    for (auto& kv : ctx_regs_) {
      ++local_tot;
      // ABI-aware residency: the callee preserves non-volatile regs, so their
      // mirrors are already correct - skip the reload to keep them resident in
      // host callee-saved registers across the call.
      if (abi_residency_ && IsAbiNonVolatileOffset(kv.first)) {
        ++local_skip;
        continue;
      }
      auto* ty = ctx_reg_ty_[kv.first];
      b_.CreateStore(b_.CreateLoad(ty, CtxPtr(kv.first)), kv.second);
    }
    if (abi_residency_ && local_tot) {
      uint64_t sk = s_skipped.fetch_add(local_skip, std::memory_order_relaxed) +
                    local_skip;
      uint64_t tt =
          s_total.fetch_add(local_tot, std::memory_order_relaxed) + local_tot;
      // Log on each ~8192-reload-site boundary so the cumulative reduction is
      // visible without per-call-site spam.
      if ((tt & 0x1FFF) < local_tot) {
        XELOGI(
            "residency_abi: eliminated {} / {} ctx-reload instructions at call "
            "sites ({:.1f}% of guest-reg reloads removed)",
            sk, tt, tt ? (100.0 * double(sk) / double(tt)) : 0.0);
      }
    }
  }
  // Flush alloca-only-managed guest regs back to the context (before a call/tail/
  // return so the callee/caller reading via x20 sees current values). Only
  // ctx_stored_ (clean single-type offsets); poisoned offsets stay write-through.
  void WriteBackCtxRegs() {
    for (uint64_t offset : ctx_stored_) {
      auto it = ctx_regs_.find(offset);
      if (it == ctx_regs_.end()) continue;
      auto* ty = ctx_reg_ty_[offset];
      b_.CreateStore(b_.CreateLoad(ty, it->second), CtxPtr(offset));
    }
  }
  // A guest RETURN (host RetVoid): flush the alloca-only guest regs to the context
  // first so the caller reads current values via x20. Use for every real return
  // EXCEPT the musttail return (musttail forbids instructions between call + ret).
  void EmitReturn() {
    if (writeback_) WriteBackCtxRegs();
    b_.CreateRetVoid();
  }
};

// Reads a reserved AArch64 register (x20=ctx, x21=membase) set up by the
// host->guest thunk. Matches the a64 ABI so the same thunk + a64<->LLVM calls
// all interoperate.
static llvm::Value* ReadReg(llvm::IRBuilder<>& b, llvm::Module* mod,
                            const char* name) {
  auto& ctx = b.getContext();
  auto* i64 = llvm::Type::getInt64Ty(ctx);
  auto* decl = llvm::Intrinsic::getOrInsertDeclaration(
      mod, llvm::Intrinsic::read_register, {i64});
  auto* md = llvm::MDNode::get(ctx, {llvm::MDString::get(ctx, name)});
  return b.CreateCall(decl, {llvm::MetadataAsValue::get(ctx, md)});
}

bool Lowerer::Run(HIRBuilder* builder) {
  auto* entry = llvm::BasicBlock::Create(ctx_, "entry", fn_);
  b_.SetInsertPoint(entry);
  if (cvars::cpu_llvm_guest_entry_census) {
    // Relaxed: a monotonic counter read from another thread needs no ordering,
    // and this is on EVERY guest function entry - the hottest path there is.
    auto* i64 = llvm::Type::getInt64Ty(ctx_);
    auto* slot = b_.CreateIntToPtr(
        llvm::ConstantInt::get(i64,
                               reinterpret_cast<uint64_t>(&g_llvm_guest_entry_count)),
        b_.getPtrTy());
    b_.CreateAtomicRMW(llvm::AtomicRMWInst::Add, slot,
                       llvm::ConstantInt::get(i64, 1), llvm::MaybeAlign(8),
                       llvm::AtomicOrdering::Monotonic);
  }
  ctx_ptr_ = b_.CreateIntToPtr(ReadReg(b_, mod_, "x20"), b_.getPtrTy(), "ctx");
  membase_ =
      b_.CreateIntToPtr(ReadReg(b_, mod_, "x21"), b_.getPtrTy(), "membase");
  // Guest-register residency. The allocas are correct at any opt level (mem2reg
  // at opt>0 lifts them to registers; at opt=0 they are correct-but-unpromoted
  // memory) - so the qemu differential validates the LOGIC at opt=0. GetCtxReg
  // anchors its allocas after membase_, so both x20/x21 are set first.
  residency_ = cvars::cpu_backend_llvm_context_residency;
  writeback_ = residency_ && cvars::cpu_backend_llvm_residency_writeback;
  // ABI-aware cross-function residency requires write-back (the mirrors must be
  // the authoritative copy that the callee preserves via the context).
  abi_residency_ = writeback_ && cvars::cpu_backend_llvm_residency_abi;

  // Save the incoming guest return address (x0, per the a64 host->guest thunk:
  // "mov x0, x2 // x0 = guest return address") and init the next-call slot.
  // Entry-block allocas are promoted to SSA by mem2reg at opt>0.
  my_ret_addr_ = b_.CreateAlloca(b_.getInt64Ty(), nullptr, "my_ret_addr");
  next_call_ret_addr_ =
      b_.CreateAlloca(b_.getInt64Ty(), nullptr, "next_call_ret_addr");
  // Return-address basis for recognizing this function's OWN blr RETURN.
  // HYBRID (matches a64's GUEST_RET_ADDR slot): use x0 when non-zero, else the
  // guest LR at entry.
  //  - bl-entered: the host->guest thunk passes x0 = the guest return address
  //    (== context->lr); both agree.
  //  - TAIL-call-entered (musttail, e.g. __savegprlr/__restgprlr): the caller
  //    passes x0 = the ORIGINAL return address, but context->lr is the tail-
  //    caller's last set value (stale) - AND __restgprlr RESTORES context->lr
  //    from the stack before its blr, so a context->lr basis never matches the
  //    restored target -> the blr became a forward call -> the wrapper that
  //    tail-returns through __restgprlr recursed forever (device-pinned: callee
  //    returned correct values but never EXITed; r28-r31 working, no unwind).
  //  - bctr-entered (indirect jump, not a call): x0 == 0 -> fall back to
  //    context->lr (the original reason this was context->lr, not x0).
  {
    auto* x0 = fn_->getArg(0);
    auto* ctx_lr = b_.CreateLoad(
        b_.getInt64Ty(), CtxPtr(offsetof(xe::cpu::ppc::PPCContext, lr)));
    auto* use_x0 = b_.CreateICmpNE(x0, b_.getInt64(0));
    b_.CreateStore(b_.CreateSelect(use_x0, x0, ctx_lr), my_ret_addr_);
  }
  b_.CreateStore(b_.getInt64(0), next_call_ret_addr_);

  // Entry trace (diagnostic): if this fn matches cpu_backend_llvm_trace_addr,
  // call xe_llvm_trace_entry(addr) to log its input regs - fires for ANY caller
  // backend (unlike the call-site trace), so a callee's args can be diffed
  // across a caller being a64 vs LLVM.
  {
    const std::string& ts = cvars::cpu_backend_llvm_trace_addr;
    uint32_t ta = ts.empty() ? 0 : uint32_t(std::strtoull(ts.c_str(), nullptr, 16));
    trace_this_ = (ta != 0 && ta == guest_addr_);
    if (trace_this_) EmitTrace(guest_addr_);  // entry: tag = fn addr
  }

  // Residency: pre-create ALL guest-register allocas up front (eager) so the
  // reload-after-call refresh covers every reg - including ones FIRST used after
  // a call. A lazily-created alloca would have missed the earlier reloads and
  // kept its stale entry-block init value (device: BD hung on a wrong post-call
  // register). The entry init-load (in GetCtxReg) dominates all uses; the
  // reload-at-call then keeps every mirror in sync with the context.
  if (residency_) {
    for (Block* blk = builder->first_block(); blk; blk = blk->next) {
      for (Instr* i = blk->instr_head; i; i = i->next) {
        Opcode op = i->opcode->num;
        if (op == OPCODE_LOAD_CONTEXT) {
          if (auto* ty = T(i->dest->type)) {
            if (!GetCtxReg(i->src1.offset, ty)) {
              ctx_poisoned_.insert(i->src1.offset);  // mixed type -> write-through
            }
          }
        } else if (op == OPCODE_STORE_CONTEXT && i->src2.value) {
          if (auto* ty = T(i->src2.value->type)) {
            if (GetCtxReg(i->src1.offset, ty)) {
              ctx_stored_.insert(i->src1.offset);  // clean alloca-only candidate
            } else {
              ctx_poisoned_.insert(i->src1.offset);
            }
          }
        }
      }
    }
    // A mixed-type (union) offset can't be cleanly alloca-only - a write-back of
    // one type would clobber a direct-ctx store of another. Keep it write-through.
    for (uint64_t off : ctx_poisoned_) ctx_stored_.erase(off);
  }

  // One llvm BB per HIR block, created up front so branches can target them.
  for (Block* blk = builder->first_block(); blk; blk = blk->next) {
    block_map_[blk->ordinal] =
        llvm::BasicBlock::Create(ctx_, "b", fn_);
  }
  Block* first = builder->first_block();
  if (!first) {
    b_.CreateRetVoid();
    return true;
  }
  b_.CreateBr(block_map_[first->ordinal]);

  for (Block* blk = first; blk; blk = blk->next) {
    b_.SetInsertPoint(block_map_[blk->ordinal]);
    // Block trace: log the block's guest address + register state at entry, so
    // the LLVM control-flow path (and where it diverges from the expected
    // return path) is visible on-device.
    if (trace_this_) {
      uint32_t ga = 0;
      for (Instr* i = blk->instr_head; i && !ga; i = i->next)
        ga = i->GuestAddressFor();
      // tag = block guest addr if found, else 0x9B0000|ordinal as a fallback.
      EmitTrace(ga ? ga : (0x9B0000u | (blk->ordinal & 0xFFFF)));
    }
    for (Instr* i = blk->instr_head; i; i = i->next) {
      if (!LowerInstr(i)) {
        // Diagnostic (rate-limited): which opcode forced this function to fall
        // back to a64. A histogram of these across boot pinpoints the highest-
        // value opcodes to lower next so HOT functions get the residency win
        // (BD's hottest fn 0x824694A0 falls back today).
        static std::atomic<uint32_t> s_fb{0};
        uint32_t fb = s_fb.fetch_add(1, std::memory_order_relaxed);
        // The cap USED to be a hardcoded 120, which is a trap: a full-boot
        // histogram then reports exactly 120 fallbacks and looks like a real
        // count. It bit me on 2026-08-08 - I read "120 functions fell back,
        // 94 mul_add + 26 mul_sub" as the truth when it was just the limit,
        // and the histogram only covered the first 120. Budgeted via a cvar so
        // a census run can raise it and get the actual distribution.
        const uint32_t budget = cvars::cpu_llvm_fallback_log_budget;
        if (fb < budget) {
          XELOGW("LLVMfallback fn=0x{:08X} opcode={} (#{}) -> a64", guest_addr_,
                 i->opcode->name, fb);
        } else if (fb == budget) {
          XELOGW(
              "LLVMfallback: budget {} reached - further fallbacks NOT logged. "
              "Raise cpu_llvm_fallback_log_budget for a full histogram.",
              budget);
        }
        return false;  // unsupported -> fallback
      }
      // A handler may emit a block terminator (a `ret` from a guest tail call /
      // return) BEFORE the HIR block's last instr; anything after it in the
      // block is then unreachable. Stop so we don't append instructions after a
      // terminator (invalid IR = "Terminator found in the middle of a basic
      // block", which forced an a64 fallback).
      if (b_.GetInsertBlock()->getTerminator()) break;
    }
    // Fall through to the next sequential block if no terminator was emitted.
    if (!b_.GetInsertBlock()->getTerminator()) {
      if (blk->next) {
        b_.CreateBr(block_map_[blk->next->ordinal]);
      } else {
        EmitReturn();
      }
    }
  }
  return true;
}

bool Lowerer::LowerInstr(Instr* i) {
  const Opcode op = i->opcode->num;
  // DIAGNOSTIC (cpu_backend_llvm_skip_opcodes): force the listed HIR opcodes onto
  // the a64 backend - any guest function using one falls back entirely (the caller
  // treats a false return like an unsupported opcode). Bisects WHICH opcode's LLVM
  // lowering corrupts a scene. Comma/space-separated DECIMAL opcode numbers, parsed
  // once (LowerInstr is per-instruction).
  static const std::unordered_set<int> s_skip_ops = [] {
    std::unordered_set<int> s;
    const std::string& v = cvars::cpu_backend_llvm_skip_opcodes;
    size_t p = 0;
    while (p < v.size()) {
      while (p < v.size() && (v[p] == ',' || v[p] == ' ')) ++p;
      size_t q = p;
      while (q < v.size() && v[q] != ',' && v[q] != ' ') ++q;
      if (q > p) s.insert(std::atoi(v.substr(p, q - p).c_str()));
      p = q;
    }
    return s;
  }();
  if (!s_skip_ops.empty() && s_skip_ops.count(int(op))) return false;
  switch (op) {
    // ---- ignorable / meta ----
    case OPCODE_COMMENT:
    case OPCODE_NOP:
    case OPCODE_SOURCE_OFFSET:
    case OPCODE_CONTEXT_BARRIER:  // direct-ctx model: every access hits memory
      return true;
    case OPCODE_MEMORY_BARRIER:
      b_.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
      return true;

    // ---- context ----
    case OPCODE_LOAD_CONTEXT: {
      auto* ty = T(i->dest->type);
      if (!ty) return false;
      if (residency_) {
        if (auto* slot = GetCtxReg(i->src1.offset, ty)) {
          Def(i->dest, b_.CreateLoad(ty, slot));
          return true;
        }
      }
      Def(i->dest, b_.CreateLoad(ty, CtxPtr(i->src1.offset)));
      return true;
    }
    case OPCODE_STORE_CONTEXT: {
      auto* val = V(i->src2.value);
      if (!val) return false;
      if (residency_) {
        if (auto* slot = GetCtxReg(i->src1.offset, val->getType())) {
          // Always mirror to the alloca (mem2reg -> host register for fast loads).
          b_.CreateStore(val, slot);
          // WRITE-THROUGH (default mode, or a poisoned/mixed-type offset): also
          // write the context so it's always current for callees via x20.
          // WRITE-BACK MODE (writeback_ + clean offset): alloca-ONLY - the context
          // store is DEFERRED to the call/return barrier (WriteBackCtxRegs), which
          // eliminates the per-store context write (the #1 residency lever).
          if (!writeback_ || !ctx_stored_.count(i->src1.offset)) {
            b_.CreateStore(val, CtxPtr(i->src1.offset));
          }
          return true;
        }
      }
      b_.CreateStore(val, CtxPtr(i->src1.offset));
      return true;
    }

    // ---- locals (function-private stack slots) ----
    case OPCODE_LOAD_LOCAL: {
      auto* ty = T(i->dest->type);
      if (!ty) return false;
      auto key = i->src1.value->ordinal;
      auto it = locals_.find(key);
      if (it == locals_.end()) return false;  // store-before-load expected
      Def(i->dest, b_.CreateLoad(ty, it->second));
      return true;
    }
    case OPCODE_STORE_LOCAL: {
      auto* val = V(i->src2.value);
      if (!val) return false;
      auto key = i->src1.value->ordinal;
      auto it = locals_.find(key);
      llvm::AllocaInst* slot;
      if (it == locals_.end()) {
        llvm::IRBuilder<> eb(&fn_->getEntryBlock(),
                             fn_->getEntryBlock().begin());
        slot = eb.CreateAlloca(val->getType());
        locals_[key] = slot;
      } else {
        slot = it->second;
      }
      b_.CreateStore(val, slot);
      return true;
    }

    // ---- moves / conversions ----
    case OPCODE_ASSIGN:
      Def(i->dest, V(i->src1.value));
      return true;
    case OPCODE_CAST: {
      // Reinterpret bits between same-size int/float.
      auto* ty = T(i->dest->type);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      Def(i->dest, b_.CreateBitCast(src, ty));
      return true;
    }
    case OPCODE_ZERO_EXTEND: {
      auto* ty = T(i->dest->type);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      Def(i->dest, b_.CreateZExt(src, ty));
      return true;
    }
    case OPCODE_SIGN_EXTEND: {
      auto* ty = T(i->dest->type);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      Def(i->dest, b_.CreateSExt(src, ty));
      return true;
    }
    case OPCODE_TRUNCATE: {
      auto* ty = T(i->dest->type);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      Def(i->dest, b_.CreateTrunc(src, ty));
      return true;
    }
    case OPCODE_CONVERT: {
      // int<->float numeric conversion (not bit reinterpret).
      auto dt = i->dest->type;
      auto st = i->src1.value->type;
      auto* ty = T(dt);
      auto* src = V(i->src1.value);
      if (!ty || !src) return false;
      if (IsFloat(dt) && IsInt(st)) {
        Def(i->dest, b_.CreateSIToFP(src, ty));
      } else if (IsInt(dt) && IsFloat(st)) {
        Def(i->dest, b_.CreateFPToSI(src, ty));
      } else if (IsFloat(dt) && IsFloat(st)) {
        Def(i->dest, dt > st ? b_.CreateFPExt(src, ty)
                             : b_.CreateFPTrunc(src, ty));
      } else {
        return false;
      }
      return true;
    }

    // ---- integer / float arithmetic ----
    case OPCODE_ADD: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      if (i->dest->type == VEC128_TYPE) {
        // V128 is a FLOAT op in a64 (ADD_V128 -> VmxFpBinOp::Add); IsFloat() is
        // false for VEC128_TYPE, so the ternary below would emit an
        // INTEGER add over float bit patterns. Same bug class as MUL.
        Def(i->dest, VmxFpBin(a, c, llvm::Instruction::FAdd));
        return true;
      }
      Def(i->dest, IsFloat(i->dest->type) ? b_.CreateFAdd(a, c)
                                          : b_.CreateAdd(a, c));
      return true;
    }
    case OPCODE_ADD_CARRY: {
      // dest = src1 + src2 + (carry & 1). src3 (carry, i8) is provably {0,1};
      // mask defensively. No carry-OUT here. Matches a64's add-the-carry path.
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      auto* carry = V(i->src3.value);
      auto* dt = T(i->dest->type);
      if (!a || !c || !carry || !dt) return false;
      auto* cy = b_.CreateAnd(b_.CreateZExtOrTrunc(carry, dt),
                              llvm::ConstantInt::get(dt, 1));
      Def(i->dest, b_.CreateAdd(b_.CreateAdd(a, c), cy));
      return true;
    }
    case OPCODE_IS_NAN: {
      // dest(i8) = isnan(src). fcmp uno src,src (unordered = NaN). Matches a64
      // (fcmp s,s; cset VS).
      auto* a = V(i->src1.value);
      auto* dt = T(i->dest->type);
      if (!a || !dt || !a->getType()->isFloatingPointTy()) return false;
      Def(i->dest, b_.CreateZExt(b_.CreateFCmpUNO(a, a), dt));
      return true;
    }
    case OPCODE_YIELD:
      // Guest spin-wait hint. a64 emits an ARM64 `yield`, but inline asm
      // report_fatal_errors in the ORCv2 AsmPrinter (no integrated assembler),
      // and llvm.aarch64.hint risks the same path - so lower it to a NO-OP. The
      // yield is a hint only, so this is correctness-exact; it just doesn't back
      // off a spinning core. BD's hottest fn 0x824694A0 uses this, so lowering it
      // (vs falling the whole fn back to a64) is what gets it the residency win.
      return true;
    case OPCODE_TRAP:
      // Unconditional guest trap (tw/td). Call the runtime trap helper; control
      // continues after (the helpers just log; the trap is rarely reached).
      EmitTrapCall(i->flags);
      return true;
    case OPCODE_TRAP_TRUE: {
      // Conditional guest trap: if cond != 0, call the trap helper, then CONTINUE
      // (the trap doesn't branch/return). Both edges rejoin the continuation.
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      bool fresh_cont = (i->next != nullptr) || (i->block->next == nullptr);
      llvm::BasicBlock* cont = fresh_cont
                                   ? llvm::BasicBlock::Create(ctx_, "c", fn_)
                                   : BlockFor(i->block->next);
      auto* trap_bb = llvm::BasicBlock::Create(ctx_, "trap", fn_);
      b_.CreateCondBr(Truth(cond), trap_bb, cont);
      b_.SetInsertPoint(trap_bb);
      EmitTrapCall(i->flags);
      b_.CreateBr(cont);
      if (fresh_cont) {
        b_.SetInsertPoint(cont);
        if (!i->next && !i->block->next) EmitReturn();
      }
      // else: cont is the next block; leave the insert point on the terminated
      // trap_bb so Run()'s fall-through adds no spurious branch.
      return true;
    }
    case OPCODE_CHECK_PREEMPT: {
      // Cooperative-scheduler safepoint (guest scheduler stage 2): inline
      // flag test on PPCContext.preempt_requested, cold-call the runtime
      // helper (which clears the flag and yields via the registered handler)
      // only when raised. Same continuation shape as TRAP_TRUE.
      auto* flag_ptr =
          CtxPtr(offsetof(xe::cpu::ppc::PPCContext, preempt_requested));
      auto* flag = b_.CreateLoad(b_.getInt8Ty(), flag_ptr);
      auto* raised = b_.CreateICmpNE(flag, b_.getInt8(0));
      bool fresh_cont = (i->next != nullptr) || (i->block->next == nullptr);
      llvm::BasicBlock* cont = fresh_cont
                                   ? llvm::BasicBlock::Create(ctx_, "c", fn_)
                                   : BlockFor(i->block->next);
      auto* yield_bb = llvm::BasicBlock::Create(ctx_, "preempt", fn_);
      b_.CreateCondBr(raised, yield_bb, cont);
      b_.SetInsertPoint(yield_bb);
      {
        auto* fty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                            {b_.getPtrTy()}, false);
        auto callee = mod_->getOrInsertFunction("xe_llvm_preempt_yield", fty);
        b_.CreateCall(callee, {ctx_ptr_});
      }
      b_.CreateBr(cont);
      if (fresh_cont) {
        b_.SetInsertPoint(cont);
        if (!i->next && !i->block->next) EmitReturn();
      }
      // else: leave the insert point on the terminated yield_bb so Run()'s
      // fall-through adds no spurious branch.
      return true;
    }
    case OPCODE_DELAY_EXECUTION:
      // Guest delay/spin hint (a64 emits `yield`). No architectural effect.
      return true;
    case OPCODE_SET_ROUNDING_MODE:
    case OPCODE_SET_NJM:
      // Set the a64 backend's CACHED FPCR (rounding mode / VMX flush-to-zero) -
      // backend-internal state, not guest PPCContext; no guest register changes.
      // The LLVM backend bakes in round-to-nearest + VMX denormal flush (the
      // common 360 mode, NJM=1), so this is a no-op. (Guest non-nearest rounding
      // / NJM=0 are not honored - rare; revisit only if a title needs them.)
      return true;
    case OPCODE_TO_SINGLE: {
      // frsp: round a double to single precision and back. fcvt round-to-nearest
      // (scalar FPU mode, no FZ); fcvt preserves NaN sign, so no fixup needed.
      auto* a = V(i->src1.value);
      if (!a || !a->getType()->isDoubleTy()) return false;
      Def(i->dest, b_.CreateFPExt(b_.CreateFPTrunc(a, b_.getFloatTy()),
                                  b_.getDoubleTy()));
      return true;
    }
    case OPCODE_DEBUG_BREAK:
      // Guest debug breakpoint -> trap (a64 emits BRK). Not differential-testable
      // (it halts); correct by composition with the trap helper.
      EmitTrapCall(0);
      return true;
    case OPCODE_DEBUG_BREAK_TRUE: {
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      bool fresh_cont = (i->next != nullptr) || (i->block->next == nullptr);
      llvm::BasicBlock* cont = fresh_cont
                                   ? llvm::BasicBlock::Create(ctx_, "c", fn_)
                                   : BlockFor(i->block->next);
      auto* brk_bb = llvm::BasicBlock::Create(ctx_, "brk", fn_);
      b_.CreateCondBr(Truth(cond), brk_bb, cont);
      b_.SetInsertPoint(brk_bb);
      EmitTrapCall(0);
      b_.CreateBr(cont);
      if (fresh_cont) {
        b_.SetInsertPoint(cont);
        if (!i->next && !i->block->next) EmitReturn();
      }
      return true;
    }
    case OPCODE_SUB: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      if (i->dest->type == VEC128_TYPE) {
        // V128 is a FLOAT op in a64 (SUB_V128 -> VmxFpBinOp::Sub); IsFloat() is
        // false for VEC128_TYPE, so the ternary below would emit an
        // INTEGER sub over float bit patterns. Same bug class as MUL.
        Def(i->dest, VmxFpBin(a, c, llvm::Instruction::FSub));
        return true;
      }
      Def(i->dest, IsFloat(i->dest->type) ? b_.CreateFSub(a, c)
                                          : b_.CreateSub(a, c));
      return true;
    }
    case OPCODE_MUL: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      if (i->dest->type == VEC128_TYPE) {
        // ⚠️ V128 MUL IS A **FLOAT** MULTIPLY. a64's only OPCODE_MUL V128
        // sequence is MUL_V128 -> EmitVmxFpBinOp_V128(VmxFpBinOp::Mul), i.e.
        // VMX float32x4 with denormal flush and PPC NaN propagation
        // (a64_sequences.cc:2315, a64_seq_util.h:851).
        //
        // IsFloat() below is TRUE ONLY for FLOAT32/FLOAT64 - VEC128_TYPE is
        // not float by that predicate - so this case previously fell into
        // CreateMul, an INTEGER multiply over the float bit patterns. Not a
        // rounding difference: completely wrong arithmetic, silently, on the
        // shipping backend.
        //
        // Reached from vcfsx/vcfux with a non-zero scale, which lower to
        // VectorConvertI2F followed by Mul(v, splat(2^-uimm))
        // (ppc_emit_altivec.cc:549) - i.e. colour/texture conversion, which is
        // the right shape to produce wrong colours.
        auto* i32x4 = T(VEC128_TYPE);
        auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
        auto* ai = b_.CreateBitCast(a, i32x4);
        auto* ci = b_.CreateBitCast(c, i32x4);
        if (cvars::cpu_llvm_vmx_float_flush) {
          ai = VmxFlushDenorm(ai);
          ci = VmxFlushDenorm(ci);
        }
        auto* prod = b_.CreateFMul(b_.CreateBitCast(ai, f32x4),
                                   b_.CreateBitCast(ci, f32x4));
        auto* resi = b_.CreateBitCast(prod, i32x4);
        resi = VmxNanFixup(resi, {ai, ci});
        if (cvars::cpu_llvm_vmx_float_flush) {
          resi = VmxFlushDenorm(resi);
        }
        Def(i->dest, b_.CreateBitCast(resi, T(VEC128_TYPE)));
        return true;
      }
      Def(i->dest, IsFloat(i->dest->type) ? b_.CreateFMul(a, c)
                                          : b_.CreateMul(a, c));
      return true;
    }
    case OPCODE_DIV: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      if (IsFloat(i->dest->type)) {
        Def(i->dest, b_.CreateFDiv(a, c));
      } else {
        Def(i->dest, (i->flags & ARITHMETIC_UNSIGNED) ? b_.CreateUDiv(a, c)
                                                       : b_.CreateSDiv(a, c));
      }
      return true;
    }
    case OPCODE_MUL_HI: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c || !IsInt(i->dest->type)) return false;
      unsigned w = a->getType()->getIntegerBitWidth();
      auto* wide = llvm::Type::getIntNTy(ctx_, w * 2);
      bool uns = (i->flags & ARITHMETIC_UNSIGNED) != 0;
      auto* aw = uns ? b_.CreateZExt(a, wide) : b_.CreateSExt(a, wide);
      auto* cw = uns ? b_.CreateZExt(c, wide) : b_.CreateSExt(c, wide);
      auto* prod = b_.CreateMul(aw, cw);
      auto* hi = b_.CreateLShr(prod, w);
      Def(i->dest, b_.CreateTrunc(hi, a->getType()));
      return true;
    }
    case OPCODE_NEG: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      if (i->dest->type == VEC128_TYPE) {
        // a64 NEG_V128 is EmitWithVmxFpcr + fneg - a FLOAT negate. The
        // ternary below would emit an INTEGER negate (0 - x) over float
        // bit patterns, which is not sign-flipping at all.
        auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
        Def(i->dest, b_.CreateBitCast(
                         b_.CreateFNeg(b_.CreateBitCast(a, f32x4)),
                         T(VEC128_TYPE)));
        return true;
      }
      Def(i->dest, IsFloat(i->dest->type) ? b_.CreateFNeg(a)
                                          : b_.CreateNeg(a));
      return true;
    }
    case OPCODE_ABS: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      if (IsFloat(i->dest->type)) {
        Def(i->dest, b_.CreateUnaryIntrinsic(llvm::Intrinsic::fabs, a));
      } else {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::abs, a,
                                              b_.getInt1(false)));
      }
      return true;
    }
    case OPCODE_SQRT: {
      auto* a = V(i->src1.value);
      if (!a || !IsFloat(i->dest->type)) return false;
      Def(i->dest, b_.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, a));
      return true;
    }
    case OPCODE_MAX: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      if (IsFloat(i->dest->type)) {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::maxnum, a, c));
      } else {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::smax, a, c));
      }
      return true;
    }
    case OPCODE_MIN: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      if (IsFloat(i->dest->type)) {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::minnum, a, c));
      } else {
        Def(i->dest, b_.CreateBinaryIntrinsic(llvm::Intrinsic::smin, a, c));
      }
      return true;
    }

    // ---- bitwise / shifts ----
    case OPCODE_AND: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateAnd(a, c));
      return true;
    }
    case OPCODE_AND_NOT: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateAnd(a, b_.CreateNot(c)));
      return true;
    }
    case OPCODE_OR: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateOr(a, c));
      return true;
    }
    case OPCODE_XOR: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateXor(a, c));
      return true;
    }
    case OPCODE_NOT: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      Def(i->dest, b_.CreateNot(a));
      return true;
    }
    case OPCODE_SHL: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateShl(a, b_.CreateZExtOrTrunc(c, a->getType())));
      return true;
    }
    case OPCODE_SHR: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateLShr(a, b_.CreateZExtOrTrunc(c, a->getType())));
      return true;
    }
    case OPCODE_SHA: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      Def(i->dest, b_.CreateAShr(a, b_.CreateZExtOrTrunc(c, a->getType())));
      return true;
    }
    case OPCODE_ROTATE_LEFT: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      auto* amt = b_.CreateZExtOrTrunc(c, a->getType());
      Def(i->dest, b_.CreateIntrinsic(llvm::Intrinsic::fshl, {a->getType()},
                                      {a, a, amt}));
      return true;
    }
    case OPCODE_CNTLZ: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      auto* r = b_.CreateBinaryIntrinsic(llvm::Intrinsic::ctlz, a,
                                         b_.getInt1(false));
      Def(i->dest, b_.CreateZExtOrTrunc(r, T(i->dest->type)));
      return true;
    }
    case OPCODE_BYTE_SWAP: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      if (a->getType()->isVectorTy()) {
        // VEC128 byteswap = a64 rev32 = byte-reverse within each 32-bit lane =
        // llvm.bswap on the <4 x i32> carrier.
        Def(i->dest, b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, a));
      } else if (a->getType()->getIntegerBitWidth() <= 8) {
        Def(i->dest, a);
      } else {
        Def(i->dest, b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, a));
      }
      return true;
    }

    // ---- compares / selects ----
    case OPCODE_COMPARE_EQ:
    case OPCODE_COMPARE_NE:
    case OPCODE_COMPARE_SLT:
    case OPCODE_COMPARE_SLE:
    case OPCODE_COMPARE_SGT:
    case OPCODE_COMPARE_SGE:
    case OPCODE_COMPARE_ULT:
    case OPCODE_COMPARE_ULE:
    case OPCODE_COMPARE_UGT:
    case OPCODE_COMPARE_UGE: {
      auto *a = V(i->src1.value), *c = V(i->src2.value);
      if (!a || !c) return false;
      llvm::Value* r;
      bool fp = a->getType()->isFloatingPointTy();
      switch (op) {
        case OPCODE_COMPARE_EQ:
          r = fp ? b_.CreateFCmpOEQ(a, c) : b_.CreateICmpEQ(a, c);
          break;
        case OPCODE_COMPARE_NE:
          r = fp ? b_.CreateFCmpUNE(a, c) : b_.CreateICmpNE(a, c);
          break;
        case OPCODE_COMPARE_SLT:
          r = fp ? b_.CreateFCmpOLT(a, c) : b_.CreateICmpSLT(a, c);
          break;
        case OPCODE_COMPARE_SLE:
          r = fp ? b_.CreateFCmpOLE(a, c) : b_.CreateICmpSLE(a, c);
          break;
        case OPCODE_COMPARE_SGT:
          r = fp ? b_.CreateFCmpOGT(a, c) : b_.CreateICmpSGT(a, c);
          break;
        case OPCODE_COMPARE_SGE:
          r = fp ? b_.CreateFCmpOGE(a, c) : b_.CreateICmpSGE(a, c);
          break;
        case OPCODE_COMPARE_ULT:
          r = fp ? b_.CreateFCmpOLT(a, c) : b_.CreateICmpULT(a, c);
          break;
        case OPCODE_COMPARE_ULE:
          r = fp ? b_.CreateFCmpOLE(a, c) : b_.CreateICmpULE(a, c);
          break;
        case OPCODE_COMPARE_UGT:
          r = fp ? b_.CreateFCmpOGT(a, c) : b_.CreateICmpUGT(a, c);
          break;
        default:
          r = fp ? b_.CreateFCmpOGE(a, c) : b_.CreateICmpUGE(a, c);
          break;
      }
      Def(i->dest, b_.CreateZExt(r, T(i->dest->type)));
      return true;
    }
    case OPCODE_IS_TRUE: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      Def(i->dest, b_.CreateZExt(Truth(a), T(i->dest->type)));
      return true;
    }
    case OPCODE_IS_FALSE: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      Def(i->dest, b_.CreateZExt(b_.CreateNot(Truth(a)), T(i->dest->type)));
      return true;
    }
    case OPCODE_SELECT: {
      auto* cond = V(i->src1.value);
      auto* tv = V(i->src2.value);
      auto* fv = V(i->src3.value);
      if (!cond || !tv || !fv) return false;
      if (IsVec(tv) || IsVec(cond)) return false;  // vsel is per-bit -> a64 (P3)
      Def(i->dest, b_.CreateSelect(Truth(cond), tv, fv));
      return true;
    }

    // ---- guest memory ----
    case OPCODE_LOAD: {
      auto* ty = T(i->dest->type);
      auto* ea = V(i->src1.value);
      if (!ty || !ea) return false;
      if (ty->isVectorTy()) {
        if (cvars::cpu_llvm_vector_qload) {
          // ONE q-load, exactly what the a64 backend emits (a64_seq_memory.cc:
          // 574: `e.ldr(i.dest, mem)`). See the cvar help for why the 4-word
          // split below is suspected to be over-conservative ON THIS PLATFORM.
          auto* v = b_.CreateLoad(ty, MemPtr(ea), /*isVolatile=*/true);
          Def(i->dest, MaybeByteSwap(v, ty, i->flags));
          return true;
        }
        // 128-bit vector load as FOUR volatile 32-bit loads (base+0/4/8/12).
        // Each is a single decodable LDR for the access-violation handler (a
        // single q-load that faults can't be decoded). volatile => LLVM won't
        // re-merge them into a q-load. Element k = word k, matching a q-load.
        auto* base = MemPtr(ea);
        auto* i32x4 = LaneVecTy(INT32_TYPE);
        llvm::Value* vec = llvm::PoisonValue::get(i32x4);
        for (int k = 0; k < 4; k++) {
          auto* p = b_.CreateGEP(b_.getInt8Ty(), base, b_.getInt64(4 * k));
          vec = b_.CreateInsertElement(
              vec, b_.CreateLoad(b_.getInt32Ty(), p, /*isVolatile=*/true),
              b_.getInt32(k));
        }
        Def(i->dest, MaybeByteSwap(vec, ty, i->flags));
        return true;
      }
      auto* v = b_.CreateLoad(ty, MemPtr(ea), /*isVolatile=*/true);
      Def(i->dest, MaybeByteSwap(v, ty, i->flags));
      return true;
    }
    case OPCODE_STORE: {
      auto* ea = V(i->src1.value);
      auto* val = V(i->src2.value);
      if (!ea || !val) return false;
      val = MaybeByteSwap(val, val->getType(), i->flags);
      if (IsVec(val)) {
        StoreVec128AsWords(val, MemPtr(ea));
        return true;
      }
      b_.CreateStore(val, MemPtr(ea), /*isVolatile=*/true);
      return true;
    }
    case OPCODE_LOAD_OFFSET: {
      auto* ty = T(i->dest->type);
      auto* base = V(i->src1.value);
      auto* off = V(i->src2.value);
      if (!ty || !base || !off) return false;
      auto* ea = b_.CreateAdd(b_.CreateZExtOrTrunc(base, b_.getInt64Ty()),
                              b_.CreateZExtOrTrunc(off, b_.getInt64Ty()));
      if (ty->isVectorTy()) {
        auto* hp = MemPtr(ea);
        auto* i32x4 = LaneVecTy(INT32_TYPE);
        llvm::Value* vec = llvm::PoisonValue::get(i32x4);
        for (int k = 0; k < 4; k++) {
          auto* p = b_.CreateGEP(b_.getInt8Ty(), hp, b_.getInt64(4 * k));
          vec = b_.CreateInsertElement(
              vec, b_.CreateLoad(b_.getInt32Ty(), p, /*isVolatile=*/true),
              b_.getInt32(k));
        }
        Def(i->dest, MaybeByteSwap(vec, ty, i->flags));
        return true;
      }
      auto* v = b_.CreateLoad(ty, MemPtr(ea), /*isVolatile=*/true);
      Def(i->dest, MaybeByteSwap(v, ty, i->flags));
      return true;
    }
    case OPCODE_STORE_OFFSET: {
      auto* base = V(i->src1.value);
      auto* off = V(i->src2.value);
      auto* val = V(i->src3.value);
      if (!base || !off || !val) return false;
      auto* ea = b_.CreateAdd(b_.CreateZExtOrTrunc(base, b_.getInt64Ty()),
                              b_.CreateZExtOrTrunc(off, b_.getInt64Ty()));
      val = MaybeByteSwap(val, val->getType(), i->flags);
      if (IsVec(val)) {
        StoreVec128AsWords(val, MemPtr(ea));
        return true;
      }
      b_.CreateStore(val, MemPtr(ea), /*isVolatile=*/true);
      return true;
    }
    case OPCODE_ATOMIC_COMPARE_EXCHANGE: {
      // dest(i8 success) = CAS(*[membase+EA], expected, desired). Matches a64's
      // casal / ldaxr-stlxr-retry: strong CAS, dest = (old == expected). src1 =
      // guest EA, src2 = expected, src3 = desired (i32 or i64). Acquire-release
      // ordering == casal (acquire on the load, release on the store).
      auto* ea = V(i->src1.value);
      auto* expected = V(i->src2.value);
      auto* desired = V(i->src3.value);
      if (!ea || !expected || !desired) return false;
      auto* ity = expected->getType();
      if (!ity->isIntegerTy() || expected->getType() != desired->getType()) {
        return false;
      }
      unsigned bits = ity->getIntegerBitWidth();
      if (bits != 32 && bits != 64) return false;
      auto* cx = b_.CreateAtomicCmpXchg(
          MemPtr(ea), expected, desired, llvm::MaybeAlign(bits / 8),
          llvm::AtomicOrdering::AcquireRelease, llvm::AtomicOrdering::Acquire,
          llvm::SyncScope::System);
      // strong CAS (no spurious failure) = the a64 retry loop / LSE casal.
      cx->setWeak(false);
      Def(i->dest, b_.CreateZExt(b_.CreateExtractValue(cx, 1), T(i->dest->type)));
      return true;
    }
    case OPCODE_CACHE_CONTROL:
      // Prefetch / cache-flush hints only (dcbt/dcbtst/dcbf/dcbst). No
      // architectural effect in the emulator: host guest-memory is always
      // coherent, and dcbz (zero a line) is lowered as a store by the PPC
      // frontend, NOT as CACHE_CONTROL. The a64 backend emits only prfm / dc
      // civac here, neither of which changes guest-visible state -> a no-op is
      // byte-identical to the a64 result (and lets the function stay in LLVM).
      return true;
    case OPCODE_MEMSET: {
      // dcbz/dcbz128: zero `length` bytes at membase+addr (value const 0).
      auto* addr = V(i->src1.value);
      auto* val = V(i->src2.value);
      auto* len = V(i->src3.value);
      if (!addr || !val || !len) return false;
      if (!val->getType()->isIntegerTy(8)) {
        val = b_.CreateTrunc(val, b_.getInt8Ty());
      }
      b_.CreateMemSet(MemPtr(addr), val, len, llvm::MaybeAlign(1));
      return true;
    }
    // OPCODE_LVL/LVR/STVL/STVR are unemittable dead opcodes: the PPC frontend
    // lowers lvlx/lvrx/stvlx/stvrx as PERMUTE + LOAD_VECTOR_SHL/SHR + a vector
    // LOAD/STORE + BYTE_SWAP (all handled above), never these opcodes. So they
    // need no lowering (they'd just fall through to the a64 fallback if emitted).

    // ---- control flow ----
    case OPCODE_BRANCH: {
      b_.CreateBr(BlockFor(i->src1.label->block));
      return true;
    }
    case OPCODE_BRANCH_TRUE:
    case OPCODE_BRANCH_FALSE: {
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      auto* c1 = Truth(cond);
      auto* target = BlockFor(i->src2.label->block);
      llvm::BasicBlock* other;
      if (i->next) {
        other = llvm::BasicBlock::Create(ctx_, "c", fn_);
      } else {
        other = i->block->next ? BlockFor(i->block->next) : nullptr;
        if (!other) other = llvm::BasicBlock::Create(ctx_, "c", fn_);
      }
      if (op == OPCODE_BRANCH_TRUE) {
        b_.CreateCondBr(c1, target, other);
      } else {
        b_.CreateCondBr(c1, other, target);
      }
      if (i->next || !i->block->next) {
        b_.SetInsertPoint(other);
        if (!i->next) EmitReturn();  // dangling guard block
      }
      return true;
    }
    case OPCODE_RETURN:
      if (writeback_) WriteBackCtxRegs();  // flush guest regs -> ctx for the caller
      b_.CreateRetVoid();
      return true;
    case OPCODE_RETURN_TRUE: {
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      auto* ret_bb = llvm::BasicBlock::Create(ctx_, "ret", fn_);
      // Same insert-point rule as BRANCH_TRUE / CALL_INDIRECT_TRUE: only step
      // into a FRESH continuation; if the false edge flows to the next block's
      // existing LLVM block, leave the insert point on the terminated ret path
      // so Run() adds no spurious self-branch.
      bool fresh_cont = (i->next != nullptr) || (i->block->next == nullptr);
      llvm::BasicBlock* cont = fresh_cont
                                   ? llvm::BasicBlock::Create(ctx_, "c", fn_)
                                   : BlockFor(i->block->next);
      b_.CreateCondBr(Truth(cond), ret_bb, cont);
      b_.SetInsertPoint(ret_bb);
      if (writeback_) WriteBackCtxRegs();
      b_.CreateRetVoid();
      if (fresh_cont) {
        b_.SetInsertPoint(cont);
        if (!i->next && !i->block->next) {
          if (writeback_) WriteBackCtxRegs();
          b_.CreateRetVoid();
        }
      }
      return true;
    }

    // ---- guest calls (P4) ----
    case OPCODE_CALL: {
      // CALL-GRAPH LOCALITY CENSUS - the measurement that decides whether
      // multi-function LLVM modules are worth building at all
      // (docs/research/20260808-multi-function-llvm-modules-design.md).
      //
      // The whole design rests on clusters of guest functions sharing a module
      // so intra-cluster calls become DIRECT LLVM calls the inliner can see. If
      // most call targets are far from their caller they will not land in any
      // reasonable cluster, no call becomes direct, and the design collapses.
      //
      // Buckets |target - caller| by magnitude rather than logging every edge:
      // ~18k functions x several calls each would be ~90k log lines, and this
      // repo has already been bitten by a diagnostic that floods logcat and
      // evicts the lines you actually wanted.
      if (cvars::cpu_llvm_callgraph_locality_census) {
        const uint32_t callee = i->src1.symbol->address();
        const uint32_t caller = guest_addr_;
        const uint32_t d = callee > caller ? callee - caller : caller - callee;
        static std::atomic<uint32_t> b_1k{0}, b_8k{0}, b_64k{0}, b_512k{0}, b_far{0};
        static std::atomic<uint32_t> total{0};
        if (d < 1024) b_1k.fetch_add(1, std::memory_order_relaxed);
        else if (d < 8192) b_8k.fetch_add(1, std::memory_order_relaxed);
        else if (d < 65536) b_64k.fetch_add(1, std::memory_order_relaxed);
        else if (d < 524288) b_512k.fetch_add(1, std::memory_order_relaxed);
        else b_far.fetch_add(1, std::memory_order_relaxed);
        uint32_t n = total.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((n % 4096) == 0) {
          XELOGI(
              "LLVMcallgraph locality: calls={} <1K={} <8K={} <64K={} <512K={} "
              ">=512K={}",
              n, b_1k.load(std::memory_order_relaxed),
              b_8k.load(std::memory_order_relaxed),
              b_64k.load(std::memory_order_relaxed),
              b_512k.load(std::memory_order_relaxed),
              b_far.load(std::memory_order_relaxed));
        }
      }
      auto* tgt = b_.getInt32(i->src1.symbol->address());
      if (i->flags & CALL_TAIL) {
        if (!EmitGuestTailCall(tgt)) return false;
      } else {
        EmitGuestCall(tgt);
      }
      return true;
    }
    case OPCODE_CALL_EXTERN: {
      // Call the extern HANDLER (C++), NOT the guest thunk address - calling the
      // address re-enters the `sc; bclr` thunk -> infinite recursion = the storm.
      EmitCallExtern(i->src1.symbol);
      return true;
    }
    case OPCODE_CALL_INDIRECT: {
      auto* t = V(i->src1.value);
      if (!t) return false;
      auto* t32 = b_.CreateZExtOrTrunc(t, b_.getInt32Ty());
      bool is_tail = (i->flags & CALL_TAIL) != 0;
      if (i->flags & CALL_POSSIBLE_RETURN) {
        // A guest `blr` whose target == our own guest return address (x0) is a
        // RETURN, not a forward call. Lower it to `ret` so the host stack
        // unwinds. Without this, EVERY guest return became a forward call and
        // the host stack only ever grew -> overflow = the device signal storm.
        // Mirrors A64Emitter::CallIndirect's CALL_POSSIBLE_RETURN check.
        auto* mine = b_.CreateTrunc(
            b_.CreateLoad(b_.getInt64Ty(), my_ret_addr_), b_.getInt32Ty());
        auto* is_ret =
            b_.CreateICmpEQ(b_.CreateZExtOrTrunc(t, b_.getInt32Ty()), mine);
        auto* ret_bb = llvm::BasicBlock::Create(ctx_, "blr_ret", fn_);
        auto* call_bb = llvm::BasicBlock::Create(ctx_, "blr_call", fn_);
        b_.CreateCondBr(is_ret, ret_bb, call_bb);
        b_.SetInsertPoint(ret_bb);
        EmitReturn();
        b_.SetInsertPoint(call_bb);
        if (is_tail) {
          // Not a return -> a tail call; reuse the frame (musttail + ret).
          if (!EmitGuestTailCall(t32)) return false;
        } else {
          EmitGuestCall(t32);
          // Continue after a (non-return) forward call. Leave call_bb
          // terminated for the block->next case so Run()'s fall-through adds
          // no self-branch.
          if (i->next) {
            auto* cont_bb = llvm::BasicBlock::Create(ctx_, "c", fn_);
            b_.CreateBr(cont_bb);
            b_.SetInsertPoint(cont_bb);
          } else if (i->block->next) {
            b_.CreateBr(BlockFor(i->block->next));
          } else {
            EmitReturn();
          }
        }
      } else if (is_tail) {
        if (!EmitGuestTailCall(t32)) return false;
      } else {
        EmitGuestCall(t32);
      }
      return true;
    }
    case OPCODE_SET_RETURN_ADDRESS: {
      // Stash the return address for the NEXT guest call (a64 stores it to its
      // GUEST_CALL_RET_ADDR stack slot; EmitGuestCall forwards it as the
      // callee's x0). Required so the callee can recognize its own blr RETURN.
      auto* v = V(i->src1.value);
      if (!v) return false;
      b_.CreateStore(b_.CreateZExtOrTrunc(v, b_.getInt64Ty()),
                     next_call_ret_addr_);
      return true;
    }
    case OPCODE_CALL_TRUE:
    case OPCODE_CALL_INDIRECT_TRUE: {
      auto* cond = V(i->src1.value);
      if (!cond) return false;
      llvm::Value* target;
      if (op == OPCODE_CALL_TRUE) {
        target = b_.getInt32(i->src2.symbol->address());
      } else {
        auto* t = V(i->src2.value);
        if (!t) return false;
        target = b_.CreateZExtOrTrunc(t, b_.getInt32Ty());
      }
      bool is_tail = (i->flags & CALL_TAIL) != 0;
      bool poss_ret = (i->flags & CALL_POSSIBLE_RETURN) != 0;
      auto* taken_bb = llvm::BasicBlock::Create(ctx_, "ctrue", fn_);
      // The cond-FALSE (not-taken) destination. CRITICAL (mirrors BRANCH_TRUE):
      // when this conditional call is the LAST instr of the block and the block
      // has a successor, the false edge flows DIRECTLY to that successor's LLVM
      // block and we must NOT leave the IRBuilder insert point there - otherwise
      // Run()'s end-of-block fall-through sees that (terminator-less) successor
      // and emits `CreateBr(successor)` INTO it = an empty `br self` infinite
      // loop (device-pinned: strncpy 0x826C0D98's copy-loop block became
      // `b1: br b1` -> livelock). `fresh_cont` = the false block is a fresh
      // continuation we keep emitting into.
      bool fresh_cont = (i->next != nullptr) || (i->block->next == nullptr);
      llvm::BasicBlock* cont_bb =
          fresh_cont ? llvm::BasicBlock::Create(ctx_, "c", fn_)
                     : BlockFor(i->block->next);
      b_.CreateCondBr(Truth(cond), taken_bb, cont_bb);
      b_.SetInsertPoint(taken_bb);
      // When the condition is TRUE this is a conditional guest blr/bctr. Honor
      // POSSIBLE_RETURN (a RETURN when target == our entry LR) and TAIL, exactly
      // like CALL_INDIRECT. Without that, a conditional RETURN became a forward
      // call (e.g. strncpy's `bclr if count==0`).
      if (poss_ret) {
        auto* mine = b_.CreateTrunc(
            b_.CreateLoad(b_.getInt64Ty(), my_ret_addr_), b_.getInt32Ty());
        auto* is_ret = b_.CreateICmpEQ(target, mine);
        auto* ret_bb = llvm::BasicBlock::Create(ctx_, "ctrue_ret", fn_);
        auto* fwd_bb = llvm::BasicBlock::Create(ctx_, "ctrue_fwd", fn_);
        b_.CreateCondBr(is_ret, ret_bb, fwd_bb);
        b_.SetInsertPoint(ret_bb);
        EmitReturn();
        b_.SetInsertPoint(fwd_bb);
        if (is_tail) {
          if (!EmitGuestTailCall(target)) return false;  // terminates fwd_bb
        } else {
          EmitGuestCall(target);
          b_.CreateBr(cont_bb);  // taken (non-return) rejoins the false path
        }
      } else if (is_tail) {
        if (!EmitGuestTailCall(target)) return false;  // terminates taken_bb
      } else {
        EmitGuestCall(target);
        b_.CreateBr(cont_bb);  // taken rejoins the false path
      }
      // Position for the rest of lowering. Only step into cont_bb when it is a
      // fresh continuation; otherwise leave the insert point on the (terminated)
      // taken/fwd block so Run() adds no spurious fall-through branch.
      if (fresh_cont) {
        b_.SetInsertPoint(cont_bb);
        if (!i->next && !i->block->next) EmitReturn();
      }
      return true;
    }

    // ---- lane-typed vector arithmetic ----
    case OPCODE_VECTOR_ADD:
    case OPCODE_VECTOR_SUB: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags & 0xFF);
      auto* lt = LaneVecTy(pt);
      if (!lt) return false;
      bool add = (op == OPCODE_VECTOR_ADD);
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      llvm::Value* r;
      if (pt == FLOAT32_TYPE) {
        if (cvars::cpu_llvm_vmx_float_flush) {
          // PPC VMX flushes denormals (VSCR.NJ). a64 gets that from HARDWARE by
          // wrapping VMX float arithmetic in EmitWithVmxFpcr, which sets
          // FPCR.FZ (a64_seq_vector.cc:254 etc, DEFAULT_VMX_FPCR = 1<<24).
          // THE LLVM BACKEND NEVER SETS FPCR AT ALL, so a bare fadd/fsub here
          // does NOT flush - vaddfp/vsubfp diverge from a64 on denormal inputs
          // or results. Flush in software on both ends, exactly as this file's
          // vmaddfp lowering already does, so the result is FPCR-independent
          // and matches a64 lane-for-lane.
          auto* i32x4 = T(VEC128_TYPE);
          auto* f32x4 = lt;
          auto* fa = b_.CreateBitCast(
              VmxFlushDenorm(b_.CreateBitCast(av, i32x4)), f32x4);
          auto* fb = b_.CreateBitCast(
              VmxFlushDenorm(b_.CreateBitCast(cv, i32x4)), f32x4);
          auto* raw = add ? b_.CreateFAdd(fa, fb) : b_.CreateFSub(fa, fb);
          r = b_.CreateBitCast(
              VmxFlushDenorm(b_.CreateBitCast(raw, i32x4)), f32x4);
        } else {
          r = add ? b_.CreateFAdd(av, cv) : b_.CreateFSub(av, cv);
        }
      } else {
        uint32_t arith = i->flags >> 8;
        if (arith & ARITHMETIC_SATURATE) {
          bool uns = (arith & ARITHMETIC_UNSIGNED) != 0;
          auto id = add ? (uns ? llvm::Intrinsic::uadd_sat
                               : llvm::Intrinsic::sadd_sat)
                        : (uns ? llvm::Intrinsic::usub_sat
                               : llvm::Intrinsic::ssub_sat);
          r = b_.CreateBinaryIntrinsic(id, av, cv);
        } else {
          r = add ? b_.CreateAdd(av, cv) : b_.CreateSub(av, cv);
        }
      }
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_MAX:
    case OPCODE_VECTOR_MIN: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags >> 8);  // part_type in high
      auto* lt = LaneVecTy(pt);
      if (!lt) return false;
      bool mx = (op == OPCODE_VECTOR_MAX);
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      llvm::Value* r;
      if (pt == FLOAT32_TYPE) {
        // NaN SEMANTICS. PPC vmaxfp PROPAGATES NaN (PEM 3.2.5.1: "if the
        // element in register vA is a NaN then the result is that NaN, else if
        // the element in vB is a NaN then the result is that NaN"). ARM FMAX
        // propagates too and therefore MATCHES PPC - that is exactly why
        // a64_vmx_native_fmax_nan was flipped default-true after
        // tools/qemu/fmax_nan_differential.c showed ARM fmax matching PPC in
        // all 8 cases.
        //
        // llvm.maxnum/minnum are IEEE-754 maxNum/minNum: they return the
        // NUMBER when one operand is NaN, and lower to FMAXNM - the OTHER
        // instruction. So the LLVM backend still has the exact bug the a64
        // backend was fixed for. llvm.maximum/minimum are the IEEE-2019
        // maximum/minimum forms, which propagate NaN and lower to FMAX/FMIN.
        auto id_nan_correct =
            mx ? llvm::Intrinsic::maximum : llvm::Intrinsic::minimum;
        auto id_legacy = mx ? llvm::Intrinsic::maxnum : llvm::Intrinsic::minnum;
        auto id = cvars::cpu_llvm_vmx_fmax_nan ? id_nan_correct : id_legacy;
        if (cvars::cpu_llvm_vmx_float_flush) {
          // Same VMX denormal flush gap as vaddfp/vsubfp - a64 gets it from
          // FPCR.FZ via EmitWithVmxFpcr, LLVM sets FPCR never.
          auto* i32x4 = T(VEC128_TYPE);
          auto* fa = b_.CreateBitCast(
              VmxFlushDenorm(b_.CreateBitCast(av, i32x4)), lt);
          auto* fb = b_.CreateBitCast(
              VmxFlushDenorm(b_.CreateBitCast(cv, i32x4)), lt);
          auto* raw = b_.CreateBinaryIntrinsic(id, fa, fb);
          r = b_.CreateBitCast(
              VmxFlushDenorm(b_.CreateBitCast(raw, i32x4)), lt);
        } else {
          r = b_.CreateBinaryIntrinsic(id, av, cv);
        }
      } else {
        bool uns = (i->flags & ARITHMETIC_UNSIGNED) != 0;
        auto id = mx ? (uns ? llvm::Intrinsic::umax : llvm::Intrinsic::smax)
                     : (uns ? llvm::Intrinsic::umin : llvm::Intrinsic::smin);
        r = b_.CreateBinaryIntrinsic(id, av, cv);
      }
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_COMPARE_EQ:
    case OPCODE_VECTOR_COMPARE_SGT:
    case OPCODE_VECTOR_COMPARE_SGE:
    case OPCODE_VECTOR_COMPARE_UGT:
    case OPCODE_VECTOR_COMPARE_UGE: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags);  // whole flags = part_type
      auto* lt = LaneVecTy(pt);
      if (!lt) return false;
      bool fp = (pt == FLOAT32_TYPE);
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      llvm::Value* m;  // <N x i1>
      switch (op) {
        case OPCODE_VECTOR_COMPARE_EQ:
          m = fp ? b_.CreateFCmpOEQ(av, cv) : b_.CreateICmpEQ(av, cv);
          break;
        case OPCODE_VECTOR_COMPARE_SGT:
          m = fp ? b_.CreateFCmpOGT(av, cv) : b_.CreateICmpSGT(av, cv);
          break;
        case OPCODE_VECTOR_COMPARE_SGE:
          m = fp ? b_.CreateFCmpOGE(av, cv) : b_.CreateICmpSGE(av, cv);
          break;
        case OPCODE_VECTOR_COMPARE_UGT:
          m = fp ? b_.CreateFCmpOGT(av, cv) : b_.CreateICmpUGT(av, cv);
          break;
        default:  // UGE
          m = fp ? b_.CreateFCmpOGE(av, cv) : b_.CreateICmpUGE(av, cv);
          break;
      }
      // All-1s/0s per lane: sext the i1 mask to the integer lane width.
      auto* int_lt = fp ? llvm::VectorType::get(llvm::Type::getInt32Ty(ctx_), 4,
                                                false)
                        : lt;
      Def(i->dest, b_.CreateBitCast(b_.CreateSExt(m, int_lt), T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_SHL:
    case OPCODE_VECTOR_SHR:
    case OPCODE_VECTOR_SHA: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags);  // whole flags = part_type
      auto* lt = LaneVecTy(pt);
      if (!lt || pt == FLOAT32_TYPE) return false;  // integer lanes only
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      // VMX masks the per-lane shift amount to (lane_width-1) (matches a64).
      unsigned w = lt->getScalarSizeInBits();
      auto* amt = b_.CreateAnd(cv, llvm::ConstantInt::get(lt, w - 1));
      llvm::Value* r = (op == OPCODE_VECTOR_SHL)   ? b_.CreateShl(av, amt)
                       : (op == OPCODE_VECTOR_SHR) ? b_.CreateLShr(av, amt)
                                                   : b_.CreateAShr(av, amt);
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_ROTATE_LEFT: {
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags);  // whole flags = part_type
      auto* lt = LaneVecTy(pt);
      if (!lt || pt == FLOAT32_TYPE) return false;
      auto* av = b_.CreateBitCast(a, lt);
      auto* cv = b_.CreateBitCast(c, lt);
      // fshl(x, x, amt) = rotate left by (amt mod width) == a64's amt & (width-1).
      auto* r = b_.CreateIntrinsic(llvm::Intrinsic::fshl, {lt}, {av, av, cv});
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_CONVERT_I2F: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      auto* iv = b_.CreateBitCast(a, LaneVecTy(INT32_TYPE));
      auto* fv = (i->flags & ARITHMETIC_UNSIGNED)
                     ? b_.CreateUIToFP(iv, LaneVecTy(FLOAT32_TYPE))
                     : b_.CreateSIToFP(iv, LaneVecTy(FLOAT32_TYPE));
      Def(i->dest, b_.CreateBitCast(fv, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_VECTOR_CONVERT_F2I: {
      auto* a = V(i->src1.value);
      if (!a) return false;
      auto* fv = b_.CreateBitCast(a, LaneVecTy(FLOAT32_TYPE));
      // fptosi/fptoui.sat: NaN->0, saturate on overflow (matches a64 fcvtzs/zu).
      auto id = (i->flags & ARITHMETIC_UNSIGNED) ? llvm::Intrinsic::fptoui_sat
                                                 : llvm::Intrinsic::fptosi_sat;
      auto* iv = b_.CreateIntrinsic(LaneVecTy(INT32_TYPE), id, {fv});
      Def(i->dest, b_.CreateBitCast(iv, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_SPLAT: {
      // Broadcast a scalar to all lanes (a64 dup). All lanes equal => lane order
      // is irrelevant; element count follows the scalar's type.
      auto* s = V(i->src1.value);
      if (!s) return false;
      unsigned lanes;
      switch (i->src1.value->type) {
        case INT8_TYPE: lanes = 16; break;
        case INT16_TYPE: lanes = 8; break;
        case INT32_TYPE:
        case FLOAT32_TYPE: lanes = 4; break;
        default: return false;
      }
      Def(i->dest,
          b_.CreateBitCast(b_.CreateVectorSplat(lanes, s), T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_LOAD_VECTOR_SHL:
    case OPCODE_LOAD_VECTOR_SHR: {
      // lvsl/lvsr permute-control vector: a base byte pattern +/- splat(sh & 0xF)
      // (8-bit wrapping), exactly matching the a64 sequence. The base is in PPC
      // byte order (byte-swapped within each 32-bit word): SHL indices 0..15 ->
      // {3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12}; SHR is indices 16..31. The
      // <16 x i8> element k maps to memory byte k (little-endian carrier), so it
      // is byte-identical to the a64 store. Pure integer => exact.
      auto* sh = V(i->src1.value);  // i8 shift amount
      if (!sh) return false;
      bool shl = (op == OPCODE_LOAD_VECTOR_SHL);
      const uint8_t shl_base[16] = {3, 2, 1, 0,  7,  6,  5,  4,
                                    11, 10, 9, 8, 15, 14, 13, 12};
      const uint8_t shr_base[16] = {19, 18, 17, 16, 23, 22, 21, 20,
                                    27, 26, 25, 24, 31, 30, 29, 28};
      auto* base = llvm::ConstantDataVector::get(
          ctx_, llvm::ArrayRef<uint8_t>(shl ? shl_base : shr_base, 16));
      auto* amt = b_.CreateVectorSplat(
          16, b_.CreateAnd(sh, b_.getInt8(0xF)));  // <16 x i8>
      auto* r = shl ? b_.CreateAdd(base, amt) : b_.CreateSub(base, amt);
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_INSERT: {
      // dest = src1 with element[idx] = src3. The logical lane index is remapped
      // to the byte-swapped physical lane exactly as a64's VEC128_B/W/D macros:
      // i8 ^ 3, i16 ^ 1, i32 unchanged (byte/halfword swap within 32-bit words).
      auto* vec = V(i->src1.value);
      auto* idxv = V(i->src2.value);
      auto* val = V(i->src3.value);
      if (!vec || !idxv || !val) return false;
      TypeName et = i->src3.value->type;
      unsigned xorm;
      switch (et) {
        case INT8_TYPE: xorm = 3; break;
        case INT16_TYPE: xorm = 1; break;
        case INT32_TYPE: xorm = 0; break;
        default: return false;  // f32/i64 element inserts -> a64
      }
      auto* lt = LaneVecTy(et);
      if (!lt || val->getType() != lt->getScalarType()) return false;
      auto* idx = b_.CreateZExtOrTrunc(idxv, b_.getInt32Ty());
      if (xorm) idx = b_.CreateXor(idx, b_.getInt32(xorm));
      auto* r = b_.CreateInsertElement(b_.CreateBitCast(vec, lt), val, idx);
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_EXTRACT: {
      // dest(iN) = src1.element[idx], same byte-swapped lane remap as INSERT.
      // Mask the (possibly dynamic) physical index to the lane count, matching
      // a64's `and w, lanes-1` so an out-of-range index can't be UB.
      auto* vec = V(i->src1.value);
      auto* idxv = V(i->src2.value);
      if (!vec || !idxv) return false;
      TypeName dt = i->dest->type;
      unsigned xorm, mask;
      switch (dt) {
        case INT8_TYPE: xorm = 3; mask = 0xF; break;
        case INT16_TYPE: xorm = 1; mask = 0x7; break;
        case INT32_TYPE: xorm = 0; mask = 0x3; break;
        default: return false;
      }
      auto* lt = LaneVecTy(dt);
      if (!lt) return false;
      auto* idx = b_.CreateZExtOrTrunc(idxv, b_.getInt32Ty());
      if (xorm) idx = b_.CreateXor(idx, b_.getInt32(xorm));
      idx = b_.CreateAnd(idx, b_.getInt32(mask));
      Def(i->dest, b_.CreateExtractElement(b_.CreateBitCast(vec, lt), idx));
      return true;
    }
    case OPCODE_MUL_ADD:
    case OPCODE_MUL_SUB: {
      // Vector vmaddfp/vnmsubfp DEVICE-miscompiles when LLVM-lowered together with
      // other vector ops in one function (a codegen/regalloc INTERACTION bug; the IR
      // is qemu-byte-correct). Fall back to a64 (correct) by default; the lowering
      // below is preserved behind the cvar for future root-cause work. Device-proven
      // fix (cpu_backend_llvm_skip_opcodes=77 rendered BD's field correctly).
      // SCALAR f32/f64 FMA (fmadd/fmsub family). Device-censused 2026-08-08:
      // ~830 of 1,022 LLVM fallbacks are THIS, not the vector form - scalar FMA
      // simply had no lowering, so every guest function containing one lost LLVM
      // *and* its register residency. It is also NOT subject to the vector
      // vmaddfp miscompile, which is a vector-regalloc interaction.
      //
      // Semantics copied from a64 EmitFmaWithPpcNan_F32/F64
      // (a64_sequences.cc:1686), which is the reference this must match:
      //   if any source is NaN -> propagate the FIRST NaN in order s1,s2,s3,
      //                           QUIETED (f32 |= 1<<22, f64 |= 1<<51)
      //   else                 -> fused multiply-add, single-rounded
      //   if the result is a GENERATED NaN -> PPC default NaN, which is NEGATIVE
      //                           (f32 0xFFC00000, f64 0xFFF8000000000000)
      // ARM FNMSUB Sd,Sn,Sm,Sa computes Sn*Sm - Sa, so MUL_SUB == fma(s1,s2,-s3).
      // Branchless via selects - no lane writes, and it keeps the function on
      // LLVM instead of bailing.
      // GATED DEFAULT-OFF 2026-08-09 after a RENDERING REGRESSION appeared in the
      // window this landed in: guest threads execute and the GPU thread runs, the
      // device heats to 57C, but the screen stays BLACK - the signature of
      // degenerate geometry, i.e. wrong float results, not a hang.
      //
      // THE SUSPECTED DEFECT, and the qemu differential could not have caught it:
      // a64's EmitFmaWithPpcNan_F32/_F64 begins with ChangeFpcrMode(FPCRMode::Fpu)
      // because scalar PPC FP must run with FPCR.FZ CLEAR. This lowering does not
      // manage FPCR at all, so if a scalar FMA executes while FPCR is still in VMX
      // mode (FZ set, DEFAULT_VMX_FPCR = 1<<24) denormal inputs and results are
      // FLUSHED TO ZERO and the answer differs from a64's. The differential in
      // tools/qemu/scalar_fma_ppc_nan_equiv.c compares a C model of the SEQUENCE;
      // it never modelled the FPCR MODE the sequence runs under, so 32/32 PASS
      // says nothing about this.
      //
      // NOT PROVEN to be the regression - the bisect was not run. Gated because
      // an unvalidated float-semantics change that is ALWAYS ON is the wrong thing
      // to leave enabled while rendering is broken. Re-enable only with a pixel
      // check, and fix the FPCR mode first if the hypothesis holds.
      if (cvars::cpu_llvm_lower_scalar_fma &&
          (i->dest->type == FLOAT32_TYPE || i->dest->type == FLOAT64_TYPE)) {
        const bool f64 = i->dest->type == FLOAT64_TYPE;
        auto* a = V(i->src1.value);
        auto* c = V(i->src2.value);
        auto* d = V(i->src3.value);
        if (!a || !c || !d) return false;
        auto* fty = a->getType();
        if (fty != c->getType() || fty != d->getType() || !fty->isFloatingPointTy())
          return false;
        auto* ity = f64 ? b_.getInt64Ty() : b_.getInt32Ty();
        auto* quiet_bit = llvm::ConstantInt::get(
            ity, f64 ? (1ull << 51) : (1ull << 22));
        auto* def_nan = llvm::ConstantInt::get(
            ity, f64 ? 0xFFF8000000000000ull : 0xFFC00000ull);
        auto quiet = [&](llvm::Value* v) {
          return b_.CreateBitCast(
              b_.CreateOr(b_.CreateBitCast(v, ity), quiet_bit), fty);
        };
        auto* addend = (op == OPCODE_MUL_SUB) ? b_.CreateFNeg(d) : d;
        auto* res = b_.CreateIntrinsic(llvm::Intrinsic::fma, {fty}, {a, c, addend});
        // A NaN out of a non-NaN input set is GENERATED (inf*0, inf-inf): PPC
        // wants its own negative default NaN, which ARM cannot produce natively.
        auto* gen = b_.CreateSelect(b_.CreateFCmpUNO(res, res),
                                    b_.CreateBitCast(def_nan, fty), res);
        // Pick the FIRST NaN source in the FP DOMAIN (fcsel), then quiet it ONCE.
        // The naive form quiets all three sources up front and selects between
        // them, which costs 3 ORs and 5 FP<->GPR round trips; those moves are
        // latency-3 M0-pipe ops on the A710, and only one of the three is ever
        // used. Measured with clang -S: 19 insns / 5 fmov naive vs 16 / 3 here,
        // and clang fuses the any-NaN test into fccmp. Semantics are identical -
        // if a is NaN both yield quiet(a), else c, else d, else the FMA result.
        auto* a_nan = b_.CreateFCmpUNO(a, a);
        auto* c_nan = b_.CreateFCmpUNO(c, c);
        auto* d_nan = b_.CreateFCmpUNO(d, d);
        auto* nan_src = b_.CreateSelect(a_nan, a, b_.CreateSelect(c_nan, c, d));
        auto* any_nan = b_.CreateOr(a_nan, b_.CreateOr(c_nan, d_nan));
        Def(i->dest, b_.CreateSelect(any_nan, quiet(nan_src), gen));
        return true;
      }
      if (!cvars::cpu_backend_llvm_lower_vmaddfp) return false;
      // VMX float32x4 fused multiply-add/sub (vmaddfp / vnmsubfp): dest =
      // s1*s2 (+/-) s3, single-rounded (llvm.fma), with the full PPC semantics =
      // flush denormal inputs -> fma -> PPC NaN fixup -> flush denormal output.
      // Byte-identical to a64 MUL_ADD_V128 / MUL_SUB_V128. Scalar f32/f64 FMA
      // still falls back to a64.
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      auto* d = V(i->src3.value);
      if (!a || !c || !d || !IsVec(a) || !IsVec(c) || !IsVec(d)) return false;
      auto* i32x4 = T(VEC128_TYPE);
      auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
      auto* s1i = VmxFlushDenorm(b_.CreateBitCast(a, i32x4));
      auto* s2i = VmxFlushDenorm(b_.CreateBitCast(c, i32x4));
      auto* s3i = VmxFlushDenorm(b_.CreateBitCast(d, i32x4));
      auto* f3 = b_.CreateBitCast(s3i, f32x4);
      if (op == OPCODE_MUL_SUB) f3 = b_.CreateFNeg(f3);  // s1*s2 - s3
      auto* res = b_.CreateIntrinsic(
          llvm::Intrinsic::fma, {f32x4},
          {b_.CreateBitCast(s1i, f32x4), b_.CreateBitCast(s2i, f32x4), f3});
      auto* resi = b_.CreateBitCast(res, i32x4);
      // NaN fixup uses the UN-negated flushed sources (a64 saves un-negated s3).
      resi = VmxNanFixup(resi, {s1i, s2i, s3i});
      resi = VmxFlushDenorm(resi);
      Def(i->dest, b_.CreateBitCast(resi, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_DOT_PRODUCT_3:
    case OPCODE_DOT_PRODUCT_4: {
      // vmsum3fp / vmsum4fp: dot product in DOUBLE precision (so f32*f32 is
      // exact), summed in the exact a64 lane order, narrowed to f32, with VMX
      // denormal flush (in + out, via the FZ the a64 path relies on) and the
      // a64 overflow rule (|result| == +inf -> 0x7FC00000 QNaN). Result is a
      // scalar (F32 dest) or splatted to all 4 lanes (V128 dest).
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c || !IsVec(a) || !IsVec(c)) return false;
      auto* i32x4 = T(VEC128_TYPE);
      auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
      auto* dblx4 = llvm::VectorType::get(b_.getDoubleTy(), 4, false);
      auto* fa = b_.CreateBitCast(VmxFlushDenorm(b_.CreateBitCast(a, i32x4)),
                                  f32x4);
      auto* fb = b_.CreateBitCast(VmxFlushDenorm(b_.CreateBitCast(c, i32x4)),
                                  f32x4);
      auto* prod = b_.CreateFMul(b_.CreateFPExt(fa, dblx4),
                                 b_.CreateFPExt(fb, dblx4));  // exact products
      auto* p0 = b_.CreateExtractElement(prod, b_.getInt32(0));
      auto* p1 = b_.CreateExtractElement(prod, b_.getInt32(1));
      auto* p2 = b_.CreateExtractElement(prod, b_.getInt32(2));
      llvm::Value* sum;
      if (op == OPCODE_DOT_PRODUCT_3) {
        sum = b_.CreateFAdd(b_.CreateFAdd(p0, p1), p2);  // (p0+p1)+p2
      } else {
        auto* p3 = b_.CreateExtractElement(prod, b_.getInt32(3));
        sum = b_.CreateFAdd(b_.CreateFAdd(p0, p2),
                            b_.CreateFAdd(p1, p3));  // (p0+p2)+(p1+p3)
      }
      auto* sumi = b_.CreateBitCast(b_.CreateFPTrunc(sum, b_.getFloatTy()),
                                    b_.getInt32Ty());
      // Flush a denormal output to signed zero.
      auto* den = b_.CreateICmpULT(
          b_.CreateSub(b_.CreateShl(sumi, b_.getInt32(1)), b_.getInt32(1)),
          b_.getInt32(0x00FFFFFF));
      sumi = b_.CreateSelect(
          den, b_.CreateAnd(sumi, b_.getInt32(0x80000000)), sumi);
      // |result| == +inf -> PPC QNaN 0x7FC00000.
      auto* isinf = b_.CreateICmpEQ(
          b_.CreateAnd(sumi, b_.getInt32(0x7FFFFFFF)), b_.getInt32(0x7F800000));
      sumi = b_.CreateSelect(isinf, b_.getInt32(0x7FC00000), sumi);
      if (i->dest->type == VEC128_TYPE) {
        Def(i->dest,
            b_.CreateBitCast(b_.CreateVectorSplat(4, sumi), T(VEC128_TYPE)));
      } else if (i->dest->type == FLOAT32_TYPE) {
        Def(i->dest, b_.CreateBitCast(sumi, b_.getFloatTy()));
      } else {
        return false;
      }
      return true;
    }
    case OPCODE_UNPACK: {
      // VMX unpack via the xe_llvm_unpack C helper (reuses xenos_half_to_float +
      // the magic-float math; all formats incl. 8_IN_16 / 16_IN_32). Pass the
      // VEC128 by pointer through an entry-block scratch alloca.
      auto* val = V(i->src1.value);
      if (!val) return false;
      auto* i32x4 = T(VEC128_TYPE);
      auto* scratch = EntryAlloca(i32x4);
      b_.CreateStore(b_.CreateBitCast(val, i32x4), scratch);
      auto callee = mod_->getOrInsertFunction(
          "xe_llvm_unpack",
          llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                  {b_.getPtrTy(), b_.getInt32Ty()}, false));
      b_.CreateCall(callee, {scratch, b_.getInt32(i->flags)});
      Def(i->dest, b_.CreateLoad(i32x4, scratch));
      return true;
    }
    case OPCODE_PACK: {
      // VMX pack. Single-input float formats -> xe_llvm_pack(scratch, flags).
      // 8_IN_16/16_IN_32 are TWO-input integer narrows -> xe_llvm_pack2(s1, s2,
      // flags). Both pass VEC128s by pointer via entry-block scratch allocas.
      uint32_t mode = i->flags & PACK_TYPE_MODE;
      auto* i32x4 = T(VEC128_TYPE);
      auto* voidTy = llvm::Type::getVoidTy(ctx_);
      if (mode == PACK_TYPE_8_IN_16 || mode == PACK_TYPE_16_IN_32) {
        auto* s1 = V(i->src1.value);
        auto* s2 = V(i->src2.value);
        if (!s1 || !s2) return false;
        auto* sc1 = EntryAlloca(i32x4);
        auto* sc2 = EntryAlloca(i32x4);
        b_.CreateStore(b_.CreateBitCast(s1, i32x4), sc1);
        b_.CreateStore(b_.CreateBitCast(s2, i32x4), sc2);
        auto callee2 = mod_->getOrInsertFunction(
            "xe_llvm_pack2",
            llvm::FunctionType::get(
                voidTy, {b_.getPtrTy(), b_.getPtrTy(), b_.getInt32Ty()}, false));
        b_.CreateCall(callee2, {sc1, sc2, b_.getInt32(i->flags)});
        Def(i->dest, b_.CreateLoad(i32x4, sc1));
        return true;
      }
      auto* val = V(i->src1.value);
      if (!val) return false;
      auto* scratch = EntryAlloca(i32x4);
      b_.CreateStore(b_.CreateBitCast(val, i32x4), scratch);
      auto callee = mod_->getOrInsertFunction(
          "xe_llvm_pack",
          llvm::FunctionType::get(voidTy, {b_.getPtrTy(), b_.getInt32Ty()},
                                  false));
      b_.CreateCall(callee, {scratch, b_.getInt32(i->flags)});
      Def(i->dest, b_.CreateLoad(i32x4, scratch));
      return true;
    }
    case OPCODE_DID_SATURATE:
      // Saturation tracking is unimplemented in BOTH the a64 and x64 backends
      // (they `mov dest, 0`), so the byte-identical lowering is a constant 0.
      Def(i->dest, b_.getInt8(0));
      return true;
    case OPCODE_LOAD_CLOCK: {
      // mftb: current guest tick count via the runtime helper. Non-deterministic
      // (not differential-testable); matches a64 LoadClock.
      auto callee = mod_->getOrInsertFunction(
          "xe_llvm_load_clock",
          llvm::FunctionType::get(b_.getInt64Ty(), {}, false));
      Def(i->dest, b_.CreateCall(callee, {}));
      return true;
    }
    case OPCODE_LOAD_MMIO: {
      // MMIO register read: the MMIORange* (src1) + address (src2) are baked in
      // by the constant-propagation pass. Call range->read(ppc_ctx=x20,
      // callback_context, addr) and byte-swap (matches a64 CallNativeSafe + rev).
      auto* range = reinterpret_cast<xe::cpu::MMIORange*>(i->src1.offset);
      uint32_t addr = uint32_t(i->src2.offset);
      auto* i32 = b_.getInt32Ty();
      auto* fty = llvm::FunctionType::get(
          i32, {b_.getPtrTy(), b_.getPtrTy(), i32}, false);
      auto* fn = b_.CreateIntToPtr(
          b_.getInt64(reinterpret_cast<uint64_t>(range->read)), b_.getPtrTy());
      auto* cbctx = b_.CreateIntToPtr(
          b_.getInt64(reinterpret_cast<uint64_t>(range->callback_context)),
          b_.getPtrTy());
      auto* res = b_.CreateCall(fty, fn, {ctx_ptr_, cbctx, b_.getInt32(addr)});
      Def(i->dest, b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, res));
      baked_host_pointer_ = true;  // run-specific MMIO ptrs -> not AOT-cacheable
      return true;
    }
    case OPCODE_STORE_MMIO: {
      // MMIO register write: range->write(ppc_ctx=x20, callback_context, addr,
      // byteswap(value)). MMIORange* + addr baked in (src1/src2); value = src3.
      auto* range = reinterpret_cast<xe::cpu::MMIORange*>(i->src1.offset);
      uint32_t addr = uint32_t(i->src2.offset);
      auto* val = V(i->src3.value);
      if (!val) return false;
      val = b_.CreateUnaryIntrinsic(llvm::Intrinsic::bswap, val);
      auto* i32 = b_.getInt32Ty();
      auto* fty = llvm::FunctionType::get(
          llvm::Type::getVoidTy(ctx_), {b_.getPtrTy(), b_.getPtrTy(), i32, i32},
          false);
      auto* fn = b_.CreateIntToPtr(
          b_.getInt64(reinterpret_cast<uint64_t>(range->write)), b_.getPtrTy());
      auto* cbctx = b_.CreateIntToPtr(
          b_.getInt64(reinterpret_cast<uint64_t>(range->callback_context)),
          b_.getPtrTy());
      b_.CreateCall(fty, fn, {ctx_ptr_, cbctx, b_.getInt32(addr), val});
      baked_host_pointer_ = true;  // run-specific MMIO ptrs -> not AOT-cacheable
      return true;
    }
    case OPCODE_VECTOR_DENORMFLUSH: {
      // Per-lane: exp==0 (zero or denormal) -> keep only the sign bit (signed
      // zero); else unchanged. Identical result to a64 VECTOR_DENORMFLUSH and
      // exactly the VmxFlushDenorm helper.
      auto* v = V(i->src1.value);
      if (!v) return false;
      Def(i->dest, VmxFlushDenorm(b_.CreateBitCast(v, T(VEC128_TYPE))));
      return true;
    }
    case OPCODE_VECTOR_AVERAGE: {
      // Rounding halving add (a+b+1)>>1 per lane = a64 urhadd/srhadd. Integer
      // lanes only; signed vs unsigned from the ARITHMETIC_UNSIGNED flag.
      auto* a = V(i->src1.value);
      auto* c = V(i->src2.value);
      if (!a || !c) return false;
      TypeName pt = static_cast<TypeName>(i->flags & 0xFF);
      bool uns = ((i->flags >> 8) & ARITHMETIC_UNSIGNED) != 0;
      auto* lt = LaneVecTy(pt);
      if (!lt || pt == FLOAT32_TYPE) return false;
      auto id = uns ? llvm::Intrinsic::aarch64_neon_urhadd
                    : llvm::Intrinsic::aarch64_neon_srhadd;
      auto* r = b_.CreateIntrinsic(id, {lt},
                                   {b_.CreateBitCast(a, lt),
                                    b_.CreateBitCast(c, lt)});
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_SWIZZLE: {
      // INT32/FLOAT32 word swizzle within one vector: dest[k] = src[(mask>>2k)&3]
      // (32-bit lanes aren't byte-swap-remapped, VEC128_D(n)=n) -> a constant
      // shufflevector. The mask is the src2 offset; identity is folded to Assign
      // by the builder so it never reaches here.
      auto* v = V(i->src1.value);
      if (!v) return false;
      TypeName pt = static_cast<TypeName>(i->flags);
      if (pt != INT32_TYPE && pt != FLOAT32_TYPE) return false;
      uint32_t mask = static_cast<uint32_t>(i->src2.offset);
      auto* lt = LaneVecTy(INT32_TYPE);
      int m[4] = {int(mask & 3), int((mask >> 2) & 3), int((mask >> 4) & 3),
                  int((mask >> 6) & 3)};
      auto* r = b_.CreateShuffleVector(b_.CreateBitCast(v, lt),
                                       llvm::PoisonValue::get(lt),
                                       llvm::ArrayRef<int>(m, 4));
      Def(i->dest, b_.CreateBitCast(r, T(VEC128_TYPE)));
      return true;
    }
    case OPCODE_PERMUTE: {
      auto* s2 = V(i->src2.value);
      auto* s3 = V(i->src3.value);
      if (!s2 || !s3) return false;
      if (i->src1.value->type == INT32_TYPE) {
        // I32-control word permute (vmrghw/vmrglw): each control byte k selects a
        // dword (sel&3) from src2 (bit2=0) or src3 (bit2=1) -> 2-input shuffle.
        if (!i->src1.value->IsConstant()) return false;
        uint32_t ctrl = i->src1.value->constant.i32;
        auto* lt = LaneVecTy(INT32_TYPE);
        int m[4];
        for (int idx = 0; idx < 4; idx++) {
          uint8_t sel = (ctrl >> (idx * 8)) & 0xFF;
          m[idx] = int(sel & 3) + (((sel >> 2) & 1) ? 4 : 0);
        }
        Def(i->dest, b_.CreateBitCast(
                         b_.CreateShuffleVector(b_.CreateBitCast(s2, lt),
                                                b_.CreateBitCast(s3, lt),
                                                llvm::ArrayRef<int>(m, 4)),
                         T(VEC128_TYPE)));
        return true;
      }
      // V128-control byte/halfword permute across {src2, src3}.
      auto* i8x16 = LaneVecTy(INT8_TYPE);
      auto* a = b_.CreateBitCast(s2, i8x16);
      auto* bb = b_.CreateBitCast(s3, i8x16);
      TypeName pt = static_cast<TypeName>(i->flags);
      if (pt == INT8_TYPE) {
        // remap = (control ^ 3) & 0x1F (PPC byte index -> LE, 5-bit table range)
        // then a 2-table byte permute across {a, bb}.
        auto* c = V(i->src1.value);
        if (!c) return false;
        auto* remap = b_.CreateAnd(
            b_.CreateXor(b_.CreateBitCast(c, i8x16),
                         llvm::ConstantInt::get(i8x16, 3)),
            llvm::ConstantInt::get(i8x16, 0x1F));
        // Emit TWO single-table TBL1s OR'd, NOT one TBL2. DEVICE-CONFIRMED: the
        // aarch64.neon.tbl2 intrinsic needs its two tables in a CONSECUTIVE
        // register pair; with x20/x21 reserved + high register pressure the
        // AArch64 backend can't satisfy that and CRASHES in the AsmPrinter (a
        // wild-pointer re-fault storm inside libLLVM.so that freezes BD - the
        // intermittent storm root-caused 2026-06-27 to THIS op via range-limited
        // IR dump). tbl1 takes a single table (no pair constraint). For each lane
        // exactly one of the two TBL1s is in range (the other returns 0, since
        // tbl1 zeroes out-of-range indices), so OR reconstructs tbl2 exactly:
        //   tbl1(a, remap)        -> a[remap]      for remap in 0..15 else 0
        //   tbl1(bb, remap - 16)  -> bb[remap-16]  for remap in 16..31 else 0
        auto* remap_hi =
            b_.CreateSub(remap, llvm::ConstantInt::get(i8x16, 16));
        if (cvars::cpu_llvm_vperm_tbl2_probe) {
          // ONE-LAUNCH EXPERIMENT, not an optimisation. Emits the two-table
          // tbl2 that the comment below says crashes our AsmPrinter, so the
          // failure MODE can finally be observed instead of inferred.
          //
          // Upstream RPCS3 hit this exact wall on SPU SHFB and got past it by
          // catching LLVM's compile failure and retrying that one function with
          // the single-table form (3 fallbacks per 10,000 blocks, keeping an 8%
          // win). Whether that design ports depends entirely on WHICH failure
          // we have, and the two candidates are indistinguishable from the
          // outside:
          //   clean report_fatal_error -> now logged as "LLVMfatal: ..." by the
          //     handler installed in llvm_backend.cc. Retry design PORTS.
          //   memory corruption        -> process dies with nothing logged.
          //     Then tbl2 is not the real bug and the attribution is wrong.
          // Our libLLVM DOES contain "ran out of registers", so the clean path
          // exists in the binary - which is why this is worth one launch.
          //
          // Run: --ez cpu_llvm_vperm_tbl2_probe true, grep for LLVMfatal.
          // EXPECT IT TO CRASH. That is the point; do not ship it enabled.
          auto* tbl2 = b_.CreateIntrinsic(llvm::Intrinsic::aarch64_neon_tbl2,
                                          {i8x16}, {a, bb, remap});
          Def(i->dest, b_.CreateBitCast(tbl2, T(VEC128_TYPE)));
          return true;
        }
        auto* lo = b_.CreateIntrinsic(llvm::Intrinsic::aarch64_neon_tbl1,
                                      {i8x16}, {a, remap});
        if (cvars::cpu_llvm_vperm_tbx) {
          // TBX1 preserves the destination for out-of-range indices where TBL1
          // zeroes, so it folds the OR into the second table lookup: 3 uOPs
          // -> 2 on the 2-wide mid-core vector pipe.
          //
          // The correctness rests on UNSIGNED WRAPAROUND: for lanes indexing
          // the LOW table (remap 0..15), remap-16 wraps as u8 to 240..255,
          // which TBX treats as out of range and therefore preserves the tbl1
          // result. Proven exhaustively rather than argued -
          // tools/qemu/vperm_tbx_vs_tbl_or.c sweeps all 32 indices x 16 lanes
          // (512 cases) plus 7 shaped cases, 8/8 bit-identical on hardware.
          //
          // NOTE this stays on the SINGLE-table intrinsic, so it does NOT
          // reintroduce the consecutive-register-pair constraint described
          // below - that is the whole point of preferring it to tbl2.
          Def(i->dest,
              b_.CreateBitCast(
                  b_.CreateIntrinsic(llvm::Intrinsic::aarch64_neon_tbx1,
                                     {i8x16}, {lo, bb, remap_hi}),
                  T(VEC128_TYPE)));
          return true;
        }
        auto* hi = b_.CreateIntrinsic(llvm::Intrinsic::aarch64_neon_tbl1,
                                      {i8x16}, {bb, remap_hi});
        Def(i->dest,
            b_.CreateBitCast(b_.CreateOr(lo, hi), T(VEC128_TYPE)));
        return true;
      }
      if (pt == INT16_TYPE) {
        // Constant halfword control -> precomputed byte-level shuffle mask: PPC
        // halfword H maps to NEON u16 (H&7)^1, +16 bytes if H>=8 (from src3).
        if (!i->src1.value->IsConstant()) return false;
        auto& cv = i->src1.value->constant.v128;
        int m[16];
        for (int k = 0; k < 8; k++) {
          uint16_t h = cv.u16[k] & 0xF;
          int base = (h >= 8) ? 16 : 0;
          int neon_hw = (h & 7) ^ 1;
          m[2 * k] = base + 2 * neon_hw;
          m[2 * k + 1] = base + 2 * neon_hw + 1;
        }
        Def(i->dest,
            b_.CreateBitCast(
                b_.CreateShuffleVector(a, bb, llvm::ArrayRef<int>(m, 16)),
                T(VEC128_TYPE)));
        return true;
      }
      return false;
    }
    case OPCODE_ROUND: {
      // Round-to-integral, scalar f32/f64 or V128 f32x4. Mode from flags maps to
      // the frint variant: zero=trunc, nearest=roundeven (round-half-to-EVEN, not
      // llvm.round), -inf=floor, +inf=ceil, dynamic->nearest (baked-in mode).
      auto* a = V(i->src1.value);
      if (!a) return false;
      llvm::Intrinsic::ID id;
      switch (i->flags) {
        case ROUND_TO_ZERO: id = llvm::Intrinsic::trunc; break;
        case ROUND_TO_NEAREST: id = llvm::Intrinsic::roundeven; break;
        case ROUND_TO_MINUS_INFINITY: id = llvm::Intrinsic::floor; break;
        case ROUND_TO_POSITIVE_INFINITY: id = llvm::Intrinsic::ceil; break;
        default: id = llvm::Intrinsic::roundeven; break;
      }
      if (IsVec(a)) {
        auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
        Def(i->dest, b_.CreateBitCast(
                         b_.CreateUnaryIntrinsic(id, b_.CreateBitCast(a, f32x4)),
                         T(VEC128_TYPE)));
      } else {
        if (!a->getType()->isFloatingPointTy()) return false;
        Def(i->dest, b_.CreateUnaryIntrinsic(id, a));
      }
      return true;
    }
    case OPCODE_RECIP: {
      // vrefp / fres: FULL-precision 1.0/x (xenia implements the estimate as a
      // real divide). Scalar (FPU mode) = no flush; V128 (VMX) = denormal flush
      // in + out. Plain fdiv NaN (no PPC NaN fixup).
      auto* a = V(i->src1.value);
      if (!a) return false;
      if (IsVec(a)) {
        auto* f32x4 = LaneVecTy(FLOAT32_TYPE);
        auto* xi = VmxFlushDenorm(b_.CreateBitCast(a, T(VEC128_TYPE)));
        auto* r = b_.CreateFDiv(llvm::ConstantFP::get(f32x4, 1.0),
                                b_.CreateBitCast(xi, f32x4));
        Def(i->dest, VmxFlushDenorm(b_.CreateBitCast(r, T(VEC128_TYPE))));
      } else {
        if (!a->getType()->isFloatingPointTy()) return false;
        Def(i->dest,
            b_.CreateFDiv(llvm::ConstantFP::get(a->getType(), 1.0), a));
      }
      return true;
    }
    case OPCODE_RSQRT: {
      // Scalar f32: full-precision 1/sqrt(x) (a64 fsqrt+fdiv). F64 (frsqrte) and
      // V128 (vrsqrtefp) use the 360 lookup-table estimate via runtime helpers.
      auto* a = V(i->src1.value);
      if (!a) return false;
      if (a->getType()->isFloatTy()) {
        Def(i->dest,
            b_.CreateFDiv(llvm::ConstantFP::get(a->getType(), 1.0),
                          b_.CreateUnaryIntrinsic(llvm::Intrinsic::sqrt, a)));
        return true;
      }
      if (a->getType()->isDoubleTy()) {
        auto* i64 = b_.getInt64Ty();
        auto callee = mod_->getOrInsertFunction(
            "xe_llvm_frsqrte", llvm::FunctionType::get(i64, {i64}, false));
        auto* res = b_.CreateCall(callee, {b_.CreateBitCast(a, i64)});
        Def(i->dest, b_.CreateBitCast(res, b_.getDoubleTy()));
        return true;
      }
      if (IsVec(a)) {
        Def(i->dest, EmitVecLaneCall("xe_llvm_vrsqrte_lane", a));
        return true;
      }
      return false;
    }
    case OPCODE_LOG2: {
      // vlogefp: per-lane log2(float) via libm helper (V128 only).
      auto* a = V(i->src1.value);
      if (!a || !IsVec(a)) return false;
      Def(i->dest, EmitVecLaneCall("xe_llvm_log2_lane", a));
      return true;
    }
    case OPCODE_POW2: {
      // vexptefp: per-lane exp2(float) via libm helper (V128 only).
      auto* a = V(i->src1.value);
      if (!a || !IsVec(a)) return false;
      Def(i->dest, EmitVecLaneCall("xe_llvm_exp2_lane", a));
      return true;
    }

    default:
      // Unsupported (calls, other vectors, atomics, packs, ...) -> a64 fallback.
      return false;
  }
}

}  // namespace

bool LLVMAssembler::LowerAndJit(GuestFunction* function, HIRBuilder* builder) {
  auto* jit_holder = llvm_backend_->jit();
  if (!jit_holder || !jit_holder->jit) return false;
  auto& jit = *jit_holder->jit;

  // Serialize ALL LLVM compilation across guest threads. Each guest thread has
  // its own LLVMAssembler, but they share ONE LLJIT; BD starts several guest
  // threads (Main XThread, MainThread, ...) that hit uncompiled functions at the
  // same time, driving the LLJIT's codegen (AsmPrinter/MCAssembler) concurrently
  // -> heap corruption -> non-deterministic SIGBUS deep inside libLLVM
  // (MCAssembler::computeFragmentSize, device-pinned) or a bogus-ctx storm.
  // The lock is acquired AFTER xenia's compile/global lock (consistent order, no
  // deadlock) and released before the JIT'd code ever runs (helpers run lock-
  // free at runtime). Correctness-first; per-fn compile is one-time.
  // STORM GUARD (device-found): an intermittent libLLVM AArch64-codegen crash
  // re-faults forever WHILE HOLDING this mutex, which hangs every other guest
  // thread that then needs a compile -> BD stalls (0 fps). Degrade to a64 instead
  // of hanging: (1) if a fault storm is already active (some compile crashed and
  // is re-faulting), skip LLVM entirely; (2) acquire with a timeout so no thread
  // blocks forever on the dead mutex. The storming thread (stuck in the signal
  // handler) still burns its core, but BD keeps rendering on a64. The unhandled-
  // fault count is the definitive storm signal; the timeout only unblocks threads
  // already waiting when the storm began. 10s never false-trips a real compile
  // (those are milliseconds); a rare slow one merely uses a64 for that function.
  if (xe::ExceptionHandler::GetUnhandledFaultCount() != 0) {
    return false;
  }
  static std::timed_mutex s_llvm_compile_mutex;
  auto acquire_compile_lock =
      [&](std::unique_lock<std::timed_mutex>& guard) -> bool {
    guard = std::unique_lock<std::timed_mutex>(s_llvm_compile_mutex,
                                               std::chrono::seconds(10));
    if (!guard.owns_lock()) {
      XELOGW(
          "LLVMAssembler: compile lock timed out (a prior compile is stuck / "
          "storming) - falling back to a64 for 0x{:08X}",
          function->address());
      return false;
    }
    if (xe::ExceptionHandler::GetUnhandledFaultCount() != 0) {
      return false;  // a storm began while we waited for the lock
    }
    return true;
  };

  // Parallel lowering: run the thread-safe IR-build + O2 optimization OUTSIDE
  // the compile lock so multiple precompile workers overlap; serialize ONLY the
  // crash-prone codegen (addIRModule/lookup) below. Requires the object cache
  // OFF - its single-TargetMachine SimpleCompiler must stay fully serialized,
  // and its skip-lowering fast path (which also mutates the shared LLJIT) is
  // then inactive, so the only shared region left is the codegen at the bottom.
  const bool parallel_lowering =
      cvars::cpu_backend_llvm_parallel_lowering && !cvars::cpu_llvm_object_cache;

  std::unique_lock<std::timed_mutex> compile_guard;
  if (!parallel_lowering) {
    // Default: hold the lock across the WHOLE compile (original behavior).
    if (!acquire_compile_lock(compile_guard)) {
      return false;
    }
  }

  auto ctx_owner = std::make_unique<llvm::LLVMContext>();
  auto& ctx = *ctx_owner;

  // Named "guest" for now; if the AOT object cache is on, the module is renamed
  // to its cache KEY after lowering (the key must reflect whether lowering baked
  // a non-portable host pointer, only known post-Run). See below, pre-addIRModule.
  auto mod = std::make_unique<llvm::Module>("guest", ctx);
  mod->setDataLayout(jit.getDataLayout());
  mod->setTargetTriple(jit.getTargetTriple().str());

  std::string name = "guest_" + std::to_string(function->address());

  // AOT object cache INCREMENT 2 (cpu_llvm_object_cache_skip_lowering, the full
  // ReXGlue/RPCS3 precompile model): on a warm HIT, skip IR-build + the O2
  // pipeline + codegen ENTIRELY by loading the cached .o via addObjectFile.
  // (Increment 1's ObjectCache only skipped codegen - IR-build + O2 still ran per
  // fn on warm.) The key matches the post-lowering cache key WITHOUT the
  // "nocache_" prefix; baked-host-pointer fns never wrote that .o (increment 1
  // skips them) so they MISS here and fall through to a fresh IR-build (correct).
  // Serialized by the compile lock above (addObjectFile mutates the shared LLJIT).
  // Gated default-off; needs device validation of warm-load symbol resolution.
  // Report the four conjuncts ONCE. Device-observed 2026-08-07: 60,606 cached
  // objects for the right title with matching key cvars, and the miss
  // diagnostic below never fired - which proves execution never reaches the
  // lookup, i.e. one of these is false. Guessing between them wasted a run;
  // printing them costs one line and ends the question.
  {
    static std::atomic<bool> gate_reported{false};
    bool gate_expected = false;
    if (gate_reported.compare_exchange_strong(gate_expected, true)) {
      XELOGI(
          "LLVMobjcache GATE: object_cache={} skip_lowering={} path_set={} "
          "has_end={} end_gt_start={}",
          cvars::cpu_llvm_object_cache ? 1 : 0,
          cvars::cpu_llvm_object_cache_skip_lowering ? 1 : 0,
          !cvars::cpu_llvm_object_cache_path.empty() ? 1 : 0,
          function->has_end_address() ? 1 : 0,
          (function->has_end_address() &&
           function->end_address() > function->address())
              ? 1
              : 0);
    }
  }
  if (cvars::cpu_llvm_object_cache && cvars::cpu_llvm_object_cache_skip_lowering &&
      !cvars::cpu_llvm_object_cache_path.empty() &&
      function->has_end_address() &&
      function->end_address() > function->address()) {
    uint64_t code_hash = 0xcbf29ce484222325ull;  // FNV-1a (must match below)
    const uint32_t lo = function->address();
    const uint32_t hi = function->end_address();
    const uint8_t* bytes =
        llvm_backend_->processor()->memory()->TranslateVirtual<const uint8_t*>(lo);
    for (uint32_t off = 0, n = hi - lo; off < n; ++off) {
      code_hash = (code_hash ^ bytes[off]) * 0x100000001b3ull;
    }
    // Key must cover EVERY cvar that changes lowering, or a warm hit serves code
    // built under different semantics. writeback and abi both change the emitted
    // code (writeback removes the per-store context writes; abi drops the
    // post-call reloads of the ABI callee-saved regs) - they were missing here.
    // 2026-08-09: two MORE lowering cvars were missing, both added the same day
    // this key's own rule was written down - cpu_llvm_vperm_tbx (tbl1+tbx1 vs
    // tbl1+tbl1+orr) and cpu_llvm_lower_scalar_fma (scalar FMA lowered vs
    // dropped to a64). Without them in the key, `--ez cpu_llvm_vperm_tbx true`
    // against a warm cache serves objects compiled WITHOUT the lowering: the
    // A/B reads flat and the conclusion is "the lever does nothing", which is
    // the most expensive possible way to be wrong about a lever. The build
    // stamp in the DIRECTORY does not help here - both arms are the same build.
    char keybuf[160];
    std::snprintf(keybuf, sizeof(keybuf),
                  "g%08X_%016llX_o%dr%dw%da%dp%df%db%dv%dc%dq%dn%dx%dm%08X",
                  function->address(),
                  static_cast<unsigned long long>(code_hash),
                  cvars::cpu_backend_llvm_opt,
                  cvars::cpu_backend_llvm_context_residency ? 1 : 0,
                  cvars::cpu_backend_llvm_residency_writeback ? 1 : 0,
                  cvars::cpu_backend_llvm_residency_abi ? 1 : 0,
                  cvars::cpu_llvm_vperm_tbx ? 1 : 0,
                  cvars::cpu_llvm_lower_scalar_fma ? 1 : 0,
                  cvars::cpu_llvm_batch_lane_calls ? 1 : 0,
                  cvars::cpu_backend_llvm_lower_vmaddfp ? 1 : 0,
                  cvars::cpu_llvm_guest_call_clobber_barrier ? 1 : 0,
                  cvars::cpu_llvm_vector_qload ? 1 : 0,
                  cvars::cpu_llvm_vmx_float_flush ? 1 : 0,
                  cvars::cpu_llvm_vmx_fmax_nan ? 1 : 0,
                  LlvmTargetCpuKeyHash());
    std::filesystem::path opath =
        std::filesystem::path(cvars::cpu_llvm_object_cache_path) /
        ("objcache_v" + std::to_string(kLlvmObjectCacheVersion) + "_opt" +
         std::to_string(cvars::cpu_backend_llvm_opt) + "_b" +
         LlvmLoweringStampTagAsm()) /
        (std::string(keybuf) + ".o");
    std::error_code fs_ec;
    // Cache-miss diagnostic. A populated objcache that never hits means the KEY
    // moved, not that the cache is broken - and the key encodes four cvars
    // (opt / residency / writeback / abi) plus a code hash, so a single changed
    // default silently invalidates the entire directory and every launch
    // recompiles the whole title. Device-observed 2026-08-07: 264MB present,
    // zero hits. Logs the computed key ONCE alongside a filename that actually
    // exists, so the differing field is visible by inspection rather than by
    // guessing which cvar changed.
    if (!std::filesystem::exists(opath, fs_ec)) {
      static std::atomic<bool> miss_reported{false};
      bool expected = false;
      if (miss_reported.compare_exchange_strong(expected, true)) {
        std::string sample = "(cache dir empty or unreadable)";
        std::error_code it_ec;
        std::filesystem::directory_iterator it(opath.parent_path(), it_ec);
        if (!it_ec) {
          for (const auto& e : it) {
            sample = e.path().filename().string();
            break;
          }
        }
        XELOGW(
            "LLVMobjcache MISS: want='{}' but dir has e.g. '{}' (dir='{}') - if "
            "only the o/r/w/a suffix differs, a cvar changed since the cache was "
            "built and EVERY lookup will miss",
            opath.filename().string(), sample, opath.parent_path().string());
      }
    }
    if (std::filesystem::exists(opath, fs_ec)) {
      auto buf = llvm::MemoryBuffer::getFile(opath.string());
      if (buf) {
        if (auto err = jit.addObjectFile(std::move(*buf))) {
          llvm::consumeError(std::move(err));  // load failed -> fresh IR-build
        } else {
          auto sym = jit.lookup(name);
          if (sym) {
            auto* code = reinterpret_cast<uint8_t*>(sym->getValue());
            static_cast<a64::A64Function*>(function)->Setup(code, 0);
            XELOGI(
                "LLVMobjload guest=0x{:08X} host=0x{:016X} (skipped IR+lowering)",
                function->address(), reinterpret_cast<uint64_t>(code));
            return true;
          }
          llvm::consumeError(sym.takeError());  // symbol missing -> fresh build
        }
      }
    }
  }
  // void guest_<addr>(i64 x0_guest_return_address): matches the a64 ABI - the
  // host->guest thunk passes the guest return address in x0, and it's used to
  // recognize a guest `blr` RETURN in CALL_INDIRECT.
  auto* fn_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx),
                                        {llvm::Type::getInt64Ty(ctx)}, false);
  auto* fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, name,
                                    mod.get());
  // Reserve x20 (guest ctx) / x21 (membase) for this function's codegen so the
  // allocator never clobbers them (set by the host->guest thunk, read via
  // @llvm.read_register). Per-function attribute, not the JTMB (which hangs
  // create() under qemu).
  // ALSO disable SVE/SVE2/SME: detectHost enables SVE2 on the Cortex-X3, but it
  // is NOT usable on the Thor's Android (executing an SVE instruction faults
  // SIGILL) - DEVICE-CONFIRMED 2026-06-27: llvm.memset codegen emitted an SVE
  // instruction (insn=0x04A84D02, code=2 SIGILL storm). Forcing -sve keeps all
  // codegen on NEON/scalar (NEON = +fp-armv8/+neon, unaffected), the only SIMD
  // the device supports. This is what unblocks MEMSET (and any future op LLVM
  // would otherwise vectorize via SVE) instead of falling back to a64.
  fn->addFnAttr("target-features", GetLlvmTargetFeatures());
  // target-cpu selects the SCHEDULING MODEL. Set alongside target-features so
  // it rides in the cached IR exactly the same way.
  if (!cvars::cpu_llvm_target_cpu.empty()) {
    fn->addFnAttr("target-cpu", cvars::cpu_llvm_target_cpu);
  }

  Lowerer lowerer(ctx, mod.get(), fn, function->address());
  if (!lowerer.Run(builder)) {
    return false;  // unsupported opcode -> caller falls back to a64
  }

  {
    // Capture the verifier's reason (errs() isn't in logcat) so invalid-IR
    // codegen bugs are diagnosable instead of a silent a64 fallback.
    std::string verr;
    llvm::raw_string_ostream vos(verr);
    if (llvm::verifyFunction(*fn, &vos)) {
      XELOGE("LLVMAssembler: verifyFunction failed for {}: {}", name, verr);
      return false;
    }
  }

  // Optimize (mem2reg/SROA/GVN/instcombine/...) for residency before codegen.
  // O2/O3 give the win but are very slow emulated under qemu, so the opt level
  // is a cvar (0 = skip = fast device-free correctness tests).
  int opt = cvars::cpu_backend_llvm_opt;
  if (opt > 0) {
    llvm::PassBuilder pb;
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cgam);
    pb.registerFunctionAnalyses(fam);
    pb.registerLoopAnalyses(lam);
    pb.crossRegisterProxies(lam, fam, cgam, mam);
    auto level = opt >= 3   ? llvm::OptimizationLevel::O3
                 : opt == 1 ? llvm::OptimizationLevel::O1
                            : llvm::OptimizationLevel::O2;
    auto mpm = pb.buildPerModuleDefaultPipeline(level);
    mpm.run(*mod, mam);
  }

  if (cvars::cpu_backend_llvm_dump_ir) {
    // Post-opt IR, module still owned locally (before addIRModule moves it).
    // Per-line so logcat doesn't truncate. Read off codegen bugs device-free.
    std::string ir;
    llvm::raw_string_ostream os(ir);
    fn->print(os);
    size_t pos = 0;
    while (pos < ir.size()) {
      size_t nl = ir.find('\n', pos);
      if (nl == std::string::npos) nl = ir.size();
      XELOGI("LLVMir {}: {}", name, ir.substr(pos, nl - pos));
      pos = nl + 1;
    }
  }
  if (cvars::cpu_backend_llvm_dump_asm) {
    // Post-codegen ASSEMBLY for this fn (the IR->asm step where device codegen/
    // regalloc bugs live; dump_ir only shows IR). Emits via a detectHost
    // TargetMachine matching the JIT; the per-fn reserve-x20/x21 + -sve attrs ride
    // along in the cloned IR. Use with _range_lo/_hi to dump ONE fn. grep 'LLVMasm'.
    if (auto tmb = llvm::orc::JITTargetMachineBuilder::detectHost()) {
      if (auto tm_or = tmb->createTargetMachine()) {
        auto tm = std::move(*tm_or);
        auto clone = llvm::CloneModule(*mod);
        clone->setDataLayout(tm->createDataLayout());
        llvm::SmallString<0> sv;
        llvm::raw_svector_ostream os(sv);
        llvm::legacy::PassManager pm;
        if (!tm->addPassesToEmitFile(pm, os, nullptr,
                                     llvm::CodeGenFileType::AssemblyFile)) {
          pm.run(*clone);
          std::string a(sv.begin(), sv.end());
          size_t pos = 0;
          while (pos < a.size()) {
            size_t nl = a.find('\n', pos);
            if (nl == std::string::npos) nl = a.size();
            XELOGI("LLVMasm {}: {}", name, a.substr(pos, nl - pos));
            pos = nl + 1;
          }
        } else {
          XELOGW("LLVMasm: addPassesToEmitFile unsupported for {}", name);
        }
      } else {
        llvm::consumeError(tm_or.takeError());
      }
    } else {
      llvm::consumeError(tmb.takeError());
    }
  }
  // AOT object cache key: rename the module to its cache KEY just before the
  // compile layer consults the ObjectCache (which keys on getModuleIdentifier).
  // Key = "g<addr>_<codehash>": guest address + FNV-1a of the guest CODE BYTES,
  // so a cached .o is reused only for byte-identical code (disambiguates titles,
  // game versions, self-modified code). If lowering baked a run-specific host
  // pointer (CALL_EXTERN target / MMIO ptr), the code is NOT portable across
  // launches -> prefix "nocache_" so getObject/notifyObjectCompiled skip it and
  // it always codegens fresh. Off => stays "guest" (no caching).
  if (cvars::cpu_llvm_object_cache &&
      !cvars::cpu_llvm_object_cache_path.empty()) {
    uint64_t code_hash = 0xcbf29ce484222325ull;  // FNV-1a offset basis
    if (function->has_end_address() &&
        function->end_address() > function->address()) {
      const uint32_t lo = function->address();
      const uint32_t hi = function->end_address();
      const uint8_t* bytes =
          llvm_backend_->processor()->memory()->TranslateVirtual<const uint8_t*>(
              lo);
      for (uint32_t off = 0, n = hi - lo; off < n; ++off) {
        code_hash = (code_hash ^ bytes[off]) * 0x100000001b3ull;
      }
    }
    // The key must also disambiguate CODEGEN CONFIG: opt level and guest-register
    // residency produce different native code from the same guest bytes. Without
    // this suffix a residency-on run would load a stale non-resident .o (a silent
    // perf regression - correct code, but none of the residency win), and opt=0
    // qemu .o's would leak into an opt=2 device run.
    // "_o<opt>r<context_residency>w<writeback>a<abi>" - writeback and abi ALSO
    // change the emitted code (writeback drops the per-store context writes; abi
    // drops the post-call reloads of the ABI callee-saved regs) and were missing
    // from this key, so a warm hit could serve a .o built under different
    // residency semantics than the run requested. Cache version bumped to 2.
    // ⚠️ THIS STRING AND THE `opath` KEY ABOVE MUST STAY IDENTICAL. They are two
    // constructions of the SAME key: `opath` drives the skip-lowering fast path
    // in this file, while THIS one becomes the module identifier that
    // XeLlvmObjectCache turns into the actual filename. Change one without the
    // other and the fast path looks for a name the cache never writes, so every
    // launch recompiles the whole title and the cache silently does nothing.
    //
    // That is not hypothetical - it happened on 2026-08-09, when p/f were added
    // to `opath` alone. Device symptom: gate all-1s, "LLVMobjcache MISS: want=
    // '...o2r0w0a0p0f0.o' but dir has '...o2r0w0a0.o'", and ~14k functions
    // recompiled every single launch (the 60-150s hot startup that was eating
    // the thermal budget of every measurement).
    char idbuf[176];
    std::snprintf(idbuf, sizeof(idbuf),
                  "%sg%08X_%016llX_o%dr%dw%da%dp%df%db%dv%dc%dq%dn%dx%dm%08X",
                  lowerer.baked_host_pointer() ? "nocache_" : "",
                  function->address(),
                  static_cast<unsigned long long>(code_hash),
                  cvars::cpu_backend_llvm_opt,
                  cvars::cpu_backend_llvm_context_residency ? 1 : 0,
                  cvars::cpu_backend_llvm_residency_writeback ? 1 : 0,
                  cvars::cpu_backend_llvm_residency_abi ? 1 : 0,
                  cvars::cpu_llvm_vperm_tbx ? 1 : 0,
                  cvars::cpu_llvm_lower_scalar_fma ? 1 : 0,
                  cvars::cpu_llvm_batch_lane_calls ? 1 : 0,
                  cvars::cpu_backend_llvm_lower_vmaddfp ? 1 : 0,
                  cvars::cpu_llvm_guest_call_clobber_barrier ? 1 : 0,
                  cvars::cpu_llvm_vector_qload ? 1 : 0,
                  cvars::cpu_llvm_vmx_float_flush ? 1 : 0,
                  cvars::cpu_llvm_vmx_fmax_nan ? 1 : 0,
                  LlvmTargetCpuKeyHash());
    mod->setModuleIdentifier(idbuf);
  }

  // Diagnostic: log the function address IMMEDIATELY BEFORE codegen (addIRModule
  // -> lazy lookup triggers libLLVM AsmPrinter/MC). A function whose codegen
  // CRASHES libLLVM (the intermittent re-fault storm device-pinned to libLLVM.so)
  // never reaches its LLVMmap/LLVMseq line, so the LAST "LLVMbegin" with no
  // matching "LLVMmap" pins the crashing guest function. Cheap; one line/fn.
  // Serialize codegen only: in parallel-lowering mode the wide lock was NOT
  // taken above, so grab it now around the crash-prone addIRModule/lookup (the
  // historical libLLVM AsmPrinter SIGBUS site). In default mode compile_guard
  // already holds it. This keeps concurrent codegen impossible either way.
  std::unique_lock<std::timed_mutex> codegen_guard;
  if (parallel_lowering) {
    if (!acquire_compile_lock(codegen_guard)) {
      return false;
    }
  }
  XELOGI("LLVMbegin guest=0x{:08X}", function->address());
  if (auto err = jit.addIRModule(
          llvm::orc::ThreadSafeModule(std::move(mod), std::move(ctx_owner)))) {
    XELOGE("LLVMAssembler: addIRModule failed: {}",
           llvm::toString(std::move(err)));
    return false;
  }
  auto sym = jit.lookup(name);
  if (!sym) {
    XELOGE("LLVMAssembler: lookup failed: {}",
           llvm::toString(sym.takeError()));
    return false;
  }
  auto* code = reinterpret_cast<uint8_t*>(sym->getValue());
  static_cast<a64::A64Function*>(function)->Setup(code, 0);
  // Host<->guest map for localizing on-device faults in JIT'd code: grep
  // "LLVMmap" and find the entry whose host addr is the greatest <= a faulting
  // pc. (Use `adb logcat -G 16M` so early entries don't wrap before the fault.)
  XELOGI("LLVMmap guest=0x{:08X} host=0x{:016X}", function->address(),
         reinterpret_cast<uint64_t>(code));
  return true;
}

#else  // !XE_LLVM_BACKEND_ENABLED

bool LLVMAssembler::LowerAndJit(GuestFunction*, HIRBuilder*) { return false; }

#endif  // XE_LLVM_BACKEND_ENABLED

bool LLVMAssembler::Assemble(GuestFunction* function, hir::HIRBuilder* builder,
                             uint32_t debug_info_flags,
                             std::unique_ptr<FunctionDebugInfo> debug_info) {
#if XE_LLVM_BACKEND_ENABLED
  // SAFETY GATE (default on): after the title's main thread launches, never
  // run libLLVM again - a runtime-discovered function compiles on the stable
  // a64 fallback. Confines the known intermittent libLLVM codegen crash to
  // the load window (behind the compile screen), where a failure is a launch
  // retry instead of a mid-race process death.
  if (cvars::cpu_llvm_no_runtime_compiles &&
      backend_->processor()->is_aot_runtime_phase()) {
    return fallback_->Assemble(function, builder, debug_info_flags,
                               std::move(debug_info));
  }
  // Range gate (bisection): only LLVM-compile functions in [range_lo, range_hi);
  // the rest use a64. Used to localize which function's LLVM codegen corrupts
  // state. Empty (default) bounds = no restriction = compile everything.
  uint32_t addr = function->address();
  // Force-a64 skip list (cpu_backend_llvm_skip_addrs): mitigate a libLLVM
  // codegen-crashing function by falling it back to a64 WITHOUT a rebuild. Parsed
  // once into a set (Assemble runs per-function).
  static const std::unordered_set<uint32_t> s_skip_addrs = [] {
    std::unordered_set<uint32_t> s;
    const std::string& v = cvars::cpu_backend_llvm_skip_addrs;
    size_t i = 0;
    while (i < v.size()) {
      while (i < v.size() && (v[i] == ',' || v[i] == ' ')) ++i;
      size_t j = i;
      while (j < v.size() && v[j] != ',' && v[j] != ' ') ++j;
      if (j > i) {
        s.insert(uint32_t(std::strtoull(v.substr(i, j - i).c_str(), nullptr, 16)));
      }
      i = j;
    }
    return s;
  }();
  if (!s_skip_addrs.empty() && s_skip_addrs.count(addr)) {
    return fallback_->Assemble(function, builder, debug_info_flags,
                               std::move(debug_info));
  }
  const std::string& lo_s = cvars::cpu_backend_llvm_range_lo;
  const std::string& hi_s = cvars::cpu_backend_llvm_range_hi;
  uint32_t lo = lo_s.empty() ? 0 : uint32_t(std::strtoull(lo_s.c_str(), nullptr, 16));
  uint32_t hi = hi_s.empty() ? 0 : uint32_t(std::strtoull(hi_s.c_str(), nullptr, 16));
  bool in_range = addr >= lo && (hi == 0 || addr < hi);
  // Count gate (monotonic bisection): LLVM-compile at most the first N functions
  // (in compile order). The counter increments only on SUCCESSFUL LLVM compiles,
  // so Set(N) is a strict subset of Set(N+1) -> binary-searching N pins the fn
  // whose LLVM codegen breaks boot. LLVMseq logs seq->addr for the map.
  static std::atomic<int32_t> s_llvm_seq{0};
  int32_t max_fns = cvars::cpu_backend_llvm_max_fns;
  bool under_cap = (max_fns <= 0) || (s_llvm_seq.load(std::memory_order_relaxed) <
                                      max_fns);
  if (in_range && under_cap && LowerAndJit(function, builder)) {
    int32_t seq = s_llvm_seq.fetch_add(1, std::memory_order_relaxed);
    XELOGI("LLVMseq {} guest=0x{:08X}", seq, addr);
    function->set_debug_info(std::move(debug_info));
    return true;
  }
#endif
  // Fall back to the a64 per-block JIT for anything the LLVM path can't lower.
  return fallback_->Assemble(function, builder, debug_info_flags,
                             std::move(debug_info));
}

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe
