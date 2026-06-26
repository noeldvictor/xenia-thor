/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/llvm/llvm_backend.h"

#include <string>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/backend/llvm/llvm_assembler.h"
#include "xenia/cpu/backend/llvm/llvm_jit_context.h"

// P0 gating: defined by the build once libLLVM is cross-built + linked for
// android-arm64 (LLVM 20.1.8, AArch64-only, ORC + JITLink). Until then the
// backend is a compile-only skeleton and IsAvailable() returns false so the
// processor keeps the a64 backend.
#ifndef XE_LLVM_BACKEND_ENABLED
#define XE_LLVM_BACKEND_ENABLED 0
#endif

#if XE_LLVM_BACKEND_ENABLED
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#endif  // XE_LLVM_BACKEND_ENABLED

DEFINE_bool(cpu_backend_llvm, false,
            "Use the LLVM-JIT CPU backend (whole-function recompile at module "
            "load for register residency) for guest functions it can lower, "
            "falling back to the a64 per-block JIT otherwise. Requires a "
            "libLLVM build (XE_LLVM_BACKEND_ENABLED). See "
            "docs/research/20260626-llvm-jit-backend-build-plan.md.",
            "CPU");

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
  // Bring up the a64 base FIRST: it creates the host<->guest thunks, the code
  // cache + indirection table, the backend context, and the kernel-HLE glue.
  // The LLVM path reuses ALL of it (an LLVM function is ABI-identical to an a64
  // one), so a64 stays the fallback and a64<->LLVM calls interoperate.
  if (!a64::A64Backend::Initialize(processor)) {
    return false;
  }

#if XE_LLVM_BACKEND_ENABLED
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  auto jtmb_or = llvm::orc::JITTargetMachineBuilder::detectHost();
  if (!jtmb_or) {
    XELOGE("LLVMBackend: JITTargetMachineBuilder::detectHost failed");
    return false;
  }
  auto jtmb = std::move(*jtmb_or);
  // Reserve x20 (guest PPCContext*) and x21 (guest membase) so the register
  // allocator never clobbers them. The host->guest thunk loads them before
  // entering the function; the function reads them via @llvm.read_register.
  jtmb.addFeatures({"+reserve-x20", "+reserve-x21"});

  auto jit_or = llvm::orc::LLJITBuilder()
                    .setJITTargetMachineBuilder(std::move(jtmb))
                    .create();
  if (!jit_or) {
    std::string msg = llvm::toString(jit_or.takeError());
    XELOGE("LLVMBackend: LLJIT creation failed: {}", msg);
    return false;
  }

  jit_ = std::make_unique<LlvmJitContext>();
  jit_->jit = std::move(*jit_or);
  jit_->initialized = true;
  XELOGI(
      "LLVMBackend: ORCv2 LLJIT initialized (AArch64, x20=ctx/x21=membase "
      "reserved). LLVM lowers what it can; a64 handles the rest.");
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
