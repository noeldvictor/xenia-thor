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

DECLARE_bool(hir_known_bits_mask_fold);

using namespace xe::cpu::hir;
using namespace xe::cpu;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

// Known-bits (NZM) redundant-mask elimination: AND(x, c) becomes a plain assign
// to x when a conservative upper bound on x's possibly-set bits already fits
// inside the mask c (x & c == x for every possible x). These tests run with the
// fold ENABLED and assert BIT-EXACT results against the value the AND must yield
// regardless of whether the fold fired - so a buggy/over-aggressive fold (one
// that drops bits the mask should have cleared) is caught. The inputs come from
// a GPR (runtime), not constants, so constant-propagation does not pre-fold the
// AND and the fold path is actually exercised. See hir_known_bits_mask_fold.

namespace {
// RAII: enable the fold for the test, restore the prior value afterward.
struct KnownBitsFoldGuard {
  bool previous_;
  KnownBitsFoldGuard() {
    previous_ = cvars::hir_known_bits_mask_fold;
    cvars::hir_known_bits_mask_fold = true;
  }
  ~KnownBitsFoldGuard() { cvars::hir_known_bits_mask_fold = previous_; }
};
}  // namespace

TEST_CASE("KNOWNBITS_REDUNDANT_MASK_ZEXT_I8", "[instr]") {
  // zero_extend(i8) has bits <= 0xFF; AND with 0xFF is redundant -> assign.
  KnownBitsFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.ZeroExtend(b.Truncate(LoadGPR(b, 4), INT8_TYPE), INT32_TYPE);
    Value* m = b.And(x, b.LoadConstantUint32(0xFFu));
    StoreGPR(b, 3, b.ZeroExtend(m, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x123456ABull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0xABull); });
}

TEST_CASE("KNOWNBITS_REDUNDANT_MASK_FULL_I32", "[instr]") {
  // AND an i32 with 0xFFFFFFFF is always redundant -> assign (low 32 bits).
  KnownBitsFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.Truncate(LoadGPR(b, 4), INT32_TYPE);
    Value* m = b.And(x, b.LoadConstantUint32(0xFFFFFFFFu));
    StoreGPR(b, 3, b.ZeroExtend(m, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x1122334455667788ull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0x55667788ull); });
}

TEST_CASE("KNOWNBITS_MASK_AFTER_SHL", "[instr]") {
  // zero_extend(i8) << 4 has bits <= 0xFF0; AND 0xFFF is redundant -> assign.
  KnownBitsFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.ZeroExtend(b.Truncate(LoadGPR(b, 4), INT8_TYPE), INT32_TYPE);
    Value* sh = b.Shl(x, int8_t(4));
    Value* m = b.And(sh, b.LoadConstantUint32(0xFFFu));
    StoreGPR(b, 3, b.ZeroExtend(m, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x000000ABull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0xAB0ull); });
}

TEST_CASE("KNOWNBITS_MASK_VIA_OR", "[instr]") {
  // OR of two zero_extend(i8) has bits <= 0xFF; AND 0xFF redundant -> assign.
  KnownBitsFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* a = b.ZeroExtend(b.Truncate(LoadGPR(b, 4), INT8_TYPE), INT32_TYPE);
    Value* c = b.ZeroExtend(b.Truncate(LoadGPR(b, 5), INT8_TYPE), INT32_TYPE);
    Value* m = b.And(b.Or(a, c), b.LoadConstantUint32(0xFFu));
    StoreGPR(b, 3, b.ZeroExtend(m, INT64_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->r[4] = 0x000000A0ull;
        ctx->r[5] = 0x0000000Bull;
      },
      [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0xABull); });
}

TEST_CASE("KNOWNBITS_PARTIAL_MASK_NOT_OVERFOLDED", "[instr]") {
  // zero_extend(i8) has bits <= 0xFF, but mask 0x0F is NARROWER -> must NOT fold
  // (a buggy fold would return the whole byte instead of byte & 0x0F).
  KnownBitsFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.ZeroExtend(b.Truncate(LoadGPR(b, 4), INT8_TYPE), INT32_TYPE);
    Value* m = b.And(x, b.LoadConstantUint32(0x0Fu));
    StoreGPR(b, 3, b.ZeroExtend(m, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0x000000ABull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0x0Bull); });
}

TEST_CASE("KNOWNBITS_UNKNOWN_VALUE_NOT_OVERFOLDED", "[instr]") {
  // A full-width i32 (unknown bits) AND 0x0F must NOT fold -> byte & 0x0F.
  KnownBitsFoldGuard guard;
  TestFunction test([](HIRBuilder& b) {
    Value* x = b.Truncate(LoadGPR(b, 4), INT32_TYPE);
    Value* m = b.And(x, b.LoadConstantUint32(0x0Fu));
    StoreGPR(b, 3, b.ZeroExtend(m, INT64_TYPE));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->r[4] = 0xFFFFFFABull; },
           [](PPCContext* ctx) { REQUIRE(ctx->r[3] == 0x0Bull); });
}
