/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/arm64/arm64_function.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "xenia/base/byte_order.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/mmio_handler.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/symbol.h"
#include "xenia/memory.h"
#include "third_party/half/include/half.hpp"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {
namespace {

using hir::TypeName;

std::atomic<int> g_call_trace_budget{120};
std::atomic<int> g_entry_instruction_trace_budget{240};

struct RuntimeValue {
  TypeName type = hir::INT64_TYPE;
  hir::Value::ConstantValue constant = {};
};

size_t TypeSize(TypeName type) { return hir::GetTypeSize(type); }

uint64_t IntegerMask(TypeName type) {
  switch (type) {
    case hir::INT8_TYPE:
      return 0xFFu;
    case hir::INT16_TYPE:
      return 0xFFFFu;
    case hir::INT32_TYPE:
      return 0xFFFFFFFFu;
    case hir::INT64_TYPE:
      return std::numeric_limits<uint64_t>::max();
    default:
      return std::numeric_limits<uint64_t>::max();
  }
}

uint64_t MaskInteger(TypeName type, uint64_t value) {
  return value & IntegerMask(type);
}

int64_t SignExtendInteger(TypeName type, uint64_t value) {
  switch (type) {
    case hir::INT8_TYPE:
      return static_cast<int8_t>(value);
    case hir::INT16_TYPE:
      return static_cast<int16_t>(value);
    case hir::INT32_TYPE:
      return static_cast<int32_t>(value);
    case hir::INT64_TYPE:
      return static_cast<int64_t>(value);
    default:
      return static_cast<int64_t>(value);
  }
}

RuntimeValue MakeZero(TypeName type) {
  RuntimeValue value;
  value.type = type;
  value.constant.v128.low = 0;
  value.constant.v128.high = 0;
  return value;
}

RuntimeValue MakeInteger(TypeName type, uint64_t integer) {
  RuntimeValue value = MakeZero(type);
  value.constant.u64 = MaskInteger(type, integer);
  return value;
}

RuntimeValue MakeFloat32(float float_value) {
  RuntimeValue value = MakeZero(hir::FLOAT32_TYPE);
  value.constant.f32 = float_value;
  return value;
}

RuntimeValue MakeFloat64(double float_value) {
  RuntimeValue value = MakeZero(hir::FLOAT64_TYPE);
  value.constant.f64 = float_value;
  return value;
}

RuntimeValue MakeVec128(const vec128_t& vec_value) {
  RuntimeValue value = MakeZero(hir::VEC128_TYPE);
  value.constant.v128 = vec_value;
  return value;
}

uint64_t ReadInteger(const RuntimeValue& value) {
  return MaskInteger(value.type, value.constant.u64);
}

bool IsTrue(const RuntimeValue& value) {
  switch (value.type) {
    case hir::INT8_TYPE:
    case hir::INT16_TYPE:
    case hir::INT32_TYPE:
    case hir::INT64_TYPE:
      return ReadInteger(value) != 0;
    case hir::FLOAT32_TYPE:
      return value.constant.f32 != 0.0f;
    case hir::FLOAT64_TYPE:
      return value.constant.f64 != 0.0;
    case hir::VEC128_TYPE:
      return value.constant.v128.low || value.constant.v128.high;
    default:
      return false;
  }
}

uint32_t NormalizeGuestAddress(uint64_t address) {
  uint32_t guest_address = static_cast<uint32_t>(address);
  if (guest_address >= 0xE0000000 &&
      xe::memory::allocation_granularity() > 0x1000) {
    guest_address += 0x1000;
  }
  return guest_address;
}

RuntimeValue ReadRawValue(const void* address, TypeName type) {
  RuntimeValue value = MakeZero(type);
  switch (type) {
    case hir::INT8_TYPE:
      std::memcpy(&value.constant.u8, address, sizeof(uint8_t));
      break;
    case hir::INT16_TYPE:
      std::memcpy(&value.constant.u16, address, sizeof(uint16_t));
      break;
    case hir::INT32_TYPE:
      std::memcpy(&value.constant.u32, address, sizeof(uint32_t));
      break;
    case hir::INT64_TYPE:
      std::memcpy(&value.constant.u64, address, sizeof(uint64_t));
      break;
    case hir::FLOAT32_TYPE:
      std::memcpy(&value.constant.f32, address, sizeof(float));
      break;
    case hir::FLOAT64_TYPE:
      std::memcpy(&value.constant.f64, address, sizeof(double));
      break;
    case hir::VEC128_TYPE:
      std::memcpy(&value.constant.v128, address, sizeof(vec128_t));
      break;
    default:
      break;
  }
  return value;
}

void WriteRawValue(void* address, const RuntimeValue& value) {
  switch (value.type) {
    case hir::INT8_TYPE: {
      uint8_t data = static_cast<uint8_t>(ReadInteger(value));
      std::memcpy(address, &data, sizeof(data));
      break;
    }
    case hir::INT16_TYPE: {
      uint16_t data = static_cast<uint16_t>(ReadInteger(value));
      std::memcpy(address, &data, sizeof(data));
      break;
    }
    case hir::INT32_TYPE: {
      uint32_t data = static_cast<uint32_t>(ReadInteger(value));
      std::memcpy(address, &data, sizeof(data));
      break;
    }
    case hir::INT64_TYPE: {
      uint64_t data = ReadInteger(value);
      std::memcpy(address, &data, sizeof(data));
      break;
    }
    case hir::FLOAT32_TYPE:
      std::memcpy(address, &value.constant.f32, sizeof(float));
      break;
    case hir::FLOAT64_TYPE:
      std::memcpy(address, &value.constant.f64, sizeof(double));
      break;
    case hir::VEC128_TYPE:
      std::memcpy(address, &value.constant.v128, sizeof(vec128_t));
      break;
    default:
      break;
  }
}

RuntimeValue AtomicExchangeRawValue(void* address, const RuntimeValue& value) {
  switch (value.type) {
    case hir::INT8_TYPE: {
      auto new_value = static_cast<uint8_t>(ReadInteger(value));
      auto old_value = __atomic_exchange_n(reinterpret_cast<uint8_t*>(address),
                                           new_value, __ATOMIC_SEQ_CST);
      return MakeInteger(value.type, old_value);
    }
    case hir::INT16_TYPE: {
      auto new_value = static_cast<uint16_t>(ReadInteger(value));
      auto old_value = __atomic_exchange_n(reinterpret_cast<uint16_t*>(address),
                                           new_value, __ATOMIC_SEQ_CST);
      return MakeInteger(value.type, old_value);
    }
    case hir::INT32_TYPE: {
      auto new_value = static_cast<uint32_t>(ReadInteger(value));
      auto old_value = __atomic_exchange_n(reinterpret_cast<uint32_t*>(address),
                                           new_value, __ATOMIC_SEQ_CST);
      return MakeInteger(value.type, old_value);
    }
    case hir::INT64_TYPE: {
      auto old_value = __atomic_exchange_n(reinterpret_cast<uint64_t*>(address),
                                           ReadInteger(value),
                                           __ATOMIC_SEQ_CST);
      return MakeInteger(value.type, old_value);
    }
    default:
      return MakeZero(value.type);
  }
}

bool AtomicCompareExchangeRawValue(void* address,
                                   const RuntimeValue& expected_value,
                                   const RuntimeValue& new_value) {
  switch (expected_value.type) {
    case hir::INT32_TYPE: {
      auto expected = static_cast<uint32_t>(ReadInteger(expected_value));
      auto desired = static_cast<uint32_t>(ReadInteger(new_value));
      return __atomic_compare_exchange_n(
          reinterpret_cast<uint32_t*>(address), &expected, desired, false,
          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    case hir::INT64_TYPE: {
      auto expected = ReadInteger(expected_value);
      auto desired = ReadInteger(new_value);
      return __atomic_compare_exchange_n(
          reinterpret_cast<uint64_t*>(address), &expected, desired, false,
          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    default:
      XELOGE("ARM64 interpreter unsupported atomic compare-exchange type {}",
             static_cast<uint32_t>(expected_value.type));
      return false;
  }
}

RuntimeValue ReadOperand(const Arm64Function::Operand& operand,
                         const std::vector<RuntimeValue>& values) {
  switch (operand.kind) {
    case Arm64Function::Operand::Kind::kValue:
      if (operand.value_ordinal < values.size()) {
        return values[operand.value_ordinal];
      }
      return MakeZero(operand.type);
    case Arm64Function::Operand::Kind::kConstant: {
      RuntimeValue value = MakeZero(operand.type);
      value.constant = operand.constant;
      if (operand.type <= hir::INT64_TYPE) {
        value.constant.u64 = MaskInteger(operand.type, value.constant.u64);
      }
      return value;
    }
    case Arm64Function::Operand::Kind::kOffset:
      return MakeInteger(hir::INT64_TYPE, operand.offset);
    default:
      return MakeZero(operand.type);
  }
}

RuntimeValue CastValue(const RuntimeValue& source, TypeName target_type) {
  switch (target_type) {
    case hir::INT8_TYPE:
    case hir::INT16_TYPE:
    case hir::INT32_TYPE:
    case hir::INT64_TYPE:
      return MakeInteger(target_type, ReadInteger(source));
    case hir::FLOAT32_TYPE:
      if (source.type == hir::FLOAT64_TYPE) {
        return MakeFloat32(static_cast<float>(source.constant.f64));
      }
      if (source.type <= hir::INT64_TYPE) {
        return MakeFloat32(static_cast<float>(ReadInteger(source)));
      }
      return MakeFloat32(source.constant.f32);
    case hir::FLOAT64_TYPE:
      if (source.type == hir::FLOAT32_TYPE) {
        return MakeFloat64(static_cast<double>(source.constant.f32));
      }
      if (source.type <= hir::INT64_TYPE) {
        return MakeFloat64(static_cast<double>(ReadInteger(source)));
      }
      return MakeFloat64(source.constant.f64);
    case hir::VEC128_TYPE:
      return MakeVec128(source.constant.v128);
    default:
      return source;
  }
}

hir::RoundMode RoundModeFromPpcFpscr(uint32_t fpscr) {
  switch (fpscr & 0x3) {
    case 0:
      return hir::ROUND_TO_NEAREST;
    case 1:
      return hir::ROUND_TO_ZERO;
    case 2:
      return hir::ROUND_TO_POSITIVE_INFINITY;
    case 3:
      return hir::ROUND_TO_MINUS_INFINITY;
    default:
      return hir::ROUND_TO_NEAREST;
  }
}

long double ApplyRoundMode(hir::RoundMode round_mode, long double value) {
  switch (round_mode) {
    case hir::ROUND_TO_ZERO:
      return std::trunc(value);
    case hir::ROUND_TO_NEAREST:
      return std::round(value);
    case hir::ROUND_TO_MINUS_INFINITY:
      return std::floor(value);
    case hir::ROUND_TO_POSITIVE_INFINITY:
      return std::ceil(value);
    default:
      return std::round(value);
  }
}

std::pair<int64_t, int64_t> SignedIntegerRange(TypeName type) {
  switch (type) {
    case hir::INT8_TYPE:
      return {std::numeric_limits<int8_t>::min(),
              std::numeric_limits<int8_t>::max()};
    case hir::INT16_TYPE:
      return {std::numeric_limits<int16_t>::min(),
              std::numeric_limits<int16_t>::max()};
    case hir::INT32_TYPE:
      return {std::numeric_limits<int32_t>::min(),
              std::numeric_limits<int32_t>::max()};
    case hir::INT64_TYPE:
      return {std::numeric_limits<int64_t>::min(),
              std::numeric_limits<int64_t>::max()};
    default:
      return {0, 0};
  }
}

RuntimeValue ConvertValue(const RuntimeValue& source, TypeName target_type,
                          hir::RoundMode round_mode) {
  if (target_type <= hir::INT64_TYPE && source.type <= hir::INT64_TYPE) {
    return MakeInteger(target_type, ReadInteger(source));
  }

  if (target_type <= hir::INT64_TYPE &&
      (source.type == hir::FLOAT32_TYPE || source.type == hir::FLOAT64_TYPE)) {
    long double input = source.type == hir::FLOAT32_TYPE
                            ? static_cast<long double>(source.constant.f32)
                            : static_cast<long double>(source.constant.f64);
    if (std::isnan(input)) {
      return MakeInteger(target_type, 0);
    }
    long double rounded = ApplyRoundMode(round_mode, input);
    auto range = SignedIntegerRange(target_type);
    long double min_value = static_cast<long double>(range.first);
    long double max_value = static_cast<long double>(range.second);
    if (rounded <= min_value) {
      return MakeInteger(target_type, static_cast<uint64_t>(range.first));
    }
    if (rounded >= max_value) {
      return MakeInteger(target_type, static_cast<uint64_t>(range.second));
    }
    return MakeInteger(target_type,
                       static_cast<uint64_t>(static_cast<int64_t>(rounded)));
  }

  if (target_type == hir::FLOAT32_TYPE) {
    if (source.type == hir::FLOAT64_TYPE) {
      return MakeFloat32(static_cast<float>(source.constant.f64));
    }
    if (source.type <= hir::INT64_TYPE) {
      return MakeFloat32(static_cast<float>(
          SignExtendInteger(source.type, ReadInteger(source))));
    }
  }

  if (target_type == hir::FLOAT64_TYPE) {
    if (source.type == hir::FLOAT32_TYPE) {
      return MakeFloat64(static_cast<double>(source.constant.f32));
    }
    if (source.type <= hir::INT64_TYPE) {
      return MakeFloat64(static_cast<double>(
          SignExtendInteger(source.type, ReadInteger(source))));
    }
  }

  return CastValue(source, target_type);
}

RuntimeValue RoundRuntimeValue(const RuntimeValue& source,
                               hir::RoundMode round_mode) {
  switch (source.type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(static_cast<float>(ApplyRoundMode(
          round_mode, static_cast<long double>(source.constant.f32))));
    case hir::FLOAT64_TYPE:
      return MakeFloat64(static_cast<double>(ApplyRoundMode(
          round_mode, static_cast<long double>(source.constant.f64))));
    case hir::VEC128_TYPE: {
      vec128_t output = source.constant.v128;
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = static_cast<float>(ApplyRoundMode(
            round_mode, static_cast<long double>(source.constant.v128.f32[i])));
      }
      return MakeVec128(output);
    }
    default:
      return source;
  }
}

RuntimeValue SignExtendValue(const RuntimeValue& source, TypeName target_type) {
  return MakeInteger(target_type,
                     static_cast<uint64_t>(
                         SignExtendInteger(source.type, ReadInteger(source))));
}

RuntimeValue ByteSwapValue(const RuntimeValue& source) {
  switch (source.type) {
    case hir::INT8_TYPE:
      return source;
    case hir::INT16_TYPE:
      return MakeInteger(source.type,
                         xe::byte_swap(static_cast<uint16_t>(
                             ReadInteger(source))));
    case hir::INT32_TYPE:
      return MakeInteger(source.type,
                         xe::byte_swap(static_cast<uint32_t>(
                             ReadInteger(source))));
    case hir::INT64_TYPE:
      return MakeInteger(source.type, xe::byte_swap(ReadInteger(source)));
    default:
      return source;
  }
}

uint32_t FloatBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

int32_t SignExtendBits(uint32_t value, uint32_t bit_count) {
  uint32_t shift = 32 - bit_count;
  return static_cast<int32_t>(value << shift) >> shift;
}

uint8_t GetVecU8(const vec128_t& value, uint32_t index) {
  return value.u8[index ^ 0x3];
}

int8_t GetVecI8(const vec128_t& value, uint32_t index) {
  return static_cast<int8_t>(GetVecU8(value, index));
}

uint16_t GetVecU16(const vec128_t& value, uint32_t index) {
  return value.u16[index ^ 0x1];
}

int16_t GetVecI16(const vec128_t& value, uint32_t index) {
  return static_cast<int16_t>(GetVecU16(value, index));
}

void SetVecU16(vec128_t* value, uint32_t index, uint16_t part) {
  value->u16[index ^ 0x1] = part;
}

void SetVecU8(vec128_t* value, uint32_t index, uint8_t part) {
  value->u8[index ^ 0x3] = part;
}

RuntimeValue AddValue(TypeName type, const RuntimeValue& lhs,
                      const RuntimeValue& rhs) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(lhs.constant.f32 + rhs.constant.f32);
    case hir::FLOAT64_TYPE:
      return MakeFloat64(lhs.constant.f64 + rhs.constant.f64);
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = lhs.constant.v128.f32[i] + rhs.constant.v128.f32[i];
      }
      return MakeVec128(output);
    }
    default:
      return MakeInteger(type, ReadInteger(lhs) + ReadInteger(rhs));
  }
}

RuntimeValue SubValue(TypeName type, const RuntimeValue& lhs,
                      const RuntimeValue& rhs) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(lhs.constant.f32 - rhs.constant.f32);
    case hir::FLOAT64_TYPE:
      return MakeFloat64(lhs.constant.f64 - rhs.constant.f64);
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = lhs.constant.v128.f32[i] - rhs.constant.v128.f32[i];
      }
      return MakeVec128(output);
    }
    default:
      return MakeInteger(type, ReadInteger(lhs) - ReadInteger(rhs));
  }
}

RuntimeValue MulValue(TypeName type, const RuntimeValue& lhs,
                      const RuntimeValue& rhs) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(lhs.constant.f32 * rhs.constant.f32);
    case hir::FLOAT64_TYPE:
      return MakeFloat64(lhs.constant.f64 * rhs.constant.f64);
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = lhs.constant.v128.f32[i] * rhs.constant.v128.f32[i];
      }
      return MakeVec128(output);
    }
    default:
      return MakeInteger(type, ReadInteger(lhs) * ReadInteger(rhs));
  }
}

RuntimeValue MulHighValue(TypeName type, const RuntimeValue& lhs,
                          const RuntimeValue& rhs, uint32_t flags) {
  uint32_t bits = static_cast<uint32_t>(TypeSize(type) * 8);
  if (flags & hir::ARITHMETIC_UNSIGNED) {
    auto product =
        static_cast<unsigned __int128>(ReadInteger(lhs)) * ReadInteger(rhs);
    return MakeInteger(type, static_cast<uint64_t>(product >> bits));
  }

  auto product =
      static_cast<__int128>(SignExtendInteger(lhs.type, ReadInteger(lhs))) *
      SignExtendInteger(rhs.type, ReadInteger(rhs));
  return MakeInteger(
      type, static_cast<uint64_t>(static_cast<unsigned __int128>(product) >>
                                  bits));
}

RuntimeValue DivValue(TypeName type, const RuntimeValue& lhs,
                      const RuntimeValue& rhs, uint32_t flags) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(lhs.constant.f32 / rhs.constant.f32);
    case hir::FLOAT64_TYPE:
      return MakeFloat64(lhs.constant.f64 / rhs.constant.f64);
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = lhs.constant.v128.f32[i] / rhs.constant.v128.f32[i];
      }
      return MakeVec128(output);
    }
    default: {
      uint64_t divisor = ReadInteger(rhs);
      if (!divisor) {
        return MakeInteger(type, 0);
      }
      if (flags & hir::ARITHMETIC_UNSIGNED) {
        return MakeInteger(type, ReadInteger(lhs) / divisor);
      }
      return MakeInteger(type,
                         static_cast<uint64_t>(
                             SignExtendInteger(lhs.type, ReadInteger(lhs)) /
                             SignExtendInteger(rhs.type, divisor)));
    }
  }
}

RuntimeValue MinMaxValue(TypeName type, const RuntimeValue& lhs,
                         const RuntimeValue& rhs, bool maximum) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(maximum ? std::fmax(lhs.constant.f32, rhs.constant.f32)
                                 : std::fmin(lhs.constant.f32, rhs.constant.f32));
    case hir::FLOAT64_TYPE:
      return MakeFloat64(maximum ? std::fmax(lhs.constant.f64, rhs.constant.f64)
                                 : std::fmin(lhs.constant.f64, rhs.constant.f64));
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] =
            maximum ? std::fmax(lhs.constant.v128.f32[i],
                                rhs.constant.v128.f32[i])
                    : std::fmin(lhs.constant.v128.f32[i],
                                rhs.constant.v128.f32[i]);
      }
      return MakeVec128(output);
    }
    default: {
      int64_t a = SignExtendInteger(lhs.type, ReadInteger(lhs));
      int64_t b = SignExtendInteger(rhs.type, ReadInteger(rhs));
      return MakeInteger(type, static_cast<uint64_t>(maximum
                                                         ? std::max(a, b)
                                                         : std::min(a, b)));
    }
  }
}

RuntimeValue NegValue(TypeName type, const RuntimeValue& source) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(-source.constant.f32);
    case hir::FLOAT64_TYPE:
      return MakeFloat64(-source.constant.f64);
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = -source.constant.v128.f32[i];
      }
      return MakeVec128(output);
    }
    default:
      return MakeInteger(type,
                         static_cast<uint64_t>(
                             -SignExtendInteger(source.type,
                                                ReadInteger(source))));
  }
}

RuntimeValue AbsValue(TypeName type, const RuntimeValue& source) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(std::fabs(source.constant.f32));
    case hir::FLOAT64_TYPE:
      return MakeFloat64(std::fabs(source.constant.f64));
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = std::fabs(source.constant.v128.f32[i]);
      }
      return MakeVec128(output);
    }
    default:
      int64_t signed_value =
          SignExtendInteger(source.type, ReadInteger(source));
      return MakeInteger(type,
                         signed_value < 0
                             ? (~static_cast<uint64_t>(signed_value) + 1)
                             : static_cast<uint64_t>(signed_value));
  }
}

RuntimeValue SqrtValue(TypeName type, const RuntimeValue& source) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(std::sqrt(source.constant.f32));
    case hir::FLOAT64_TYPE:
      return MakeFloat64(std::sqrt(source.constant.f64));
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = std::sqrt(source.constant.v128.f32[i]);
      }
      return MakeVec128(output);
    }
    default:
      return source;
  }
}

RuntimeValue ReciprocalSqrtValue(TypeName type, const RuntimeValue& source) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(1.0f / std::sqrt(source.constant.f32));
    case hir::FLOAT64_TYPE:
      return MakeFloat64(1.0 / std::sqrt(source.constant.f64));
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = 1.0f / std::sqrt(source.constant.v128.f32[i]);
      }
      return MakeVec128(output);
    }
    default:
      return source;
  }
}

RuntimeValue ReciprocalValue(TypeName type, const RuntimeValue& source) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(1.0f / source.constant.f32);
    case hir::FLOAT64_TYPE:
      return MakeFloat64(1.0 / source.constant.f64);
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = 1.0f / source.constant.v128.f32[i];
      }
      return MakeVec128(output);
    }
    default:
      return source;
  }
}

RuntimeValue Pow2Value(TypeName type, const RuntimeValue& source) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(std::exp2(source.constant.f32));
    case hir::FLOAT64_TYPE:
      return MakeFloat64(std::exp2(source.constant.f64));
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = std::exp2(source.constant.v128.f32[i]);
      }
      return MakeVec128(output);
    }
    default:
      return source;
  }
}

RuntimeValue Log2Value(TypeName type, const RuntimeValue& source) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeFloat32(std::log2(source.constant.f32));
    case hir::FLOAT64_TYPE:
      return MakeFloat64(std::log2(source.constant.f64));
    case hir::VEC128_TYPE: {
      vec128_t output = {};
      for (uint32_t i = 0; i < 4; ++i) {
        output.f32[i] = std::log2(source.constant.v128.f32[i]);
      }
      return MakeVec128(output);
    }
    default:
      return source;
  }
}

RuntimeValue MulAddValue(TypeName type, const RuntimeValue& lhs,
                         const RuntimeValue& rhs,
                         const RuntimeValue& addend) {
  return AddValue(type, MulValue(type, lhs, rhs), addend);
}

RuntimeValue MulSubValue(TypeName type, const RuntimeValue& lhs,
                         const RuntimeValue& rhs,
                         const RuntimeValue& subtrahend) {
  return SubValue(type, MulValue(type, lhs, rhs), subtrahend);
}

RuntimeValue DotProduct4Value(const RuntimeValue& lhs,
                              const RuntimeValue& rhs) {
  float result = 0.0f;
  for (uint32_t i = 0; i < 4; ++i) {
    result += lhs.constant.v128.f32[i] * rhs.constant.v128.f32[i];
  }
  return MakeFloat32(result);
}

bool CompareSigned(int64_t lhs, int64_t rhs, hir::Opcode opcode) {
  switch (opcode) {
    case hir::OPCODE_VECTOR_COMPARE_EQ:
      return lhs == rhs;
    case hir::OPCODE_VECTOR_COMPARE_SGT:
      return lhs > rhs;
    case hir::OPCODE_VECTOR_COMPARE_SGE:
      return lhs >= rhs;
    default:
      return false;
  }
}

bool CompareUnsigned(uint64_t lhs, uint64_t rhs, hir::Opcode opcode) {
  switch (opcode) {
    case hir::OPCODE_VECTOR_COMPARE_EQ:
      return lhs == rhs;
    case hir::OPCODE_VECTOR_COMPARE_UGT:
      return lhs > rhs;
    case hir::OPCODE_VECTOR_COMPARE_UGE:
      return lhs >= rhs;
    default:
      return false;
  }
}

bool CompareFloat(float lhs, float rhs, hir::Opcode opcode) {
  switch (opcode) {
    case hir::OPCODE_VECTOR_COMPARE_EQ:
      return lhs == rhs;
    case hir::OPCODE_VECTOR_COMPARE_SGT:
    case hir::OPCODE_VECTOR_COMPARE_UGT:
      return lhs > rhs;
    case hir::OPCODE_VECTOR_COMPARE_SGE:
    case hir::OPCODE_VECTOR_COMPARE_UGE:
      return lhs >= rhs;
    default:
      return false;
  }
}

RuntimeValue VectorCompareValue(hir::Opcode opcode, TypeName part_type,
                                const RuntimeValue& lhs,
                                const RuntimeValue& rhs) {
  vec128_t output = {};
  const auto& a = lhs.constant.v128;
  const auto& b = rhs.constant.v128;

  switch (part_type) {
    case hir::INT8_TYPE:
      for (uint32_t i = 0; i < 16; ++i) {
        bool result =
            opcode == hir::OPCODE_VECTOR_COMPARE_EQ
                ? GetVecU8(a, i) == GetVecU8(b, i)
                : opcode == hir::OPCODE_VECTOR_COMPARE_UGT ||
                          opcode == hir::OPCODE_VECTOR_COMPARE_UGE
                      ? CompareUnsigned(GetVecU8(a, i), GetVecU8(b, i),
                                        opcode)
                      : CompareSigned(GetVecI8(a, i), GetVecI8(b, i),
                                      opcode);
        SetVecU8(&output, i, result ? 0xFF : 0x00);
      }
      break;
    case hir::INT16_TYPE:
      for (uint32_t i = 0; i < 8; ++i) {
        bool result =
            opcode == hir::OPCODE_VECTOR_COMPARE_EQ
                ? GetVecU16(a, i) == GetVecU16(b, i)
                : opcode == hir::OPCODE_VECTOR_COMPARE_UGT ||
                          opcode == hir::OPCODE_VECTOR_COMPARE_UGE
                      ? CompareUnsigned(GetVecU16(a, i), GetVecU16(b, i),
                                        opcode)
                      : CompareSigned(GetVecI16(a, i), GetVecI16(b, i),
                                      opcode);
        SetVecU16(&output, i, result ? 0xFFFF : 0x0000);
      }
      break;
    case hir::INT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        bool result =
            opcode == hir::OPCODE_VECTOR_COMPARE_EQ
                ? a.u32[i] == b.u32[i]
                : opcode == hir::OPCODE_VECTOR_COMPARE_UGT ||
                          opcode == hir::OPCODE_VECTOR_COMPARE_UGE
                      ? CompareUnsigned(a.u32[i], b.u32[i], opcode)
                      : CompareSigned(static_cast<int32_t>(a.u32[i]),
                                      static_cast<int32_t>(b.u32[i]), opcode);
        output.u32[i] = result ? 0xFFFFFFFFu : 0;
      }
      break;
    case hir::FLOAT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        output.u32[i] =
            CompareFloat(a.f32[i], b.f32[i], opcode) ? 0xFFFFFFFFu : 0;
      }
      break;
    default:
      break;
  }

  return MakeVec128(output);
}

RuntimeValue VectorMinMaxValue(TypeName part_type, uint32_t arithmetic_flags,
                               const RuntimeValue& lhs,
                               const RuntimeValue& rhs, bool maximum) {
  vec128_t output = {};
  const auto& a = lhs.constant.v128;
  const auto& b = rhs.constant.v128;
  bool is_unsigned = (arithmetic_flags & hir::ARITHMETIC_UNSIGNED) != 0;

  switch (part_type) {
    case hir::INT8_TYPE:
      for (uint32_t i = 0; i < 16; ++i) {
        if (is_unsigned) {
          uint8_t av = GetVecU8(a, i);
          uint8_t bv = GetVecU8(b, i);
          SetVecU8(&output, i, maximum ? std::max(av, bv) : std::min(av, bv));
        } else {
          int8_t av = GetVecI8(a, i);
          int8_t bv = GetVecI8(b, i);
          SetVecU8(&output, i,
                   static_cast<uint8_t>(maximum ? std::max(av, bv)
                                                : std::min(av, bv)));
        }
      }
      break;
    case hir::INT16_TYPE:
      for (uint32_t i = 0; i < 8; ++i) {
        if (is_unsigned) {
          uint16_t av = GetVecU16(a, i);
          uint16_t bv = GetVecU16(b, i);
          SetVecU16(&output, i,
                    maximum ? std::max(av, bv) : std::min(av, bv));
        } else {
          int16_t av = GetVecI16(a, i);
          int16_t bv = GetVecI16(b, i);
          SetVecU16(&output, i,
                    static_cast<uint16_t>(maximum ? std::max(av, bv)
                                                  : std::min(av, bv)));
        }
      }
      break;
    case hir::INT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        if (is_unsigned) {
          output.u32[i] =
              maximum ? std::max(a.u32[i], b.u32[i])
                      : std::min(a.u32[i], b.u32[i]);
        } else {
          int32_t av = static_cast<int32_t>(a.u32[i]);
          int32_t bv = static_cast<int32_t>(b.u32[i]);
          output.u32[i] = static_cast<uint32_t>(maximum ? std::max(av, bv)
                                                        : std::min(av, bv));
        }
      }
      break;
    default:
      break;
  }

  return MakeVec128(output);
}

RuntimeValue IsNanValue(TypeName type, const RuntimeValue& source) {
  switch (type) {
    case hir::FLOAT32_TYPE:
      return MakeInteger(hir::INT8_TYPE, std::isnan(source.constant.f32));
    case hir::FLOAT64_TYPE:
      return MakeInteger(hir::INT8_TYPE, std::isnan(source.constant.f64));
    default:
      return MakeInteger(hir::INT8_TYPE, 0);
  }
}

RuntimeValue LoadVectorShiftValue(uint32_t shift, bool left) {
  vec128_t output = {};
  shift &= 0xF;
  for (uint32_t i = 0; i < 16; ++i) {
    SetVecU8(&output, i,
             static_cast<uint8_t>(left ? (i + shift) : (i + 16 - shift)));
  }
  return MakeVec128(output);
}

RuntimeValue ExtractValue(TypeName type, const RuntimeValue& source,
                          const RuntimeValue& index_value) {
  uint32_t index = static_cast<uint32_t>(ReadInteger(index_value));
  const auto& input = source.constant.v128;
  switch (type) {
    case hir::INT8_TYPE:
      return MakeInteger(type, GetVecU8(input, index & 0xF));
    case hir::INT16_TYPE:
      return MakeInteger(type, GetVecU16(input, index & 0x7));
    case hir::INT32_TYPE:
      return MakeInteger(type, input.u32[index & 0x3]);
    case hir::FLOAT32_TYPE:
      return MakeFloat32(input.f32[index & 0x3]);
    default:
      return MakeZero(type);
  }
}

RuntimeValue SplatValue(const RuntimeValue& source) {
  vec128_t output = {};
  switch (source.type) {
    case hir::INT8_TYPE:
      for (uint32_t i = 0; i < 16; ++i) {
        SetVecU8(&output, i, static_cast<uint8_t>(ReadInteger(source)));
      }
      break;
    case hir::INT16_TYPE:
      for (uint32_t i = 0; i < 8; ++i) {
        SetVecU16(&output, i, static_cast<uint16_t>(ReadInteger(source)));
      }
      break;
    case hir::INT32_TYPE:
    case hir::FLOAT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        output.u32[i] = source.constant.u32;
      }
      break;
    case hir::INT64_TYPE:
    case hir::FLOAT64_TYPE:
      output.u64[0] = source.constant.u64;
      output.u64[1] = source.constant.u64;
      break;
    default:
      break;
  }
  return MakeVec128(output);
}

RuntimeValue PermuteValue(TypeName part_type, const RuntimeValue& control,
                          const RuntimeValue& src1,
                          const RuntimeValue& src2) {
  vec128_t output = {};
  const auto& a = src1.constant.v128;
  const auto& b = src2.constant.v128;

  if (control.type != hir::VEC128_TYPE) {
    uint32_t mask = static_cast<uint32_t>(ReadInteger(control));
    for (uint32_t i = 0; i < 4; ++i) {
      uint32_t index = (mask >> (i * 8)) & 0x3;
      bool select_b = ((mask >> (i * 8 + 2)) & 0x1) != 0;
      output.u32[i] = (select_b ? b : a).u32[index];
    }
    return MakeVec128(output);
  }

  const auto& c = control.constant.v128;
  switch (part_type) {
    case hir::INT8_TYPE:
      for (uint32_t i = 0; i < 16; ++i) {
        uint8_t index = GetVecU8(c, i) & 0x1F;
        SetVecU8(&output, i,
                 GetVecU8(index > 15 ? b : a, index & 0xF));
      }
      break;
    case hir::INT16_TYPE:
      for (uint32_t i = 0; i < 8; ++i) {
        uint16_t index = GetVecU16(c, i) & 0xF;
        SetVecU16(&output, i,
                  GetVecU16(index > 7 ? b : a, index & 0x7));
      }
      break;
    case hir::INT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        uint32_t index = c.u32[i] & 0x7;
        output.u32[i] = (index > 3 ? b : a).u32[index & 0x3];
      }
      break;
    default:
      XELOGE("ARM64 interpreter unsupported permute part type {}",
             static_cast<uint32_t>(part_type));
      break;
  }
  return MakeVec128(output);
}

RuntimeValue SwizzleValue(const RuntimeValue& source, uint32_t swizzle_mask) {
  vec128_t output = {};
  const auto& input = source.constant.v128;
  for (uint32_t i = 0; i < 4; ++i) {
    output.u32[i] = input.u32[(swizzle_mask >> (i * 2)) & 0x3];
  }
  return MakeVec128(output);
}

RuntimeValue UnpackValue(const RuntimeValue& source, uint32_t flags) {
  const auto& input = source.constant.v128;
  vec128_t output = {};

  switch (flags & hir::PACK_TYPE_MODE) {
    case hir::PACK_TYPE_D3DCOLOR: {
      uint32_t packed = input.u32[3];
      output.u32[0] = 0x3F800000u | ((packed >> 16) & 0xFFu);
      output.u32[1] = 0x3F800000u | ((packed >> 8) & 0xFFu);
      output.u32[2] = 0x3F800000u | (packed & 0xFFu);
      output.u32[3] = 0x3F800000u | ((packed >> 24) & 0xFFu);
      return MakeVec128(output);
    }
    case hir::PACK_TYPE_FLOAT16_2:
      output.u32[0] = FloatBits(
          half_float::detail::half2float(static_cast<uint16_t>(input.u32[3] >> 16)));
      output.u32[1] = FloatBits(
          half_float::detail::half2float(static_cast<uint16_t>(input.u32[3])));
      output.u32[2] = 0;
      output.u32[3] = 0x3F800000u;
      return MakeVec128(output);
    case hir::PACK_TYPE_FLOAT16_4:
      for (uint32_t i = 0; i < 4; ++i) {
        output.u32[i] = FloatBits(
            half_float::detail::half2float(GetVecU16(input, 4 + i)));
      }
      return MakeVec128(output);
    case hir::PACK_TYPE_SHORT_2: {
      uint32_t packed = input.u32[3];
      output.u32[0] =
          0x40400000u + static_cast<int32_t>(static_cast<int16_t>(packed >> 16));
      output.u32[1] =
          0x40400000u + static_cast<int32_t>(static_cast<int16_t>(packed));
      output.u32[2] = 0;
      output.u32[3] = 0x3F800000u;
      return MakeVec128(output);
    }
    case hir::PACK_TYPE_SHORT_4:
      for (uint32_t i = 0; i < 4; ++i) {
        output.u32[i] =
            0x40400000u + static_cast<int32_t>(GetVecI16(input, 4 + i));
      }
      return MakeVec128(output);
    case hir::PACK_TYPE_UINT_2101010: {
      uint32_t packed = input.u32[3];
      output.u32[0] = 0x40400000u + SignExtendBits((packed >> 0) & 0x3FFu, 10);
      output.u32[1] =
          0x40400000u + SignExtendBits((packed >> 10) & 0x3FFu, 10);
      output.u32[2] =
          0x40400000u + SignExtendBits((packed >> 20) & 0x3FFu, 10);
      output.u32[3] = 0x3F800000u + ((packed >> 30) & 0x3u);
      return MakeVec128(output);
    }
    case hir::PACK_TYPE_8_IN_16: {
      uint32_t base = hir::IsPackToHi(flags) ? 0 : 8;
      for (uint32_t i = 0; i < 8; ++i) {
        SetVecU16(&output, i,
                  static_cast<uint16_t>(
                      static_cast<int16_t>(GetVecI8(input, base + i))));
      }
      return MakeVec128(output);
    }
    case hir::PACK_TYPE_16_IN_32: {
      uint32_t base = hir::IsPackToHi(flags) ? 0 : 4;
      for (uint32_t i = 0; i < 4; ++i) {
        output.i32[i] = static_cast<int32_t>(GetVecI16(input, base + i));
      }
      return MakeVec128(output);
    }
    default:
      XELOGE("ARM64 interpreter unsupported unpack flags {:04X}", flags);
      return MakeZero(hir::VEC128_TYPE);
  }
}

uint32_t CountLeadingZeros(const RuntimeValue& source) {
  uint64_t value = ReadInteger(source);
  switch (source.type) {
    case hir::INT8_TYPE:
      return value ? static_cast<uint32_t>(__builtin_clz(
                         static_cast<uint32_t>(value) << 24))
                   : 8;
    case hir::INT16_TYPE:
      return value ? static_cast<uint32_t>(__builtin_clz(
                         static_cast<uint32_t>(value) << 16))
                   : 16;
    case hir::INT32_TYPE:
      return value ? static_cast<uint32_t>(
                         __builtin_clz(static_cast<uint32_t>(value)))
                   : 32;
    case hir::INT64_TYPE:
      return value ? static_cast<uint32_t>(__builtin_clzll(value)) : 64;
    default:
      return 0;
  }
}

bool InvokeHostFunction(Function* function, ThreadState* thread_state) {
  if (!function) {
    return false;
  }

  if (function->behavior() == Function::Behavior::kBuiltin) {
    return function->Call(thread_state, 0);
  }

  if (function->behavior() == Function::Behavior::kExtern) {
    auto guest_function = static_cast<GuestFunction*>(function);
    if (guest_function->extern_handler()) {
      guest_function->extern_handler()(thread_state->context(),
                                       thread_state->context()->kernel_state);
      return true;
    }
    XELOGE("ARM64 interpreter missing extern handler for {:08X} {}",
           function->address(), function->name());
    return false;
  }

  return false;
}

bool InvokeGuestFunction(Function* function, ThreadState* thread_state,
                         uint32_t return_address) {
  if (!function) {
    return false;
  }
  int trace_budget = g_call_trace_budget.load();
  if (trace_budget > 0 && g_call_trace_budget.compare_exchange_strong(
                              trace_budget, trace_budget - 1)) {
    XELOGI("ARM64 interpreter direct call {:08X} {} return {:08X}",
           function->address(), function->name(), return_address);
  }
  if (function->behavior() == Function::Behavior::kBuiltin ||
      function->behavior() == Function::Behavior::kExtern) {
    bool result = InvokeHostFunction(function, thread_state);
    int return_trace_budget = g_call_trace_budget.load();
    if (return_trace_budget > 0 &&
        g_call_trace_budget.compare_exchange_strong(return_trace_budget,
                                                    return_trace_budget - 1)) {
      XELOGI("ARM64 interpreter direct return {:08X} result {}",
             function->address(), result ? 1 : 0);
    }
    return result;
  }
  auto resolved = thread_state->processor()->ResolveFunction(function->address());
  if (!resolved) {
    XELOGE("ARM64 interpreter failed to resolve direct call target {:08X}",
           function->address());
    return false;
  }
  bool result = resolved->Call(thread_state, return_address);
  int return_trace_budget = g_call_trace_budget.load();
  if (return_trace_budget > 0 &&
      g_call_trace_budget.compare_exchange_strong(return_trace_budget,
                                                  return_trace_budget - 1)) {
    XELOGI("ARM64 interpreter direct return {:08X} result {}",
           function->address(), result ? 1 : 0);
  }
  return result;
}

bool InvokeResolvedGuestAddress(ThreadState* thread_state, uint32_t address,
                                uint32_t return_address) {
  auto target = thread_state->processor()->ResolveFunction(address);
  if (!target) {
    XELOGE("ARM64 interpreter failed to resolve call target {:08X}", address);
    return false;
  }
  return InvokeGuestFunction(target, thread_state, return_address);
}

}  // namespace

Arm64Function::Arm64Function(Module* module, uint32_t address)
    : GuestFunction(module, address) {}

Arm64Function::~Arm64Function() = default;

void Arm64Function::Setup(uint8_t* machine_code, size_t machine_code_length) {
  machine_code_ = machine_code;
  machine_code_length_ = machine_code_length;
}

void Arm64Function::SetupProgram(std::unique_ptr<Program> program) {
  program_ = std::move(program);
}

bool Arm64Function::CallImpl(ThreadState* thread_state,
                             uint32_t return_address) {
  if (program_) {
    return ExecuteProgram(thread_state, return_address);
  }

  if (!machine_code_) {
    XELOGE("ARM64 guest function {:08X} has no generated machine code",
           address());
    return false;
  }

  XELOGE("ARM64 guest function calls need a host-to-guest thunk");
  return false;
}

bool Arm64Function::ExecuteProgram(ThreadState* thread_state,
                                   uint32_t return_address) {
  if (!program_) {
    return false;
  }

  std::vector<RuntimeValue> values(program_->value_types.size());
  for (size_t i = 0; i < values.size(); ++i) {
    values[i] = MakeZero(program_->value_types[i]);
  }

  std::vector<RuntimeValue> locals(program_->max_value_ordinal + 1);

  uint32_t call_return_address = 0;
  uint32_t pc = program_->blocks.empty() ? 0 : program_->blocks[0].instruction_start;
  uint64_t step_count = 0;
  hir::RoundMode dynamic_rounding_mode = hir::ROUND_TO_NEAREST;
  constexpr uint64_t kMaxInterpreterSteps = 50000000;

  auto jump_to_block = [&](uint32_t block_index) {
    if (block_index >= program_->blocks.size()) {
      XELOGE("ARM64 interpreter branch target out of range in {:08X}: {}",
             address(), block_index);
      return false;
    }
    pc = program_->blocks[block_index].instruction_start;
    return true;
  };

  while (pc < program_->instructions.size()) {
    if (++step_count > kMaxInterpreterSteps) {
      const auto& stuck_instr =
          pc < program_->instructions.size() ? program_->instructions[pc]
                                             : program_->instructions.back();
      auto context = thread_state->context();
      XELOGE(
          "ARM64 interpreter step limit hit in {:08X}: pc {}, block {}, guest "
          "{:08X}, lr {:08X}, ctr {:08X}, r1 {:08X}, r3 {:08X}, r4 {:08X}, "
          "r5 {:08X}, r11 {:08X}, r12 {:08X}",
          address(), pc, stuck_instr.block_index, stuck_instr.source_offset,
          static_cast<uint32_t>(context->lr),
          static_cast<uint32_t>(context->ctr),
          static_cast<uint32_t>(context->r[1]),
          static_cast<uint32_t>(context->r[3]),
          static_cast<uint32_t>(context->r[4]),
          static_cast<uint32_t>(context->r[5]),
          static_cast<uint32_t>(context->r[11]),
          static_cast<uint32_t>(context->r[12]));
      return false;
    }

    if (step_count == 500000 || step_count == 5000000 ||
        step_count == 25000000) {
      auto context = thread_state->context();
      XELOGW(
          "ARM64 interpreter slow function {:08X}: step {}, pc {}, lr {:08X}, "
          "ctr {:08X}, r3 {:08X}, r4 {:08X}, r5 {:08X}",
          address(), step_count, pc, static_cast<uint32_t>(context->lr),
          static_cast<uint32_t>(context->ctr),
          static_cast<uint32_t>(context->r[3]),
          static_cast<uint32_t>(context->r[4]),
          static_cast<uint32_t>(context->r[5]));
    }

    const auto& instr = program_->instructions[pc];
    bool advance_pc = true;

    if (address() == 0x824669E0) {
      int trace_budget = g_entry_instruction_trace_budget.load();
      if (trace_budget > 0 &&
          g_entry_instruction_trace_budget.compare_exchange_strong(
              trace_budget, trace_budget - 1)) {
        XELOGI(
            "ARM64 entry exec step {} pc {} block {} guest {:08X} opcode {}",
            step_count, pc, instr.block_index, instr.source_offset,
            static_cast<uint32_t>(instr.opcode));
      }
    }

    auto read = [&](const Operand& operand) { return ReadOperand(operand, values); };
    auto store_dest = [&](const RuntimeValue& value) {
      if (instr.dest_ordinal != UINT32_MAX && instr.dest_ordinal < values.size()) {
        values[instr.dest_ordinal] = CastValue(value, instr.dest_type);
      }
    };
    auto read_memory = [&](uint64_t address_value, TypeName type) {
      return ReadRawValue(
          thread_state->memory()->TranslateVirtual(
              NormalizeGuestAddress(address_value)),
          type);
    };
    auto write_memory = [&](uint64_t address_value, const RuntimeValue& value) {
      WriteRawValue(thread_state->memory()->TranslateVirtual(
                        NormalizeGuestAddress(address_value)),
                    value);
    };

    switch (instr.opcode) {
      case hir::OPCODE_COMMENT:
      case hir::OPCODE_NOP:
      case hir::OPCODE_SOURCE_OFFSET:
      case hir::OPCODE_CONTEXT_BARRIER:
      case hir::OPCODE_MEMORY_BARRIER:
        break;

      case hir::OPCODE_DEBUG_BREAK:
      case hir::OPCODE_TRAP:
        XELOGW("ARM64 interpreter trap/debugbreak in {:08X} at guest {:08X}",
               address(), instr.source_offset);
        break;

      case hir::OPCODE_DEBUG_BREAK_TRUE:
      case hir::OPCODE_TRAP_TRUE:
        if (IsTrue(read(instr.src1))) {
          XELOGW(
              "ARM64 interpreter conditional trap/debugbreak in {:08X} at "
              "guest {:08X}",
              address(), instr.source_offset);
        }
        break;

      case hir::OPCODE_SET_RETURN_ADDRESS:
        call_return_address = static_cast<uint32_t>(ReadInteger(read(instr.src1)));
        break;

      case hir::OPCODE_CALL:
        if (instr.src1.kind != Operand::Kind::kSymbol ||
            !InvokeGuestFunction(instr.src1.symbol, thread_state,
                                 (instr.flags & hir::CALL_TAIL)
                                     ? return_address
                                     : call_return_address)) {
          XELOGE("ARM64 interpreter call failed in {:08X} at guest {:08X}",
                 address(), instr.source_offset);
          return false;
        }
        if (instr.flags & hir::CALL_TAIL) {
          return true;
        }
        break;

      case hir::OPCODE_CALL_TRUE:
        if (IsTrue(read(instr.src1))) {
          if (instr.src2.kind != Operand::Kind::kSymbol ||
              !InvokeGuestFunction(instr.src2.symbol, thread_state,
                                   (instr.flags & hir::CALL_TAIL)
                                       ? return_address
                                       : call_return_address)) {
            XELOGE(
                "ARM64 interpreter conditional call failed in {:08X} at guest "
                "{:08X}",
                address(), instr.source_offset);
            return false;
          }
          if (instr.flags & hir::CALL_TAIL) {
            return true;
          }
        }
        break;

      case hir::OPCODE_CALL_EXTERN:
        if (instr.src1.kind != Operand::Kind::kSymbol ||
            !InvokeHostFunction(instr.src1.symbol, thread_state)) {
          XELOGE("ARM64 interpreter extern call failed in {:08X} at guest {:08X}",
                 address(), instr.source_offset);
          return false;
        }
        break;

      case hir::OPCODE_CALL_INDIRECT: {
        uint32_t target_address = static_cast<uint32_t>(ReadInteger(read(instr.src1)));
        int trace_budget = g_call_trace_budget.load();
        if (trace_budget > 0 &&
            g_call_trace_budget.compare_exchange_strong(trace_budget,
                                                        trace_budget - 1)) {
          XELOGI(
              "ARM64 interpreter indirect call from {:08X} guest {:08X} "
              "target {:08X} return {:08X} flags {:X}",
              address(), instr.source_offset, target_address, return_address,
              instr.flags);
        }
        if ((instr.flags & hir::CALL_POSSIBLE_RETURN) &&
            target_address == return_address) {
          return true;
        }
        if (!InvokeResolvedGuestAddress(
                thread_state, target_address,
                (instr.flags & hir::CALL_TAIL) ? return_address
                                               : call_return_address)) {
          XELOGE("ARM64 interpreter indirect call failed in {:08X} to {:08X}",
                 address(), target_address);
          return false;
        }
        if (instr.flags & hir::CALL_TAIL) {
          return true;
        }
        break;
      }

      case hir::OPCODE_CALL_INDIRECT_TRUE:
        if (IsTrue(read(instr.src1))) {
          uint32_t target_address =
              static_cast<uint32_t>(ReadInteger(read(instr.src2)));
          int trace_budget = g_call_trace_budget.load();
          if (trace_budget > 0 &&
              g_call_trace_budget.compare_exchange_strong(trace_budget,
                                                          trace_budget - 1)) {
            XELOGI(
                "ARM64 interpreter conditional indirect call from {:08X} "
                "guest {:08X} target {:08X} return {:08X} flags {:X}",
                address(), instr.source_offset, target_address,
                return_address, instr.flags);
          }
          if ((instr.flags & hir::CALL_POSSIBLE_RETURN) &&
              target_address == return_address) {
            return true;
          }
          if (!InvokeResolvedGuestAddress(
                  thread_state, target_address,
                  (instr.flags & hir::CALL_TAIL) ? return_address
                                                 : call_return_address)) {
            XELOGE(
                "ARM64 interpreter conditional indirect call failed in {:08X} "
                "to {:08X}",
                address(), target_address);
            return false;
          }
          if (instr.flags & hir::CALL_TAIL) {
            return true;
          }
        }
        break;

      case hir::OPCODE_RETURN:
        return true;

      case hir::OPCODE_RETURN_TRUE:
        if (IsTrue(read(instr.src1))) {
          return true;
        }
        break;

      case hir::OPCODE_BRANCH:
        if (!jump_to_block(instr.src1.block_index)) {
          return false;
        }
        advance_pc = false;
        break;

      case hir::OPCODE_BRANCH_TRUE:
        if (IsTrue(read(instr.src1))) {
          if (!jump_to_block(instr.src2.block_index)) {
            return false;
          }
          advance_pc = false;
        }
        break;

      case hir::OPCODE_BRANCH_FALSE:
        if (!IsTrue(read(instr.src1))) {
          if (!jump_to_block(instr.src2.block_index)) {
            return false;
          }
          advance_pc = false;
        }
        break;

      case hir::OPCODE_ASSIGN:
      case hir::OPCODE_CAST:
      case hir::OPCODE_TRUNCATE:
        store_dest(read(instr.src1));
        break;

      case hir::OPCODE_ZERO_EXTEND:
        store_dest(MakeInteger(instr.dest_type, ReadInteger(read(instr.src1))));
        break;

      case hir::OPCODE_SIGN_EXTEND:
        store_dest(SignExtendValue(read(instr.src1), instr.dest_type));
        break;

      case hir::OPCODE_CONVERT: {
        auto src = read(instr.src1);
        auto round_mode = static_cast<hir::RoundMode>(instr.flags);
        if (round_mode == hir::ROUND_DYNAMIC) {
          round_mode = dynamic_rounding_mode;
        }
        store_dest(ConvertValue(src, instr.dest_type, round_mode));
        break;
      }

      case hir::OPCODE_ROUND: {
        auto round_mode = static_cast<hir::RoundMode>(instr.flags);
        if (round_mode == hir::ROUND_DYNAMIC) {
          round_mode = dynamic_rounding_mode;
        }
        store_dest(RoundRuntimeValue(read(instr.src1), round_mode));
        break;
      }

      case hir::OPCODE_LOAD_CLOCK:
        store_dest(MakeInteger(hir::INT64_TYPE, Clock::QueryGuestTickCount()));
        break;

      case hir::OPCODE_LOAD_VECTOR_SHL:
        store_dest(LoadVectorShiftValue(
            static_cast<uint32_t>(ReadInteger(read(instr.src1))), true));
        break;

      case hir::OPCODE_LOAD_VECTOR_SHR:
        store_dest(LoadVectorShiftValue(
            static_cast<uint32_t>(ReadInteger(read(instr.src1))), false));
        break;

      case hir::OPCODE_LOAD_LOCAL: {
        uint32_t slot = static_cast<uint32_t>(instr.src1.offset);
        if (slot < locals.size()) {
          store_dest(locals[slot]);
        } else {
          XELOGE("ARM64 interpreter local load out of range in {:08X}: {}",
                 address(), slot);
          return false;
        }
        break;
      }

      case hir::OPCODE_STORE_LOCAL: {
        uint32_t slot = static_cast<uint32_t>(instr.src1.offset);
        if (slot < locals.size()) {
          locals[slot] = read(instr.src2);
        } else {
          XELOGE("ARM64 interpreter local store out of range in {:08X}: {}",
                 address(), slot);
          return false;
        }
        break;
      }

      case hir::OPCODE_LOAD_CONTEXT:
        store_dest(ReadRawValue(reinterpret_cast<uint8_t*>(thread_state->context()) +
                                    instr.src1.offset,
                                instr.dest_type));
        break;

      case hir::OPCODE_STORE_CONTEXT:
        WriteRawValue(reinterpret_cast<uint8_t*>(thread_state->context()) +
                          instr.src1.offset,
                      read(instr.src2));
        break;

      case hir::OPCODE_LOAD_MMIO: {
        auto mmio_range = reinterpret_cast<xe::cpu::MMIORange*>(
            instr.src1.offset);
        auto read_address = static_cast<uint32_t>(instr.src2.offset);
        if (!mmio_range || !mmio_range->read) {
          XELOGE(
              "ARM64 interpreter invalid MMIO load in {:08X} at guest {:08X}",
              address(), instr.source_offset);
          return false;
        }
        uint32_t value = mmio_range->read(thread_state->context(),
                                          mmio_range->callback_context,
                                          read_address);
        store_dest(MakeInteger(instr.dest_type, xe::byte_swap(value)));
        break;
      }

      case hir::OPCODE_STORE_MMIO: {
        auto mmio_range = reinterpret_cast<xe::cpu::MMIORange*>(
            instr.src1.offset);
        auto write_address = static_cast<uint32_t>(instr.src2.offset);
        if (!mmio_range || !mmio_range->write) {
          XELOGE(
              "ARM64 interpreter invalid MMIO store in {:08X} at guest {:08X}",
              address(), instr.source_offset);
          return false;
        }
        auto value = static_cast<uint32_t>(ReadInteger(read(instr.src3)));
        mmio_range->write(thread_state->context(), mmio_range->callback_context,
                          write_address, xe::byte_swap(value));
        break;
      }

      case hir::OPCODE_LOAD_OFFSET:
      {
        auto offset = read(instr.src2);
        store_dest(read_memory(
            ReadInteger(read(instr.src1)) +
                SignExtendInteger(offset.type, ReadInteger(offset)),
            instr.dest_type));
        break;
      }

      case hir::OPCODE_STORE_OFFSET:
      {
        auto offset = read(instr.src2);
        write_memory(ReadInteger(read(instr.src1)) +
                         SignExtendInteger(offset.type, ReadInteger(offset)),
                     read(instr.src3));
        break;
      }

      case hir::OPCODE_LOAD:
        store_dest(read_memory(ReadInteger(read(instr.src1)), instr.dest_type));
        break;

      case hir::OPCODE_STORE:
        write_memory(ReadInteger(read(instr.src1)), read(instr.src2));
        break;

      case hir::OPCODE_MEMSET: {
        auto address_value = ReadInteger(read(instr.src1));
        auto fill_value = static_cast<uint8_t>(ReadInteger(read(instr.src2)));
        auto length = static_cast<size_t>(ReadInteger(read(instr.src3)));
        std::memset(thread_state->memory()->TranslateVirtual(
                        NormalizeGuestAddress(address_value)),
                    fill_value, length);
        break;
      }
      case hir::OPCODE_ATOMIC_EXCHANGE: {
        auto address_value = ReadInteger(read(instr.src1));
        auto new_value = read(instr.src2);
        store_dest(AtomicExchangeRawValue(
            thread_state->memory()->TranslateVirtual(
                NormalizeGuestAddress(address_value)),
            new_value));
        break;
      }
      case hir::OPCODE_ATOMIC_COMPARE_EXCHANGE: {
        auto address_value = ReadInteger(read(instr.src1));
        auto expected = read(instr.src2);
        auto new_value = CastValue(read(instr.src3), expected.type);
        store_dest(MakeInteger(
            instr.dest_type,
            AtomicCompareExchangeRawValue(
                thread_state->memory()->TranslateVirtual(
                    NormalizeGuestAddress(address_value)),
                expected, new_value)));
        break;
      }

      case hir::OPCODE_SELECT:
        store_dest(IsTrue(read(instr.src1)) ? read(instr.src2)
                                            : read(instr.src3));
        break;

      case hir::OPCODE_IS_TRUE:
        store_dest(MakeInteger(instr.dest_type, IsTrue(read(instr.src1))));
        break;

      case hir::OPCODE_IS_FALSE:
        store_dest(MakeInteger(instr.dest_type, !IsTrue(read(instr.src1))));
        break;

      case hir::OPCODE_IS_NAN:
        store_dest(IsNanValue(read(instr.src1).type, read(instr.src1)));
        break;

      case hir::OPCODE_COMPARE_EQ:
        store_dest(MakeInteger(instr.dest_type,
                               ReadInteger(read(instr.src1)) ==
                                   ReadInteger(read(instr.src2))));
        break;
      case hir::OPCODE_COMPARE_NE:
        store_dest(MakeInteger(instr.dest_type,
                               ReadInteger(read(instr.src1)) !=
                                   ReadInteger(read(instr.src2))));
        break;
      case hir::OPCODE_COMPARE_SLT:
        store_dest(MakeInteger(instr.dest_type,
                               SignExtendInteger(read(instr.src1).type,
                                                 ReadInteger(read(instr.src1))) <
                                   SignExtendInteger(
                                       read(instr.src2).type,
                                       ReadInteger(read(instr.src2)))));
        break;
      case hir::OPCODE_COMPARE_SLE:
        store_dest(MakeInteger(instr.dest_type,
                               SignExtendInteger(read(instr.src1).type,
                                                 ReadInteger(read(instr.src1))) <=
                                   SignExtendInteger(
                                       read(instr.src2).type,
                                       ReadInteger(read(instr.src2)))));
        break;
      case hir::OPCODE_COMPARE_SGT:
        store_dest(MakeInteger(instr.dest_type,
                               SignExtendInteger(read(instr.src1).type,
                                                 ReadInteger(read(instr.src1))) >
                                   SignExtendInteger(
                                       read(instr.src2).type,
                                       ReadInteger(read(instr.src2)))));
        break;
      case hir::OPCODE_COMPARE_SGE:
        store_dest(MakeInteger(instr.dest_type,
                               SignExtendInteger(read(instr.src1).type,
                                                 ReadInteger(read(instr.src1))) >=
                                   SignExtendInteger(
                                       read(instr.src2).type,
                                       ReadInteger(read(instr.src2)))));
        break;
      case hir::OPCODE_COMPARE_ULT:
        store_dest(MakeInteger(instr.dest_type,
                               ReadInteger(read(instr.src1)) <
                                   ReadInteger(read(instr.src2))));
        break;
      case hir::OPCODE_COMPARE_ULE:
        store_dest(MakeInteger(instr.dest_type,
                               ReadInteger(read(instr.src1)) <=
                                   ReadInteger(read(instr.src2))));
        break;
      case hir::OPCODE_COMPARE_UGT:
        store_dest(MakeInteger(instr.dest_type,
                               ReadInteger(read(instr.src1)) >
                                   ReadInteger(read(instr.src2))));
        break;
      case hir::OPCODE_COMPARE_UGE:
        store_dest(MakeInteger(instr.dest_type,
                               ReadInteger(read(instr.src1)) >=
                                   ReadInteger(read(instr.src2))));
        break;

      case hir::OPCODE_VECTOR_COMPARE_EQ:
      case hir::OPCODE_VECTOR_COMPARE_SGT:
      case hir::OPCODE_VECTOR_COMPARE_SGE:
      case hir::OPCODE_VECTOR_COMPARE_UGT:
      case hir::OPCODE_VECTOR_COMPARE_UGE:
        store_dest(VectorCompareValue(instr.opcode,
                                      static_cast<TypeName>(instr.flags),
                                      read(instr.src1), read(instr.src2)));
        break;

      case hir::OPCODE_MAX:
        store_dest(MinMaxValue(instr.dest_type, read(instr.src1),
                               read(instr.src2), true));
        break;
      case hir::OPCODE_MIN:
        store_dest(MinMaxValue(instr.dest_type, read(instr.src1),
                               read(instr.src2), false));
        break;
      case hir::OPCODE_VECTOR_MAX:
        store_dest(VectorMinMaxValue(static_cast<TypeName>(instr.flags >> 8),
                                     instr.flags, read(instr.src1),
                                     read(instr.src2), true));
        break;
      case hir::OPCODE_VECTOR_MIN:
        store_dest(VectorMinMaxValue(static_cast<TypeName>(instr.flags >> 8),
                                     instr.flags, read(instr.src1),
                                     read(instr.src2), false));
        break;

      case hir::OPCODE_ADD:
        store_dest(AddValue(instr.dest_type, read(instr.src1),
                            read(instr.src2)));
        break;
      case hir::OPCODE_ADD_CARRY:
        store_dest(MakeInteger(instr.dest_type, ReadInteger(read(instr.src1)) +
                                                    ReadInteger(read(instr.src2)) +
                                                    (IsTrue(read(instr.src3)) ? 1 : 0)));
        break;
      case hir::OPCODE_SUB:
        store_dest(SubValue(instr.dest_type, read(instr.src1),
                            read(instr.src2)));
        break;
      case hir::OPCODE_MUL:
        store_dest(MulValue(instr.dest_type, read(instr.src1),
                            read(instr.src2)));
        break;
      case hir::OPCODE_MUL_HI:
        store_dest(MulHighValue(instr.dest_type, read(instr.src1),
                                read(instr.src2), instr.flags));
        break;
      case hir::OPCODE_DIV:
        store_dest(DivValue(instr.dest_type, read(instr.src1), read(instr.src2),
                            instr.flags));
        break;
      case hir::OPCODE_NEG:
        store_dest(NegValue(instr.dest_type, read(instr.src1)));
        break;
      case hir::OPCODE_ABS:
        store_dest(AbsValue(instr.dest_type, read(instr.src1)));
        break;
      case hir::OPCODE_SQRT:
        store_dest(SqrtValue(instr.dest_type, read(instr.src1)));
        break;
      case hir::OPCODE_RSQRT:
        store_dest(ReciprocalSqrtValue(instr.dest_type, read(instr.src1)));
        break;
      case hir::OPCODE_RECIP:
        store_dest(ReciprocalValue(instr.dest_type, read(instr.src1)));
        break;
      case hir::OPCODE_POW2:
        store_dest(Pow2Value(instr.dest_type, read(instr.src1)));
        break;
      case hir::OPCODE_LOG2:
        store_dest(Log2Value(instr.dest_type, read(instr.src1)));
        break;
      case hir::OPCODE_MUL_ADD:
        store_dest(MulAddValue(instr.dest_type, read(instr.src1),
                               read(instr.src2), read(instr.src3)));
        break;
      case hir::OPCODE_MUL_SUB:
        store_dest(MulSubValue(instr.dest_type, read(instr.src1),
                               read(instr.src2), read(instr.src3)));
        break;
      case hir::OPCODE_DOT_PRODUCT_4:
        store_dest(DotProduct4Value(read(instr.src1), read(instr.src2)));
        break;

      case hir::OPCODE_AND:
        store_dest(MakeInteger(instr.dest_type, ReadInteger(read(instr.src1)) &
                                                    ReadInteger(read(instr.src2))));
        break;
      case hir::OPCODE_AND_NOT:
        store_dest(MakeInteger(instr.dest_type, ReadInteger(read(instr.src1)) &
                                                    ~ReadInteger(read(instr.src2))));
        break;
      case hir::OPCODE_OR:
        store_dest(MakeInteger(instr.dest_type, ReadInteger(read(instr.src1)) |
                                                    ReadInteger(read(instr.src2))));
        break;
      case hir::OPCODE_XOR:
        store_dest(MakeInteger(instr.dest_type, ReadInteger(read(instr.src1)) ^
                                                    ReadInteger(read(instr.src2))));
        break;
      case hir::OPCODE_NOT:
        store_dest(MakeInteger(instr.dest_type, ~ReadInteger(read(instr.src1))));
        break;

      case hir::OPCODE_SWIZZLE:
        store_dest(SwizzleValue(read(instr.src1),
                                static_cast<uint32_t>(instr.src2.offset)));
        break;
      case hir::OPCODE_EXTRACT:
        store_dest(ExtractValue(instr.dest_type, read(instr.src1),
                                read(instr.src2)));
        break;
      case hir::OPCODE_SPLAT:
        store_dest(SplatValue(read(instr.src1)));
        break;
      case hir::OPCODE_PERMUTE:
        store_dest(PermuteValue(static_cast<TypeName>(instr.flags),
                                read(instr.src1), read(instr.src2),
                                read(instr.src3)));
        break;
      case hir::OPCODE_UNPACK:
        store_dest(UnpackValue(read(instr.src1), instr.flags));
        break;

      case hir::OPCODE_SHL:
        store_dest(MakeInteger(instr.dest_type,
                               ReadInteger(read(instr.src1)) <<
                                   (ReadInteger(read(instr.src2)) & 0x3F)));
        break;
      case hir::OPCODE_SHR:
        store_dest(MakeInteger(instr.dest_type,
                               ReadInteger(read(instr.src1)) >>
                                   (ReadInteger(read(instr.src2)) & 0x3F)));
        break;
      case hir::OPCODE_SHA:
        store_dest(MakeInteger(
            instr.dest_type,
            static_cast<uint64_t>(
                SignExtendInteger(read(instr.src1).type,
                                  ReadInteger(read(instr.src1))) >>
                (ReadInteger(read(instr.src2)) & 0x3F))));
        break;
      case hir::OPCODE_ROTATE_LEFT: {
        auto value = ReadInteger(read(instr.src1));
        uint32_t width = static_cast<uint32_t>(TypeSize(read(instr.src1).type) * 8);
        uint32_t shift = static_cast<uint32_t>(ReadInteger(read(instr.src2))) &
                         (width - 1);
        uint64_t mask = IntegerMask(read(instr.src1).type);
        uint64_t rotated =
            shift ? ((value << shift) | (value >> (width - shift))) & mask
                  : value & mask;
        store_dest(MakeInteger(instr.dest_type, rotated));
        break;
      }

      case hir::OPCODE_BYTE_SWAP:
        store_dest(ByteSwapValue(read(instr.src1)));
        break;

      case hir::OPCODE_CNTLZ:
        store_dest(MakeInteger(instr.dest_type, CountLeadingZeros(read(instr.src1))));
        break;

      case hir::OPCODE_SET_ROUNDING_MODE:
        dynamic_rounding_mode = RoundModeFromPpcFpscr(
            static_cast<uint32_t>(ReadInteger(read(instr.src1))));
        break;

      case hir::OPCODE_CACHE_CONTROL:
        break;

      default:
        XELOGE(
            "ARM64 interpreter unimplemented HIR opcode {} in {:08X} at guest "
            "{:08X}",
            static_cast<uint32_t>(instr.opcode), address(), instr.source_offset);
        return false;
    }

    if (advance_pc) {
      ++pc;
    }
  }

  return true;
}

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
