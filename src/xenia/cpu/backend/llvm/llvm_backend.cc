/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/llvm/llvm_backend.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/backend/llvm/llvm_assembler.h"

// P0 gating: defined by the build once libLLVM is cross-built + linked for
// android-arm64 (LLVM 18.1.8, AArch64-only, ORC + JITLink). Until then the
// backend is a compile-only skeleton and IsAvailable() returns false so the
// processor keeps the a64 backend.
#ifndef XE_LLVM_BACKEND_ENABLED
#define XE_LLVM_BACKEND_ENABLED 0
#endif

DEFINE_bool(cpu_backend_llvm, false,
            "Use the LLVM-JIT CPU backend (whole-function recompile at module "
            "load for register residency) instead of the a64 per-block JIT. "
            "Requires a libLLVM build (XE_LLVM_BACKEND_ENABLED); default-off "
            "while P0-P5 of the build are in progress. See "
            "docs/research/20260626-llvm-jit-backend-build-plan.md.",
            "CPU");

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

// The real definition lands in P0/P4 (holds llvm::orc::LLJIT + LLVMContext +
// the JITLink ObjectLinkingLayer with the W^X dual-mapping memory manager).
struct LlvmJitContext {
  // TODO(P0): std::unique_ptr<llvm::orc::LLJIT> jit; etc.
  bool initialized = false;
};

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
  if (!Backend::Initialize(processor)) {
    return false;
  }
#if XE_LLVM_BACKEND_ENABLED
  // TODO(P0/P4): InitializeNativeTarget + AsmPrinter; build LLJIT with a
  // JITLink ObjectLinkingLayer + dual RW/RX (memfd) memory manager; allocate
  // the code cache; set up the resolve thunks (reuse the a64 entry-table path).
  jit_ = std::make_unique<LlvmJitContext>();
  XELOGI("LLVMBackend: initialized (P-stub)");
  return true;
#else
  XELOGW(
      "LLVMBackend::Initialize: libLLVM not linked (XE_LLVM_BACKEND_ENABLED=0) "
      "- the LLVM backend is not yet usable; keep cpu_backend_llvm=false.");
  return false;
#endif
}

void LLVMBackend::CommitExecutableRange(uint32_t guest_low,
                                        uint32_t guest_high) {
  // TODO(P4): invalidate the host icache for the compiled range. With JITLink
  // the object layer manages this; likely a no-op or a __builtin___clear_cache.
}

std::unique_ptr<Assembler> LLVMBackend::CreateAssembler() {
  return std::make_unique<LLVMAssembler>(this);
}

std::unique_ptr<GuestFunction> LLVMBackend::CreateGuestFunction(
    Module* module, uint32_t address) {
  // TODO(P1/P4): return an LLVMFunction holding the JIT'd code pointer + the
  // guest metadata (model on a64_function). For now this path is unreachable
  // (Initialize fails when LLVM is unlinked).
  return nullptr;
}

uint64_t LLVMBackend::CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                                   uint64_t current_pc) {
  // TODO(P6): debugger single-step support. Not needed for boot/perf.
  return current_pc;
}

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe
