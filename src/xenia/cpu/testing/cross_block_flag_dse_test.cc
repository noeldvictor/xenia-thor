/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Validates the tier-2 cross-block CR/XER dead-flag elimination pass
// (ppc_cross_block_dead_flag_elim): a condition-register / carry context store
// that is dead across ALL successor paths (overwritten before any read, and not
// live at a call/return) is removed. The transform must be SEMANTICALLY
// TRANSPARENT, so every test is a DIFFERENTIAL: compile + run the same
// multi-block function with the pass OFF then ON and require byte-identical
// guest register state (GPRs, CR0-7, and XER carry). Plus a non-vacuity check
// that the pass actually removed stores on the dead cases and removed NOTHING on
// the call/return-guarded cases (proving the cross-barrier guard). The pass is
// backend-independent, so this exercises the real logic on the x64 host build;
// the a64 codegen of the result is covered by the qemu-a64 run of this file.

#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/testing/util.h"

DECLARE_bool(ppc_cross_block_dead_flag_elim);
DECLARE_bool(ppc_cross_block_dead_flag_elim_audit);
DECLARE_bool(ppc_cross_block_dead_gpr_elim);
DECLARE_bool(ppc_cross_block_dead_gpr_elim_audit);
DECLARE_bool(ppc_cross_block_const_promotion);
DECLARE_bool(ppc_cross_block_const_promotion_audit);

// Forward-declared (context_promotion_pass.h pulls in llvm headers not on the
// test include path). Defined in context_promotion_pass.cc.
namespace xe {
namespace cpu {
namespace compiler {
namespace passes {
uint64_t CrossBlockFlagDseStoresRemovedForTest();
uint64_t CrossBlockGprDseStoresRemovedForTest();
uint64_t CrossBlockConstPromotedForTest();
}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

namespace {

struct ScopedFlagDse {
  explicit ScopedFlagDse(bool enable) {
    prev_ = cvars::ppc_cross_block_dead_flag_elim;
    prev_audit_ = cvars::ppc_cross_block_dead_flag_elim_audit;
    cvars::ppc_cross_block_dead_flag_elim = enable;
    cvars::ppc_cross_block_dead_flag_elim_audit = enable;
  }
  ~ScopedFlagDse() {
    cvars::ppc_cross_block_dead_flag_elim = prev_;
    cvars::ppc_cross_block_dead_flag_elim_audit = prev_audit_;
  }
  bool prev_;
  bool prev_audit_;
};

// Byte offset of CRn field bit (0=lt,1=gt,2=eq,3=so) and the XER carry.
size_t CrByte(int crn, int bit) {
  return offsetof(PPCContext, cr0) + size_t(crn) * 4 + size_t(bit);
}
size_t CaByte() { return offsetof(PPCContext, xer_ca); }

// Compile+run `gen` with the pass OFF then ON under the same inputs; require
// every guest GPR, CR0-7, and the XER carry to match.
void RequireTransparent(std::function<void(HIRBuilder&)> gen,
                        std::function<void(PPCContext*)> pre) {
  uint64_t off_r[32] = {}, on_r[32] = {};
  uint32_t off_cr[8] = {}, on_cr[8] = {};
  uint8_t off_ca = 0, on_ca = 0;
  auto capture = [](PPCContext* ctx, uint64_t* r, uint32_t* cr, uint8_t* ca) {
    for (int i = 0; i < 32; ++i) {
      r[i] = ctx->r[i];
    }
    for (int i = 0; i < 8; ++i) {
      cr[i] = (&ctx->cr0.value)[i];  // cr0.value..cr7.value are contiguous
    }
    *ca = ctx->xer_ca;
  };
  {
    ScopedFlagDse scope(false);
    TestFunction test(gen);
    test.Run(pre, [&](PPCContext* ctx) { capture(ctx, off_r, off_cr, &off_ca); });
  }
  {
    ScopedFlagDse scope(true);
    TestFunction test(gen);
    test.Run(pre, [&](PPCContext* ctx) { capture(ctx, on_r, on_cr, &on_ca); });
  }
  for (int i = 0; i < 32; ++i) {
    INFO("GPR r[" << i << "] mismatch (off vs flag-DSE)");
    REQUIRE(on_r[i] == off_r[i]);
  }
  for (int i = 0; i < 8; ++i) {
    INFO("CR" << i << " mismatch (off vs flag-DSE)");
    REQUIRE(on_cr[i] == off_cr[i]);
  }
  INFO("XER carry mismatch (off vs flag-DSE)");
  REQUIRE(on_ca == off_ca);
}

// How many CR/XER stores the pass removed while compiling `gen` (ON config).
// Compilation is lazy (it happens in Run via ResolveFunction), so we must Run.
uint64_t RemovedDelta(std::function<void(HIRBuilder&)> gen) {
  ScopedFlagDse scope(true);
  uint64_t before = xe::cpu::compiler::passes::CrossBlockFlagDseStoresRemovedForTest();
  TestFunction test(gen);
  test.Run([](PPCContext*) {}, [](PPCContext*) {});
  return xe::cpu::compiler::passes::CrossBlockFlagDseStoresRemovedForTest() -
         before;
}

// Deterministic inputs; also seed CR0-7/carry so a wrongly-removed (but
// should-be-kept) store leaves a value distinct from the compare result.
std::function<void(PPCContext*)> MakePre(int32_t r4, int32_t r5, int32_t r6,
                                         int32_t r7, int32_t r8, int32_t r9,
                                         int32_t r10, int32_t r11) {
  return [=](PPCContext* ctx) {
    ctx->r[4] = uint32_t(r4);
    ctx->r[5] = uint32_t(r5);
    ctx->r[6] = uint32_t(r6);
    ctx->r[7] = uint32_t(r7);
    ctx->r[8] = uint32_t(r8);
    ctx->r[9] = uint32_t(r9);
    ctx->r[10] = uint32_t(r10);
    ctx->r[11] = uint32_t(r11);
    for (int i = 0; i < 8; ++i) {
      (&ctx->cr0.value)[i] = 0xA1B2C3D4u;
    }
    ctx->xer_ca = 0xAA;
  };
}

void SetCr0AndCarry(HIRBuilder& b, int ra, int rb) {
  auto a = b.Truncate(LoadGPR(b, ra), INT32_TYPE);
  auto c = b.Truncate(LoadGPR(b, rb), INT32_TYPE);
  b.StoreContext(CrByte(0, 0), b.CompareSLT(a, c));  // lt
  b.StoreContext(CrByte(0, 1), b.CompareSGT(a, c));  // gt
  b.StoreContext(CrByte(0, 2), b.CompareEQ(a, c));   // eq
  b.StoreContext(CaByte(), b.CompareULT(a, c));      // a carry-shaped byte
}

// Entry sets CR0+carry from r4/r5 (DEAD: both successors overwrite all of them
// before returning). Two-block-deep CFG so the BLOCK-scoped DSE can't catch it -
// only the cross-block pass can. Expect: removed > 0, behavior unchanged.
void GenDeadOverwrite(HIRBuilder& b) {
  SetCr0AndCarry(b, 4, 5);  // dead
  auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 8), INT32_TYPE),
                          b.Truncate(LoadGPR(b, 9), INT32_TYPE));
  auto taken = b.NewLabel();
  auto fall = b.NewLabel();
  b.BranchTrue(cmp, taken);
  b.Branch(fall);
  b.MarkLabel(taken);
  SetCr0AndCarry(b, 6, 7);  // overwrite
  b.Return();
  b.MarkLabel(fall);
  SetCr0AndCarry(b, 10, 11);  // overwrite
  b.Return();
}

// Entry sets CR0+carry, then returns with NO overwrite. The return barrier keeps
// every store live for the caller. Expect: removed == 0, behavior unchanged.
void GenLiveAtReturn(HIRBuilder& b) {
  SetCr0AndCarry(b, 4, 5);
  b.Return();
}

// Entry sets CR0.eq, branches on an unrelated GPR condition; the `taken`
// SUCCESSOR re-reads CR0.eq (a genuine CROSS-BLOCK read that load-promotion does
// not fold away) and observes it in a GPR. The eq store is LIVE across the block
// boundary and must be KEPT. Expect behavior unchanged and removed == 0.
void GenLiveCrossBlock(HIRBuilder& b) {
  auto a = b.Truncate(LoadGPR(b, 4), INT32_TYPE);
  auto c = b.Truncate(LoadGPR(b, 5), INT32_TYPE);
  b.StoreContext(CrByte(0, 2), b.CompareEQ(a, c));  // eq - LIVE cross-block
  auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 8), INT32_TYPE),
                          b.Truncate(LoadGPR(b, 9), INT32_TYPE));
  auto taken = b.NewLabel();
  auto fall = b.NewLabel();
  b.BranchTrue(cmp, taken);
  b.Branch(fall);
  b.MarkLabel(taken);
  auto eq = b.LoadContext(CrByte(0, 2), INT8_TYPE);  // cross-block read
  StoreGPR(b, 3, b.ZeroExtend(eq, INT64_TYPE));
  b.Return();
  b.MarkLabel(fall);
  StoreGPR(b, 3, b.LoadConstantUint64(0));
  b.Return();
}

// --- GPR cross-block DSE (ppc_cross_block_dead_gpr_elim) --------------------
struct ScopedGprDse {
  explicit ScopedGprDse(bool enable) {
    prev_ = cvars::ppc_cross_block_dead_gpr_elim;
    prev_audit_ = cvars::ppc_cross_block_dead_gpr_elim_audit;
    cvars::ppc_cross_block_dead_gpr_elim = enable;
    cvars::ppc_cross_block_dead_gpr_elim_audit = enable;
  }
  ~ScopedGprDse() {
    cvars::ppc_cross_block_dead_gpr_elim = prev_;
    cvars::ppc_cross_block_dead_gpr_elim_audit = prev_audit_;
  }
  bool prev_;
  bool prev_audit_;
};

void RequireGprTransparent(std::function<void(HIRBuilder&)> gen,
                           std::function<void(PPCContext*)> pre) {
  uint64_t off_r[32] = {}, on_r[32] = {};
  uint32_t off_cr[8] = {}, on_cr[8] = {};
  uint8_t off_ca = 0, on_ca = 0;
  auto capture = [](PPCContext* ctx, uint64_t* r, uint32_t* cr, uint8_t* ca) {
    for (int i = 0; i < 32; ++i) r[i] = ctx->r[i];
    for (int i = 0; i < 8; ++i) cr[i] = (&ctx->cr0.value)[i];
    *ca = ctx->xer_ca;
  };
  {
    ScopedGprDse scope(false);
    TestFunction test(gen);
    test.Run(pre, [&](PPCContext* ctx) { capture(ctx, off_r, off_cr, &off_ca); });
  }
  {
    ScopedGprDse scope(true);
    TestFunction test(gen);
    test.Run(pre, [&](PPCContext* ctx) { capture(ctx, on_r, on_cr, &on_ca); });
  }
  for (int i = 0; i < 32; ++i) {
    INFO("GPR r[" << i << "] mismatch (off vs gpr-DSE)");
    REQUIRE(on_r[i] == off_r[i]);
  }
  for (int i = 0; i < 8; ++i) {
    INFO("CR" << i << " mismatch (off vs gpr-DSE)");
    REQUIRE(on_cr[i] == off_cr[i]);
  }
  INFO("XER carry mismatch (off vs gpr-DSE)");
  REQUIRE(on_ca == off_ca);
}

uint64_t RemovedGprDelta(std::function<void(HIRBuilder&)> gen) {
  ScopedGprDse scope(true);
  uint64_t before =
      xe::cpu::compiler::passes::CrossBlockGprDseStoresRemovedForTest();
  TestFunction test(gen);
  test.Run([](PPCContext*) {}, [](PPCContext*) {});
  return xe::cpu::compiler::passes::CrossBlockGprDseStoresRemovedForTest() -
         before;
}

// Entry sets r3 = r4+r5 (DEAD: both successors overwrite r3 before returning, no
// read between). Two-block-deep so block-scoped DSE can't catch it - only the
// cross-block GPR pass can. Expect removed > 0, behavior unchanged.
void GenDeadGpr(HIRBuilder& b) {
  StoreGPR(b, 3, b.Add(LoadGPR(b, 4), LoadGPR(b, 5)));  // dead
  auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 8), INT32_TYPE),
                          b.Truncate(LoadGPR(b, 9), INT32_TYPE));
  auto taken = b.NewLabel();
  auto fall = b.NewLabel();
  b.BranchTrue(cmp, taken);
  b.Branch(fall);
  b.MarkLabel(taken);
  StoreGPR(b, 3, b.Add(LoadGPR(b, 6), LoadGPR(b, 7)));  // overwrite
  b.Return();
  b.MarkLabel(fall);
  StoreGPR(b, 3, b.Add(LoadGPR(b, 10), LoadGPR(b, 11)));  // overwrite
  b.Return();
}

// Entry sets r3, returns with no overwrite. The return barrier forces all GPRs
// live for the caller. Expect removed == 0, behavior unchanged.
void GenGprLiveAtReturn(HIRBuilder& b) {
  StoreGPR(b, 3, b.Add(LoadGPR(b, 4), LoadGPR(b, 5)));
  b.Return();
}

// Entry sets r3; a successor re-reads r3 cross-block (into r12). r3 is LIVE across
// the edge and must be KEPT. Expect removed == 0, behavior unchanged.
void GenGprLiveCrossBlock(HIRBuilder& b) {
  StoreGPR(b, 3, b.Add(LoadGPR(b, 4), LoadGPR(b, 5)));  // live cross-block
  auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 8), INT32_TYPE),
                          b.Truncate(LoadGPR(b, 9), INT32_TYPE));
  auto taken = b.NewLabel();
  auto fall = b.NewLabel();
  b.BranchTrue(cmp, taken);
  b.Branch(fall);
  b.MarkLabel(taken);
  StoreGPR(b, 12, LoadGPR(b, 3));  // cross-block read of r3
  b.Return();
  b.MarkLabel(fall);
  b.Return();
}

// --- Unit #3: cross-block constant promotion (ppc_cross_block_const_promotion) ---
struct ScopedConstPromo {
  explicit ScopedConstPromo(bool enable) {
    prev_ = cvars::ppc_cross_block_const_promotion;
    prev_audit_ = cvars::ppc_cross_block_const_promotion_audit;
    cvars::ppc_cross_block_const_promotion = enable;
    cvars::ppc_cross_block_const_promotion_audit = enable;
  }
  ~ScopedConstPromo() {
    cvars::ppc_cross_block_const_promotion = prev_;
    cvars::ppc_cross_block_const_promotion_audit = prev_audit_;
  }
  bool prev_;
  bool prev_audit_;
};

void RequireConstTransparent(std::function<void(HIRBuilder&)> gen,
                             std::function<void(PPCContext*)> pre) {
  uint64_t off_r[32] = {}, on_r[32] = {};
  uint32_t off_cr[8] = {}, on_cr[8] = {};
  uint8_t off_ca = 0, on_ca = 0;
  auto capture = [](PPCContext* ctx, uint64_t* r, uint32_t* cr, uint8_t* ca) {
    for (int i = 0; i < 32; ++i) r[i] = ctx->r[i];
    for (int i = 0; i < 8; ++i) cr[i] = (&ctx->cr0.value)[i];
    *ca = ctx->xer_ca;
  };
  {
    ScopedConstPromo scope(false);
    TestFunction test(gen);
    test.Run(pre, [&](PPCContext* ctx) { capture(ctx, off_r, off_cr, &off_ca); });
  }
  {
    ScopedConstPromo scope(true);
    TestFunction test(gen);
    test.Run(pre, [&](PPCContext* ctx) { capture(ctx, on_r, on_cr, &on_ca); });
  }
  for (int i = 0; i < 32; ++i) {
    INFO("GPR r[" << i << "] mismatch (off vs const-promo)");
    REQUIRE(on_r[i] == off_r[i]);
  }
  for (int i = 0; i < 8; ++i) {
    INFO("CR" << i << " mismatch (off vs const-promo)");
    REQUIRE(on_cr[i] == off_cr[i]);
  }
  INFO("XER carry mismatch (off vs const-promo)");
  REQUIRE(on_ca == off_ca);
}

uint64_t PromotedConstDelta(std::function<void(HIRBuilder&)> gen) {
  ScopedConstPromo scope(true);
  uint64_t before =
      xe::cpu::compiler::passes::CrossBlockConstPromotedForTest();
  TestFunction test(gen);
  test.Run([](PPCContext*) {}, [](PPCContext*) {});
  return xe::cpu::compiler::passes::CrossBlockConstPromotedForTest() - before;
}

// Block A stores r3 = constant, then conditionally branches (so A keeps two
// successors and is NOT merged with the fallthrough). The fallthrough block B is
// a single-DOMINATES fallthrough of A, so it inherits A's constant and its
// load_context(r3) is cross-block-promoted to the constant (block-scoped
// promotion can't - the store is in a different block). Expect promoted > 0,
// behavior unchanged.
void GenCrossBlockConst(HIRBuilder& b) {
  StoreGPR(b, 3, b.LoadConstantUint64(0x12345678ull));  // const in block A
  auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 8), INT32_TYPE),
                          b.Truncate(LoadGPR(b, 9), INT32_TYPE));
  auto taken = b.NewLabel();
  auto fall = b.NewLabel();
  b.BranchTrue(cmp, taken);
  b.Branch(fall);
  b.MarkLabel(fall);  // block B: single-DOMINATES fallthrough of A
  StoreGPR(b, 12, LoadGPR(b, 3));  // load r3 -> promoted to the constant
  b.Return();
  b.MarkLabel(taken);
  b.Return();
}

}  // namespace

TEST_CASE("cross_block_flag_dse_dead_overwrite_transparent",
          "[CROSSBLOCK_FLAG_DSE]") {
  // Exercise both branch directions (r8<r9 taken, else fallthrough).
  RequireTransparent(GenDeadOverwrite, MakePre(1, 2, 3, 4, 5, 6, 7, 8));
  RequireTransparent(GenDeadOverwrite, MakePre(9, 8, 7, 6, 60, 5, 4, 3));
  RequireTransparent(GenDeadOverwrite, MakePre(-5, -5, 0, 0, 0, 0, 0, 0));
}

TEST_CASE("cross_block_flag_dse_dead_overwrite_nonvacuous",
          "[CROSSBLOCK_FLAG_DSE]") {
  // The entry's 4 CR0/carry stores are dead on both paths -> removed.
  REQUIRE(RemovedDelta(GenDeadOverwrite) >= 4);
}

TEST_CASE("cross_block_flag_dse_live_at_return_kept", "[CROSSBLOCK_FLAG_DSE]") {
  RequireTransparent(GenLiveAtReturn, MakePre(1, 2, 0, 0, 0, 0, 0, 0));
  RequireTransparent(GenLiveAtReturn, MakePre(7, 7, 0, 0, 0, 0, 0, 0));
  // Return barrier => nothing removed (the caller may read CR/XER).
  REQUIRE(RemovedDelta(GenLiveAtReturn) == 0);
}

TEST_CASE("cross_block_flag_dse_live_cross_block_kept",
          "[CROSSBLOCK_FLAG_DSE]") {
  // Both branch directions (r8<r9 taken reads CR0.eq; else fall ignores it).
  RequireTransparent(GenLiveCrossBlock, MakePre(3, 3, 0, 0, 1, 2, 0, 0));
  RequireTransparent(GenLiveCrossBlock, MakePre(3, 9, 0, 0, 9, 1, 0, 0));
  RequireTransparent(GenLiveCrossBlock, MakePre(7, 7, 0, 0, 1, 2, 0, 0));
  // The eq store is read in a successor -> live across the edge -> not removed.
  REQUIRE(RemovedDelta(GenLiveCrossBlock) == 0);
}

TEST_CASE("cross_block_gpr_dse_dead_overwrite_transparent",
          "[CROSSBLOCK_GPR_DSE]") {
  RequireGprTransparent(GenDeadGpr, MakePre(1, 2, 3, 4, 5, 6, 7, 8));
  RequireGprTransparent(GenDeadGpr, MakePre(9, 8, 7, 6, 60, 5, 4, 3));
  RequireGprTransparent(GenDeadGpr, MakePre(-5, -5, 0, 0, 0, 0, 0, 0));
}

TEST_CASE("cross_block_gpr_dse_dead_overwrite_nonvacuous",
          "[CROSSBLOCK_GPR_DSE]") {
  // The entry's r3 store is dead on both successor paths -> removed.
  REQUIRE(RemovedGprDelta(GenDeadGpr) >= 1);
}

TEST_CASE("cross_block_gpr_dse_live_at_return_kept", "[CROSSBLOCK_GPR_DSE]") {
  RequireGprTransparent(GenGprLiveAtReturn, MakePre(1, 2, 0, 0, 0, 0, 0, 0));
  // Return barrier => all GPRs live for the caller => nothing removed.
  REQUIRE(RemovedGprDelta(GenGprLiveAtReturn) == 0);
}

TEST_CASE("cross_block_gpr_dse_live_cross_block_kept", "[CROSSBLOCK_GPR_DSE]") {
  RequireGprTransparent(GenGprLiveCrossBlock, MakePre(3, 3, 0, 0, 1, 2, 0, 0));
  RequireGprTransparent(GenGprLiveCrossBlock, MakePre(3, 9, 0, 0, 9, 1, 0, 0));
  // r3 is read in a successor -> live across the edge -> not removed.
  REQUIRE(RemovedGprDelta(GenGprLiveCrossBlock) == 0);
}

TEST_CASE("cross_block_const_promotion_transparent", "[CROSSBLOCK_CONST]") {
  RequireConstTransparent(GenCrossBlockConst, MakePre(1, 2, 3, 4, 5, 6, 7, 8));
  RequireConstTransparent(GenCrossBlockConst, MakePre(0, 0, 0, 0, 0, 0, 0, 0));
}

TEST_CASE("cross_block_const_promotion_nonvacuous", "[CROSSBLOCK_CONST]") {
  // r3's constant store in block A dominates the cross-block load in B -> promoted.
  REQUIRE(PromotedConstDelta(GenCrossBlockConst) >= 1);
}
