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

// Constant folding of SHL/SHR/SHA must mask the shift amount to the operand
// width, exactly as the backends do (a64 & 0x1F/0x3F; host shl/shr mask by
// width) and as PPC requires (count >= width yields 0 / sign for arithmetic).
// hir::Value::Shl/Shr/Sha previously shifted by the raw count: for I8/I16 the
// operand promotes to int and a count >= width yields 0 (a WRONG fold), and for
// I32/I64 a count >= width is C++ undefined behavior. The I8/I16 cases below
// are discriminating on any host (they fold to 0 without the width mask).

TEST_CASE("SHL_I8_CONST_MASK_BY_8", "[instr]") {
  // 8 & 0x7 == 0 -> value unchanged (unmasked u8 << 8 would be 0).
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Shl(b.LoadConstantInt8(0x12), b.LoadConstantInt8(8)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {},
           [](PPCContext* ctx) { REQUIRE(static_cast<uint8_t>(ctx->r[3]) == 0x12); });
}

TEST_CASE("SHL_I16_CONST_MASK_BY_16", "[instr]") {
  // 16 & 0xF == 0 -> unchanged.
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(
                 b.Shl(b.LoadConstantInt16(0x1234), b.LoadConstantInt8(16)),
                 INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint16_t>(ctx->r[3]) == 0x1234);
  });
}

TEST_CASE("SHR_I8_CONST_MASK_BY_8", "[instr]") {
  // logical >> : 8 & 0x7 == 0 -> unchanged (unmasked u8 >> 8 would be 0).
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(b.Shr(b.LoadConstantInt8(0x12), b.LoadConstantInt8(8)),
                          INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {},
           [](PPCContext* ctx) { REQUIRE(static_cast<uint8_t>(ctx->r[3]) == 0x12); });
}

TEST_CASE("SHA_I8_CONST_MASK_BY_8_NEG", "[instr]") {
  // arithmetic >> : 8 & 0x7 == 0 -> unchanged (unmasked i8(-128) >> 8 would be
  // -1 = 0xFF; the mask keeps the original 0x80).
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(
                 b.Sha(b.LoadConstantInt8(static_cast<int8_t>(0x80)),
                       b.LoadConstantInt8(8)),
                 INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {},
           [](PPCContext* ctx) { REQUIRE(static_cast<uint8_t>(ctx->r[3]) == 0x80); });
}

TEST_CASE("SHL_I32_CONST_MASK_BY_33", "[instr]") {
  // 33 & 0x1F == 1 -> << 1 (documents the I32 mask; matches the backend).
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3,
             b.ZeroExtend(
                 b.Shl(b.LoadConstantUint32(0x00000001u), b.LoadConstantInt8(33)),
                 INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    REQUIRE(static_cast<uint32_t>(ctx->r[3]) == 0x00000002u);
  });
}

TEST_CASE("SHL_I64_CONST_MASK_BY_64", "[instr]") {
  // 64 & 0x3F == 0 -> unchanged.
  TestFunction test([](HIRBuilder& b) {
    StoreGPR(b, 3, b.Shl(b.LoadConstantInt64(0x1ULL), b.LoadConstantInt8(64)));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {},
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0x1ULL); });
}
