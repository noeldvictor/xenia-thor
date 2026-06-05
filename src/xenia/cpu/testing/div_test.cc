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

// These exercise constant folding of OPCODE_DIV through hir::Value::Div in the
// constant_propagation pass (both operands constant). PPC integer divides never
// trap: divide-by-zero and signed overflow (INT_MIN / -1) both yield 0. The fix
// routes folding through PPCIDiv/PPCUDiv; before it, folding did raw host C++
// '/', which is undefined behavior (a #DE/SIGFPE host crash on x86) - so the
// by-zero/overflow cases here would crash the test runner during module
// compilation without the fix, and return 0 with it.

TEST_CASE("DIV_I32_CONST_NORMAL", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(
        b, 3,
        b.ZeroExtend(b.Div(b.LoadConstantInt32(20), b.LoadConstantInt32(4)),
                     INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {},
           [](PPCContext* ctx) { REQUIRE(static_cast<int32_t>(ctx->r[3]) == 5); });
}

TEST_CASE("DIV_I32_CONST_BY_ZERO", "[instr]") {
  // PPC: x / 0 == 0 (no trap). Must not host-crash during constant folding.
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(
        b, 3,
        b.ZeroExtend(b.Div(b.LoadConstantInt32(123), b.LoadConstantInt32(0)),
                     INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {},
           [](PPCContext* ctx) { REQUIRE(static_cast<int32_t>(ctx->r[3]) == 0); });
}

TEST_CASE("DIV_I32_CONST_SIGNED_OVERFLOW", "[instr]") {
  // PPC: INT_MIN / -1 == 0 (host signed-overflow would be UB).
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Div(b.LoadConstantInt32(INT32_MIN),
                                b.LoadConstantInt32(-1)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {},
           [](PPCContext* ctx) { REQUIRE(static_cast<int32_t>(ctx->r[3]) == 0); });
}

TEST_CASE("DIV_I32_CONST_UNSIGNED_BY_ZERO", "[instr]") {
  // PPC divwu: x / 0 == 0. High-bit-set numerator to also confirm the unsigned
  // path is taken (signed folding of 0x80000001 / 0 would also be 0 here, but
  // the helper routing must use the unsigned divide).
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Div(b.LoadConstantUint32(0x80000001u),
                                b.LoadConstantUint32(0u), ARITHMETIC_UNSIGNED),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0u);
  });
}

TEST_CASE("DIV_I64_CONST_SIGNED_OVERFLOW", "[instr]") {
  // 64-bit INT_MIN / -1 == 0.
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.Div(b.LoadConstantInt64(INT64_MIN), b.LoadConstantInt64(-1)));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {},
           [](PPCContext* ctx) { REQUIRE(static_cast<int64_t>(ctx->r[3]) == 0); });
}
