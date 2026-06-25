/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_CONTEXT_PROMOTION_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_CONTEXT_PROMOTION_PASS_H_

#include <cmath>
#include <cstdint>
#include <vector>

#include "xenia/base/platform.h"
#include "xenia/cpu/compiler/compiler_pass.h"

#if XE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include <llvm/ADT/BitVector.h>
#pragma warning(pop)
#else
#include <llvm/ADT/BitVector.h>
#endif  // XE_COMPILER_MSVC

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class ContextPromotionPass : public CompilerPass {
 public:
  ContextPromotionPass();
  virtual ~ContextPromotionPass() override;

  bool Initialize(Compiler* compiler) override;

  bool Run(hir::HIRBuilder* builder) override;

 private:
  void PromoteBlock(hir::Block* block);
  bool ShouldRunGuestStateRegisterCacheAudit(hir::HIRBuilder* builder,
                                             uint32_t function_filter) const;
  void AuditGuestStateRegisterCache(hir::HIRBuilder* builder,
                                    bool residual_phase);
  void AuditGuestStateNonClosedCache(hir::HIRBuilder* builder);
  bool ShouldRunGprLocalSlotPromotion(hir::HIRBuilder* builder) const;
  void PromoteDominatedGprLocalSlots(hir::HIRBuilder* builder);
  bool ShouldRunCrossBlockGprPromotion(hir::HIRBuilder* builder) const;
  void PromoteCrossBlockGprSlots(hir::HIRBuilder* builder,
                                 const std::vector<size_t>& slot_offsets);
  bool ShouldRunGprLiveInR1Promotion(hir::HIRBuilder* builder) const;
  void PromoteGprLiveInR1(hir::HIRBuilder* builder);
  void RemoveDeadStoresBlock(hir::Block* block);
  bool TryGetContextValue(size_t offset, size_t size, hir::TypeName type,
                          hir::Value** out_value) const;
  void SetContextValueRange(size_t offset, size_t size, hir::Value* value);
  bool IsContextRangeValid(size_t offset, size_t size) const;
  void MarkContextRange(size_t offset, size_t size);

 private:
  std::vector<hir::Value*> context_values_;
  llvm::BitVector context_validity_;
};

// Cross-block dead-store elimination for the PowerPC condition register (CR0-7)
// and XER carry context slots. The block-scoped DSE in ContextPromotionPass
// resets liveness at every block boundary, so flag stores dead across ALL
// successor paths survive to ARM64 (PPC sets record-form CR / carry liberally).
// This computes per-block CR/XER live-out via a backward dataflow - any call /
// return / context barrier conservatively marks ALL flag slots live, so live
// state is never elided across a call (it only removes dead STORES, staying on
// the safe side of the cross-barrier wall) - and drops the stores dead on every
// path; the following DeadCodeEliminationPass then reaps the feeding compares.
// Gated by ppc_cross_block_dead_flag_elim (default-off). Backend-independent
// (runs before register allocation), so it is host-x64 + qemu-a64 testable.
class CrossBlockFlagDeadStoreEliminationPass : public CompilerPass {
 public:
  CrossBlockFlagDeadStoreEliminationPass();
  ~CrossBlockFlagDeadStoreEliminationPass() override;

  bool Run(hir::HIRBuilder* builder) override;
};

// Total CR/XER stores removed by CrossBlockFlagDeadStoreEliminationPass since
// process start - used by the host differential test to prove non-vacuity.
uint64_t CrossBlockFlagDseStoresRemovedForTest();

// Total GPR stores removed by the cross-block GPR DSE (ppc_cross_block_dead_gpr_elim).
uint64_t CrossBlockGprDseStoresRemovedForTest();

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_CONTEXT_PROMOTION_PASS_H_
