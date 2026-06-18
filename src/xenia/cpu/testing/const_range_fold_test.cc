/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/testing/util.h"

#include <cstdint>

#include "xenia/base/cvar.h"

DECLARE_bool(hir_const_range_fold);

using namespace xe::cpu::hir;
using namespace xe::cpu;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

// hir_const_range_fold: when MaxNonzeroBits proves an integer op cannot have any
// set bit, it folds to constant 0. These tests run with the fold ENABLED and
// assert BIT-EXACT results against what the op must yield regardless - so a
// buggy/over-aggressive fold (one that zeroes a value that should survive) is
// caught. Inputs come from a GPR (runtime) so constant-propagation doesn't
// pre-fold the op and the fold path is actually exercised.

namespace {
struct ConstRangeFoldGuard {
  bool previous_;
  ConstRangeFoldGuard() {
    previous_ = cvars::hir_const_range_fold;
    cvars::hir_const_range_fold = true;
  }
  ~ConstRangeFoldGuard() { cvars::hir_const_range_fold = previous_; }
};
}  // namespace

TEST_CASE("CONSTRANGE_AND_NONOVERLAP_ZERO", "[instr]") {
  // zero_extend(i8) has bits <= 0xFF; AND 0xFF00 cannot keep any -> 0.
  ConstRangeFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.ZeroExtend(b.Truncate(LoadGPR(b, 4), INT8_TYPE), INT32_TYPE);
    Value* m = b.And(x, b.LoadConstantUint32(0xFF00u));
    StoreGPR(b, 3, b.ZeroExtend(m, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x123456ABull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0ull); });
}

TEST_CASE("CONSTRANGE_SHR_ALL_BITS_OUT_ZERO", "[instr]") {
  // zero_extend(i8) (< 256) >> 8 shifts every possible bit out -> 0.
  ConstRangeFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.ZeroExtend(b.Truncate(LoadGPR(b, 4), INT8_TYPE), INT32_TYPE);
    Value* s = b.Shr(x, int8_t(8));
    StoreGPR(b, 3, b.ZeroExtend(s, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x000000ABull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0ull); });
}

TEST_CASE("CONSTRANGE_AND_OVERLAP_NOT_FOLDED", "[instr]") {
  // zero_extend(i8) AND 0x0F overlaps -> must NOT fold; result = byte & 0x0F.
  ConstRangeFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.ZeroExtend(b.Truncate(LoadGPR(b, 4), INT8_TYPE), INT32_TYPE);
    Value* m = b.And(x, b.LoadConstantUint32(0x0Fu));
    StoreGPR(b, 3, b.ZeroExtend(m, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x000000ABull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0x0Bull); });
}

TEST_CASE("CONSTRANGE_SHR_BITS_REMAIN_NOT_FOLDED", "[instr]") {
  // zero_extend(i8) >> 4 leaves bits -> must NOT fold; result = byte >> 4.
  ConstRangeFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.ZeroExtend(b.Truncate(LoadGPR(b, 4), INT8_TYPE), INT32_TYPE);
    Value* s = b.Shr(x, int8_t(4));
    StoreGPR(b, 3, b.ZeroExtend(s, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x000000ABull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0x0Aull); });
}

TEST_CASE("CONSTRANGE_FULL_VALUE_AND_NOT_FOLDED", "[instr]") {
  // A full-width i32 (unknown bits) AND 0xFF00 must NOT fold -> (r4 >> 8 ... )
  // i.e. result = low32(r4) & 0xFF00, here 0x3456 & 0xFF00 = 0x3400.
  ConstRangeFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.Truncate(LoadGPR(b, 4), INT32_TYPE);
    Value* m = b.And(x, b.LoadConstantUint32(0xFF00u));
    StoreGPR(b, 3, b.ZeroExtend(m, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x12343456ull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0x3400ull); });
}
