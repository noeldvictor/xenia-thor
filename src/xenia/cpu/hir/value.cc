/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/hir/value.h"

#include <climits>
#include <cmath>
#include <cstdlib>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/math.h"

namespace xe {
namespace cpu {
namespace hir {

Value::Use* Value::AddUse(Arena* arena, Instr* instr) {
  Use* use = arena->Alloc<Use>();
  use->instr = instr;
  use->prev = NULL;
  use->next = use_head;
  if (use_head) {
    use_head->prev = use;
  }
  use_head = use;
  return use;
}

void Value::RemoveUse(Use* use) {
  if (use == use_head) {
    use_head = use->next;
  } else {
    use->prev->next = use->next;
  }
  if (use->next) {
    use->next->prev = use->prev;
  }
}

uint32_t Value::AsUint32() {
  assert_true(IsConstant());
  switch (type) {
    case INT8_TYPE:
      return constant.u8;
    case INT16_TYPE:
      return constant.u16;
    case INT32_TYPE:
      return constant.u32;
    case INT64_TYPE:
      return (uint32_t)constant.u64;
    default:
      assert_unhandled_case(type);
      return 0;
  }
}

uint64_t Value::AsUint64() {
  assert_true(IsConstant());
  switch (type) {
    case INT8_TYPE:
      return constant.u8;
    case INT16_TYPE:
      return constant.u16;
    case INT32_TYPE:
      return constant.u32;
    case INT64_TYPE:
      return constant.u64;
    default:
      assert_unhandled_case(type);
      return 0;
  }
}

void Value::Cast(TypeName target_type) {
  // Only need a type change.
  type = target_type;
}

void Value::ZeroExtend(TypeName target_type) {
  switch (type) {
    case INT8_TYPE:
      type = target_type;
      constant.u64 = constant.u8;
      return;
    case INT16_TYPE:
      type = target_type;
      constant.u64 = constant.u16;
      return;
    case INT32_TYPE:
      type = target_type;
      constant.u64 = constant.u32;
      return;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::SignExtend(TypeName target_type) {
  switch (type) {
    case INT8_TYPE:
      type = target_type;
      switch (target_type) {
        case INT16_TYPE:
          constant.i16 = constant.i8;
          return;
        case INT32_TYPE:
          constant.i32 = constant.i8;
          return;
        case INT64_TYPE:
          constant.i64 = constant.i8;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case INT16_TYPE:
      type = target_type;
      switch (target_type) {
        case INT32_TYPE:
          constant.i32 = constant.i16;
          return;
        case INT64_TYPE:
          constant.i64 = constant.i16;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case INT32_TYPE:
      type = target_type;
      switch (target_type) {
        case INT64_TYPE:
          constant.i64 = constant.i32;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    default:
      assert_unhandled_case(type);
      return;
  }
}

void Value::Truncate(TypeName target_type) {
  switch (type) {
    case INT16_TYPE:
      switch (target_type) {
        case INT8_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFF;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case INT32_TYPE:
      switch (target_type) {
        case INT8_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFF;
          return;
        case INT16_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFFFF;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case INT64_TYPE:
      switch (target_type) {
        case INT8_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFF;
          return;
        case INT16_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFFFF;
          return;
        case INT32_TYPE:
          type = target_type;
          constant.i64 = constant.i64 & 0xFFFFFFFF;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    default:
      assert_unhandled_case(type);
      return;
  }
}

void Value::Convert(TypeName target_type, RoundMode round_mode) {
  switch (type) {
    case FLOAT32_TYPE:
      switch (target_type) {
        case FLOAT64_TYPE:
          type = target_type;
          constant.f64 = constant.f32;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case INT64_TYPE:
      switch (target_type) {
        case FLOAT64_TYPE:
          type = target_type;
          constant.f64 = (double)constant.i64;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    case FLOAT64_TYPE:
      switch (target_type) {
        case FLOAT32_TYPE:
          type = target_type;
          constant.f32 = (float)constant.f64;
          return;
        case INT32_TYPE:
          type = target_type;
          constant.i32 = (int32_t)constant.f64;
          return;
        case INT64_TYPE:
          type = target_type;
          constant.i64 = (int64_t)constant.f64;
          return;
        default:
          assert_unhandled_case(target_type);
          return;
      }
    default:
      assert_unhandled_case(type);
      return;
  }
}

template <typename T>
T __inline RoundValue(RoundMode round_mode, T value) {
  switch (round_mode) {
    case ROUND_TO_ZERO:
      return std::trunc(value);
    case ROUND_TO_NEAREST:
      return std::round(value);
    case ROUND_TO_MINUS_INFINITY:
      return std::floor(value);
    case ROUND_TO_POSITIVE_INFINITY:
      return std::ceil(value);
    default:
      assert_unhandled_case(round_mode);
      return value;
  }
}

void Value::Round(RoundMode round_mode) {
  switch (type) {
    case FLOAT32_TYPE:
      constant.f32 = RoundValue(round_mode, constant.f32);
      return;
    case FLOAT64_TYPE:
      constant.f64 = RoundValue(round_mode, constant.f64);
      return;
    case VEC128_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.f32[i] = RoundValue(round_mode, constant.v128.f32[i]);
      }
      return;
    default:
      assert_unhandled_case(type);
      return;
  }
}

bool Value::Add(Value* other) {
#define CHECK_DID_CARRY(v1, v2) (((uint64_t)v2) > ~((uint64_t)v1))
#define ADD_DID_CARRY(a, b) CHECK_DID_CARRY(a, b)
  assert_true(type == other->type);
  bool did_carry = false;
  switch (type) {
    case INT8_TYPE:
      did_carry = ADD_DID_CARRY(constant.i8, other->constant.i8);
      constant.i8 += other->constant.i8;
      break;
    case INT16_TYPE:
      did_carry = ADD_DID_CARRY(constant.i16, other->constant.i16);
      constant.i16 += other->constant.i16;
      break;
    case INT32_TYPE:
      did_carry = ADD_DID_CARRY(constant.i32, other->constant.i32);
      constant.i32 += other->constant.i32;
      break;
    case INT64_TYPE:
      did_carry = ADD_DID_CARRY(constant.i64, other->constant.i64);
      constant.i64 += other->constant.i64;
      break;
    case FLOAT32_TYPE:
      constant.f32 += other->constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 += other->constant.f64;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
  return did_carry;
}

bool Value::Sub(Value* other) {
#define SUB_DID_CARRY(a, b) (b == 0 || a > (~(0 - b)))
  assert_true(type == other->type);
  bool did_carry = false;
  switch (type) {
    case INT8_TYPE:
      did_carry =
          SUB_DID_CARRY(uint16_t(constant.i8), uint16_t(other->constant.i8));
      constant.i8 -= other->constant.i8;
      break;
    case INT16_TYPE:
      did_carry =
          SUB_DID_CARRY(uint16_t(constant.i16), uint16_t(other->constant.i16));
      constant.i16 -= other->constant.i16;
      break;
    case INT32_TYPE:
      did_carry =
          SUB_DID_CARRY(uint32_t(constant.i32), uint32_t(other->constant.i32));
      constant.i32 -= other->constant.i32;
      break;
    case INT64_TYPE:
      did_carry =
          SUB_DID_CARRY(uint64_t(constant.i64), uint64_t(other->constant.i64));
      constant.i64 -= other->constant.i64;
      break;
    case FLOAT32_TYPE:
      constant.f32 -= other->constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 -= other->constant.f64;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
  return did_carry;
}

void Value::Mul(Value* other) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      constant.i8 *= other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 *= other->constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 *= other->constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 *= other->constant.i64;
      break;
    case FLOAT32_TYPE:
      constant.f32 *= other->constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 *= other->constant.f64;
      break;
    case VEC128_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.f32[i] *= other->constant.v128.f32[i];
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::MulHi(Value* other, bool is_unsigned) {
  assert_true(type == other->type);
  switch (type) {
    case INT32_TYPE:
      if (is_unsigned) {
        constant.i32 = (int32_t)(((uint64_t)((uint32_t)constant.i32) *
                                  (uint32_t)other->constant.i32) >>
                                 32);
      } else {
        constant.i32 =
            (int32_t)(((int64_t)constant.i32 * (int64_t)other->constant.i32) >>
                      32);
      }
      break;
    case INT64_TYPE:
#if XE_COMPILER_MSVC
      if (is_unsigned) {
        constant.i64 = __umulh(constant.i64, other->constant.i64);
      } else {
        constant.i64 = __mulh(constant.i64, other->constant.i64);
      }
#else
      if (is_unsigned) {
        // Zero-extend (not sign-extend) the signed int64_t storage before
        // widening: casting constant.i64 straight to unsigned __int128
        // sign-extends a high-bit-set value, corrupting unsigned mulhdu/mulhwu
        // constant folding on the GCC/Clang (Android/ARM64) path. Mirrors the
        // already-correct INT32 case above. Ported from xenia-edge 643c13668.
        constant.i64 = static_cast<uint64_t>(
            (static_cast<unsigned __int128>(static_cast<uint64_t>(constant.i64)) *
             static_cast<unsigned __int128>(
                 static_cast<uint64_t>(other->constant.i64))) >>
            64);
      } else {
        constant.i64 = static_cast<uint64_t>(
            (static_cast<__int128>(constant.i64) *
             static_cast<__int128>(other->constant.i64)) >>
            64);
      }
#endif  // XE_COMPILER_MSVC
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

// PPC integer divides never trap: divide-by-zero yields 0, and signed
// overflow (INT_MIN / -1) also yields 0. The host C++ `/` is undefined
// behavior for both, which on x86 SIGFPEs and on ARM64 returns the wrong
// value (INT_MIN for the overflow case) and lets the optimizer make unsafe
// assumptions. Route constant folding through these so it matches the guest
// exactly and can never crash/UB. Ported from xenia-edge 324a8eb81.
template <typename T>
static T PPCUDiv(T numer, T denom) {
  if (!denom) {
    return 0;
  } else {
    return numer / denom;
  }
}
template <typename T>
static T PPCIDiv(T numer, T denom) {
  if (!denom) {
    return 0;
  } else if (numer == static_cast<T>(1LL << ((sizeof(T) * CHAR_BIT) - 1)) &&
             !~denom) {  // numer is signbit and denom is all ones -> signed
                         // overflow
    return 0;
  } else {
    return numer / denom;
  }
}

void Value::Div(Value* other, bool is_unsigned) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      if (is_unsigned) {
        constant.i8 = PPCUDiv<uint8_t>(constant.i8, other->constant.i8);
      } else {
        constant.i8 = PPCIDiv<int8_t>(constant.i8, other->constant.i8);
      }
      break;
    case INT16_TYPE:
      if (is_unsigned) {
        constant.i16 = PPCUDiv<uint16_t>(constant.i16, other->constant.i16);
      } else {
        constant.i16 = PPCIDiv<int16_t>(constant.i16, other->constant.i16);
      }
      break;
    case INT32_TYPE:
      if (is_unsigned) {
        constant.i32 = PPCUDiv<uint32_t>(constant.i32, other->constant.i32);
      } else {
        constant.i32 = PPCIDiv<int32_t>(constant.i32, other->constant.i32);
      }
      break;
    case INT64_TYPE:
      if (is_unsigned) {
        constant.i64 = PPCUDiv<uint64_t>(constant.i64, other->constant.i64);
      } else {
        constant.i64 = PPCIDiv<int64_t>(constant.i64, other->constant.i64);
      }
      break;
    case FLOAT32_TYPE:
      constant.f32 /= other->constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 /= other->constant.f64;
      break;
    case VEC128_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.f32[i] /= other->constant.v128.f32[i];
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Max(Value* other) {
  assert_true(type == other->type);
  switch (type) {
    case FLOAT32_TYPE:
      constant.f32 = std::max(constant.f32, other->constant.f32);
      break;
    case FLOAT64_TYPE:
      constant.f64 = std::max(constant.f64, other->constant.f64);
      break;
    case VEC128_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.f32[i] =
            std::max(constant.v128.f32[i], other->constant.v128.f32[i]);
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::MulAdd(Value* dest, Value* value1, Value* value2, Value* value3) {
  switch (dest->type) {
    case VEC128_TYPE:
      for (int i = 0; i < 4; i++) {
        dest->constant.v128.f32[i] =
            (value1->constant.v128.f32[i] * value2->constant.v128.f32[i]) +
            value3->constant.v128.f32[i];
      }
      break;
    case FLOAT32_TYPE:
      dest->constant.f32 =
          (value1->constant.f32 * value2->constant.f32) + value3->constant.f32;
      break;
    case FLOAT64_TYPE:
      dest->constant.f64 =
          (value1->constant.f64 * value2->constant.f64) + value3->constant.f64;
      break;
    default:
      assert_unhandled_case(dest->type);
      break;
  }
}

void Value::MulSub(Value* dest, Value* value1, Value* value2, Value* value3) {
  switch (dest->type) {
    case VEC128_TYPE:
      for (int i = 0; i < 4; i++) {
        dest->constant.v128.f32[i] =
            (value1->constant.v128.f32[i] * value2->constant.v128.f32[i]) -
            value3->constant.v128.f32[i];
      }
      break;
    case FLOAT32_TYPE:
      dest->constant.f32 =
          (value1->constant.f32 * value2->constant.f32) - value3->constant.f32;
      break;
    case FLOAT64_TYPE:
      dest->constant.f64 =
          (value1->constant.f64 * value2->constant.f64) - value3->constant.f64;
      break;
    default:
      assert_unhandled_case(dest->type);
      break;
  }
}

void Value::Neg() {
  switch (type) {
    case INT8_TYPE:
      constant.i8 = -constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 = -constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 = -constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 = -constant.i64;
      break;
    case FLOAT32_TYPE:
      constant.f32 = -constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 = -constant.f64;
      break;
    case VEC128_TYPE:
      for (int i = 0; i < 4; ++i) {
        constant.v128.f32[i] = -constant.v128.f32[i];
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Abs() {
  switch (type) {
    case INT8_TYPE:
      constant.i8 = int8_t(std::abs(constant.i8));
      break;
    case INT16_TYPE:
      constant.i16 = int16_t(std::abs(constant.i16));
      break;
    case INT32_TYPE:
      constant.i32 = std::abs(constant.i32);
      break;
    case INT64_TYPE:
      constant.i64 = std::abs(constant.i64);
      break;
    case FLOAT32_TYPE:
      constant.f32 = std::abs(constant.f32);
      break;
    case FLOAT64_TYPE:
      constant.f64 = std::abs(constant.f64);
      break;
    case VEC128_TYPE:
      for (int i = 0; i < 4; ++i) {
        constant.v128.f32[i] = std::abs(constant.v128.f32[i]);
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Sqrt() {
  switch (type) {
    case FLOAT32_TYPE:
      constant.f32 = std::sqrt(constant.f32);
      break;
    case FLOAT64_TYPE:
      constant.f64 = std::sqrt(constant.f64);
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::RSqrt() {
  switch (type) {
    case FLOAT32_TYPE:
      constant.f32 = 1.0f / std::sqrt(constant.f32);
      break;
    case FLOAT64_TYPE:
      constant.f64 = 1.0f / std::sqrt(constant.f64);
      break;
    case VEC128_TYPE:
      for (int i = 0; i < 4; ++i) {
        constant.v128.f32[i] = 1.0f / std::sqrt(constant.v128.f32[i]);
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Recip() {
  switch (type) {
    case FLOAT32_TYPE:
      constant.f32 = 1.0f / constant.f32;
      break;
    case FLOAT64_TYPE:
      constant.f64 = 1.0f / constant.f64;
      break;
    case VEC128_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.f32[i] = 1.0f / constant.v128.f32[i];
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::And(Value* other) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      constant.i8 &= other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 &= other->constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 &= other->constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 &= other->constant.i64;
      break;
    case VEC128_TYPE:
      constant.v128 &= other->constant.v128;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Or(Value* other) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      constant.i8 |= other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 |= other->constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 |= other->constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 |= other->constant.i64;
      break;
    case VEC128_TYPE:
      constant.v128 |= other->constant.v128;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Xor(Value* other) {
  assert_true(type == other->type);
  switch (type) {
    case INT8_TYPE:
      constant.i8 ^= other->constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 ^= other->constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 ^= other->constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 ^= other->constant.i64;
      break;
    case VEC128_TYPE:
      constant.v128 ^= other->constant.v128;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Not() {
  switch (type) {
    case INT8_TYPE:
      constant.i8 = ~constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 = ~constant.i16;
      break;
    case INT32_TYPE:
      constant.i32 = ~constant.i32;
      break;
    case INT64_TYPE:
      constant.i64 = ~constant.i64;
      break;
    case VEC128_TYPE:
      constant.v128.low = ~constant.v128.low;
      constant.v128.high = ~constant.v128.high;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Shl(Value* other) {
  assert_true(other->type == INT8_TYPE);
  // Mask the shift amount to the operand width. A count >= the bit width is
  // C++ undefined behavior, and the backends mask too (a64 SHL_I32 uses
  // & 0x1F, SHL_I64 & 0x3F; host x86/arm shl mask by width) - so fold this
  // identically to keep folded == runtime and avoid UB.
  switch (type) {
    case INT8_TYPE:
      constant.u8 <<= other->constant.u8 & 0x7;
      break;
    case INT16_TYPE:
      constant.u16 <<= other->constant.u8 & 0xF;
      break;
    case INT32_TYPE:
      constant.u32 <<= other->constant.u8 & 0x1F;
      break;
    case INT64_TYPE:
      constant.u64 <<= other->constant.u8 & 0x3F;
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Shr(Value* other) {
  assert_true(other->type == INT8_TYPE);
  // Mask the shift amount to the operand width (see Value::Shl): count >= width
  // is C++ UB and the backends mask, so keep the fold defined and == runtime.
  switch (type) {
    case INT8_TYPE:
      constant.u8 = constant.u8 >> (other->constant.u8 & 0x7);
      break;
    case INT16_TYPE:
      constant.u16 = constant.u16 >> (other->constant.u8 & 0xF);
      break;
    case INT32_TYPE:
      constant.u32 = constant.u32 >> (other->constant.u8 & 0x1F);
      break;
    case INT64_TYPE:
      constant.u64 = constant.u64 >> (other->constant.u8 & 0x3F);
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Sha(Value* other) {
  assert_true(other->type == INT8_TYPE);
  // Mask the shift amount to the operand width (see Value::Shl): count >= width
  // is C++ UB and the backends mask, so keep the fold defined and == runtime.
  switch (type) {
    case INT8_TYPE:
      constant.i8 = constant.i8 >> (other->constant.u8 & 0x7);
      break;
    case INT16_TYPE:
      constant.i16 = constant.i16 >> (other->constant.u8 & 0xF);
      break;
    case INT32_TYPE:
      constant.i32 = constant.i32 >> (other->constant.u8 & 0x1F);
      break;
    case INT64_TYPE:
      constant.i64 = constant.i64 >> (other->constant.u8 & 0x3F);
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::RotateLeft(Value* other) {
  assert_true(other->type == INT8_TYPE);
  uint8_t rotation = other->constant.u8;
  // Match the a64 ROTATE_LEFT_* sequences exactly: the amount is masked to the
  // type width and a rotation of 0 leaves the value unchanged. The 0 case must
  // be guarded because the generic base/math.h rotate_left() shifts by the full
  // width when sh==0, which is undefined behavior on the non-Win32 (Android/
  // ARM64) path that has no _rotl intrinsic specialization. PPC rlwinm with
  // SH==0 (clrlwi-style masking) makes the 0 case common.
  switch (type) {
    case INT8_TYPE: {
      uint8_t sh = rotation & 0x7;
      if (sh) constant.u8 = rotate_left<uint8_t>(constant.u8, sh);
      break;
    }
    case INT16_TYPE: {
      uint8_t sh = rotation & 0xF;
      if (sh) constant.u16 = rotate_left<uint16_t>(constant.u16, sh);
      break;
    }
    case INT32_TYPE: {
      uint8_t sh = rotation & 0x1F;
      if (sh) constant.u32 = rotate_left<uint32_t>(constant.u32, sh);
      break;
    }
    case INT64_TYPE: {
      uint8_t sh = rotation & 0x3F;
      if (sh) constant.u64 = rotate_left<uint64_t>(constant.u64, sh);
      break;
    }
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Extract(Value* vec, Value* index) {
  assert_true(vec->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE:
      constant.u8 = vec->constant.v128.u8[index->constant.u8 & 0x1F];
      break;
    case INT16_TYPE:
      constant.u16 = vec->constant.v128.u16[index->constant.u16 & 0x7];
      break;
    case INT32_TYPE:
      constant.u32 = vec->constant.v128.u32[index->constant.u32 & 0x3];
      break;
    case INT64_TYPE:
      constant.u64 = vec->constant.v128.u64[index->constant.u64 & 0x1];
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::Select(Value* other, Value* ctrl) {
  // TODO
  assert_always();
}

void Value::Splat(Value* other) {
  assert_true(type == VEC128_TYPE);
  switch (other->type) {
    case INT8_TYPE:
      for (int i = 0; i < 16; i++) {
        constant.v128.i8[i] = other->constant.i8;
      }
      break;
    case INT16_TYPE:
      for (int i = 0; i < 8; i++) {
        constant.v128.i16[i] = other->constant.i16;
      }
      break;
    case INT32_TYPE:
    case FLOAT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.i32[i] = other->constant.i32;
      }
      break;
    case INT64_TYPE:
    case FLOAT64_TYPE:
      for (int i = 0; i < 2; i++) {
        constant.v128.i64[i] = other->constant.i64;
      }
      break;
    default:
      assert_unhandled_case(other->type);
      break;
  }
}

void Value::VectorCompareEQ(Value* other, TypeName type) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE:
      for (int i = 0; i < 16; i++) {
        constant.v128.u8[i] =
            constant.v128.u8[i] == other->constant.v128.u8[i] ? -1 : 0;
      }
      break;
    case INT16_TYPE:
      for (int i = 0; i < 8; i++) {
        constant.v128.u16[i] =
            constant.v128.u16[i] == other->constant.v128.u16[i] ? -1 : 0;
      }
      break;
    case INT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] =
            constant.v128.u32[i] == other->constant.v128.u32[i] ? -1 : 0;
      }
      break;
    case INT64_TYPE:
      for (int i = 0; i < 2; i++) {
        constant.v128.u64[i] =
            constant.v128.u64[i] == other->constant.v128.u64[i] ? -1 : 0;
      }
      break;
    // Float compares are UNORDERED and treat the two zeroes as EQUAL. A bitwise
    // compare gets both wrong: -0.0 != +0.0 bitwise but compares equal, and a
    // NaN equals itself bitwise but must not compare equal.
    case FLOAT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] =
            constant.v128.f32[i] == other->constant.v128.f32[i] ? -1 : 0;
      }
      break;
    case FLOAT64_TYPE:
      for (int i = 0; i < 2; i++) {
        constant.v128.u64[i] =
            constant.v128.f64[i] == other->constant.v128.f64[i] ? -1 : 0;
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorCompareSGT(Value* other, TypeName type) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE:
      for (int i = 0; i < 16; i++) {
        constant.v128.u8[i] =
            constant.v128.i8[i] > other->constant.v128.i8[i] ? -1 : 0;
      }
      break;
    case INT16_TYPE:
      for (int i = 0; i < 8; i++) {
        constant.v128.u16[i] =
            constant.v128.i16[i] > other->constant.v128.i16[i] ? -1 : 0;
      }
      break;
    case INT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] =
            constant.v128.i32[i] > other->constant.v128.i32[i] ? -1 : 0;
      }
      break;
    case INT64_TYPE:
      for (int i = 0; i < 2; i++) {
        constant.v128.u64[i] =
            constant.v128.i64[i] > other->constant.v128.i64[i] ? -1 : 0;
      }
      break;
    case FLOAT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] =
            constant.v128.f32[i] > other->constant.v128.f32[i] ? -1 : 0;
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorCompareSGE(Value* other, TypeName type) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE:
      for (int i = 0; i < 16; i++) {
        constant.v128.u8[i] =
            constant.v128.i8[i] >= other->constant.v128.i8[i] ? -1 : 0;
      }
      break;
    case INT16_TYPE:
      for (int i = 0; i < 8; i++) {
        constant.v128.u16[i] =
            constant.v128.i16[i] >= other->constant.v128.i16[i] ? -1 : 0;
      }
      break;
    case INT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] =
            constant.v128.i32[i] >= other->constant.v128.i32[i] ? -1 : 0;
      }
      break;
    case INT64_TYPE:
      for (int i = 0; i < 2; i++) {
        constant.v128.u64[i] =
            constant.v128.i64[i] >= other->constant.v128.i64[i] ? -1 : 0;
      }
      break;
    case FLOAT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] =
            constant.v128.f32[i] >= other->constant.v128.f32[i] ? -1 : 0;
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorCompareUGT(Value* other, TypeName type) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE:
      for (int i = 0; i < 16; i++) {
        constant.v128.u8[i] =
            constant.v128.u8[i] > other->constant.v128.u8[i] ? -1 : 0;
      }
      break;
    case INT16_TYPE:
      for (int i = 0; i < 8; i++) {
        constant.v128.u16[i] =
            constant.v128.u16[i] > other->constant.v128.u16[i] ? -1 : 0;
      }
      break;
    case INT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] =
            constant.v128.u32[i] > other->constant.v128.u32[i] ? -1 : 0;
      }
      break;
    case INT64_TYPE:
      for (int i = 0; i < 2; i++) {
        constant.v128.u64[i] =
            constant.v128.u64[i] > other->constant.v128.u64[i] ? -1 : 0;
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorCompareUGE(Value* other, TypeName type) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE:
      for (int i = 0; i < 16; i++) {
        constant.v128.u8[i] =
            constant.v128.u8[i] >= other->constant.v128.u8[i] ? -1 : 0;
      }
      break;
    case INT16_TYPE:
      for (int i = 0; i < 8; i++) {
        constant.v128.u16[i] =
            constant.v128.u16[i] >= other->constant.v128.u16[i] ? -1 : 0;
      }
      break;
    case INT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] =
            constant.v128.u32[i] >= other->constant.v128.u32[i] ? -1 : 0;
      }
      break;
    case INT64_TYPE:
      for (int i = 0; i < 2; i++) {
        constant.v128.u64[i] =
            constant.v128.u64[i] >= other->constant.v128.u64[i] ? -1 : 0;
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorConvertI2F(Value* other, bool is_unsigned) {
  assert_true(type == VEC128_TYPE);
  for (int i = 0; i < 4; i++) {
    if (is_unsigned) {
      constant.v128.f32[i] = (float)other->constant.v128.u32[i];
    } else {
      constant.v128.f32[i] = (float)other->constant.v128.i32[i];
    }
  }
}

void Value::VectorConvertF2I(Value* other, bool is_unsigned) {
  assert_true(type == VEC128_TYPE);

  // Saturating, NaN-clean float->int per lane, matching the backends' fcvtzs/
  // fcvtzu (a64) and the x64 saturation sequence: NaN -> 0, out-of-range
  // saturates to the type min/max, truncation is toward zero. A plain C cast of
  // a NaN/inf/out-of-range float to int is C++ undefined behavior and diverges
  // from the saturating runtime (the guest converts are int_sat: vctsxs/vctuxs,
  // vcfpsxws128/vcfpuxws128).
  for (int i = 0; i < 4; i++) {
    float f = other->constant.v128.f32[i];
    if (is_unsigned) {
      if (std::isnan(f) || f <= 0.0f) {
        constant.v128.u32[i] = 0u;
      } else if (f >= 4294967296.0f) {  // >= 2^32
        constant.v128.u32[i] = 0xFFFFFFFFu;
      } else {
        constant.v128.u32[i] = (uint32_t)f;
      }
    } else {
      if (std::isnan(f)) {
        constant.v128.i32[i] = 0;
      } else if (f >= 2147483648.0f) {  // >= 2^31
        constant.v128.u32[i] = 0x7FFFFFFFu;
      } else if (f < -2147483648.0f) {  // < -2^31
        constant.v128.u32[i] = 0x80000000u;
      } else {
        constant.v128.i32[i] = (int32_t)f;
      }
    }
  }
}

void Value::VectorShl(Value* other, TypeName type) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE:
      for (int i = 0; i < 16; i++) {
        constant.v128.u8[i] <<= other->constant.v128.u8[i] & 0x7;
      }
      break;
    case INT16_TYPE:
      for (int i = 0; i < 8; i++) {
        constant.v128.u16[i] <<= other->constant.v128.u16[i] & 0xF;
      }
      break;
    case INT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] <<= other->constant.v128.u32[i] & 0x1F;
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorShr(Value* other, TypeName type) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE:
      for (int i = 0; i < 16; i++) {
        constant.v128.u8[i] >>= other->constant.v128.u8[i] & 0x7;
      }
      break;
    case INT16_TYPE:
      for (int i = 0; i < 8; i++) {
        constant.v128.u16[i] >>= other->constant.v128.u16[i] & 0xF;
      }
      break;
    case INT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] >>= other->constant.v128.u32[i] & 0x1F;
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorRol(Value* other, TypeName type) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE:
      for (int i = 0; i < 16; i++) {
        constant.v128.u8[i] = xe::rotate_left(constant.v128.u8[i],
                                              other->constant.v128.i8[i] & 0x7);
      }
      break;
    case INT16_TYPE:
      for (int i = 0; i < 8; i++) {
        constant.v128.u16[i] = xe::rotate_left(
            constant.v128.u16[i], other->constant.v128.u16[i] & 0xF);
      }
      break;
    case INT32_TYPE:
      for (int i = 0; i < 4; i++) {
        constant.v128.u32[i] = xe::rotate_left(
            constant.v128.u32[i], other->constant.v128.u32[i] & 0x1F);
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorAdd(Value* other, TypeName type, bool is_unsigned,
                      bool saturate) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case FLOAT32_TYPE:
      if (saturate) {
        assert_always();
      } else {
        constant.v128.x += other->constant.v128.x;
        constant.v128.y += other->constant.v128.y;
        constant.v128.z += other->constant.v128.z;
        constant.v128.w += other->constant.v128.w;
      }
      break;
    case FLOAT64_TYPE:
      if (saturate) {
        assert_always();
      } else {
        constant.v128.f64[0] += other->constant.v128.f64[0];
        constant.v128.f64[1] += other->constant.v128.f64[1];
      }
      break;
    case INT8_TYPE:
      if (saturate) {
        for (int i = 0; i < 16; i++) {
          if (is_unsigned) {
            constant.v128.u8[i] =
                xe::sat_add(constant.v128.u8[i], other->constant.v128.u8[i]);
          } else {
            constant.v128.i8[i] =
                xe::sat_add(constant.v128.i8[i], other->constant.v128.i8[i]);
          }
        }
        // TODO(Triang3l): Trace DID_SATURATE.
      } else {
        for (int i = 0; i < 16; i++) {
          if (is_unsigned) {
            constant.v128.u8[i] += other->constant.v128.u8[i];
          } else {
            constant.v128.i8[i] += other->constant.v128.i8[i];
          }
        }
      }
      break;
    case INT16_TYPE:
      if (saturate) {
        for (int i = 0; i < 8; i++) {
          if (is_unsigned) {
            constant.v128.u16[i] =
                xe::sat_add(constant.v128.u16[i], other->constant.v128.u16[i]);
          } else {
            constant.v128.i16[i] =
                xe::sat_add(constant.v128.i16[i], other->constant.v128.i16[i]);
          }
        }
        // TODO(Triang3l): Trace DID_SATURATE.
      } else {
        for (int i = 0; i < 8; i++) {
          if (is_unsigned) {
            constant.v128.u16[i] += other->constant.v128.u16[i];
          } else {
            constant.v128.i16[i] += other->constant.v128.i16[i];
          }
        }
      }
      break;
    case INT32_TYPE:
      if (saturate) {
        for (int i = 0; i < 4; i++) {
          if (is_unsigned) {
            constant.v128.u32[i] =
                xe::sat_add(constant.v128.u32[i], other->constant.v128.u32[i]);
          } else {
            constant.v128.i32[i] =
                xe::sat_add(constant.v128.i32[i], other->constant.v128.i32[i]);
          }
        }
        // TODO(Triang3l): Trace DID_SATURATE.
      } else {
        for (int i = 0; i < 4; i++) {
          if (is_unsigned) {
            constant.v128.u32[i] += other->constant.v128.u32[i];
          } else {
            constant.v128.i32[i] += other->constant.v128.i32[i];
          }
        }
      }
      break;
    case INT64_TYPE:
      if (saturate) {
        for (int i = 0; i < 2; i++) {
          if (is_unsigned) {
            constant.v128.u64[i] =
                xe::sat_add(constant.v128.u64[i], other->constant.v128.u64[i]);
          } else {
            constant.v128.i64[i] =
                xe::sat_add(constant.v128.i64[i], other->constant.v128.i64[i]);
          }
        }
        // TODO(Triang3l): Trace DID_SATURATE.
      } else {
        if (is_unsigned) {
          constant.v128.u64[0] += other->constant.v128.u64[0];
          constant.v128.u64[1] += other->constant.v128.u64[1];
        } else {
          constant.v128.i64[0] += other->constant.v128.i64[0];
          constant.v128.i64[1] += other->constant.v128.i64[1];
        }
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorSub(Value* other, TypeName type, bool is_unsigned,
                      bool saturate) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case FLOAT32_TYPE:
      if (saturate) {
        assert_always();
      } else {
        constant.v128.x -= other->constant.v128.x;
        constant.v128.y -= other->constant.v128.y;
        constant.v128.z -= other->constant.v128.z;
        constant.v128.w -= other->constant.v128.w;
      }
      break;
    case FLOAT64_TYPE:
      if (saturate) {
        assert_always();
      } else {
        constant.v128.f64[0] -= other->constant.v128.f64[0];
        constant.v128.f64[1] -= other->constant.v128.f64[1];
      }
      break;
    case INT8_TYPE:
      if (saturate) {
        for (int i = 0; i < 16; i++) {
          if (is_unsigned) {
            constant.v128.u8[i] =
                xe::sat_sub(constant.v128.u8[i], other->constant.v128.u8[i]);
          } else {
            constant.v128.i8[i] =
                xe::sat_sub(constant.v128.i8[i], other->constant.v128.i8[i]);
          }
        }
        // TODO(Triang3l): Trace DID_SATURATE.
      } else {
        for (int i = 0; i < 16; i++) {
          if (is_unsigned) {
            constant.v128.u8[i] -= other->constant.v128.u8[i];
          } else {
            constant.v128.i8[i] -= other->constant.v128.i8[i];
          }
        }
      }
      break;
    case INT16_TYPE:
      if (saturate) {
        for (int i = 0; i < 8; i++) {
          if (is_unsigned) {
            constant.v128.u16[i] =
                xe::sat_sub(constant.v128.u16[i], other->constant.v128.u16[i]);
          } else {
            constant.v128.i16[i] =
                xe::sat_sub(constant.v128.i16[i], other->constant.v128.i16[i]);
          }
        }
        // TODO(Triang3l): Trace DID_SATURATE.
      } else {
        for (int i = 0; i < 8; i++) {
          if (is_unsigned) {
            constant.v128.u16[i] -= other->constant.v128.u16[i];
          } else {
            constant.v128.i16[i] -= other->constant.v128.i16[i];
          }
        }
      }
      break;
    case INT32_TYPE:
      if (saturate) {
        for (int i = 0; i < 4; i++) {
          if (is_unsigned) {
            constant.v128.u32[i] =
                xe::sat_sub(constant.v128.u32[i], other->constant.v128.u32[i]);
          } else {
            constant.v128.i32[i] =
                xe::sat_sub(constant.v128.i32[i], other->constant.v128.i32[i]);
          }
        }
        // TODO(Triang3l): Trace DID_SATURATE.
      } else {
        for (int i = 0; i < 4; i++) {
          if (is_unsigned) {
            constant.v128.u32[i] -= other->constant.v128.u32[i];
          } else {
            constant.v128.i32[i] -= other->constant.v128.i32[i];
          }
        }
      }
      break;
    case INT64_TYPE:
      if (saturate) {
        for (int i = 0; i < 2; i++) {
          if (is_unsigned) {
            constant.v128.u64[i] =
                xe::sat_sub(constant.v128.u64[i], other->constant.v128.u64[i]);
          } else {
            constant.v128.i64[i] =
                xe::sat_sub(constant.v128.i64[i], other->constant.v128.i64[i]);
          }
        }
        // TODO(Triang3l): Trace DID_SATURATE.
      } else {
        if (is_unsigned) {
          constant.v128.u64[0] -= other->constant.v128.u64[0];
          constant.v128.u64[1] -= other->constant.v128.u64[1];
        } else {
          constant.v128.i64[0] -= other->constant.v128.i64[0];
          constant.v128.i64[1] -= other->constant.v128.i64[1];
        }
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::DotProduct3(Value* other) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case VEC128_TYPE: {
      type = FLOAT32_TYPE;
      // Match the backend EmulateDotProduct3 exactly: accumulate the lane
      // products in double precision, narrow once to float, and convert an
      // infinite result to QNaN (0x7FC00000). The previous FP32-only fold
      // without the inf->QNaN step diverged from the runtime on overflow (inf
      // vs QNaN) and by up to a ULP from the double-precision a64 path.
      double d0 = double(constant.v128.f32[0]) * double(other->constant.v128.f32[0]);
      double d1 = double(constant.v128.f32[1]) * double(other->constant.v128.f32[1]);
      double d2 = double(constant.v128.f32[2]) * double(other->constant.v128.f32[2]);
      float result = float(d0 + d1 + d2);
      if (std::isinf(result)) {
        constant.u32 = 0x7FC00000u;
      } else {
        constant.f32 = result;
      }
    } break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::DotProduct4(Value* other) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case VEC128_TYPE: {
      type = FLOAT32_TYPE;
      // Match the backend EmulateDotProduct4 exactly: accumulate the lane
      // products in double precision, narrow once to float, and convert an
      // infinite result to QNaN (0x7FC00000). See DotProduct3.
      double d0 = double(constant.v128.f32[0]) * double(other->constant.v128.f32[0]);
      double d1 = double(constant.v128.f32[1]) * double(other->constant.v128.f32[1]);
      double d2 = double(constant.v128.f32[2]) * double(other->constant.v128.f32[2]);
      double d3 = double(constant.v128.f32[3]) * double(other->constant.v128.f32[3]);
      float result = float(d0 + d1 + d2 + d3);
      if (std::isinf(result)) {
        constant.u32 = 0x7FC00000u;
      } else {
        constant.f32 = result;
      }
    } break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::VectorAverage(Value* other, TypeName type, bool is_unsigned,
                          bool saturate) {
  assert_true(this->type == VEC128_TYPE && other->type == VEC128_TYPE);
  switch (type) {
    case INT8_TYPE: {
      for (int i = 0; i < 16; i++) {
        if (is_unsigned) {
          constant.v128.u8[i] =
              uint8_t((uint16_t(constant.v128.u8[i]) +
                       uint16_t(other->constant.v128.u8[i]) + 1) >>
                      1);
        } else {
          constant.v128.i8[i] =
              int8_t((int16_t(constant.v128.i8[i]) +
                      int16_t(other->constant.v128.i8[i]) + 1) >>
                     1);
        }
      }
    } break;
    case INT16_TYPE: {
      for (int i = 0; i < 8; i++) {
        if (is_unsigned) {
          constant.v128.u16[i] =
              uint16_t((uint32_t(constant.v128.u16[i]) +
                        uint32_t(other->constant.v128.u16[i]) + 1) >>
                       1);
        } else {
          constant.v128.i16[i] =
              int16_t((int32_t(constant.v128.i16[i]) +
                       int32_t(other->constant.v128.i16[i]) + 1) >>
                      1);
        }
      }
    } break;
    case INT32_TYPE: {
      for (int i = 0; i < 4; i++) {
        if (is_unsigned) {
          constant.v128.u32[i] =
              uint32_t((uint64_t(constant.v128.u32[i]) +
                        uint64_t(other->constant.v128.u32[i]) + 1) >>
                       1);
        } else {
          constant.v128.i32[i] =
              int32_t((int64_t(constant.v128.i32[i]) +
                       int64_t(other->constant.v128.i32[i]) + 1) >>
                      1);
        }
      }
    } break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::ByteSwap() {
  switch (type) {
    case INT8_TYPE:
      constant.i8 = constant.i8;
      break;
    case INT16_TYPE:
      constant.i16 = xe::byte_swap(constant.i16);
      break;
    case INT32_TYPE:
      constant.i32 = xe::byte_swap(constant.i32);
      break;
    case INT64_TYPE:
      constant.i64 = xe::byte_swap(constant.i64);
      break;
    case VEC128_TYPE:
      for (int n = 0; n < 4; n++) {
        constant.v128.u32[n] = xe::byte_swap(constant.v128.u32[n]);
      }
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

void Value::CountLeadingZeros(const Value* other) {
  switch (other->type) {
    case INT8_TYPE:
      constant.i8 = xe::lzcnt(other->constant.i8);
      break;
    case INT16_TYPE:
      constant.i8 = xe::lzcnt(other->constant.i16);
      break;
    case INT32_TYPE:
      constant.i8 = xe::lzcnt(other->constant.i32);
      break;
    case INT64_TYPE:
      constant.i8 = xe::lzcnt(other->constant.i64);
      break;
    default:
      assert_unhandled_case(type);
      break;
  }
}

bool Value::Compare(Opcode opcode, Value* other) {
  assert_true(type == other->type);
  switch (other->type) {
    case INT8_TYPE:
      return CompareInt8(opcode, this, other);
    case INT16_TYPE:
      return CompareInt16(opcode, this, other);
    case INT32_TYPE:
      return CompareInt32(opcode, this, other);
    case INT64_TYPE:
      return CompareInt64(opcode, this, other);
    default:
      assert_unhandled_case(type);
      return false;
  }
}

bool Value::CompareInt8(Opcode opcode, Value* a, Value* b) {
  switch (opcode) {
    case OPCODE_COMPARE_EQ:
      return a->constant.i8 == b->constant.i8;
    case OPCODE_COMPARE_NE:
      return a->constant.i8 != b->constant.i8;
    case OPCODE_COMPARE_SLT:
      return a->constant.i8 < b->constant.i8;
    case OPCODE_COMPARE_SLE:
      return a->constant.i8 <= b->constant.i8;
    case OPCODE_COMPARE_SGT:
      return a->constant.i8 > b->constant.i8;
    case OPCODE_COMPARE_SGE:
      return a->constant.i8 >= b->constant.i8;
    case OPCODE_COMPARE_ULT:
      return uint8_t(a->constant.i8) < uint8_t(b->constant.i8);
    case OPCODE_COMPARE_ULE:
      return uint8_t(a->constant.i8) <= uint8_t(b->constant.i8);
    case OPCODE_COMPARE_UGT:
      return uint8_t(a->constant.i8) > uint8_t(b->constant.i8);
    case OPCODE_COMPARE_UGE:
      return uint8_t(a->constant.i8) >= uint8_t(b->constant.i8);
    default:
      assert_unhandled_case(opcode);
      return false;
  }
}

bool Value::CompareInt16(Opcode opcode, Value* a, Value* b) {
  switch (opcode) {
    case OPCODE_COMPARE_EQ:
      return a->constant.i16 == b->constant.i16;
    case OPCODE_COMPARE_NE:
      return a->constant.i16 != b->constant.i16;
    case OPCODE_COMPARE_SLT:
      return a->constant.i16 < b->constant.i16;
    case OPCODE_COMPARE_SLE:
      return a->constant.i16 <= b->constant.i16;
    case OPCODE_COMPARE_SGT:
      return a->constant.i16 > b->constant.i16;
    case OPCODE_COMPARE_SGE:
      return a->constant.i16 >= b->constant.i16;
    case OPCODE_COMPARE_ULT:
      return uint16_t(a->constant.i16) < uint16_t(b->constant.i16);
    case OPCODE_COMPARE_ULE:
      return uint16_t(a->constant.i16) <= uint16_t(b->constant.i16);
    case OPCODE_COMPARE_UGT:
      return uint16_t(a->constant.i16) > uint16_t(b->constant.i16);
    case OPCODE_COMPARE_UGE:
      return uint16_t(a->constant.i16) >= uint16_t(b->constant.i16);
    default:
      assert_unhandled_case(opcode);
      return false;
  }
}

bool Value::CompareInt32(Opcode opcode, Value* a, Value* b) {
  switch (opcode) {
    case OPCODE_COMPARE_EQ:
      return a->constant.i32 == b->constant.i32;
    case OPCODE_COMPARE_NE:
      return a->constant.i32 != b->constant.i32;
    case OPCODE_COMPARE_SLT:
      return a->constant.i32 < b->constant.i32;
    case OPCODE_COMPARE_SLE:
      return a->constant.i32 <= b->constant.i32;
    case OPCODE_COMPARE_SGT:
      return a->constant.i32 > b->constant.i32;
    case OPCODE_COMPARE_SGE:
      return a->constant.i32 >= b->constant.i32;
    case OPCODE_COMPARE_ULT:
      return uint32_t(a->constant.i32) < uint32_t(b->constant.i32);
    case OPCODE_COMPARE_ULE:
      return uint32_t(a->constant.i32) <= uint32_t(b->constant.i32);
    case OPCODE_COMPARE_UGT:
      return uint32_t(a->constant.i32) > uint32_t(b->constant.i32);
    case OPCODE_COMPARE_UGE:
      return uint32_t(a->constant.i32) >= uint32_t(b->constant.i32);
    default:
      assert_unhandled_case(opcode);
      return false;
  }
}

bool Value::CompareInt64(Opcode opcode, Value* a, Value* b) {
  switch (opcode) {
    case OPCODE_COMPARE_EQ:
      return a->constant.i64 == b->constant.i64;
    case OPCODE_COMPARE_NE:
      return a->constant.i64 != b->constant.i64;
    case OPCODE_COMPARE_SLT:
      return a->constant.i64 < b->constant.i64;
    case OPCODE_COMPARE_SLE:
      return a->constant.i64 <= b->constant.i64;
    case OPCODE_COMPARE_SGT:
      return a->constant.i64 > b->constant.i64;
    case OPCODE_COMPARE_SGE:
      return a->constant.i64 >= b->constant.i64;
    case OPCODE_COMPARE_ULT:
      return uint64_t(a->constant.i64) < uint64_t(b->constant.i64);
    case OPCODE_COMPARE_ULE:
      return uint64_t(a->constant.i64) <= uint64_t(b->constant.i64);
    case OPCODE_COMPARE_UGT:
      return uint64_t(a->constant.i64) > uint64_t(b->constant.i64);
    case OPCODE_COMPARE_UGE:
      return uint64_t(a->constant.i64) >= uint64_t(b->constant.i64);
    default:
      assert_unhandled_case(opcode);
      return false;
  }
}

}  // namespace hir
}  // namespace cpu
}  // namespace xe
