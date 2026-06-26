/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_LLVM_LLVM_ASSEMBLER_H_
#define XENIA_CPU_BACKEND_LLVM_LLVM_ASSEMBLER_H_

#include <memory>

#include "xenia/cpu/backend/assembler.h"

// LLVMAssembler: the HIR -> LLVM IR -> ORC JIT seam. Per guest function:
//   1. Build void @guest_<addr>() with x20=ctx / x21=membase read via reserved
//      registers (ABI-identical to an a64 function).
//   2. Lower each HIR Block -> llvm::BasicBlock, each HIR Instr -> LLVM IR.
//      Guest regs flow across blocks through LOAD/STORE_CONTEXT, which lower to
//      ctx-memory loads/stores that LLVM's whole-function optimizer (GVN /
//      SROA / mem2reg) promotes to SSA = register RESIDENCY, the win over the
//      a64 per-block JIT.
//   3. Run the LLVM pass pipeline; addIRModule to the LLJIT; lookup -> native
//      code pointer -> A64Function::Setup.
// Any opcode not yet lowered makes Assemble fall back to the a64 assembler, so
// coverage grows incrementally without breaking a title.

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

class LLVMBackend;

class LLVMAssembler : public Assembler {
 public:
  explicit LLVMAssembler(LLVMBackend* backend);
  ~LLVMAssembler() override;

  bool Initialize() override;
  void Reset() override;

  bool Assemble(GuestFunction* function, hir::HIRBuilder* builder,
                uint32_t debug_info_flags,
                std::unique_ptr<FunctionDebugInfo> debug_info) override;

 private:
  // Lowers the whole function to LLVM IR, JITs it, and Setup()s the native
  // pointer onto the A64Function. Returns false (leaving `function` untouched)
  // if any opcode is unsupported, so the caller can fall back to a64.
  bool LowerAndJit(GuestFunction* function, hir::HIRBuilder* builder);

  LLVMBackend* llvm_backend_;
  // a64 assembler used for functions the LLVM path can't lower yet.
  std::unique_ptr<Assembler> fallback_;
};

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_LLVM_LLVM_ASSEMBLER_H_
