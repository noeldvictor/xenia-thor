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

using namespace xe::cpu::hir;
using namespace xe::cpu;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

// The SimplificationPass AND(x, NOT(y)) -> AND_NOT fold is gated behind this
// cvar (default-off, pending the default-on flip). Declared here so the fold
// can be forced on to validate the transform itself (not just the opcode).
DECLARE_bool(hir_fold_and_not);

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

TEST_CASE("AND_NOT_FOLD_TRANSFORM", "[instr]") {
  // Directly exercise the SimplificationPass fold (the prior tests only cover the
  // AND_NOT opcode + the equivalence, NOT the rewrite). With hir_fold_and_not ON,
  // the test_module pipeline's SimplificationPass rewrites And(x, Not(y)) ->
  // AND_NOT(x, y) and DCE drops the orphaned NOT; the result must still be x & ~y.
  // Verifies BOTH operand orders (AND commutes; the fold normalizes the NOT to
  // src2). Backend-independent HIR transform -> validating on the host x64 suite
  // (and qemu a64) is the device-free half of the default-on decision.
  struct FoldGuard {
    bool prev;
    FoldGuard() : prev(cvars::hir_fold_and_not) {
      cvars::hir_fold_and_not = true;
    }
    ~FoldGuard() { cvars::hir_fold_and_not = prev; }
  } guard;

  TestFunction test([](HIRBuilder& b) {
    // r3 = And(x, Not(y))      -> AND_NOT(x, y)
    StoreGPR(b, 3,
             b.ZeroExtend(b.And(b.Truncate(LoadGPR(b, 4), INT32_TYPE),
                                b.Not(b.Truncate(LoadGPR(b, 5), INT32_TYPE))),
                          INT64_TYPE));
    // r6 = And(Not(y), x)      -> AND_NOT(x, y)  (commuted operand)
    StoreGPR(b, 6,
             b.ZeroExtend(b.And(b.Not(b.Truncate(LoadGPR(b, 5), INT32_TYPE)),
                                b.Truncate(LoadGPR(b, 4), INT32_TYPE)),
                          INT64_TYPE));
    b.Return();
  });
  auto check = [](PPCContext* ctx) {
    const uint32_t x = static_cast<uint32_t>(ctx->r[4]);
    const uint32_t y = static_cast<uint32_t>(ctx->r[5]);
    const uint32_t expected = x & ~y;
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == expected);
    REQUIRE(static_cast<uint32_t>(ctx->r[6]) == expected);
  };
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x12345678u; ctx->r[5] = 0xDEADBEEFu; },
           check);
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0xFFFFFFFFu; ctx->r[5] = 0x00000000u; },
           check);
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0xAAAAAAAAu; ctx->r[5] = 0x55555555u; },
           check);
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x00000000u; ctx->r[5] = 0xFFFFFFFFu; },
           check);
}
