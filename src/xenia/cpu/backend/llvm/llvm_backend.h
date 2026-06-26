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

#include "xenia/cpu/backend/a64/a64_backend.h"

// LLVM-JIT backend (authorized 2026-06-26): lowers xenia HIR -> LLVM IR ->
// ORCv2 LLJIT -> native ARM64, precompiled at module load. The win is register
// RESIDENCY from LLVM's whole-function optimizer, eliminating the per-block
// PPCContext round-trip tax the a64 per-block JIT pays. Build plan:
// docs/research/20260626-llvm-jit-backend-build-plan.md.
//
// HYBRID DESIGN: LLVMBackend DERIVES from A64Backend so it inherits the entire
// runtime integration that already works on-device:
//   - the host<->guest thunks (host_to_guest sets x20=ctx, x21=membase),
//   - the code cache + indirection table + guest-call dispatch/resolve,
//   - A64Function (machine_code()/CallImpl) as the GuestFunction type,
//   - InitializeBackendContext + all the kernel-HLE interop.
// A JIT'd LLVM function is ABI-identical to an a64 one (reads ctx from x20 and
// membase from x21 via reserved-register reads, saves x30/LR, rets), so the
// a64 thunk calls it unchanged and a64<->LLVM calls interoperate.
//
// LLVMAssembler lowers each guest function to LLVM IR when every opcode is
// supported, JITs it, and Setup()s the resulting native pointer onto the
// A64Function; for any unsupported opcode it FALLS BACK to the a64 assembler,
// so coverage can grow incrementally without ever breaking a title.

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

// Opaque holder for the LLVM ORCv2 JIT (LLVMContext + LLJIT configured with the
// AArch64 target and x20/x21 reserved). Forward-declared so this header never
// pulls in LLVM headers; the real definition is in llvm_backend.cc behind
// #if XE_LLVM_BACKEND_ENABLED.
struct LlvmJitContext;

class LLVMBackend : public a64::A64Backend {
 public:
  LLVMBackend();
  ~LLVMBackend() override;

  // True once libLLVM is cross-built + linked (XE_LLVM_BACKEND_ENABLED). The
  // processor only instantiates this backend when cpu_backend_llvm is set AND
  // this returns true; otherwise it keeps the plain a64 backend.
  static bool IsAvailable();

  bool Initialize(Processor* processor) override;

  std::unique_ptr<Assembler> CreateAssembler() override;

  LlvmJitContext* jit() const { return jit_.get(); }

 private:
  std::unique_ptr<LlvmJitContext> jit_;
};

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_LLVM_LLVM_BACKEND_H_
