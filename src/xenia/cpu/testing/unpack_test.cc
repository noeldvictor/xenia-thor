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

constexpr uint32_t kUnpack8In16SignedSignedLo =
    PACK_TYPE_8_IN_16 | PACK_TYPE_TO_LO | PACK_TYPE_IN_SIGNED |
    PACK_TYPE_OUT_SIGNED | PACK_TYPE_OUT_UNSATURATE;
constexpr uint32_t kUnpack8In16SignedSignedHi =
    PACK_TYPE_8_IN_16 | PACK_TYPE_TO_HI | PACK_TYPE_IN_SIGNED |
    PACK_TYPE_OUT_SIGNED | PACK_TYPE_OUT_UNSATURATE;
constexpr uint32_t kUnpack16In32SignedSignedLo =
    PACK_TYPE_16_IN_32 | PACK_TYPE_TO_LO | PACK_TYPE_IN_SIGNED |
    PACK_TYPE_OUT_SIGNED | PACK_TYPE_OUT_UNSATURATE;
constexpr uint32_t kUnpack16In32SignedSignedHi =
    PACK_TYPE_16_IN_32 | PACK_TYPE_TO_HI | PACK_TYPE_IN_SIGNED |
    PACK_TYPE_OUT_SIGNED | PACK_TYPE_OUT_UNSATURATE;

}  // namespace

TEST_CASE("UNPACK_D3DCOLOR", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Unpack(LoadVR(b, 4), PACK_TYPE_D3DCOLOR));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        uint32_t value = 0;
        ctx->v[4] = vec128i(0, 0, 0, value);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128f(1.0f, 1.0f, 1.0f, 1.0f));
      });
  test.Run(
      [](PPCContext* ctx) {
        uint32_t value = 0x80506070;
        ctx->v[4] = vec128i(0, 0, 0, value);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0x3F800050, 0x3F800060, 0x3F800070, 0x3F800080));
      });
}

TEST_CASE("UNPACK_FLOAT16_2", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Unpack(LoadVR(b, 4), PACK_TYPE_FLOAT16_2));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128i(0, 0, 0, 0x3F800000));
           });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0, 0, 0, 0x7FFFFFFF); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result ==
                     vec128i(0x47FFE000, 0xC7FFE000, 0x00000000, 0x3F800000));
           });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0, 0, 0, 0x55556666); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result ==
                     vec128i(0x42AAA000, 0x44CCC000, 0x00000000, 0x3F800000));
           });
}

TEST_CASE("UNPACK_FLOAT16_4", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Unpack(LoadVR(b, 4), PACK_TYPE_FLOAT16_4));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128i(0));
           });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(0, 0, 0, 0, 0x64D2, 0x6D8B, 0x4881, 0x4491);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0x449A4000, 0x45B16000, 0x41102000, 0x40922000));
      });
}

TEST_CASE("UNPACK_SHORT_2", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Unpack(LoadVR(b, 4), PACK_TYPE_SHORT_2));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result ==
                     vec128i(0x40400000, 0x40400000, 0x00000000, 0x3F800000));
           });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0x7004FD60, 0x8201C990, 0x00000000, 0x7FFF8001);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0x40407FFF, 0x403F8001, 0x00000000, 0x3F800000));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0, 0, 0, (0x1234u << 16) | 0x5678u);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0x40401234, 0x40405678, 0x00000000, 0x3F800000));
      });
}

TEST_CASE("UNPACK_SHORT_4", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Unpack(LoadVR(b, 4), PACK_TYPE_SHORT_4));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128i(0x40400000u, 0x40400000u,
                                       0x40400000u, 0x40400000u));
           });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0, 0, 0x7FFF8001u, 0x12345678u);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0x40407FFFu, 0x403F8001u, 0x40401234u,
                                  0x40405678u));
      });
}

TEST_CASE("UNPACK_UINT_2101010", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Unpack(LoadVR(b, 4), PACK_TYPE_UINT_2101010));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128i(0x40400000u, 0x40400000u,
                                       0x40400000u, 0x3F800000u));
           });
  test.Run(
      [](PPCContext* ctx) { ctx->v[4] = vec128i(0, 0, 0, 0x40300801u); },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0x40400001u, 0x40400002u, 0x40400003u,
                                  0x3F800001u));
      });
}

TEST_CASE("UNPACK_ULONG_4202020", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.Unpack(LoadVR(b, 4), PACK_TYPE_ULONG_4202020));
    b.Return();
  });
  test.Run([](PPCContext* ctx) { ctx->v[4] = vec128i(0); },
           [](PPCContext* ctx) {
             auto result = ctx->v[3];
             REQUIRE(result == vec128i(0x40400000u, 0x40400000u,
                                       0x40400000u, 0x3F800000u));
           });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0, 0, 0x10000300u, 0x00200001u);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0x40400001u, 0x40400002u, 0x40400003u,
                                  0x3F800001u));
      });
}

TEST_CASE("UNPACK_8_IN_16", "[instr]") {
  {
    TestFunction to_lo([](HIRBuilder& b) {
      StoreVR(b, 3, b.Unpack(LoadVR(b, 4), kUnpack8In16SignedSignedLo));
      b.Return();
    });
    to_lo.Run(
        [](PPCContext* ctx) {
          ctx->v[4] = vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                              13, 14, 15, 16);
        },
        [](PPCContext* ctx) {
          auto result = ctx->v[3];
          REQUIRE(result == vec128s(9, 10, 11, 12, 13, 14, 15, 16));
        });
  }

  {
    TestFunction to_hi([](HIRBuilder& b) {
      StoreVR(b, 3, b.Unpack(LoadVR(b, 4), kUnpack8In16SignedSignedHi));
      b.Return();
    });
    to_hi.Run(
        [](PPCContext* ctx) {
          ctx->v[4] = vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                              13, 14, 15, 16);
        },
        [](PPCContext* ctx) {
          auto result = ctx->v[3];
          REQUIRE(result == vec128s(1, 2, 3, 4, 5, 6, 7, 8));
        });
  }
}

TEST_CASE("UNPACK_16_IN_32", "[instr]") {
  {
    TestFunction to_lo([](HIRBuilder& b) {
      StoreVR(b, 3, b.Unpack(LoadVR(b, 4), kUnpack16In32SignedSignedLo));
      b.Return();
    });
    to_lo.Run([](PPCContext* ctx) {
      ctx->v[4] = vec128s(1, 2, 3, 4, 5, 6, 7, 8);
    },
              [](PPCContext* ctx) {
                auto result = ctx->v[3];
                REQUIRE(result == vec128i(5, 6, 7, 8));
              });
  }

  {
    TestFunction to_hi([](HIRBuilder& b) {
      StoreVR(b, 3, b.Unpack(LoadVR(b, 4), kUnpack16In32SignedSignedHi));
      b.Return();
    });
    to_hi.Run([](PPCContext* ctx) {
      ctx->v[4] = vec128s(1, 2, 3, 4, 5, 6, 7, 8);
    },
              [](PPCContext* ctx) {
                auto result = ctx->v[3];
                REQUIRE(result == vec128i(1, 2, 3, 4));
              });
  }
}
