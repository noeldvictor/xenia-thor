/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Validates the tier-2 cross-block GPR register-caching promotion
// (arm64_context_promotion_gpr_crossblock): a callee-saved PPC GPR that is
// loaded in one block and re-read in a dominated successor is carried through a
// HIR local instead of re-loaded from PPCContext. The transform must be
// SEMANTICALLY TRANSPARENT, so every test here is a DIFFERENTIAL: compile +
// run the same multi-block function with the promotion OFF then ON and require
// byte-identical guest register state. The ContextPromotionPass is
// backend-independent, so this exercises the real promotion logic on the x64
// host build (the a64 codegen of the result is covered by the qemu-a64 run).

#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"
#include "xenia/cpu/testing/util.h"

DECLARE_bool(arm64_context_promotion_gpr_crossblock);
DECLARE_uint32(arm64_context_promotion_gpr_crossblock_mask);
DECLARE_bool(arm64_context_promotion_gpr_crossblock_audit);
DECLARE_bool(arm64_context_promotion_gpr_crossblock_cond_branch_carry);
DECLARE_bool(arm64_register_cache_inherit);
DECLARE_bool(arm64_register_inheritance_audit);

using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

namespace {

// Default callee-saved mask r27..r31 (matches the cvar default).
constexpr uint32_t kCalleeSavedMask = 0xF8000000u;

struct ScopedCrossBlockGpr {
  ScopedCrossBlockGpr(bool enable, uint32_t mask) {
    prev_enable_ = cvars::arm64_context_promotion_gpr_crossblock;
    prev_mask_ = cvars::arm64_context_promotion_gpr_crossblock_mask;
    prev_audit_ = cvars::arm64_context_promotion_gpr_crossblock_audit;
    prev_cond_ = cvars::arm64_context_promotion_gpr_crossblock_cond_branch_carry;
    prev_inherit_ = cvars::arm64_register_cache_inherit;
    prev_inherit_audit_ = cvars::arm64_register_inheritance_audit;
    cvars::arm64_context_promotion_gpr_crossblock = enable;
    cvars::arm64_context_promotion_gpr_crossblock_mask = mask;
    // Emit promotion counters when enabled so the differential tests are
    // demonstrably non-vacuous (the audit line shows loads_replaced > 0).
    cvars::arm64_context_promotion_gpr_crossblock_audit = enable;
    // These tests use conditional-branch CFGs; enable the (host-correct, but
    // device-experimental) conditional-branch carry so the carrier actually
    // fires here. This is exactly the aggressive path whose HOST correctness we
    // validate differentially - the separate device crash is not host-visible.
    cvars::arm64_context_promotion_gpr_crossblock_cond_branch_carry = enable;
    // U4: the "ON" case also enables register-allocator inheritance so the
    // differential validates carrier+inheritance is byte-identical to baseline,
    // and the inheritance audit reports elided_loads (the non-vacuity evidence).
    cvars::arm64_register_cache_inherit = enable;
    cvars::arm64_register_inheritance_audit = enable;
  }
  ~ScopedCrossBlockGpr() {
    cvars::arm64_context_promotion_gpr_crossblock = prev_enable_;
    cvars::arm64_context_promotion_gpr_crossblock_mask = prev_mask_;
    cvars::arm64_context_promotion_gpr_crossblock_audit = prev_audit_;
    cvars::arm64_context_promotion_gpr_crossblock_cond_branch_carry = prev_cond_;
    cvars::arm64_register_cache_inherit = prev_inherit_;
    cvars::arm64_register_inheritance_audit = prev_inherit_audit_;
  }
  bool prev_enable_;
  uint32_t prev_mask_;
  bool prev_audit_;
  bool prev_cond_;
  bool prev_inherit_;
  bool prev_inherit_audit_;
};

// Compile+run `gen` with promotion OFF and ON under the same inputs and require
// every guest GPR to match. A divergence means the promotion changed behavior.
void RequireTransparent(std::function<void(HIRBuilder&)> gen,
                        std::function<void(PPCContext*)> pre,
                        uint32_t mask = kCalleeSavedMask) {
  uint64_t off_r[32] = {};
  uint64_t on_r[32] = {};
  {
    ScopedCrossBlockGpr scope(false, mask);
    TestFunction test(gen);
    test.Run(pre, [&](PPCContext* ctx) {
      for (int i = 0; i < 32; ++i) {
        off_r[i] = ctx->r[i];
      }
    });
  }
  {
    ScopedCrossBlockGpr scope(true, mask);
    TestFunction test(gen);
    test.Run(pre, [&](PPCContext* ctx) {
      for (int i = 0; i < 32; ++i) {
        on_r[i] = ctx->r[i];
      }
    });
  }
  for (int i = 0; i < 32; ++i) {
    INFO("GPR r[" << i << "] mismatch (off vs promoted)");
    REQUIRE(on_r[i] == off_r[i]);
  }
}

// Entry loads r31, conditionally branches; BOTH dominated successors re-read
// r31. The conditional branch ends the block with an EXPLICIT fallthrough
// branch so ControlFlowAnalysisPass forms both edges, and the `fall` block is
// emitted AFTER `taken` so the unconditional `branch fall` does not target the
// immediately-next label (which the builder would elide into a fallthrough,
// merging the block away). The successor reloads are the promotion targets.
void GenCarryUnmodified(HIRBuilder& b) {
  auto r31 = LoadGPR(b, 31);
  StoreGPR(b, 10, b.Add(r31, b.LoadConstantUint64(1)));  // use it in entry too
  auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                          b.Truncate(LoadGPR(b, 5), INT32_TYPE));
  auto taken = b.NewLabel();
  auto fall = b.NewLabel();
  b.BranchTrue(cmp, taken);
  b.Branch(fall);
  b.MarkLabel(taken);
  StoreGPR(b, 3, b.Add(LoadGPR(b, 31), b.LoadConstantUint64(200)));
  b.Return();
  b.MarkLabel(fall);
  StoreGPR(b, 3, b.Add(LoadGPR(b, 31), b.LoadConstantUint64(100)));
  b.Return();
}

// Entry MODIFIES r31 (store_context) before branching; successors must observe
// the modified value carried through the local, not a stale original.
void GenCarryModified(HIRBuilder& b) {
  auto r31 = LoadGPR(b, 31);
  auto modified = b.Add(r31, b.LoadConstantUint64(0x1000));
  StoreGPR(b, 31, modified);
  auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                          b.Truncate(LoadGPR(b, 5), INT32_TYPE));
  auto taken = b.NewLabel();
  auto fall = b.NewLabel();
  b.BranchTrue(cmp, taken);
  b.Branch(fall);
  b.MarkLabel(taken);
  StoreGPR(b, 3, b.Add(LoadGPR(b, 31), b.LoadConstantUint64(7)));
  b.Return();
  b.MarkLabel(fall);
  StoreGPR(b, 3, LoadGPR(b, 31));
  b.Return();
}

// Two-level dominated structure: entry -> outer -> {inner, fall2}. `outer`
// seeds r30/r31 from entry, and its own dominated successors seed from `outer`,
// exercising MULTI-HOP carry propagation through outgoing_states (the value
// loaded in entry must reach the leaf reloads two edges away). Block emission
// order keeps every unconditional fallthrough branch pointing past the next
// label so none of the blocks are merged.
void GenNestedDominatedChain(HIRBuilder& b) {
  LoadGPR(b, 30);
  LoadGPR(b, 31);
  auto cmp1 = b.CompareSLT(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                           b.Truncate(LoadGPR(b, 5), INT32_TYPE));
  auto outer = b.NewLabel();
  auto fall1 = b.NewLabel();
  b.BranchTrue(cmp1, outer);
  b.Branch(fall1);
  b.MarkLabel(outer);
  StoreGPR(b, 6, b.Add(LoadGPR(b, 30), LoadGPR(b, 31)));  // 1st-hop reloads
  auto cmp2 = b.CompareSLT(b.Truncate(LoadGPR(b, 7), INT32_TYPE),
                           b.Truncate(LoadGPR(b, 8), INT32_TYPE));
  auto inner = b.NewLabel();
  auto fall2 = b.NewLabel();
  b.BranchTrue(cmp2, inner);
  b.Branch(fall2);
  b.MarkLabel(inner);
  StoreGPR(b, 3, b.Add(LoadGPR(b, 30), LoadGPR(b, 31)));  // 2nd-hop reloads
  b.Return();
  b.MarkLabel(fall2);
  StoreGPR(b, 3, b.Sub(LoadGPR(b, 31), LoadGPR(b, 30)));  // 2nd-hop reloads
  b.Return();
  b.MarkLabel(fall1);
  StoreGPR(b, 3, b.Add(LoadGPR(b, 31), b.LoadConstantUint64(1)));
  b.Return();
}

// A counted loop whose body carries r31/r30 across a conditional branch (the
// branch-carry path), inside a back-edge CFG (header is multi-pred -> not
// seeded; body/inner/cont form single-dominating chains that DO carry). This is
// the complex CFG the simpler tests don't cover. r31 is used as a "pointer"
// base whose final value lands in r3, so a stale/wrong carry diverges.
void GenLoopCarry(HIRBuilder& b) {
  StoreGPR(b, 9, LoadGPR(b, 31));  // stash initial r31 (entry establishes it)
  auto header = b.NewLabel();
  b.Branch(header);
  b.MarkLabel(header);  // multi-pred: entry + latch back-edge
  auto cond = b.CompareSLT(LoadGPR(b, 5), LoadGPR(b, 6));
  auto body = b.NewLabel();
  auto exit = b.NewLabel();
  b.BranchTrue(cond, body);
  b.Branch(exit);
  b.MarkLabel(body);  // dom by header
  auto r31b = LoadGPR(b, 31);
  auto r30b = LoadGPR(b, 30);
  StoreGPR(b, 3, b.Add(r31b, r30b));
  auto cond2 = b.CompareSLT(r31b, LoadGPR(b, 6));
  auto inner = b.NewLabel();
  auto cont = b.NewLabel();
  b.BranchTrue(cond2, inner);
  b.Branch(cont);
  b.MarkLabel(inner);  // dom by body -> carries r31/r30
  StoreGPR(b, 3, b.Sub(LoadGPR(b, 31), LoadGPR(b, 30)));
  auto latch = b.NewLabel();
  b.Branch(latch);
  b.MarkLabel(cont);  // dom by body -> carries r31/r30
  StoreGPR(b, 3, b.Add(LoadGPR(b, 31), LoadGPR(b, 30)));
  b.Branch(latch);
  b.MarkLabel(latch);  // multi-pred inner+cont
  StoreGPR(b, 5, b.Add(LoadGPR(b, 5), b.LoadConstantUint64(1)));
  StoreGPR(b, 8, LoadGPR(b, 31));
  b.Branch(header);  // back-edge
  b.MarkLabel(exit);
  StoreGPR(b, 3, b.Add(LoadGPR(b, 31), LoadGPR(b, 30)));
  b.Return();
}

}  // namespace

TEST_CASE("CROSSBLOCK_GPR_LOOP_CARRY", "[instr]") {
  RequireTransparent(GenLoopCarry, [](PPCContext* ctx) {
    ctx->r[30] = 0x0000000000000010ull;
    ctx->r[31] = 0x0000000080001000ull;
    ctx->r[5] = 0;  // counter
    ctx->r[6] = 3;  // limit (3 iterations)
  });
  RequireTransparent(GenLoopCarry, [](PPCContext* ctx) {
    ctx->r[30] = 0xFFFFFFFFFFFFFFF0ull;
    ctx->r[31] = 0x0000000000000008ull;
    ctx->r[5] = 5;  // counter >= limit -> 0 iterations (straight to exit)
    ctx->r[6] = 2;
  });
}

TEST_CASE("CROSSBLOCK_GPR_CARRY_UNMODIFIED_TAKEN", "[instr]") {
  RequireTransparent(GenCarryUnmodified, [](PPCContext* ctx) {
    ctx->r[31] = 0x1122334455667788ull;
    ctx->r[4] = 1;  // 1 < 2 -> taken
    ctx->r[5] = 2;
  });
}

TEST_CASE("CROSSBLOCK_GPR_CARRY_UNMODIFIED_FALLTHROUGH", "[instr]") {
  RequireTransparent(GenCarryUnmodified, [](PPCContext* ctx) {
    ctx->r[31] = 0xDEADBEEFCAFEF00Dull;
    ctx->r[4] = 9;  // 9 < 2 false -> fallthrough
    ctx->r[5] = 2;
  });
}

TEST_CASE("CROSSBLOCK_GPR_CARRY_MODIFIED_BOTH_PATHS", "[instr]") {
  RequireTransparent(GenCarryModified, [](PPCContext* ctx) {
    ctx->r[31] = 0x0000000000001000ull;
    ctx->r[4] = 1;
    ctx->r[5] = 5;  // taken
  });
  RequireTransparent(GenCarryModified, [](PPCContext* ctx) {
    ctx->r[31] = 0xFFFFFFFFFFFF0000ull;
    ctx->r[4] = 5;
    ctx->r[5] = 1;  // fallthrough
  });
}

TEST_CASE("CROSSBLOCK_GPR_NESTED_CHAIN_INNER", "[instr]") {
  RequireTransparent(GenNestedDominatedChain, [](PPCContext* ctx) {
    ctx->r[30] = 0x0000000012340000ull;
    ctx->r[31] = 0x0000000000005678ull;
    ctx->r[4] = 1;  // cmp1 true -> outer
    ctx->r[5] = 2;
    ctx->r[7] = 1;  // cmp2 true -> inner
    ctx->r[8] = 2;
  });
}

TEST_CASE("CROSSBLOCK_GPR_NESTED_CHAIN_FALL2", "[instr]") {
  RequireTransparent(GenNestedDominatedChain, [](PPCContext* ctx) {
    ctx->r[30] = 0xAAAAAAAA00000000ull;
    ctx->r[31] = 0x00000000BBBBBBBBull;
    ctx->r[4] = 1;  // cmp1 true -> outer
    ctx->r[5] = 2;
    ctx->r[7] = 9;  // cmp2 false -> fall2
    ctx->r[8] = 2;
  });
}

// Widen the mask to also cover r1 (the stack pointer) + r11 — verifies the
// carrier stays transparent even for the most safety-critical register (matches
// the already-shipped r1 dominated-slot promoter's guarantee).
TEST_CASE("CROSSBLOCK_GPR_WIDE_MASK_INCLUDES_R1", "[instr]") {
  const uint32_t wide_mask = kCalleeSavedMask | (1u << 1) | (1u << 11);
  RequireTransparent(
      GenNestedDominatedChain,
      [](PPCContext* ctx) {
        ctx->r[30] = 0x1111111100000000ull;
        ctx->r[31] = 0x0000000022222222ull;
        ctx->r[4] = 1;
        ctx->r[5] = 2;
        ctx->r[7] = 1;
        ctx->r[8] = 2;
      },
      wide_mask);
}
