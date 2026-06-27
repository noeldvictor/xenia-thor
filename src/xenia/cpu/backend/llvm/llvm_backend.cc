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
#include <cstdio>
#include <string>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/backend/llvm/llvm_assembler.h"
#include "xenia/cpu/backend/llvm/llvm_jit_context.h"
#include "xenia/cpu/function.h"
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
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/TaskDispatch.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
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
extern "C" void xe_llvm_guest_call(uint32_t target, uint32_t ret_addr) {
  auto* ts = xe::cpu::ThreadState::Get();
  // DIAGNOSTIC (rate-limited): host-call nesting depth, to localize the stack-
  // overflow storm. ALL guest calls route here, so if this climbs without bound
  // the call/return tree isn't unwinding; target/ret_addr show WHAT recurses.
  static thread_local uint32_t s_depth = 0;
  static std::atomic<uint32_t> s_deep_log{0};
  ++s_depth;
  if (s_depth >= 300 && (s_depth & 0xFF) == 0) {
    uint32_t n = s_deep_log.fetch_add(1, std::memory_order_relaxed);
    if (n < 24) {
      XELOGE(
          "xe_llvm_guest_call DEEP depth={} target=0x{:08X} ret_addr=0x{:08X}",
          s_depth, target, ret_addr);
    }
  }
  auto* fn = ts->processor()->ResolveFunction(target);
  if (fn) {
    fn->Call(ts, ret_addr);
  }
  --s_depth;
}

// Resolve a guest call target to its host machine-code entry (compiling it if
// needed) WITHOUT calling it. Used to emit a TRUE tail call (musttail) from
// JIT'd code: a guest tail-call (b/bctr in tail position) must REUSE the host
// frame, not nest one per call. The old path nested via fn->Call, so a guest
// tail-call loop (e.g. 0x8273EFB4) grew the host stack unboundedly -> overflow
// = the device signal storm. The returned entry is callable directly with x20/
// x21 live and x0 = guest return address (the a64 guest->guest ABI, no thunk).
extern "C" void* xe_llvm_resolve_function(uint32_t target) {
  auto* ts = xe::cpu::ThreadState::Get();
  auto* fn = ts->processor()->ResolveFunction(target);
  // Only GuestFunctions have host machine_code to tail-jump to; externs/
  // builtins have none and must go through the non-tail xe_llvm_guest_call path.
  auto* gf = fn ? dynamic_cast<xe::cpu::GuestFunction*>(fn) : nullptr;
  return gf ? reinterpret_cast<void*>(gf->machine_code()) : nullptr;
}

// Invoke a guest CALL_EXTERN target. The HIR symbol is the extern Function (a
// kExtern GuestFunction whose guest body is just `sc; bclr`, e.g.
// RtlImageXexHeaderField). Calling its guest ADDRESS re-enters that thunk ->
// infinite recursion -> host-stack overflow = the device signal storm. Instead
// call its extern_handler (the C++ HLE function), exactly like a64's
// EmitKernelExternHostCall: handler(context, context->kernel_state).
extern "C" void xe_llvm_call_extern(void* sym_ptr) {
  auto* ts = xe::cpu::ThreadState::Get();
  auto* fn = reinterpret_cast<xe::cpu::Function*>(sym_ptr);
  if (fn->behavior() == xe::cpu::Function::Behavior::kExtern) {
    auto* gf = static_cast<xe::cpu::GuestFunction*>(fn);
    auto handler = gf->extern_handler();
    if (handler) {
      handler(ts->context(), ts->context()->kernel_state);
    }
  } else {
    // kBuiltin: BuiltinFunction::Call dispatches to its C++ handler. kDefault:
    // an ordinary guest fn (shouldn't appear as call_extern; handle anyway).
    fn->Call(ts, static_cast<uint32_t>(ts->context()->lr));
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
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  auto jit_or = llvm::orc::LLJITBuilder().create();
  if (!jit_or) {
    std::string msg = llvm::toString(jit_or.takeError());
    XELOGE("LLVMBackend: LLJIT creation failed: {}", msg);
    return false;
  }
  jit_ = std::make_unique<LlvmJitContext>();
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
