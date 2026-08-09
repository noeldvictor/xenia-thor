/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/llvm/llvm_backend.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/cpu/backend/llvm/llvm_assembler.h"
#include "xenia/cpu/backend/llvm/llvm_jit_context.h"
#include "xenia/cpu/backend/llvm/llvm_object_cache.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/hir/opcodes.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"

// P0 gating: defined by the build once libLLVM is cross-built + linked for
// android-arm64 (LLVM 20.1.8, AArch64-only, ORC + JITLink). Until then the
// backend is a compile-only skeleton and IsAvailable() returns false so the
// processor keeps the a64 backend.
#ifndef XE_LLVM_BACKEND_ENABLED
#define XE_LLVM_BACKEND_ENABLED 0
#endif

#if XE_LLVM_BACKEND_ENABLED
#include <filesystem>

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/TaskDispatch.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

// Short, stable hash of the lowering TU's compile stamp - folded into the cache
// directory so a rebuilt lowering cannot be served objects from the old one.
// FNV-1a, 32-bit, rendered as 8 hex chars: short enough to keep the path sane,
// wide enough that two builds colliding is not a practical concern.
static std::string LlvmLoweringStampTag() {
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
#endif  // XE_LLVM_BACKEND_ENABLED

DEFINE_bool(cpu_backend_llvm, false,
            "Use the LLVM-JIT CPU backend (whole-function recompile at module "
            "load for register residency) for guest functions it can lower, "
            "falling back to the a64 per-block JIT otherwise. Requires a "
            "libLLVM build (XE_LLVM_BACKEND_ENABLED). See "
            "docs/research/20260626-llvm-jit-backend-build-plan.md.",
            "CPU");

DEFINE_int32(cpu_backend_llvm_opt, 2,
             "LLVM middle-end optimization level for the LLVM-JIT backend "
             "(0=none/fast-correctness, 1=O1, 2=O2, 3=O3). O2/O3 give the "
             "register-residency win but are slow to run under qemu-user "
             "(emulated); set 0 for device-free correctness tests.",
             "CPU");

DEFINE_string(cpu_backend_llvm_range_lo, "",
              "Only LLVM-compile guest functions with address >= this (hex, "
              "e.g. 82467198). With _range_hi, restricts the LLVM backend to a "
              "guest-address window (the rest use a64) - used to BISECT which "
              "function's LLVM codegen corrupts state. Empty = no lower bound.",
              "CPU");
DEFINE_string(cpu_backend_llvm_range_hi, "",
              "Only LLVM-compile guest functions with address < this (hex). "
              "Empty = no upper bound (compile all in range). See _range_lo.",
              "CPU");

DEFINE_int32(cpu_backend_llvm_max_fns, 0,
             "LLVM-compile at most the FIRST N guest functions (in compile "
             "order); the rest use a64. 0 = unlimited. Monotonic - Set(N) is a "
             "subset of Set(N+1) - so binary-searching N pins the exact function "
             "whose LLVM codegen breaks boot (no address-bisection confound). "
             "Each accepted compile logs 'LLVMseq <n> guest=0x..' for the map.",
             "CPU");

DEFINE_bool(cpu_backend_llvm_dump_ir, false,
            "Dump the generated LLVM IR (post-opt) for each LLVM-compiled guest "
            "function to the log (grep 'LLVMir'). Use with _range_lo/_hi to dump "
            "a single function and read off codegen bugs device-free-ishly.",
            "CPU");

DEFINE_bool(cpu_backend_llvm_dump_asm, false,
            "Dump the post-CODEGEN AArch64 ASSEMBLY for each LLVM-compiled guest "
            "function to the log (grep 'LLVMasm'), via a detectHost TargetMachine "
            "matching the JIT. Unlike _dump_ir (which shows correct-looking IR), "
            "this shows the IR->asm output where device codegen/regalloc bugs live. "
            "Use with _range_lo/_hi to dump ONE function's machine code.",
            "CPU");

DEFINE_bool(cpu_backend_llvm_context_residency, false,
            "LLVM backend: promote guest registers (LOAD/STORE_CONTEXT) to "
            "entry-block ALLOCAS that mem2reg lifts into host registers, instead "
            "of the direct ctx+offset memory accesses the backend emits today. "
            "Device-confirmed the IR has ~99 ctx memory ops + 1 alloca = NO "
            "register residency (the guest thread is memory-bound, ~half of BD's "
            "field CPU). Guest regs are loaded from the context once at entry, "
            "used as allocas (-> registers), and written back to the context at "
            "call/return barriers (the callee/caller reads the context via x20). "
            "Default off pending qemu-differential + device A/B validation.",
            "CPU");

DEFINE_bool(
    cpu_backend_llvm_residency_writeback, false,
    "LLVM backend (requires cpu_backend_llvm_context_residency): the RPCS3-class "
    "residency - STORE_CONTEXT writes ONLY the entry alloca (mem2reg -> host "
    "register), NOT the context memory, so the ~372 per-store context writes the "
    "write-through path leaves behind are ELIMINATED. Guest regs are flushed back "
    "to the context ONLY at call/return barriers (write-back before each call/tail/"
    "return; reload after each call). Mixed-type (union) offsets stay write-through "
    "for safety. This is the #1 LLVM perf lever (guest thread is memory-bound). "
    "Default off pending qemu-differential + device validation.",
    "CPU");

DEFINE_bool(
    cpu_backend_llvm_residency_abi, false,
    "LLVM backend (requires cpu_backend_llvm_residency_writeback): ABI-aware "
    "cross-function residency (XenonRecomp non_volatile_as_local / Box64 CALLRET "
    "class, the #1 lever toward big CPU speedups). After a guest call, DON'T "
    "reload the mirrors for the PPC ABI callee-saved registers (GPR r14-r31, FPR "
    "f14-f31, VMX v14-v31 AND v64-v127 - the 360's VMX128 non-volatile set, "
    "matched to the __savevmx_* helpers, NOT the narrower standard AltiVec "
    "v20-v31) - an ABI-compliant callee preserves them, so the "
    "pre-call alloca values are still correct, and skipping the reload lets LLVM "
    "keep those ~18+ guest registers RESIDENT in host callee-saved registers "
    "ACROSS the call instead of round-tripping every one through the context at "
    "each call boundary (the residency trap that makes the guest thread "
    "memory-bound). Volatile regs (r0/r3-r12, LR/CTR/XER/CR) are still reloaded. "
    "ASSUMES ABI compliance (like XenonRecomp) - a callee that clobbers a "
    "non-volatile without restoring would desync; validate with the qemu-a64 "
    "byte-differential + device render before enabling. Default off.",
    "CPU");

DEFINE_string(cpu_backend_llvm_skip_addrs, "",
              "Comma/space-separated hex guest addresses to FORCE onto the a64 "
              "backend (skip LLVM) WITHOUT a rebuild. Use to pin/mitigate a "
              "function whose LLVM codegen crashes libLLVM (the intermittent "
              "AsmPrinter re-fault storm): find the last 'LLVMbegin' with no "
              "matching 'LLVMmap' in logcat, then list that address here.",
              "CPU");

DEFINE_string(cpu_backend_llvm_skip_opcodes, "",
              "Comma/space-separated DECIMAL HIR opcode numbers (xe::cpu::hir::"
              "Opcode enum order) to FORCE onto the a64 backend: any guest function "
              "that uses one of these opcodes falls back entirely to a64. Bisects "
              "WHICH opcode's LLVM lowering corrupts a scene (e.g. the BD post-load "
              "3D 'cyan-bars' miscompile) WITHOUT a rebuild - skip a swath, see if "
              "the corruption clears, then narrow. e.g. 110=UNPACK, 109=PACK, "
              "82=RSQRT, 86/87=DOT_PRODUCT_3/4, 77/78=MUL_ADD/SUB.",
              "CPU");

DEFINE_bool(cpu_backend_llvm_lower_vmaddfp, false,
            "Lower VECTOR MUL_ADD/MUL_SUB (vmaddfp/vnmsubfp) in the LLVM backend. "
            "DEFAULT FALSE = fall back to the a64 backend for any guest function "
            "using vector vmaddfp. WHY: the LLVM lowering is qemu-byte-correct in "
            "isolation, but on-device it MISCOMPILES a function that uses vmaddfp "
            "TOGETHER with other vector ops (BD's vertex-transform routine, e.g. "
            "0x82282490) -> degenerate geometry ('cyan-bars'), at opt=0 AND opt=2. "
            "It is a device codegen/regalloc INTERACTION bug (the IR is correct), "
            "not fixable from the IR; a64's vmaddfp (FPCR-mode + lazy NaN fixup) is "
            "correct, so falling back renders BD's 3D correctly (device-proven via "
            "cpu_backend_llvm_skip_opcodes=77). Set TRUE only to re-investigate the "
            "lowering (needs device asm-level debugging). See memory "
            "bd-llvm-postload-3d-cyan-bug.",
            "CPU");

DEFINE_string(cpu_backend_llvm_trace_addr, "",
              "Hex guest address. When an LLVM-compiled CALLER invokes this guest "
              "fn via xe_llvm_guest_call, log its input regs (r3/r4/r5/r1/lr) "
              "before and output (r3) after the call (grep 'LLVMtrace'). Compare "
              "across max_fns=K-1 (callee a64) vs K (callee LLVM) on the same "
              "deterministic input to pin a miscompiled fn's exact value bug.",
              "CPU");

DEFINE_bool(cpu_llvm_no_runtime_compiles, true,
            "LLVM compiles ONLY during the load window (the AOT precompile "
            "pass, before the title's main thread launches). Any function "
            "first discovered DURING gameplay is compiled by the stable a64 "
            "fallback instead of libLLVM - the known intermittent libLLVM "
            "codegen crash (AsmPrinter) can therefore never kill a running "
            "game (device-observed on Burnout Revenge 2026-07-31). With the "
            "AOT precompiler's ~97%+ coverage the perf cost of a64-compiled "
            "runtime misses is negligible. Set false to restore always-LLVM.",
            "CPU");
DEFINE_bool(cpu_llvm_object_cache, false,
            "AOT object cache: persist each LLVM-compiled guest function's "
            "machine code (.o) to disk and reuse it on the next launch / on "
            "re-entry, skipping the LLVM codegen (AsmPrinter/MCAssembler) that is "
            "~3-6% of BD CPU during play and the whole relaunch warm-up. The .o "
            "keeps external calls (xe_llvm_*) UNRESOLVED and is re-linked by name "
            "per load, so it is ASLR-safe across runs. The cache key is the guest "
            "address + a hash of the guest CODE BYTES, so a .o is reused only for "
            "byte-identical code (disambiguates titles, game versions, and "
            "self-modified code); a per-version+opt subdir guards against xenia "
            "lowering changes. RPCS3-style precompile builds on this. Needs "
            "cpu_llvm_object_cache_path. Default off pending device validation.",
            "CPU");
DEFINE_string(cpu_llvm_object_cache_path, "",
              "Directory for the LLVM AOT object cache (set by the Android app to "
              "its private files dir). MUST be per-title-safe; the code-byte hash "
              "in the key disambiguates titles even if shared. Empty disables the "
              "object cache regardless of cpu_llvm_object_cache.",
              "CPU");
DEFINE_bool(cpu_llvm_object_cache_skip_lowering, false,
            "AOT object cache INCREMENT 2 (the full ReXGlue/RPCS3 precompile model). "
            "Requires cpu_llvm_object_cache. On a warm cache HIT, skip the LLVM "
            "IR-build AND the O2 optimization pipeline AND codegen ENTIRELY by "
            "loading the cached .o directly via LLJIT::addObjectFile (increment 1's "
            "ObjectCache only skipped codegen - the per-fn IR-build + O2 still ran "
            "every warm launch). This is the real relaunch/revisit/stutter win. "
            "Baked-host-pointer fns (CALL_EXTERN/MMIO) never wrote a cacheable .o so "
            "they miss and rebuild fresh (correct). Default off pending device "
            "validation of warm-load symbol resolution. Helps boot/stutter, not the "
            "GPU-bound steady-state field.",
            "CPU");

#if XE_LLVM_BACKEND_ENABLED
// Runtime helper the JIT'd code calls for a guest CALL/CALL_INDIRECT/CALL_EXTERN:
// resolve the target guest function and invoke it. x20/x21 (ctx/membase) are
// AAPCS callee-saved across this C call so they survive; the callee re-derives
// them via the host->guest thunk inside Call(). Correctness-first (resolve per
// call); the indirection-table fast path is a later perf step.
//   ret_addr = the guest return address the guest's SET_RETURN_ADDRESS stashed;
//   it is forwarded to the callee as its x0 (guest return address) by the
//   host->guest thunk, so the callee can recognize its own `blr` RETURN
//   (CALL_INDIRECT target == x0). Previously this passed context()->lr, which
//   this backend does not maintain -> callees never matched their return and
//   every guest return became a forward call -> host stack overflow.
// Lock-free direct-mapped cache: guest target addr -> resolved Function*. The
// EntryTable hash lookup (std::unordered_map<uint32, Entry_t*>::find) is the top
// host CPU hot spot on the guest-call path (~3% of BD CPU, device-profiled
// 2026-06-27); this skips it on hits. Process-wide; benign races (a miss just
// re-resolves). A resolved Function* is stable - the EntryTable owns it and
// patches apply at module load, before execution.
namespace {
constexpr uint32_t kResolveCacheSize = 1u << 13;  // 8192 entries (64 KB)
// SELF-VALIDATING: one atomic<Function*> per slot, no separate target field.
// On a hit we verify fn->address() == target, so a stale entry (index
// collision) OR a torn multi-writer update simply fails the check and
// re-resolves - there is no way to return the WRONG Function* (a separate
// target+fn pair is racy: an interleaved update can leave target=tA,fn=fnB and
// hand back fnB for tA -> wild call -> fault storm; this avoids that entirely).
std::atomic<xe::cpu::Function*> g_resolve_cache[kResolveCacheSize];
inline xe::cpu::Function* xe_llvm_resolve_cached(xe::cpu::ThreadState* ts,
                                                 uint32_t target) {
  auto& slot = g_resolve_cache[(target >> 2) & (kResolveCacheSize - 1)];
  // RELAXED load (was acquire): the resolved Function* and its fields are stable
  // before any execution (EntryTable owns it; patches apply at module load), so
  // there is no producer->consumer data to acquire here - and the self-validating
  // fn->address()==target check below rejects any stale/torn read (re-resolves).
  // Drops the ldar barrier on this per-guest-call hot path.
  auto* fn = slot.load(std::memory_order_relaxed);
  if (fn && fn->address() == target) return fn;
  fn = ts->processor()->ResolveFunction(target);
  if (fn) slot.store(fn, std::memory_order_release);
  return fn;
}
}  // namespace

extern "C" void xe_llvm_guest_call(void* ctx, uint32_t target,
                                   uint32_t ret_addr) {
  // ts from the guest context (x20, passed by the JIT'd caller) instead of a
  // thread_local lookup: ThreadState::Get() is a TLS read (emutls /
  // pthread_getspecific) that cost ~5% of BD CPU per guest call (device-profiled
  // 2026-06-28). ctx->thread_state (offset 0) is the same value, one load off x20.
  auto* ts = reinterpret_cast<xe::cpu::ppc::PPCContext*>(ctx)->thread_state;
  // VALUE TRACE (debug, off by default via cpu_backend_llvm_trace_addr): log a
  // target's input regs before the call and r3/nonvolatiles after, to pin a
  // miscompiled callee. Cheap when disabled (s_trace_addr == 0 short-circuits).
  // NOTE: the old per-call thread_local depth probe was REMOVED - it cost an
  // __emutls_get_address per guest call (~1.8% of BD CPU) and only existed to
  // diagnose the (since-fixed) stack-overflow storm; the UNHANDLED-fault log is
  // the storm detector now.
  static uint32_t s_trace_addr = [] {
    const std::string& s = cvars::cpu_backend_llvm_trace_addr;
    return s.empty() ? 0u : uint32_t(std::strtoull(s.c_str(), nullptr, 16));
  }();
  bool trace = (s_trace_addr != 0) && (target == s_trace_addr);
  static std::atomic<uint32_t> s_trace_n{0};
  uint32_t tn = 0;
  if (trace) {
    auto* c = ts->context();
    tn = s_trace_n.fetch_add(1, std::memory_order_relaxed);
    if (tn < 40) {
      XELOGE(
          "LLVMtrace #{} ENTER 0x{:08X} r3=0x{:X} | r28=0x{:X} r29=0x{:X} "
          "r30=0x{:X} r31=0x{:X}",
          tn, target, c->r[3], c->r[28], c->r[29], c->r[30], c->r[31]);
    }
  }
  auto* fn = xe_llvm_resolve_cached(ts, target);
  if (fn) {
    // ts is the current thread's (ctx->thread_state above), so for GuestFunctions
    // skip Call()'s per-call ThreadState::Get() TLS lookup + bind and invoke
    // CallImpl directly. Builtins keep the full Call() path (own bind + handler).
    if (fn->is_guest()) {
      static_cast<xe::cpu::GuestFunction*>(fn)->CallOnCurrentThread(ts, ret_addr);
    } else {
      fn->Call(ts, ret_addr);
    }
  }
  if (trace && tn < 40) {
    auto* c = ts->context();
    XELOGE(
        "LLVMtrace #{} EXIT  0x{:08X} r3=0x{:X} | r28=0x{:X} r29=0x{:X} "
        "r30=0x{:X} r31=0x{:X}",
        tn, target, c->r[3], c->r[28], c->r[29], c->r[30], c->r[31]);
  }
}

// Entry trace: logged at the START of an LLVM-compiled fn whose address matches
// cpu_backend_llvm_trace_addr. Unlike the call-site trace, this fires regardless
// of the CALLER's backend, so it captures a fn's input regs even when an a64
// caller invokes it - letting us diff a callee's args across a caller's backends.
extern "C" void xe_llvm_trace_entry(uint32_t addr) {
  auto* ts = xe::cpu::ThreadState::Get();
  auto* c = ts->context();
  static std::atomic<uint32_t> s_entry_n{0};
  uint32_t n = s_entry_n.fetch_add(1, std::memory_order_relaxed);
  if (n < 160) {
    XELOGE(
        "LLVMentry #{} 0x{:08X} r3=0x{:X} r1=0x{:X} | r28=0x{:X} r29=0x{:X} "
        "r30=0x{:X} r31=0x{:X}",
        n, addr, c->r[3], c->r[1], c->r[28], c->r[29], c->r[30], c->r[31]);
  }
}

// Resolve a guest call target to its host machine-code entry (compiling it if
// needed) WITHOUT calling it. Used to emit a TRUE tail call (musttail) from
// JIT'd code: a guest tail-call (b/bctr in tail position) must REUSE the host
// frame, not nest one per call. The old path nested via fn->Call, so a guest
// tail-call loop (e.g. 0x8273EFB4) grew the host stack unboundedly -> overflow
// = the device signal storm. The returned entry is callable directly with x20/
// x21 live and x0 = guest return address (the a64 guest->guest ABI, no thunk).
extern "C" void* xe_llvm_resolve_function(void* ctx, uint32_t target) {
  auto* ts = reinterpret_cast<xe::cpu::ppc::PPCContext*>(ctx)->thread_state;
  auto* fn = xe_llvm_resolve_cached(ts, target);
  // Only GuestFunctions have host machine_code to tail-jump to; externs/
  // builtins have none and must go through the non-tail xe_llvm_guest_call path.
  // is_guest() (behavior_ != kBuiltin) is EXACTLY "is a GuestFunction" - the only
  // non-guest Function subclass is BuiltinFunction - so this replaces a hot RTTI
  // dynamic_cast (~2.2% of BD CPU, device-profiled) with one enum compare.
  auto* gf =
      (fn && fn->is_guest()) ? static_cast<xe::cpu::GuestFunction*>(fn) : nullptr;
  return gf ? reinterpret_cast<void*>(gf->machine_code()) : nullptr;
}

// Invoke a guest CALL_EXTERN target. The HIR symbol is the extern Function (a
// kExtern GuestFunction whose guest body is just `sc; bclr`, e.g.
// RtlImageXexHeaderField). Calling its guest ADDRESS re-enters that thunk ->
// infinite recursion -> host-stack overflow = the device signal storm. Instead
// call its extern_handler (the C++ HLE function), exactly like a64's
// EmitKernelExternHostCall: handler(context, context->kernel_state).
extern "C" void xe_llvm_call_extern(void* ctx, void* sym_ptr) {
  // context from x20 (passed by the JIT'd caller) instead of a per-call
  // thread_local ThreadState::Get() (residual emutls/pthread_getspecific cost).
  auto* context = reinterpret_cast<xe::cpu::ppc::PPCContext*>(ctx);
  auto* fn = reinterpret_cast<xe::cpu::Function*>(sym_ptr);
  if (fn->behavior() == xe::cpu::Function::Behavior::kExtern) {
    auto* gf = static_cast<xe::cpu::GuestFunction*>(fn);
    auto handler = gf->extern_handler();
    if (handler) {
      handler(context, context->kernel_state);
    }
  } else {
    // kBuiltin: BuiltinFunction::Call dispatches to its C++ handler. kDefault:
    // an ordinary guest fn (shouldn't appear as call_extern; handle anyway).
    fn->Call(context->thread_state, static_cast<uint32_t>(context->lr));
  }
}

// Guest TRAP (PPC tw/twi/td forced trap). Replicates a64's A64Emitter::Trap:
// type 20/26 = debug print (r3 -> string), 0/22 = forced-trap log, 25/other =
// no-op. Rarely taken (a guest assertion). Lets functions with trap/trap_true
// LLVM-compile instead of falling the whole function back to a64.
// Cooperative-scheduler safepoint cold path (guest scheduler stage 2): the
// inline IR only tests PPCContext.preempt_requested; this clears it and
// yields through the registered handler. Null until the scheduler starts,
// and a stale flag can arrive after it stops, hence the check.
extern "C" void xe_llvm_preempt_yield(void* raw_context) {
  auto* context = reinterpret_cast<xe::cpu::ppc::PPCContext*>(raw_context);
  context->preempt_requested = 0;
  auto handler = xe::cpu::backend::preempt_yield_handler;
  if (handler) {
    handler(raw_context);
  }
}

extern "C" void xe_llvm_trap(uint32_t trap_type) {
  auto* ts = xe::cpu::ThreadState::Get();
  auto* c = ts ? ts->context() : nullptr;
  switch (trap_type) {
    case 20:
    case 26:
      if (c && c->virtual_membase) {
        uint32_t str_ptr = static_cast<uint32_t>(c->r[3]);
        auto str = reinterpret_cast<const char*>(c->virtual_membase + str_ptr);
        XELOGD("(DebugPrint) {}", str ? str : "");
      }
      break;
    case 0:
    case 22:
      XELOGE("tw/td forced trap hit (LLVM) thid {:08X}",
             c ? c->thread_id : 0u);
      break;
    default:
      break;
  }
}

// vrsqrtefp per-lane estimate (PowerISA reciprocal-sqrt estimate). Byte-exact
// replica of a64_sequences.cc PpcVrsqrtefpLane (pure integer math, no FPCR);
// the qemu differential verifies it lane-for-lane against the a64 original.
extern "C" uint32_t xe_llvm_vrsqrte_lane(uint32_t bits) {
  static const uint32_t table[32] = {
      0x0568B4FD, 0x04F3AF97, 0x048DAAA5, 0x0435A618, 0x03E7A1E4, 0x03A29DFE,
      0x03659A5C, 0x032E96F8, 0x02FC93CA, 0x02D090CE, 0x02A88DFE, 0x02838B57,
      0x026188D4, 0x02438673, 0x02268431, 0x020B820B, 0x03D27FFA, 0x03807C29,
      0x033878AA, 0x02F97572, 0x02C27279, 0x02926FB7, 0x02666D26, 0x023F6AC0,
      0x021D6881, 0x01FD6665, 0x01E16468, 0x01C76287, 0x01AF60C1, 0x01995F12,
      0x01855D79, 0x01735BF4,
  };
  uint32_t sign = bits >> 31;
  uint32_t biased_exp = (bits >> 23) & 0xFF;
  uint32_t mantissa = bits & 0x007FFFFF;
  if (bits == 0xFF800000u) return 0x7FC00000u;          // -Inf -> QNaN
  if (biased_exp == 0) return sign ? 0xFF800000u : 0x7F800000u;  // 0/denorm
  if (biased_exp == 255) {
    if (mantissa == 0) return 0;                         // +Inf -> +0
    return bits | 0x00400000u;                           // NaN -> QNaN
  }
  if (sign) return 0x7FC00000u;                          // negative -> QNaN
  int32_t unbiased_exp = (int32_t)biased_exp - 127;
  uint32_t exp_parity = ((uint32_t)(unbiased_exp << 4)) & 16;
  uint32_t top4 = mantissa >> 19;
  uint32_t index = (exp_parity | top4) ^ 16;
  uint32_t interp = (mantissa >> 9) & 1023;
  int32_t result_exp = (127 - (int32_t)biased_exp) >> 1;
  uint32_t entry = table[index];
  uint32_t slope = entry >> 16;
  uint32_t base = (entry << 10) & 0x3FFFC00u;
  int32_t raw = (int32_t)base - (int32_t)(interp * slope);
  if (!(raw & (1 << 25))) {
    uint32_t val = (uint32_t)raw & 0x1FFFFFF;
    uint32_t lz = (uint32_t)xe::lzcnt(val);
    int32_t shift = (int32_t)lz - 6;
    result_exp += 6;
    result_exp -= (int32_t)lz;
    raw <<= shift;
  }
  if ((raw & 5) && (raw & 2)) raw += 4;
  uint32_t res_exp = (uint32_t)((result_exp << 23) + 0x3F800000);
  uint32_t res_man = ((uint32_t)raw >> 2) & 0x7FFFFF;
  uint32_t result = res_exp | res_man;
  if (((result >> 23) & 0xFF) == 0 && (result & 0x7FFFFF)) result = 0;  // DAZ
  return result;
}

// frsqrte scalar f64 estimate (PowerISA Table E-5). Byte-exact replica of
// a64_sequences.cc PpcFrsqrte; verified by the qemu differential.
extern "C" uint64_t xe_llvm_frsqrte(uint64_t bits) {
  uint32_t sign = (uint32_t)(bits >> 63);
  uint32_t exp = (uint32_t)((bits >> 52) & 0x7FF);
  uint64_t mantissa = bits & 0x000FFFFFFFFFFFFFULL;
  if (exp == 0x7FF && mantissa != 0) return bits | (1ULL << 51);  // NaN -> QNaN
  if (exp == 0 && mantissa == 0)
    return sign ? 0xFFF0000000000000ULL : 0x7FF0000000000000ULL;  // 0 -> inf
  if (exp == 0x7FF && !sign) return 0;                            // +inf -> +0
  if (sign) return 0x7FF8000000000000ULL;                         // neg -> QNaN
  int32_t effective_exp = (int32_t)exp;
  uint64_t norm_mantissa = mantissa;
  if (exp == 0) {
    int lz = (int)xe::lzcnt(mantissa);
    norm_mantissa = mantissa << (lz - 11);
    effective_exp = 12 - lz;
  }
  static const uint8_t table[] = {241, 216, 192, 168, 152, 136, 128, 112,
                                  96,  76,  60,  48,  32,  24,  16,  8};
  uint32_t top3 = (uint32_t)(norm_mantissa >> 49) & 7;
  uint32_t index = (((uint32_t)effective_exp & 1) << 3) | top3;
  index ^= 8;
  int32_t unbiased = effective_exp - 1023;
  int32_t half = unbiased >> 1;
  uint32_t result_exp = (uint32_t)(1022 - half);
  return ((uint64_t)result_exp << 52) | ((uint64_t)table[index] << 44);
}

// vlogefp / vexptefp per-lane (log2 / exp2 of a float, bit-in/bit-out). The a64
// path runs std::log2/std::exp2 in host FPCR via CallNativeSafe; same here.
extern "C" uint32_t xe_llvm_log2_lane(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, 4);
  f = std::log2(f);
  std::memcpy(&bits, &f, 4);
  return bits;
}
extern "C" uint32_t xe_llvm_exp2_lane(uint32_t bits) {
  float f;
  std::memcpy(&f, &bits, 4);
  f = std::exp2(f);
  std::memcpy(&bits, &f, 4);
  return bits;
}

// mftb (LOAD_CLOCK): the current guest tick count. Non-deterministic, so it is
// not differential-testable; matches a64 LoadClock (Clock::QueryGuestTickCount).
extern "C" uint64_t xe_llvm_load_clock() {
  return xe::Clock::QueryGuestTickCount();
}

// VMX UNPACK (in-place on a vec128_t scratch). Byte-exact replica of a64 UNPACK
// for the formats with a clean spec; the caller only invokes this for those
// modes (8_IN_16 / 16_IN_32 fall back to a64). The qemu differential verifies
// each lane vs the a64 sequence. Magic-float trick: an integer add of a bias
// constant to a sign-extended field reinterprets as the target float.
extern "C" void xe_llvm_unpack(void* vd, uint32_t flags) {
  auto* v = reinterpret_cast<xe::vec128_t*>(vd);
  switch (flags & xe::cpu::hir::PACK_TYPE_MODE) {
    case xe::cpu::hir::PACK_TYPE_D3DCOLOR: {
      uint32_t r0 = uint32_t(v->u8[14]) | 0x3F800000u;
      uint32_t r1 = uint32_t(v->u8[13]) | 0x3F800000u;
      uint32_t r2 = uint32_t(v->u8[12]) | 0x3F800000u;
      uint32_t r3 = uint32_t(v->u8[15]) | 0x3F800000u;
      v->u32[0] = r0; v->u32[1] = r1; v->u32[2] = r2; v->u32[3] = r3;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_FLOAT16_2: {
      float f0 = xe::xenos_half_to_float(v->u16[7]);
      float f1 = xe::xenos_half_to_float(v->u16[6]);
      v->f32[0] = f0; v->f32[1] = f1; v->f32[2] = 0.0f; v->u32[3] = 0x3F800000u;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_FLOAT16_4: {
      float f[4];
      for (int k = 0; k < 4; k++) f[k] = xe::xenos_half_to_float(v->u16[(4 + k) ^ 1]);
      for (int k = 0; k < 4; k++) v->f32[k] = f[k];
      break;
    }
    case xe::cpu::hir::PACK_TYPE_SHORT_2: {
      uint32_t r0 = uint32_t(int32_t(int16_t(v->u16[7]))) + 0x40400000u;
      uint32_t r1 = uint32_t(int32_t(int16_t(v->u16[6]))) + 0x40400000u;
      v->u32[0] = (r0 == 0x403F8000u) ? 0x7FC00000u : r0;
      v->u32[1] = (r1 == 0x403F8000u) ? 0x7FC00000u : r1;
      v->u32[2] = 0;
      v->u32[3] = 0x3F800000u;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_SHORT_4: {
      const int idx[4] = {5, 4, 7, 6};
      for (int k = 0; k < 4; k++) {
        uint32_t r = uint32_t(int32_t(int16_t(v->u16[idx[k]]))) + 0x40400000u;
        v->u32[k] = (r == 0x403F8000u) ? 0x7FC00000u : r;
      }
      break;
    }
    case xe::cpu::hir::PACK_TYPE_UINT_2101010: {
      uint32_t p = v->u32[3];
      int32_t x = int32_t((p & 0x3FFu) << 22) >> 22;
      int32_t y = int32_t(((p >> 10) & 0x3FFu) << 22) >> 22;
      int32_t z = int32_t(((p >> 20) & 0x3FFu) << 22) >> 22;
      uint32_t w = (p >> 30) & 0x3u;
      uint32_t r0 = uint32_t(x) + 0x40400000u;
      uint32_t r1 = uint32_t(y) + 0x40400000u;
      uint32_t r2 = uint32_t(z) + 0x40400000u;
      v->u32[0] = (r0 == 0x403FFE00u) ? 0x7FC00000u : r0;
      v->u32[1] = (r1 == 0x403FFE00u) ? 0x7FC00000u : r1;
      v->u32[2] = (r2 == 0x403FFE00u) ? 0x7FC00000u : r2;
      v->u32[3] = w + 0x3F800000u;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_ULONG_4202020: {
      uint64_t pk = (uint64_t(v->u32[2]) << 32) | uint64_t(v->u32[3]);
      int32_t x = int32_t(int64_t(pk << 44) >> 44);
      int32_t y = int32_t(int64_t(pk << 24) >> 44);
      int32_t z = int32_t(int64_t(pk << 4) >> 44);
      uint32_t w = uint32_t((pk >> 60) & 0xFu);
      uint32_t r0 = uint32_t(x) + 0x40400000u;
      uint32_t r1 = uint32_t(y) + 0x40400000u;
      uint32_t r2 = uint32_t(z) + 0x40400000u;
      v->u32[0] = (r0 == 0x40380000u) ? 0x7FC00000u : r0;
      v->u32[1] = (r1 == 0x40380000u) ? 0x7FC00000u : r1;
      v->u32[2] = (r2 == 0x40380000u) ? 0x7FC00000u : r2;
      v->u32[3] = w + 0x3F800000u;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_8_IN_16: {
      // vupkhsb/vupklsb-class: widen 8 bytes -> 8 halfwords. a64 = rev32.8H
      // (swap the 2 halfwords within each 32-bit word) then uxtl/sxtl of the
      // low 8 bytes (to_hi) or uxtl2/sxtl2 of the high 8 bytes (to_lo).
      bool to_hi = xe::cpu::hir::IsPackToHi(flags);
      bool uns = xe::cpu::hir::IsPackOutUnsigned(flags);
      uint8_t rb[16];
      for (int w = 0; w < 4; w++) {
        rb[4 * w + 0] = v->u8[4 * w + 2];
        rb[4 * w + 1] = v->u8[4 * w + 3];
        rb[4 * w + 2] = v->u8[4 * w + 0];
        rb[4 * w + 3] = v->u8[4 * w + 1];
      }
      const uint8_t* b = to_hi ? rb : rb + 8;
      xe::vec128_t r = {};
      for (int k = 0; k < 8; k++)
        r.u16[k] = uns ? uint16_t(b[k]) : uint16_t(int16_t(int8_t(b[k])));
      *v = r;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_16_IN_32: {
      // widen 4 halfwords -> 4 words. a64 = uxtl/sxtl of low 4 hw (to_hi) or
      // uxtl2/sxtl2 of high 4 hw (to_lo), then rev64.s4 (swap word pairs).
      bool to_hi = xe::cpu::hir::IsPackToHi(flags);
      bool uns = xe::cpu::hir::IsPackOutUnsigned(flags);
      int base = to_hi ? 0 : 4;
      uint32_t w[4];
      for (int k = 0; k < 4; k++)
        w[k] = uns ? uint32_t(v->u16[base + k])
                   : uint32_t(int32_t(int16_t(v->u16[base + k])));
      xe::vec128_t r = {};
      r.u32[0] = w[1]; r.u32[1] = w[0]; r.u32[2] = w[3]; r.u32[3] = w[2];
      *v = r;
      break;
    }
    default:
      break;  // caller doesn't invoke for unsupported modes
  }
}

// VMX PACK (in-place on a vec128_t scratch). Reverse of xe_llvm_unpack for the
// single-input float formats. Clamps are NaN-aware (std::fmax/fmin = ARM
// fmaxnm/fminnm: NaN -> the other operand = the min). Byte-exact vs a64;
// 8_IN_16/16_IN_32 (2-input + saturate) fall back to a64. qemu-verified.
extern "C" void xe_llvm_pack(void* vd, uint32_t flags) {
  auto* v = reinterpret_cast<xe::vec128_t*>(vd);
  auto bits = [](float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; };
  auto fbits = [](uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; };
  auto clampbits = [&](float f, uint32_t mnb, uint32_t mxb) {
    return bits(std::fmin(std::fmax(f, fbits(mnb)), fbits(mxb)));
  };
  switch (flags & xe::cpu::hir::PACK_TYPE_MODE) {
    case xe::cpu::hir::PACK_TYPE_D3DCOLOR: {
      auto enc = [&](float f) { return clampbits(f, 0x40400000u, 0x404000FFu) & 0xFFu; };
      uint32_t l0 = enc(v->f32[0]), l1 = enc(v->f32[1]);
      uint32_t l2 = enc(v->f32[2]), l3 = enc(v->f32[3]);
      v->u32[0] = 0; v->u32[1] = 0; v->u32[2] = 0;
      v->u32[3] = l2 | (l1 << 8) | (l0 << 16) | (l3 << 24);
      break;
    }
    case xe::cpu::hir::PACK_TYPE_FLOAT16_2: {
      uint16_t h0 = xe::float_to_xenos_half(v->f32[0]);
      uint16_t h1 = xe::float_to_xenos_half(v->f32[1]);
      xe::vec128_t r = {};
      r.u16[7] = h0; r.u16[6] = h1;
      *v = r;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_FLOAT16_4: {
      uint16_t h[4];
      for (int k = 0; k < 4; k++)
        h[k] = xe::float_to_xenos_half(v->f32[k], false, true);
      xe::vec128_t r = {};
      r.u16[5] = h[0]; r.u16[4] = h[1]; r.u16[7] = h[2]; r.u16[6] = h[3];
      *v = r;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_SHORT_2: {
      auto enc = [&](float f) { return clampbits(f, 0x403F8001u, 0x40407FFFu) & 0xFFFFu; };
      uint32_t s0 = enc(v->f32[0]), s1 = enc(v->f32[1]);
      xe::vec128_t r = {};
      r.u32[3] = s1 | (s0 << 16);
      *v = r;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_SHORT_4: {
      auto enc = [&](float f) { return clampbits(f, 0x403F8001u, 0x40407FFFu) & 0xFFFFu; };
      uint32_t s[4];
      for (int k = 0; k < 4; k++) s[k] = enc(v->f32[k]);
      xe::vec128_t r = {};
      r.u32[2] = s[1] | (s[0] << 16);
      r.u32[3] = s[3] | (s[2] << 16);
      *v = r;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_UINT_2101010: {
      uint32_t x = clampbits(v->f32[0], 0x403FFE01u, 0x404001FFu) & 0x3FFu;
      uint32_t y = clampbits(v->f32[1], 0x403FFE01u, 0x404001FFu) & 0x3FFu;
      uint32_t z = clampbits(v->f32[2], 0x403FFE01u, 0x404001FFu) & 0x3FFu;
      uint32_t w = clampbits(v->f32[3], 0x40400000u, 0x40400003u) & 0x3u;
      uint32_t p = x | (y << 10) | (z << 20) | (w << 30);
      v->u32[0] = p; v->u32[1] = p; v->u32[2] = p; v->u32[3] = p;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_ULONG_4202020: {
      uint64_t x = clampbits(v->f32[0], 0x40380001u, 0x4047FFFFu) & 0xFFFFFu;
      uint64_t y = clampbits(v->f32[1], 0x40380001u, 0x4047FFFFu) & 0xFFFFFu;
      uint64_t z = clampbits(v->f32[2], 0x40380001u, 0x4047FFFFu) & 0xFFFFFu;
      uint64_t wv = clampbits(v->f32[3], 0x40400000u, 0x4040000Fu) & 0xFu;
      uint64_t pk = x | (y << 20) | (z << 40) | (wv << 60);
      xe::vec128_t r = {};
      r.u32[2] = uint32_t(pk >> 32);
      r.u32[3] = uint32_t(pk);
      *v = r;
      break;
    }
    default:
      break;
  }
}

// VMX PACK 8_IN_16 / 16_IN_32 (TWO inputs: src1=VA, src2=VB). Narrows
// halfwords->bytes or words->halfwords with saturate/modulo, then rev32.8H
// (swap the two sub-elements within each 32-bit word). vs1_dst is src1 in and
// the result out. Byte-exact vs a64; qemu-verified.
extern "C" void xe_llvm_pack2(void* vs1_dst, void* vs2, uint32_t flags) {
  auto* s1 = reinterpret_cast<xe::vec128_t*>(vs1_dst);
  auto* s2 = reinterpret_cast<xe::vec128_t*>(vs2);
  bool sat = xe::cpu::hir::IsPackOutSaturate(flags);
  bool in_u = xe::cpu::hir::IsPackInUnsigned(flags);
  bool out_u = xe::cpu::hir::IsPackOutUnsigned(flags);
  switch (flags & xe::cpu::hir::PACK_TYPE_MODE) {
    case xe::cpu::hir::PACK_TYPE_8_IN_16: {
      auto nrw = [&](uint16_t h) -> uint8_t {
        if (!sat) return uint8_t(h & 0xFFu);
        if (in_u && out_u) return h > 255u ? 255u : uint8_t(h);          // uqxtn
        if (!in_u && out_u) {                                            // sqxtun
          int v = int16_t(h);
          return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
        if (!in_u && !out_u) {                                          // sqxtn
          int v = int16_t(h);
          return uint8_t(int8_t(v < -128 ? -128 : (v > 127 ? 127 : v)));
        }
        return h > 255u ? 255u : uint8_t(h);                            // uqxtn
      };
      uint8_t nb[16];
      for (int k = 0; k < 8; k++) {
        nb[k] = nrw(s1->u16[k]);
        nb[8 + k] = nrw(s2->u16[k]);
      }
      xe::vec128_t r = {};
      for (int w = 0; w < 4; w++) {
        r.u8[4 * w + 0] = nb[4 * w + 2]; r.u8[4 * w + 1] = nb[4 * w + 3];
        r.u8[4 * w + 2] = nb[4 * w + 0]; r.u8[4 * w + 3] = nb[4 * w + 1];
      }
      *s1 = r;
      break;
    }
    case xe::cpu::hir::PACK_TYPE_16_IN_32: {
      auto nrw = [&](uint32_t wv) -> uint16_t {
        if (!sat) return uint16_t(wv & 0xFFFFu);
        if (in_u && out_u) return wv > 0xFFFFu ? 0xFFFFu : uint16_t(wv);  // uqxtn
        if (!in_u && out_u) {                                            // sqxtun
          int64_t v = int32_t(wv);
          return uint16_t(v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : v));
        }
        if (!in_u && !out_u) {                                          // sqxtn
          int64_t v = int32_t(wv);
          return uint16_t(int16_t(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)));
        }
        return wv > 0xFFFFu ? 0xFFFFu : uint16_t(wv);                    // uqxtn
      };
      uint16_t nhw[8];
      for (int k = 0; k < 4; k++) {
        nhw[k] = nrw(s1->u32[k]);
        nhw[4 + k] = nrw(s2->u32[k]);
      }
      xe::vec128_t r = {};
      for (int w = 0; w < 4; w++) {
        r.u16[2 * w] = nhw[2 * w + 1];
        r.u16[2 * w + 1] = nhw[2 * w];
      }
      *s1 = r;
      break;
    }
    default:
      break;
  }
}
#endif  // XE_LLVM_BACKEND_ENABLED

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

// LlvmJitContext is defined in llvm_jit_context.h (shared with the assembler).

LLVMBackend::LLVMBackend() = default;
LLVMBackend::~LLVMBackend() = default;

bool LLVMBackend::IsAvailable() {
#if XE_LLVM_BACKEND_ENABLED
  return true;
#else
  return false;
#endif
}

bool LLVMBackend::Initialize(Processor* processor) {
#if XE_LLVM_BACKEND_ENABLED
  // Init LLVM + create the LLJIT BEFORE A64Backend::Initialize installs its
  // SIGSEGV handler / reserves address space (create() crashes/hangs if it runs
  // after, though it works standalone). x20/x21 reserved per-function in the
  // lowering via a target-features attribute.
  // Fatal-error probe. LLVM reports unrecoverable codegen failures - notably
  // "ran out of registers during register allocation" - through
  // report_fatal_error, which prints to stderr and aborts. On Android stderr is
  // NOT in logcat, so such a failure is indistinguishable from a silent crash,
  // and that ambiguity is exactly what has kept the OPCODE_PERMUTE tbl2
  // question open: our note says the AsmPrinter dies in a "wild-pointer
  // re-fault storm", while the clean diagnostic string IS present in our
  // libLLVM (verified by grepping the stripped .so). Those imply different
  // fixes - upstream's catch-and-retry works for the first and not the second.
  //
  // Routing the message into XELOGE settles it in ONE launch: if the handler
  // fires, the failure is a clean report_fatal_error and the retry design is
  // portable; if the process dies with nothing logged, it is memory corruption
  // and the tbl2 attribution is wrong.
  //
  // The handler deliberately does NOT try to recover. report_fatal_error must
  // not return, and longjmp-ing out of LLVM's internals to retry is only worth
  // building once we know there is something clean to catch.
  llvm::install_fatal_error_handler(
      [](void*, const char* reason, bool) {
        XELOGE("LLVMfatal: {}", reason ? reason : "(null)");
      },
      nullptr);

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::orc::LLJITBuilder builder;

  // AOT object cache (cpu_llvm_object_cache): persist each compiled function's
  // .o + reuse it next launch / on re-entry, skipping codegen. The cache subclass
  // + SimpleCompiler wiring live in the -fno-rtti llvm_object_cache.cc (see that
  // header for the typeinfo-link reason); we only build the per-version dir here.
  std::unique_ptr<llvm::ObjectCache> object_cache;
  if (cvars::cpu_llvm_object_cache &&
      !cvars::cpu_llvm_object_cache_path.empty()) {
    std::filesystem::path dir =
        std::filesystem::path(cvars::cpu_llvm_object_cache_path) /
        ("objcache_v" + std::to_string(kLlvmObjectCacheVersion) + "_opt" +
         std::to_string(cvars::cpu_backend_llvm_opt) + "_b" +
         LlvmLoweringStampTag());
    object_cache = CreateAndWireObjectCache(builder, dir.string());
    XELOGI("LLVMBackend: AOT object cache enabled at '{}'", dir.string());
  }

  auto jit_or = builder.create();
  if (!jit_or) {
    std::string msg = llvm::toString(jit_or.takeError());
    XELOGE("LLVMBackend: LLJIT creation failed: {}", msg);
    return false;
  }
  jit_ = std::make_unique<LlvmJitContext>();
  // The cache must outlive the LLJIT (its compiler holds a raw pointer to it);
  // LlvmJitContext destroys `jit` before `object_cache` (declaration order).
  jit_->object_cache = std::move(object_cache);
  jit_->jit = std::move(*jit_or);
  jit_->initialized = true;

  // Make the guest-call runtime helper resolvable by name from the JIT'd code.
  {
    auto& jd = jit_->jit->getMainJITDylib();
    auto name = jit_->jit->mangleAndIntern("xe_llvm_guest_call");
    llvm::cantFail(jd.define(llvm::orc::absoluteSymbols(llvm::orc::SymbolMap{
        {name, llvm::orc::ExecutorSymbolDef(
                   llvm::orc::ExecutorAddr::fromPtr(&xe_llvm_guest_call),
                   llvm::JITSymbolFlags::Exported |
                       llvm::JITSymbolFlags::Callable)}})));
    auto rname = jit_->jit->mangleAndIntern("xe_llvm_resolve_function");
    llvm::cantFail(jd.define(llvm::orc::absoluteSymbols(llvm::orc::SymbolMap{
        {rname, llvm::orc::ExecutorSymbolDef(
                    llvm::orc::ExecutorAddr::fromPtr(&xe_llvm_resolve_function),
                    llvm::JITSymbolFlags::Exported |
                        llvm::JITSymbolFlags::Callable)}})));
    auto ename = jit_->jit->mangleAndIntern("xe_llvm_call_extern");
    llvm::cantFail(jd.define(llvm::orc::absoluteSymbols(llvm::orc::SymbolMap{
        {ename, llvm::orc::ExecutorSymbolDef(
                    llvm::orc::ExecutorAddr::fromPtr(&xe_llvm_call_extern),
                    llvm::JITSymbolFlags::Exported |
                        llvm::JITSymbolFlags::Callable)}})));
    auto tname = jit_->jit->mangleAndIntern("xe_llvm_trace_entry");
    llvm::cantFail(jd.define(llvm::orc::absoluteSymbols(llvm::orc::SymbolMap{
        {tname, llvm::orc::ExecutorSymbolDef(
                    llvm::orc::ExecutorAddr::fromPtr(&xe_llvm_trace_entry),
                    llvm::JITSymbolFlags::Exported |
                        llvm::JITSymbolFlags::Callable)}})));
    auto trname = jit_->jit->mangleAndIntern("xe_llvm_trap");
    llvm::cantFail(jd.define(llvm::orc::absoluteSymbols(llvm::orc::SymbolMap{
        {trname, llvm::orc::ExecutorSymbolDef(
                     llvm::orc::ExecutorAddr::fromPtr(&xe_llvm_trap),
                     llvm::JITSymbolFlags::Exported |
                         llvm::JITSymbolFlags::Callable)}})));
    // PPC vector-math runtime helpers (estimate tables / libm), called per-lane
    // from the lowered RSQRT / LOG2 / POW2.
    auto pyname = jit_->jit->mangleAndIntern("xe_llvm_preempt_yield");
    llvm::cantFail(jd.define(llvm::orc::absoluteSymbols(llvm::orc::SymbolMap{
        {pyname, llvm::orc::ExecutorSymbolDef(
                     llvm::orc::ExecutorAddr::fromPtr(&xe_llvm_preempt_yield),
                     llvm::JITSymbolFlags::Exported |
                         llvm::JITSymbolFlags::Callable)}})));
    auto define_helper = [&](const char* nm, void* fp) {
      auto n = jit_->jit->mangleAndIntern(nm);
      llvm::cantFail(jd.define(llvm::orc::absoluteSymbols(llvm::orc::SymbolMap{
          {n, llvm::orc::ExecutorSymbolDef(
                  llvm::orc::ExecutorAddr::fromPtr(fp),
                  llvm::JITSymbolFlags::Exported |
                      llvm::JITSymbolFlags::Callable)}})));
    };
    define_helper("xe_llvm_vrsqrte_lane",
                  reinterpret_cast<void*>(&xe_llvm_vrsqrte_lane));
    define_helper("xe_llvm_frsqrte", reinterpret_cast<void*>(&xe_llvm_frsqrte));
    define_helper("xe_llvm_log2_lane",
                  reinterpret_cast<void*>(&xe_llvm_log2_lane));
    define_helper("xe_llvm_exp2_lane",
                  reinterpret_cast<void*>(&xe_llvm_exp2_lane));
    define_helper("xe_llvm_unpack", reinterpret_cast<void*>(&xe_llvm_unpack));
    define_helper("xe_llvm_pack", reinterpret_cast<void*>(&xe_llvm_pack));
    define_helper("xe_llvm_pack2", reinterpret_cast<void*>(&xe_llvm_pack2));
    define_helper("xe_llvm_load_clock",
                  reinterpret_cast<void*>(&xe_llvm_load_clock));
  }
#endif

  // Bring up the a64 base: host<->guest thunks, code cache + indirection table,
  // backend context, kernel-HLE glue. The LLVM path reuses ALL of it.
  if (!a64::A64Backend::Initialize(processor)) {
    return false;
  }

#if XE_LLVM_BACKEND_ENABLED
  XELOGI(
      "LLVMBackend: ORCv2 LLJIT initialized; LLVM lowers what it can, a64 the "
      "rest.");
  return true;
#else
  XELOGW(
      "LLVMBackend::Initialize: libLLVM not linked (XE_LLVM_BACKEND_ENABLED=0) "
      "- keep cpu_backend_llvm=false.");
  return false;
#endif
}

std::unique_ptr<Assembler> LLVMBackend::CreateAssembler() {
  return std::make_unique<LLVMAssembler>(this);
}

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe
