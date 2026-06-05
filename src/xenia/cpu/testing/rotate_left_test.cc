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

// These exercise constant folding of OPCODE_ROTATE_LEFT through
// hir::Value::RotateLeft (constant_propagation pass; both operands constant).
// The fold masks the amount to the type width and treats a masked amount of 0
// as a no-op, matching the a64 ROTATE_LEFT sequences. The masked-to-zero case
// is the important one: base/math.h rotate_left() shifts by the full width when
// the amount is 0 (undefined behavior on the non-Win32 path), so the fold must
// guard it rather than call the helper with 0.

TEST_CASE("ROTATE_LEFT_I32_CONST_BY_4", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.RotateLeft(b.LoadConstantUint32(0x12345678u),
                                       b.LoadConstantInt8(4)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0x23456781u);
  });
}

TEST_CASE("ROTATE_LEFT_I32_CONST_BY_31", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.RotateLeft(b.LoadConstantUint32(1u),
                                       b.LoadConstantInt8(31)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0x80000000u);
  });
}

TEST_CASE("ROTATE_LEFT_I32_CONST_BY_32_MASKS_TO_ZERO", "[instr]") {
  // amount & 31 == 0 -> value unchanged (and must not hit the rotate_left()
  // shift-by-width UB). HIRBuilder short-circuits a *literal* 0 amount, so use
  // 32 to reach the fold and verify the mask + zero guard in Value::RotateLeft.
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.RotateLeft(b.LoadConstantUint32(0xDEADBEEFu),
                                       b.LoadConstantInt8(32)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0xDEADBEEFu);
  });
}

TEST_CASE("ROTATE_LEFT_I64_CONST_BY_8", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.RotateLeft(b.LoadConstantInt64(0x0123456789ABCDEFLL),
                          b.LoadConstantInt8(8)));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint64_t>(ctx->r[3]) == 0x23456789ABCDEF01ULL);
  });
}
