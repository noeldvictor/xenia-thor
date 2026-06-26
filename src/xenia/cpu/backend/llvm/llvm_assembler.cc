/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/llvm/llvm_assembler.h"

#include "xenia/base/logging.h"
#include "xenia/cpu/backend/llvm/llvm_backend.h"
#include "xenia/cpu/hir/hir_builder.h"

#ifndef XE_LLVM_BACKEND_ENABLED
#define XE_LLVM_BACKEND_ENABLED 0
#endif

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

LLVMAssembler::LLVMAssembler(LLVMBackend* backend)
    : Assembler(backend), llvm_backend_(backend) {}

LLVMAssembler::~LLVMAssembler() = default;

bool LLVMAssembler::Initialize() { return Assembler::Initialize(); }

void LLVMAssembler::Reset() { Assembler::Reset(); }

bool LLVMAssembler::Assemble(GuestFunction* function, hir::HIRBuilder* builder,
                             uint32_t debug_info_flags,
                             std::unique_ptr<FunctionDebugInfo> debug_info) {
#if XE_LLVM_BACKEND_ENABLED
  // TODO(P2-P3): the HIR -> LLVM IR lowering.
  //
  //   auto ctx = std::make_unique<llvm::LLVMContext>();
  //   auto mod = std::make_unique<llvm::Module>("guest", *ctx);
  //   // void @guest_<addr>(i8* %ctx, i8* %membase)
  //   auto fn = llvm::Function::Create(fn_ty, ExternalLinkage, name, *mod);
  //   RegTable regs;  // per-fn SSA value cache, indexed by guest reg slot
  //   for (hir::Block* b = builder->first_block(); b; b = b->next) {
  //     auto* bb = GetOrCreateBlock(b);
  //     for (hir::Instr* i = b->instr_head; i; i = i->next) {
  //       switch (i->opcode->num) {
  //         case OPCODE_LOAD_CONTEXT:  dest = regs.Load(i->src1.offset); break;
  //         case OPCODE_STORE_CONTEXT: regs.Store(i->src1.offset, V(i->src2)); break;
  //         case OPCODE_ADD: ... = b.CreateAdd(...); break;
  //         case OPCODE_LOAD:  ... = LoadGuestMem(addr, type); break;  // base+rev
  //         case OPCODE_COMPARE_SLT: ... = b.CreateICmpSLT(...); break;
  //         case OPCODE_BRANCH_TRUE: regs.Flush(); b.CreateCondBr(...); break;
  //         case OPCODE_CALL: regs.Flush(); EmitGuestCall(...); break;
  //         case OPCODE_RETURN: regs.Flush(); b.CreateRetVoid(); break;
  //         ... (full opcode table in the build-plan doc)
  //       }
  //     }
  //   }
  //   RunPasses(*mod);  // mem2reg, GVN, instcombine, simplifycfg
  //   auto addr = JitAndLookup(std::move(mod), std::move(ctx), name);
  //   function->SetupCode(addr);  // (mirror a64_function)
  //   return true;
  XELOGW("LLVMAssembler::Assemble: lowering not yet implemented (P2-P3 TODO)");
  return false;
#else
  // No LLVM linked; this assembler should never be reached (the backend's
  // Initialize fails). Return false so the caller treats it as a failure.
  (void)function;
  (void)builder;
  (void)debug_info_flags;
  (void)debug_info;
  return false;
#endif
}

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe
