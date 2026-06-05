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

using namespace xe;
using namespace xe::cpu::hir;
using namespace xe::cpu;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

// OPCODE_DOT_PRODUCT_3/4 constant folding must match the backend
// EmulateDotProduct3/4: accumulate lane products in double precision, narrow to
// float, and convert an infinite result to QNaN (0x7FC00000). The previous fold
// summed in FP32 with no inf->QNaN step, diverging from the runtime on overflow
// (inf vs QNaN). The result is read by bit-casting the scalar float to int.

TEST_CASE("DOT_PRODUCT_3_CONST_NORMAL", "[instr]") {
  // 1*1 + 2*2 + 3*3 = 14.0f (0x41600000); lane 3 must be ignored.
  TestFunction test([](HIRBuilder& b) {
    vec128_t a = vec128f(1.0f, 2.0f, 3.0f, 1.0e20f);
    vec128_t c = vec128f(1.0f, 2.0f, 3.0f, 1.0e20f);
    StoreGPR(b, 3,
             b.ZeroExtend(b.Cast(b.DotProduct3(b.LoadConstantVec128(a),
                                               b.LoadConstantVec128(c)),
                                 INT32_TYPE),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0x41600000u);
  });
}

TEST_CASE("DOT_PRODUCT_4_CONST_NORMAL", "[instr]") {
  // 1*1 + 2*2 + 3*3 + 4*4 = 30.0f (0x41F00000).
  TestFunction test([](HIRBuilder& b) {
    vec128_t a = vec128f(1.0f, 2.0f, 3.0f, 4.0f);
    vec128_t c = vec128f(1.0f, 2.0f, 3.0f, 4.0f);
    StoreGPR(b, 3,
             b.ZeroExtend(b.Cast(b.DotProduct4(b.LoadConstantVec128(a),
                                               b.LoadConstantVec128(c)),
                                 INT32_TYPE),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0x41F00000u);
  });
}

TEST_CASE("DOT_PRODUCT_4_CONST_OVERFLOW_QNAN", "[instr]") {
  // 3e38 * 3 overflows FP32 -> +inf; fold must yield QNaN 0x7FC00000 (the old
  // fold left +inf 0x7F800000).
  TestFunction test([](HIRBuilder& b) {
    vec128_t a = vec128f(3.0e38f, 0.0f, 0.0f, 0.0f);
    vec128_t c = vec128f(3.0f, 0.0f, 0.0f, 0.0f);
    StoreGPR(b, 3,
             b.ZeroExtend(b.Cast(b.DotProduct4(b.LoadConstantVec128(a),
                                               b.LoadConstantVec128(c)),
                                 INT32_TYPE),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0x7FC00000u);
  });
}

TEST_CASE("DOT_PRODUCT_3_CONST_OVERFLOW_QNAN", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    vec128_t a = vec128f(3.0e38f, 0.0f, 0.0f, 0.0f);
    vec128_t c = vec128f(3.0f, 0.0f, 0.0f, 0.0f);
    StoreGPR(b, 3,
             b.ZeroExtend(b.Cast(b.DotProduct3(b.LoadConstantVec128(a),
                                               b.LoadConstantVec128(c)),
                                 INT32_TYPE),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0x7FC00000u);
  });
}
