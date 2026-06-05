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
#include <limits>

using namespace xe;
using namespace xe::cpu::hir;
using namespace xe::cpu;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

// OPCODE_VECTOR_CONVERT_F2I constant folding must SATURATE and handle NaN like
// the backends (a64 fcvtzs/fcvtzu, x64 saturation sequence): NaN -> 0,
// out-of-range -> type min/max, truncation toward zero. The fold previously did
// a raw C cast, which is C++ UB for NaN/inf/out-of-range and (on x64) yields
// 0x80000000 for all of them - diverging from the saturating runtime. These
// cases are discriminating: without the saturating fold the NaN/overflow lanes
// come out 0x80000000 instead of 0 / INT_MAX.

TEST_CASE("VECTOR_CONVERT_F2I_SIGNED_SATURATE", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    vec128_t in = vec128f(std::numeric_limits<float>::quiet_NaN(), 3.0e9f,
                          -3.0e9f, 5.5f);
    StoreVR(b, 3, b.VectorConvertF2I(b.LoadConstantVec128(in)));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    // NaN->0, +overflow->INT_MAX, -overflow->INT_MIN, 5.5->5
    REQUIRE(ctx->v[3] == vec128i(0u, 0x7FFFFFFFu, 0x80000000u, 5u));
  });
}

TEST_CASE("VECTOR_CONVERT_F2I_UNSIGNED_SATURATE", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    vec128_t in = vec128f(std::numeric_limits<float>::quiet_NaN(), 5.0e9f,
                          -1.0f, 100.9f);
    StoreVR(b, 3,
            b.VectorConvertF2I(b.LoadConstantVec128(in), ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    // NaN->0, >=2^32->UINT_MAX, negative->0, 100.9->100
    REQUIRE(ctx->v[3] == vec128i(0u, 0xFFFFFFFFu, 0u, 100u));
  });
}

TEST_CASE("VECTOR_CONVERT_F2I_SIGNED_INRANGE", "[instr]") {
  // In-range values truncate toward zero (saturation must not perturb them).
  TestFunction test([](HIRBuilder& b) {
    vec128_t in = vec128f(1.9f, -1.9f, 1000.0f, -1000.0f);
    StoreVR(b, 3, b.VectorConvertF2I(b.LoadConstantVec128(in)));
    b.Return();
  });
  test.Run([](PPCContext* ctx) {}, [](PPCContext* ctx) {
    // 1.9->1, -1.9->-1, 1000->1000, -1000->-1000
    REQUIRE(ctx->v[3] == vec128i(1u, 0xFFFFFFFFu, 1000u, 0xFFFFFC18u));
  });
}
