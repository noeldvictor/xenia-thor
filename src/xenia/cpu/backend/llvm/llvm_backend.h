/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_LLVM_LLVM_BACKEND_H_
#define XENIA_CPU_BACKEND_LLVM_LLVM_BACKEND_H_

#include <memory>

#include "xenia/cpu/backend/backend.h"

// LLVM-JIT backend (authorized 2026-06-26): lowers xenia HIR -> LLVM IR -> ORCv2
// LLJIT -> native ARM64, precompiled at module load. The win is register
// RESIDENCY from LLVM's whole-function optimizer, eliminating the per-block
// PPCContext round-trip tax the a64 per-block JIT pays (see
// docs/research/20260626-llvm-jit-backend-build-plan.md).
//
// Build phases (P0..P7) in the build-plan doc. This skeleton (P1) compiles
// without LLVM linked yet (the ORC JIT lives behind LlvmJitContext, a pimpl
// defined only in the .cc once libLLVM is cross-built for android-arm64, P0).
// Default-off cvar cpu_backend_llvm; the a64 backend stays the default until P4.

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

// Opaque holder for the LLVM ORCv2 JIT (LLVMContext, LLJIT, JITLink object
// layer). Forward-declared so this header never pulls in LLVM headers; the real
// definition is in llvm_backend.cc behind #if XE_LLVM_BACKEND_ENABLED.
struct LlvmJitContext;

class LLVMBackend : public Backend {
 public:
  LLVMBackend();
  ~LLVMBackend() override;

  // Returns true once libLLVM is cross-built + linked (XE_LLVM_BACKEND_ENABLED)
  // AND the host ORC JIT initialized. Until P0 lands this is false and the
  // processor must keep the a64 backend.
  static bool IsAvailable();

  bool Initialize(Processor* processor) override;

  void CommitExecutableRange(uint32_t guest_low, uint32_t guest_high) override;

  std::unique_ptr<Assembler> CreateAssembler() override;

  std::unique_ptr<GuestFunction> CreateGuestFunction(Module* module,
                                                     uint32_t address) override;

  uint64_t CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                        uint64_t current_pc) override;

  LlvmJitContext* jit() const { return jit_.get(); }

 private:
  std::unique_ptr<LlvmJitContext> jit_;
};

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_LLVM_LLVM_BACKEND_H_
