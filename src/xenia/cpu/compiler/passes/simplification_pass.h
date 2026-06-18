/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_

#include "xenia/cpu/compiler/passes/conditional_group_subpass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class SimplificationPass : public ConditionalGroupSubpass {
 public:
  SimplificationPass();
  ~SimplificationPass() override;

  bool Run(hir::HIRBuilder* builder, bool& result) override;

 private:
  bool EliminateConversions(hir::HIRBuilder* builder);
  bool CheckTruncate(hir::Instr* i);
  bool CheckByteSwap(hir::Instr* i);

  bool SimplifyAssignments(hir::HIRBuilder* builder);
  hir::Value* CheckValue(hir::Value* value, bool& result);

  // Integer algebraic identities (x+0, x-0, x*1, x|0, x^0, x<<0/x>>0 -> x).
  bool SimplifyAlgebraicIdentities(hir::HIRBuilder* builder);

  // Fold AND(x, NOT(y)) -> AND_NOT(x, y) (one ARM64 BIC; dead NOT removed by DCE).
  bool SimplifyAndNot(hir::HIRBuilder* builder);

  // Known-bits (NZM) redundant-mask elimination: drop AND(x, c) when every bit x
  // could possibly set already survives the mask c. Removes the redundant masks
  // that pile up emulating 32-bit PPC ops / zero-extending loads on 64-bit values.
  bool SimplifyRedundantMask(hir::HIRBuilder* builder);

  // Known-bits range fold: replace an integer op with constant 0 when its
  // operands' possible bits (MaxNonzeroBits) prove the result must be 0
  // (AND with a non-overlapping mask; SHR that shifts all possible bits out).
  bool SimplifyConstRange(hir::HIRBuilder* builder);
  // Conservative upper bound on the bits that may be set in v (1 = maybe set).
  static uint64_t MaxNonzeroBits(const hir::Value* v, int depth);
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_
