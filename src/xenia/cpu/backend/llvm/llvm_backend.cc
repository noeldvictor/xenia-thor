/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/llvm/llvm_backend.h"

#include <cstdio>
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
