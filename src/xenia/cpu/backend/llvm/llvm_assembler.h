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
//   1. Build an llvm::Function(i8* ctx_ptr, i8* membase) in a fresh Module.
//   2. Lower each HIR Block -> llvm::BasicBlock, each HIR Instr -> LLVM IR.
//      RESIDENCY: keep a per-function SSA-value table indexed by guest reg
//      (RPCS3 m_locals model). LOAD_CONTEXT returns the cached value or emits
//      ONE load; STORE_CONTEXT just updates the table; FLUSH to the PPCContext
//      struct only at CALL/CALL_INDIRECT/RETURN/CONTEXT_BARRIER boundaries.
//      (First impl: alloca-per-reg + mem2reg, identical effect, less code.)
//   3. Run the LLVM pass pipeline (mem2reg, GVN, instcombine, ...).
//   4. addIRModule to the LLJIT; lookup -> native code pointer -> GuestFunction.
// Full design + the opcode lowering table: build-plan doc, sections "Residency",
// "Flags", "Memory model", "Dispatch", "Integration map".

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
  LLVMBackend* llvm_backend_;
};

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_LLVM_LLVM_ASSEMBLER_H_
