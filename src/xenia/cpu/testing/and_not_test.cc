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

using namespace xe::cpu::hir;
using namespace xe::cpu;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

// Covers OPCODE_AND_NOT (dest = src1 & ~src2): the constant-propagation fold
// (constant_propagation_pass), the runtime backend lowering, and crucially the
// And(x, Not(y)) == AndNot(x, y) equivalence that the SimplificationPass
// AND(x, NOT(y)) -> AND_NOT fold relies on. AND_NOT is NOT commutative.

TEST_CASE("AND_NOT_I32_CONST", "[instr]") {
  // 0xF0F0F0F0 & ~0x0FFFF000 = 0xF0F0F0F0 & 0xF0000FFF = 0xF00000F0
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.AndNot(b.LoadConstantUint32(0xF0F0F0F0u),
                                   b.LoadConstantUint32(0x0FFFF000u)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0xF00000F0u);
  });
}

TEST_CASE("AND_NOT_I32_RUNTIME", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.AndNot(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                                   b.Truncate(LoadGPR(b, 5), INT32_TYPE)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0xF0F0F0F0u;
        ctx->r[5] = 0x0FFFF000u;
      },
      [](PPCContext* ctx) {
        REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0xF00000F0u);
      });
}

TEST_CASE("AND_NOT_EQUIV_AND_OF_NOT", "[instr]") {
  // r3 = And(x, Not(y)); r6 = AndNot(x, y). These must match for all inputs -
  // the equivalence the AND(x, NOT(y)) -> AND_NOT fold depends on.
  TestFunction test([](HIRBuilder& b) {
    auto x = b.Truncate(LoadGPR(b, 4), INT32_TYPE);
    auto y = b.Truncate(LoadGPR(b, 5), INT32_TYPE);
    StoreGPR(b, 3, b.ZeroExtend(b.And(x, b.Not(y)), INT64_TYPE));
    auto x2 = b.Truncate(LoadGPR(b, 4), INT32_TYPE);
    auto y2 = b.Truncate(LoadGPR(b, 5), INT32_TYPE);
    StoreGPR(b, 6, b.ZeroExtend(b.AndNot(x2, y2), INT64_TYPE));
    b.Return();
  });
  auto check = [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == static_cast<uint32_t>(ctx->r[6]));
  };
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0x12345678u;
        ctx->r[5] = 0xDEADBEEFu;
      },
      check);
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0xFFFFFFFFu;
        ctx->r[5] = 0x00000000u;
      },
      check);
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0xAAAAAAAAu;
        ctx->r[5] = 0x55555555u;
      },
      check);
}

TEST_CASE("AND_NOT_I64_RUNTIME", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3, b.AndNot(LoadGPR(b, 4), LoadGPR(b, 5)));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0xFFFF0000FFFF0000ULL;
        ctx->r[5] = 0x0F0F0F0F0F0F0F0FULL;
      },
      [](PPCContext* ctx) {
        REQUIRE(ctx->r[3] == (0xFFFF0000FFFF0000ULL & ~0x0F0F0F0F0F0F0F0FULL));
      });
}
