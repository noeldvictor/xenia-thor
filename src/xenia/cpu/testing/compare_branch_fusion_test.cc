/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Validates the ARM64 single COMPARE -> BRANCH fusion
// (arm64_single_compare_branch_fusion: cmp + b.cond instead of
// cmp + cset + cbnz when the compare's only consumer is the immediately
// following branch). The HIR shapes here exercise BOTH fused-path variants:
// single-use (the cset is dropped) and multi-use (the bool must still be
// materialized for the second consumer). On non-ARM64 hosts the cvar doesn't
// exist and the tests still validate the same HIR semantics on the host
// backend.

#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"
#include "xenia/cpu/testing/util.h"

#if XE_ARCH_ARM64
DECLARE_bool(arm64_single_compare_branch_fusion);
#endif  // XE_ARCH_ARM64

using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

namespace {

// Enables the fusion for the scope of one test on ARM64; no-op elsewhere.
struct ScopedCompareBranchFusion {
  ScopedCompareBranchFusion() {
#if XE_ARCH_ARM64
    previous_ = cvars::arm64_single_compare_branch_fusion;
    cvars::arm64_single_compare_branch_fusion = true;
#endif  // XE_ARCH_ARM64
  }
  ~ScopedCompareBranchFusion() {
#if XE_ARCH_ARM64
    cvars::arm64_single_compare_branch_fusion = previous_;
#endif  // XE_ARCH_ARM64
  }

 private:
#if XE_ARCH_ARM64
  bool previous_ = false;
#endif  // XE_ARCH_ARM64
};

}  // namespace

TEST_CASE("COMPARE_BRANCH_FUSION_SLT_SINGLE_USE", "[instr]") {
  ScopedCompareBranchFusion fusion;
  TestFunction test([](HIRBuilder& b) {
    // The compare result's ONLY use is the branch -> the fused path may drop
    // the materialized bool entirely.
    auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                            b.Truncate(LoadGPR(b, 5), INT32_TYPE));
    auto taken = b.NewLabel();
    b.BranchTrue(cmp, taken);
    StoreGPR(b, 3, b.LoadConstantUint64(0));
    b.Return();
    b.MarkLabel(taken);
    StoreGPR(b, 3, b.LoadConstantUint64(1));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = uint64_t(int64_t(-5));
        ctx->r[5] = 3;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 1); });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 3;
        ctx->r[5] = uint64_t(int64_t(-5));
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0); });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 7;
        ctx->r[5] = 7;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0); });
  test.Run(
      [](PPCContext* ctx) {
        // INT32_MIN < 0 signed, but not unsigned - catches LT/LO mixups.
        ctx->r[4] = 0x80000000u;
        ctx->r[5] = 0;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 1); });
}

TEST_CASE("COMPARE_BRANCH_FUSION_SLT_I8_SIGNEXT", "[instr]") {
  // Signed sub-word compare: 0xFF (=-1) must compare LESS THAN 1, not as 255.
  // The fused path routes through EmitIntegerCompareFlags, which must
  // sxtb-extend for signed I8 - a plain cmp would branch the wrong way.
  ScopedCompareBranchFusion fusion;
  TestFunction test([](HIRBuilder& b) {
    auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 4), INT8_TYPE),
                            b.Truncate(LoadGPR(b, 5), INT8_TYPE));
    auto taken = b.NewLabel();
    b.BranchTrue(cmp, taken);
    StoreGPR(b, 3, b.LoadConstantUint64(0));
    b.Return();
    b.MarkLabel(taken);
    StoreGPR(b, 3, b.LoadConstantUint64(1));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        // (int8)0xFF = -1 < (int8)1 -> taken.
        ctx->r[4] = 0xFF;
        ctx->r[5] = 1;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 1); });
  test.Run(
      [](PPCContext* ctx) {
        // (int8)1 < (int8)0xFF(=-1) is false -> not taken.
        ctx->r[4] = 1;
        ctx->r[5] = 0xFF;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0); });
  test.Run(
      [](PPCContext* ctx) {
        // (int8)0x80 = -128 < (int8)0x7F = 127 -> taken.
        ctx->r[4] = 0x80;
        ctx->r[5] = 0x7F;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 1); });
}

TEST_CASE("COMPARE_BRANCH_FUSION_SGT_I16_SIGNEXT", "[instr]") {
  // Signed I16: 0xFFFF (=-1) must NOT be > 1 (sxth path).
  ScopedCompareBranchFusion fusion;
  TestFunction test([](HIRBuilder& b) {
    auto cmp = b.CompareSGT(b.Truncate(LoadGPR(b, 4), INT16_TYPE),
                            b.Truncate(LoadGPR(b, 5), INT16_TYPE));
    auto taken = b.NewLabel();
    b.BranchTrue(cmp, taken);
    StoreGPR(b, 3, b.LoadConstantUint64(0));
    b.Return();
    b.MarkLabel(taken);
    StoreGPR(b, 3, b.LoadConstantUint64(1));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        // (int16)0xFFFF = -1 > (int16)1 is false -> not taken.
        ctx->r[4] = 0xFFFF;
        ctx->r[5] = 1;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0); });
  test.Run(
      [](PPCContext* ctx) {
        // (int16)0x7FFF = 32767 > (int16)0x8000 = -32768 -> taken.
        ctx->r[4] = 0x7FFF;
        ctx->r[5] = 0x8000;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 1); });
}

TEST_CASE("COMPARE_BRANCH_FUSION_UGT_SINGLE_USE", "[instr]") {
  ScopedCompareBranchFusion fusion;
  TestFunction test([](HIRBuilder& b) {
    auto cmp = b.CompareUGT(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                            b.Truncate(LoadGPR(b, 5), INT32_TYPE));
    auto taken = b.NewLabel();
    b.BranchTrue(cmp, taken);
    StoreGPR(b, 3, b.LoadConstantUint64(0));
    b.Return();
    b.MarkLabel(taken);
    StoreGPR(b, 3, b.LoadConstantUint64(1));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        // 0xFFFFFFFF > 1 unsigned (but -1 < 1 signed) - catches HI/GT mixups.
        ctx->r[4] = 0xFFFFFFFFu;
        ctx->r[5] = 1;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 1); });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 1;
        ctx->r[5] = 0xFFFFFFFFu;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0); });
}

TEST_CASE("COMPARE_BRANCH_FUSION_EQ_BRANCH_FALSE", "[instr]") {
  ScopedCompareBranchFusion fusion;
  TestFunction test([](HIRBuilder& b) {
    auto cmp = b.CompareEQ(LoadGPR(b, 4), LoadGPR(b, 5));
    auto not_equal = b.NewLabel();
    b.BranchFalse(cmp, not_equal);
    StoreGPR(b, 3, b.LoadConstantUint64(1));
    b.Return();
    b.MarkLabel(not_equal);
    StoreGPR(b, 3, b.LoadConstantUint64(0));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0x123456789ABCDEF0ull;
        ctx->r[5] = 0x123456789ABCDEF0ull;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 1); });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 1;
        ctx->r[5] = 2;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0); });
}

TEST_CASE("COMPARE_BRANCH_FUSION_MULTI_USE", "[instr]") {
  ScopedCompareBranchFusion fusion;
  TestFunction test([](HIRBuilder& b) {
    // The branch immediately follows the compare (so the fusion engages) but
    // the compare result is ALSO read on the fallthrough path - the fused
    // path must still materialize the bool for the second consumer.
    auto cmp = b.CompareSLT(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                            b.Truncate(LoadGPR(b, 5), INT32_TYPE));
    auto taken = b.NewLabel();
    b.BranchTrue(cmp, taken);
    StoreGPR(b, 6, b.ZeroExtend(cmp, INT64_TYPE));
    StoreGPR(b, 3, b.LoadConstantUint64(0));
    b.Return();
    b.MarkLabel(taken);
    StoreGPR(b, 3, b.LoadConstantUint64(1));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = uint64_t(int64_t(-1));
        ctx->r[5] = 0;
        ctx->r[6] = 0xDEAD;
      },
      [](PPCContext* ctx) {
        REQUIRE(ctx->r[3] == 1);
        // Taken path - r6 untouched.
        REQUIRE(ctx->r[6] == 0xDEAD);
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 5;
        ctx->r[5] = 5;
        ctx->r[6] = 0xDEAD;
      },
      [](PPCContext* ctx) {
        REQUIRE(ctx->r[3] == 0);
        // Fallthrough - the materialized bool (false) was stored.
        REQUIRE(ctx->r[6] == 0);
      });
}
