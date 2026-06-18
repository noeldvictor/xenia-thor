/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/simplification_pass.h"

#include "xenia/base/cvar.h"
#include "xenia/base/profiling.h"

DEFINE_bool(hir_algebraic_identities, true,
            "Simplify integer algebraic identities in the HIR (x+0, x-0, x*1, "
            "x|0, x^0, x<<0, x>>0 -> x) so they fold away before register "
            "allocation - fewer host instructions. Integer-only (float "
            "identities are unsafe re: NaN/signed zero). Default-on (matches the "
            "opt_algebraic_identities toggle, device-validated on Blue Dragon).",
            "CPU");

DEFINE_bool(hir_fold_and_not, true,
            "Fold dependent AND(x, NOT(y)) HIR sequences into a single AND_NOT "
            "opcode (the a64 backend lowers AND_NOT to one BIC). When the NOT "
            "has no other uses, dead-code elimination then removes it, so the "
            "two-op AND+NOT becomes one BIC. Bit-exact (definitional). "
            "Default-on: the fold transform is host-x64 unit-tested "
            "(AND_NOT_FOLD_TRANSFORM) and the a64 AND_NOT->BIC lowering is "
            "qemu-tested (AND_NOT_*_RUNTIME); Blue Dragon device run regression-"
            "clean. Ported from xenia-edge c383d049e.",
            "CPU");

DEFINE_bool(
    hir_known_bits_mask_fold, false,
    "Thor CPU codegen lever: known-bits (NZM) redundant-mask elimination. Drop "
    "an AND(x, constant) when a conservative upper bound on x's possibly-set "
    "bits already fits entirely inside the mask (x & c == x), so the AND is a "
    "no-op. Targets the redundant masks that pile up emulating 32-bit PPC ops "
    "and zero-extending loads (lbz/lhz, clrlwi, rlwinm) on 64-bit host values - "
    "exactly the branchy integer hot code of CPU-bound titles (Burnout, Lost "
    "Odyssey). Bit-exact (definitional). Additive to the per-instruction "
    "rlwinm fast-paths (this catches CROSS-instruction redundancy they can't). "
    "Inspired by xenia-edge's NZM simplification_pass. Default-off until "
    "host+qemu differential-validated; then a stacking XeniaOptimizations toggle.",
    "CPU");

DEFINE_bool(
    hir_const_range_fold, false,
    "Thor CPU codegen lever (extends hir_known_bits_mask_fold): fold an integer "
    "op to a constant 0 when a conservative upper bound on its operands' bits "
    "(MaxNonzeroBits) proves the result CANNOT have any bit set - AND(x,const) "
    "where x's possible bits don't overlap the mask, and SHR(x,const) that "
    "shifts every possible bit of x out the bottom. Common in PPC bitfield/mask "
    "and shifted zero-extended-load code. Bit-exact (definitional); the dead op "
    "is removed and DCE cleans the rest. Default-off until host+qemu validated, "
    "then a stacking XeniaOptimizations toggle.",
    "CPU");

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using xe::cpu::hir::Value;

SimplificationPass::SimplificationPass() : ConditionalGroupSubpass() {}

SimplificationPass::~SimplificationPass() {}

bool SimplificationPass::Run(HIRBuilder* builder, bool& result) {
  result = false;
  result |= EliminateConversions(builder);
  if (cvars::hir_algebraic_identities) {
    result |= SimplifyAlgebraicIdentities(builder);
  }
  if (cvars::hir_fold_and_not) {
    result |= SimplifyAndNot(builder);
  }
  if (cvars::hir_known_bits_mask_fold) {
    result |= SimplifyRedundantMask(builder);
  }
  if (cvars::hir_const_range_fold) {
    result |= SimplifyConstRange(builder);
  }
  result |= SimplifyAssignments(builder);
  return true;
}

// Returns a mask whose set bits are an UPPER BOUND on the bits that value v can
// possibly have set (a 0 bit is provably always 0). Conservative: when in doubt
// it returns all bits of v's type width (so the only error is missing a fold,
// never an unsound one). Integer values only; recursion is depth-bounded so this
// stays cheap during the pass.
uint64_t SimplificationPass::MaxNonzeroBits(const Value* v, int depth) {
  // Width mask for the value's type (bits outside it are always zero).
  uint64_t type_mask;
  switch (v->type) {
    case INT8_TYPE:
      type_mask = 0xFFull;
      break;
    case INT16_TYPE:
      type_mask = 0xFFFFull;
      break;
    case INT32_TYPE:
      type_mask = 0xFFFFFFFFull;
      break;
    case INT64_TYPE:
      type_mask = ~0ull;
      break;
    default:
      // Non-integer (float/vec) - no useful integer bit knowledge.
      return ~0ull;
  }

  if (v->IsConstant()) {
    uint64_t cv;
    switch (v->type) {
      case INT8_TYPE:
        cv = v->constant.u8;
        break;
      case INT16_TYPE:
        cv = v->constant.u16;
        break;
      case INT32_TYPE:
        cv = v->constant.u32;
        break;
      default:
        cv = v->constant.u64;
        break;
    }
    return cv & type_mask;
  }

  if (depth <= 0 || !v->def) {
    return type_mask;
  }

  const Instr* def = v->def;
  auto op = def->opcode;
  Value* s1 = def->src1.value;
  Value* s2 = def->src2.value;

  if (op == &OPCODE_ASSIGN_info && s1) {
    return MaxNonzeroBits(s1, depth - 1) & type_mask;
  } else if (op == &OPCODE_ZERO_EXTEND_info && s1) {
    // High bits are zero-filled; the source's known bits carry up unchanged.
    return MaxNonzeroBits(s1, depth - 1) & type_mask;
  } else if (op == &OPCODE_TRUNCATE_info && s1) {
    return MaxNonzeroBits(s1, depth - 1) & type_mask;
  } else if (op == &OPCODE_AND_info && s1 && s2) {
    return MaxNonzeroBits(s1, depth - 1) & MaxNonzeroBits(s2, depth - 1) &
           type_mask;
  } else if ((op == &OPCODE_OR_info || op == &OPCODE_XOR_info) && s1 && s2) {
    return (MaxNonzeroBits(s1, depth - 1) | MaxNonzeroBits(s2, depth - 1)) &
           type_mask;
  } else if (op == &OPCODE_SHL_info && s1 && s2 && s2->IsConstant()) {
    uint64_t sh = s2->constant.u8 & 63;  // shift amount is i8 in HIR
    return (MaxNonzeroBits(s1, depth - 1) << sh) & type_mask;
  } else if (op == &OPCODE_SHR_info && s1 && s2 && s2->IsConstant()) {
    // Logical shift right: high bits zero-filled, so known bits move down.
    uint64_t sh = s2->constant.u8 & 63;
    return (MaxNonzeroBits(s1, depth - 1) >> sh) & type_mask;
  }

  // Unknown producer - assume any bit of the type may be set.
  return type_mask;
}

bool SimplificationPass::SimplifyRedundantMask(HIRBuilder* builder) {
  // Drop AND(x, c) when x's possibly-set bits all survive the constant mask c
  // (i.e. x & c == x for every possible x), turning the AND into a plain assign.
  // SimplifyAssignments + DCE then remove the leftover. Bit-exact (definitional).
  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      auto next = i->next;
      if (i->opcode == &OPCODE_AND_info && i->flags == 0 && i->dest &&
          i->dest->type <= INT64_TYPE && i->src1.value && i->src2.value) {
        Value* s1 = i->src1.value;
        Value* s2 = i->src2.value;
        // AND commutes: the constant mask may be either operand.
        Value* x = nullptr;
        Value* mask = nullptr;
        if (s2->IsConstant()) {
          x = s1;
          mask = s2;
        } else if (s1->IsConstant()) {
          x = s2;
          mask = s1;
        }
        if (x && mask && x->type == i->dest->type) {
          uint64_t c = MaxNonzeroBits(mask, 1);  // exact (constant)
          uint64_t mx = MaxNonzeroBits(x, 8);
          // If none of x's possible bits fall outside the mask, x & c == x.
          if ((mx & ~c) == 0) {
            i->Replace(&OPCODE_ASSIGN_info, 0);
            i->set_src1(x);
            result = true;
          }
        }
      }
      i = next;
    }
    block = block->next;
  }
  return result;
}

bool SimplificationPass::SimplifyConstRange(HIRBuilder* builder) {
  // Known-bits "result is provably 0" folds. When MaxNonzeroBits proves an
  // integer op cannot have any set bit, replace its dest with constant 0 and
  // remove the op (mirrors constant_propagation's set_zero + Remove; DCE and
  // SimplifyAssignments clean up the rest). Bit-exact.
  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      auto next = i->next;
      if (i->flags == 0 && i->dest && i->dest->type <= INT64_TYPE &&
          i->src1.value) {
        auto op = i->opcode;
        bool folds_to_zero = false;
        if (op == &OPCODE_AND_info && i->src2.value) {
          // AND(x, c) == 0 when none of x's possible bits fall inside the mask
          // (constant may be either operand; AND commutes).
          Value* s1 = i->src1.value;
          Value* s2 = i->src2.value;
          if (s2->IsConstant()) {
            folds_to_zero = (MaxNonzeroBits(s1, 8) & MaxNonzeroBits(s2, 1)) == 0;
          } else if (s1->IsConstant()) {
            folds_to_zero = (MaxNonzeroBits(s2, 8) & MaxNonzeroBits(s1, 1)) == 0;
          }
        } else if (op == &OPCODE_SHR_info && i->src2.value &&
                   i->src2.value->IsConstant()) {
          // logical shift right by a constant: 0 if every possible bit of x is
          // below the shift amount. MaxNonzeroBits is already type-width-masked.
          uint64_t sh = i->src2.value->constant.u8 & 63;
          folds_to_zero = (MaxNonzeroBits(i->src1.value, 8) >> sh) == 0;
        }
        if (folds_to_zero) {
          i->dest->set_zero(i->dest->type);
          i->Remove();
          result = true;
        }
      }
      i = next;
    }
    block = block->next;
  }
  return result;
}

bool SimplificationPass::EliminateConversions(HIRBuilder* builder) {
  // First, we check for truncates/extensions that can be skipped.
  // This generates some assignments which then the second step will clean up.
  // Both zero/sign extends can be skipped:
  //   v1.i64 = zero/sign_extend v0.i32
  //   v2.i32 = truncate v1.i64
  // becomes:
  //   v1.i64 = zero/sign_extend v0.i32 (may be dead code removed later)
  //   v2.i32 = v0.i32

  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      // To make things easier we check in reverse (source of truncate/extend
      // back to definition).
      if (i->opcode == &OPCODE_TRUNCATE_info) {
        // Matches zero/sign_extend + truncate.
        result |= CheckTruncate(i);
      } else if (i->opcode == &OPCODE_BYTE_SWAP_info) {
        // Matches byte swap + byte swap.
        // This is pretty rare within the same basic block, but is in the
        // memcpy hot path and (probably) worth it. Maybe.
        result |= CheckByteSwap(i);
      }
      i = i->next;
    }
    block = block->next;
  }
  return result;
}

bool SimplificationPass::CheckTruncate(Instr* i) {
  // Walk backward up src's chain looking for an extend. We may have
  // assigns, so skip those.
  auto src = i->src1.value;
  auto def = src->def;
  while (def && def->opcode == &OPCODE_ASSIGN_info) {
    // Skip asignments.
    def = def->src1.value->def;
  }
  if (def) {
    if (def->opcode == &OPCODE_SIGN_EXTEND_info) {
      // Value comes from a sign extend.
      if (def->src1.value->type == i->dest->type) {
        // Types match, use original by turning this into an assign.
        i->Replace(&OPCODE_ASSIGN_info, 0);
        i->set_src1(def->src1.value);
        return true;
      }
    } else if (def->opcode == &OPCODE_ZERO_EXTEND_info) {
      // Value comes from a zero extend.
      if (def->src1.value->type == i->dest->type) {
        // Types match, use original by turning this into an assign.
        i->Replace(&OPCODE_ASSIGN_info, 0);
        i->set_src1(def->src1.value);
        return true;
      }
    }
  }
  return false;
}

bool SimplificationPass::CheckByteSwap(Instr* i) {
  // Walk backward up src's chain looking for a byte swap. We may have
  // assigns, so skip those.
  auto src = i->src1.value;
  auto def = src->def;
  while (def && def->opcode == &OPCODE_ASSIGN_info) {
    // Skip asignments.
    def = def->src1.value->def;
  }
  if (def && def->opcode == &OPCODE_BYTE_SWAP_info) {
    // Value comes from a byte swap.
    if (def->src1.value->type == i->dest->type) {
      // Types match, use original by turning this into an assign.
      i->Replace(&OPCODE_ASSIGN_info, 0);
      i->set_src1(def->src1.value);
      return true;
    }
  }
  return false;
}

bool SimplificationPass::SimplifyAlgebraicIdentities(HIRBuilder* builder) {
  // Turn integer algebraic-identity ops into a plain assign to the surviving
  // operand; SimplifyAssignments + dead-code elimination then remove the rest.
  // INTEGER ONLY (dest type <= INT64_TYPE) - float x+0.0 / x*1.0 / x-x are not
  // identities (NaN, signed zero, inf-inf). Only the plain (flags == 0) variants
  // are touched so flag-carrying arithmetic is never altered.
  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      auto next = i->next;
      if (i->flags == 0 && i->dest && i->dest->type <= INT64_TYPE &&
          i->src1.value && i->src2.value) {
        Value* s1 = i->src1.value;
        Value* s2 = i->src2.value;
        Value* survivor = nullptr;
        auto op = i->opcode;
        if (op == &OPCODE_ADD_info || op == &OPCODE_OR_info ||
            op == &OPCODE_XOR_info) {
          if (s2->IsConstant() && s2->IsConstantZero()) {
            survivor = s1;
          } else if (s1->IsConstant() && s1->IsConstantZero()) {
            survivor = s2;
          }
        } else if (op == &OPCODE_SUB_info) {
          if (s2->IsConstant() && s2->IsConstantZero()) {
            survivor = s1;
          }
        } else if (op == &OPCODE_MUL_info) {
          if (s2->IsConstant() && s2->IsConstantOne()) {
            survivor = s1;
          } else if (s1->IsConstant() && s1->IsConstantOne()) {
            survivor = s2;
          }
        } else if (op == &OPCODE_SHL_info || op == &OPCODE_SHR_info ||
                   op == &OPCODE_SHA_info) {
          if (s2->IsConstant() && s2->IsConstantZero()) {
            survivor = s1;
          }
        }
        if (survivor && survivor->type == i->dest->type) {
          i->Replace(&OPCODE_ASSIGN_info, 0);
          i->set_src1(survivor);
          result = true;
        }
      }
      i = next;
    }
    block = block->next;
  }
  return result;
}

bool SimplificationPass::SimplifyAndNot(HIRBuilder* builder) {
  // Fold a dependent AND + NOT into a single AND_NOT opcode:
  //   v = NOT(y);  d = AND(x, v)   ->   d = AND_NOT(x, y)   (== x & ~y)
  // The a64 backend lowers AND_NOT to one BIC. AND is commutative, so the NOT
  // may feed either operand; AND_NOT is NOT commutative (dest = src1 & ~src2),
  // so the surviving (non-NOT) operand always becomes src1 and the NOT's input
  // becomes src2. The leftover NOT is removed by the later dead-code-
  // elimination pass when it has no other uses. Ported from xenia-edge
  // c383d049e, adapted to this fork's standalone block-walk style.
  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      auto next = i->next;
      // Only plain AND (flags == 0); bitwise AND never carries flags, but guard
      // defensively so a flag-carrying variant is never silently dropped.
      if (i->opcode == &OPCODE_AND_info && i->flags == 0 && i->src1.value &&
          i->src2.value) {
        Value* src1 = i->src1.value;
        Value* src2 = i->src2.value;
        Instr* def1 = src1->def;
        Instr* def2 = src2->def;
        if (def2 && def2->opcode == &OPCODE_NOT_info) {
          // AND(x, NOT(y)) -> AND_NOT(x, y)
          i->Replace(&OPCODE_AND_NOT_info, 0);
          i->set_src1(src1);
          i->set_src2(def2->src1.value);
          result = true;
        } else if (def1 && def1->opcode == &OPCODE_NOT_info) {
          // AND(NOT(y), x) -> AND_NOT(x, y)  (AND commutes)
          i->Replace(&OPCODE_AND_NOT_info, 0);
          i->set_src1(src2);
          i->set_src2(def1->src1.value);
          result = true;
        }
      }
      i = next;
    }
    block = block->next;
  }
  return result;
}

bool SimplificationPass::SimplifyAssignments(HIRBuilder* builder) {
  // Run over the instructions and rename assigned variables:
  //   v1 = v0
  //   v2 = v1
  //   v3 = add v0, v2
  // becomes:
  //   v1 = v0
  //   v2 = v0
  //   v3 = add v0, v0
  // This could be run several times, as it could make other passes faster
  // to compute (for example, ConstantPropagation). DCE will take care of
  // the useless assigns.
  //
  // We do this by walking each instruction. For each value op we
  // look at its def instr to see if it's an assign - if so, we use the src
  // of that instr. Because we may have chains, we do this recursively until
  // we find a non-assign def.

  bool result = false;
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      uint32_t signature = i->opcode->signature;
      if (GET_OPCODE_SIG_TYPE_SRC1(signature) == OPCODE_SIG_TYPE_V) {
        bool modified = false;
        i->set_src1(CheckValue(i->src1.value, modified));
        result |= modified;
      }
      if (GET_OPCODE_SIG_TYPE_SRC2(signature) == OPCODE_SIG_TYPE_V) {
        bool modified = false;
        i->set_src2(CheckValue(i->src2.value, modified));
        result |= modified;
      }
      if (GET_OPCODE_SIG_TYPE_SRC3(signature) == OPCODE_SIG_TYPE_V) {
        bool modified = false;
        i->set_src3(CheckValue(i->src3.value, modified));
        result |= modified;
      }
      i = i->next;
    }
    block = block->next;
  }
  return result;
}

Value* SimplificationPass::CheckValue(Value* value, bool& result) {
  auto def = value->def;
  if (def && def->opcode == &OPCODE_ASSIGN_info) {
    // Value comes from an assignment - recursively find if it comes from
    // another assignment. It probably doesn't, if we already replaced it.
    auto replacement = def->src1.value;
    while (true) {
      def = replacement->def;
      if (!def || def->opcode != &OPCODE_ASSIGN_info) {
        break;
      }
      replacement = def->src1.value;
    }
    result = true;
    return replacement;
  }
  result = false;
  return value;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
