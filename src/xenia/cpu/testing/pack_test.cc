/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/testing/util.h"

using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

namespace {

constexpr uint32_t kPack8In16SignedSignedSaturate =
    PACK_TYPE_8_IN_16 | PACK_TYPE_IN_SIGNED | PACK_TYPE_OUT_SIGNED |
    PACK_TYPE_OUT_SATURATE;
constexpr uint32_t kPack16In32SignedSignedSaturate =
    PACK_TYPE_16_IN_32 | PACK_TYPE_IN_SIGNED | PACK_TYPE_OUT_SIGNED |
    PACK_TYPE_OUT_SATURATE;

}  // namespace

TEST_CASE("PACK_D3DCOLOR", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Pack(LoadVR(b, 4), PACK_TYPE_D3DCOLOR));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128f(1.0f); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128i(0));
           });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x40400050, 0x40400060, 0x40400070, 0x40400080);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0, 0, 0, 0x80506070));
      });
}

TEST_CASE("PACK_FLOAT16_2", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Pack(LoadVR(b, 4), PACK_TYPE_FLOAT16_2));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0, 0, 0, 0x3F800000); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128i(0));
           });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x47FFE000, 0xC7FFE000, 0x00000000, 0x3F800000);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0, 0, 0, 0x7FFFFFFF));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x42AAA000, 0x44CCC000, 0x00000000, 0x3F800000);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0, 0, 0, 0x55556666));
      });
}

TEST_CASE("PACK_FLOAT16_4", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Pack(LoadVR(b, 4), PACK_TYPE_FLOAT16_4));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0, 0, 0, 0); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128i(0));
           });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x449A4000, 0x45B17000, 0x41103261, 0x40922B6B);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0x00000000, 0x00000000, 0x64D26D8C, 0x48824491));
      });
}

TEST_CASE("PACK_SHORT_2", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Pack(LoadVR(b, 4), PACK_TYPE_SHORT_2));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128i(0));
           });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x43817E00, 0xC37CFC00, 0, 0);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0, 0, 0, 0x7FFF8001));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0xC0D47D97, 0xC2256E9D, 0, 0);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0, 0, 0, 0x80018001));
      });
}

TEST_CASE("PACK_SHORT_4", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Pack(LoadVR(b, 4), PACK_TYPE_SHORT_4));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] =
            vec128i(0x40407FFFu, 0x403F8001u, 0x40401234u, 0x40405678u);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0, 0, 0x7FFF8001u, 0x12345678u));
      });
}

TEST_CASE("PACK_UINT_2101010", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Pack(LoadVR(b, 4), PACK_TYPE_UINT_2101010));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] =
            vec128i(0x40400001u, 0x40400002u, 0x40400003u, 0x3F800001u);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result.u32[3] == 0x40300801u);
      });
}

TEST_CASE("PACK_ULONG_4202020", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Pack(LoadVR(b, 4), PACK_TYPE_ULONG_4202020));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] =
            vec128i(0x40400001u, 0x40400002u, 0x40400003u, 0x3F800001u);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result.u32[2] == 0x10000300u);
        REQUIRE(result.u32[3] == 0x00200001u);
      });
}

TEST_CASE("PACK_8_IN_16", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 6,
            b.Pack(LoadVR(b, 4), LoadVR(b, 5),
                   kPack8In16SignedSignedSaturate));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(1, 2, 3, 4, 5, 6, 7, 8);
        ctx->v[5] = vec128s(9, 10, 11, 12, 13, 14, 15, 16);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[6];
        REQUIRE(result ==
                vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                        14, 15, 16));
      });
}

TEST_CASE("PACK_16_IN_32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 6,
            b.Pack(LoadVR(b, 4), LoadVR(b, 5),
                   kPack16In32SignedSignedSaturate));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(1, 2, 3, 4);
        ctx->v[5] = vec128i(5, 6, 7, 8);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[6];
        REQUIRE(result == vec128s(1, 2, 3, 4, 5, 6, 7, 8));
      });
}
