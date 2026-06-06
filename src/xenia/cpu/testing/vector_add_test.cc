/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/testing/util.h"

#include <cfloat>
#include <climits>

using namespace xe;
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

TEST_CASE("VECTOR_ADD_I8", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] =
            vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        ctx->v[5] =
            vec128b(100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(100, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22,
                                  24, 26, 28, 30));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(UINT8_MAX);
        ctx->v[5] = vec128b(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(0));
      });
}

TEST_CASE("VECTOR_ADD_I8_SAT_SIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE,
                        ARITHMETIC_SATURATE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(INT8_MAX);
        ctx->v[5] = vec128b(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(INT8_MAX));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(INT8_MIN);
        ctx->v[5] = vec128b(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(INT8_MIN));
      });
}

TEST_CASE("VECTOR_ADD_I8_SAT_UNSIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE,
                        ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(UINT8_MAX);
        ctx->v[5] = vec128b(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(UINT8_MAX));
      });
}

TEST_CASE("VECTOR_ADD_I16", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(0, 1, 2, 3, 4, 5, 6, 7);
        ctx->v[5] = vec128s(100, 1, 2, 3, 4, 5, 6, 7);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(100, 2, 4, 6, 8, 10, 12, 14));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(UINT16_MAX);
        ctx->v[5] = vec128s(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(0));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(0);
        ctx->v[5] = vec128s(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(UINT16_MAX));
      });
}

TEST_CASE("VECTOR_ADD_I16_SAT_SIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE,
                        ARITHMETIC_SATURATE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(INT16_MAX);
        ctx->v[5] = vec128s(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(INT16_MAX));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(INT16_MIN);
        ctx->v[5] = vec128s(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(INT16_MIN));
      });
}

TEST_CASE("VECTOR_ADD_I16_SAT_UNSIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE,
                        ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(UINT16_MAX);
        ctx->v[5] = vec128s(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(UINT16_MAX));
      });
}

TEST_CASE("VECTOR_ADD_I32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0, 1, 2, 3);
        ctx->v[5] = vec128i(100, 1, 2, 3);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(100, 2, 4, 6));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(UINT32_MAX);
        ctx->v[5] = vec128i(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(0);
        ctx->v[5] = vec128i(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(UINT32_MAX));
      });
}

TEST_CASE("VECTOR_ADD_I32_SAT_SIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE,
                        ARITHMETIC_SATURATE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(5);
        ctx->v[5] = vec128i(5);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(10));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(INT32_MAX);
        ctx->v[5] = vec128i(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(INT32_MAX));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(INT32_MIN);
        ctx->v[5] = vec128i(-1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(INT32_MIN));
      });
}

TEST_CASE("VECTOR_ADD_I32_SAT_UNSIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE,
                        ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(5);
        ctx->v[5] = vec128i(5);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(10));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(UINT32_MAX);
        ctx->v[5] = vec128i(1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(UINT32_MAX));
      });
}

TEST_CASE("VECTOR_ADD_F32", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAdd(LoadVR(b, 4), LoadVR(b, 5), FLOAT32_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128f(0.12f, 0.34f, 0.56f, 0.78f);
        ctx->v[5] = vec128f(0.12f, 0.34f, 0.56f, 0.78f);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result ==
                vec128i(0x3E75C28F, 0x3F2E147B, 0x3F8F5C29, 0x3FC7AE14));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128f(FLT_MAX);
        ctx->v[5] = vec128f(FLT_MAX);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0x7F800000));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128f(-FLT_MIN);
        ctx->v[5] = vec128f(-1.0f);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0xBF800000));
      });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128f(FLT_MAX);
        ctx->v[5] = vec128f(1.0f);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(0x7F7FFFFF));
      });
}

// VECTOR_AVERAGE (vavg*) = (a + b + 1) >> 1 in extended precision (rounds up,
// floors toward -inf for signed). a64 has native srhadd/urhadd for every width;
// x64 has only unsigned byte/word PAVG and emulates signed + all 32-bit (and,
// before this, asserted on signed byte/word). Cross-backend differential.
TEST_CASE("VECTOR_AVERAGE_I8_SIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAverage(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE, 0));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(127, -128, 100, -100, -7, 99, -99, 0, 1, -1, 50,
                            -50, 3, -3, 10, -10);
        ctx->v[5] = vec128b(127, -128, 50, 50, -8, 99, -99, 0, 1, -1, 50, -50,
                            2, 2, 21, -21);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(127, -128, 75, -25, -7, 99, -99, 0, 1, -1, 50,
                                  -50, 3, 0, 16, -15));
      });
}

TEST_CASE("VECTOR_AVERAGE_I8_UNSIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAverage(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE,
                            ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(255, 200, 0, 1, 128, 64, 10, 255, 254, 2, 100, 150,
                            7, 9, 255, 17);
        ctx->v[5] = vec128b(255, 100, 0, 1, 128, 64, 21, 0, 1, 3, 100, 150, 8,
                            9, 1, 18);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(255, 150, 0, 1, 128, 64, 16, 128, 128, 3, 100,
                                  150, 8, 9, 128, 18));
      });
}

TEST_CASE("VECTOR_AVERAGE_I16_SIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAverage(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE, 0));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128s(32767, -32768, 1000, -1000, 100, -100, 5, -5);
        ctx->v[5] = vec128s(32767, -32768, 500, 500, 50, 50, 6, -6);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(32767, -32768, 750, -250, 75, -25, 6, -5));
      });
}

TEST_CASE("VECTOR_AVERAGE_I16_UNSIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAverage(LoadVR(b, 4), LoadVR(b, 5), INT16_TYPE,
                            ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        // -1 == 0xFFFF == 65535u, -32768 == 0x8000 == 32768u (unsigned view).
        ctx->v[4] = vec128s(-1, 1000, 0, 1, -32768, 100, 7, 9);
        ctx->v[5] = vec128s(-1, 500, 0, 1, -32768, 100, 8, 9);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128s(-1, 750, 0, 1, -32768, 100, 8, 9));
      });
}

TEST_CASE("VECTOR_AVERAGE_I32_SIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorAverage(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE, 0));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(INT_MAX, INT_MIN, 1000000, -1000000);
        ctx->v[5] = vec128i(INT_MAX, INT_MIN, 500000, 500000);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(INT_MAX, INT_MIN, 750000, -250000));
      });
}

TEST_CASE("VECTOR_AVERAGE_I32_UNSIGNED", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorAverage(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE,
                            ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        // -1 == 0xFFFFFFFF == UINT_MAX (unsigned view).
        ctx->v[4] = vec128i(-1, 0, 2000000000, 1);
        ctx->v[5] = vec128i(-1, 0, 2000000000, 1);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(-1, 0, 2000000000, 1));
      });
}

// VECTOR_SUB saturating (vsubsws/vsubuws/vsubsbs...) clamps a-b to the type
// range. a64 has native sqsub/uqsub for every width; x64 has PSUBS/PSUBUS only
// for byte/word and EMULATES the 32-bit saturating cases with hand-rolled
// overflow detection - so the 32-bit signed/unsigned saturate is the key
// cross-backend differential (emulation vs native).
TEST_CASE("VECTOR_SUB_I32_SIGNED_SATURATE", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorSub(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE,
                        ARITHMETIC_SATURATE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(INT_MAX, INT_MIN, 100, -100);
        ctx->v[5] = vec128i(INT_MIN, INT_MAX, 50, 50);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        // MAX-MIN overflows +, MIN-MAX overflows -, both saturate.
        REQUIRE(result == vec128i(INT_MAX, INT_MIN, 50, -150));
      });
}

TEST_CASE("VECTOR_SUB_I32_UNSIGNED_SATURATE", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorSub(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE,
                        ARITHMETIC_SATURATE | ARITHMETIC_UNSIGNED));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        // -1 == UINT_MAX (unsigned). Underflow (b>a) saturates to 0.
        ctx->v[4] = vec128i(100, 0, -1, 5);
        ctx->v[5] = vec128i(50, 1, 0, 10);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128i(50, 0, -1, 0));
      });
}

TEST_CASE("VECTOR_SUB_I8_SIGNED_SATURATE", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3,
            b.VectorSub(LoadVR(b, 4), LoadVR(b, 5), INT8_TYPE,
                        ARITHMETIC_SATURATE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128b(127, -128, 100, -100, 0, 50, -50, 10, 1, -1, 60,
                            -60, 127, -128, 5, -5);
        ctx->v[5] = vec128b(-128, 127, 50, 50, 0, 50, -50, 10, 1, -1, -70, 70,
                            1, -1, 6, 6);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        REQUIRE(result == vec128b(127, -128, 50, -128, 0, 0, 0, 0, 0, 0, 127,
                                  -128, 126, -127, -1, -11));
      });
}

TEST_CASE("VECTOR_SUB_I32_MODULO", "[instr]") {
  TestFunction test([](HIRBuilder& b) {
    StoreVR(b, 3, b.VectorSub(LoadVR(b, 4), LoadVR(b, 5), INT32_TYPE));
    b.Return();
  });
  test.Run(
      [](PPCContext* ctx) {
        ctx->v[4] = vec128i(INT_MAX, 0, 100, -100);
        ctx->v[5] = vec128i(-1, 1, 50, 50);
      },
      [](PPCContext* ctx) {
        auto result = ctx->v[3];
        // INT_MAX-(-1) wraps to INT_MIN (modulo, no saturate).
        REQUIRE(result == vec128i(INT_MIN, -1, 50, -150));
      });
}
