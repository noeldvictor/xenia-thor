/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/arm64/arm64_jit.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "xenia/base/byte_order.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/cpu/backend/arm64/arm64_backend.h"
#include "xenia/cpu/backend/arm64/arm64_code_cache.h"
#include "xenia/cpu/mmio_handler.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/memory.h"
#include "third_party/half/include/half.hpp"

#include "xbyak_aarch64/xbyak_aarch64.h"

DECLARE_bool(arm64_ignore_undefined_externs);
DECLARE_string(arm64_guest_store_watch);

DEFINE_bool(arm64_enable_mini_jit, true,
            "ARM64 bring-up: attempt the tiny experimental AArch64 mini-JIT. "
            "Disable to force the HIR interpreter without rebuilding.",
            "CPU");
DEFINE_string(
    arm64_mini_jit_blacklist, "",
    "ARM64 bring-up: comma/semicolon/space separated guest function start "
    "addresses that should skip the tiny mini-JIT and run in the interpreter.",
    "CPU");
DEFINE_string(
    arm64_force_interpreter_guest_ranges, "",
    "ARM64 bring-up: comma/semicolon/space separated guest function addresses "
    "or inclusive ranges, such as 826A0000-826AFFFF, that should run in the "
    "interpreter.",
    "CPU");
DEFINE_uint32(arm64_mini_jit_max_stack_bytes, 256 * 1024,
              "ARM64 bring-up: maximum temporary stack frame size accepted by "
              "the tiny experimental AArch64 mini-JIT.",
              "CPU");

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {
namespace {

using Program = Arm64Function::Program;
using Instruction = Arm64Function::Instruction;
using Operand = Arm64Function::Operand;
using Xbyak_aarch64::Cond;
using Xbyak_aarch64::EQ;
using Xbyak_aarch64::GE;
using Xbyak_aarch64::GT;
using Xbyak_aarch64::HI;
using Xbyak_aarch64::HS;
using Xbyak_aarch64::LE;
using Xbyak_aarch64::LO;
using Xbyak_aarch64::LS;
using Xbyak_aarch64::LT;
using Xbyak_aarch64::NE;
using Xbyak_aarch64::WReg;
using Xbyak_aarch64::XReg;
using Xbyak_aarch64::ptr;

constexpr size_t kMaxCodeSize = 2 * 1024 * 1024;

std::atomic<int> g_jit_compile_log_budget{80};

std::string_view Trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return value;
}

bool ParseGuestAddress(std::string_view value, uint32_t* out_address) {
  value = Trim(value);
  if (value.empty()) {
    return false;
  }
  int base = 10;
  if (value.size() > 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
    base = 16;
  } else {
    if (value.size() >= 8) {
      base = 16;
    }
    for (char c : value) {
      if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
        base = 16;
        break;
      }
    }
  }

  uint64_t parsed = 0;
  auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed, base);
  if (result.ec != std::errc() || result.ptr != value.data() + value.size() ||
      parsed > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  *out_address = static_cast<uint32_t>(parsed);
  return true;
}

bool GuestAddressMatchesList(uint32_t address, std::string_view list) {
  size_t token_start = 0;
  while (token_start < list.size()) {
    while (token_start < list.size() &&
           (std::isspace(static_cast<unsigned char>(list[token_start])) ||
            list[token_start] == ',' || list[token_start] == ';')) {
      ++token_start;
    }
    if (token_start >= list.size()) {
      break;
    }

    size_t token_end = token_start;
    while (token_end < list.size() && list[token_end] != ',' &&
           list[token_end] != ';' &&
           !std::isspace(static_cast<unsigned char>(list[token_end]))) {
      ++token_end;
    }

    std::string_view token =
        Trim(list.substr(token_start, token_end - token_start));
    size_t range_separator = token.find('-');
    uint32_t start = 0;
    uint32_t end = 0;
    if (range_separator != std::string_view::npos) {
      if (ParseGuestAddress(token.substr(0, range_separator), &start) &&
          ParseGuestAddress(token.substr(range_separator + 1), &end)) {
        if (start > end) {
          std::swap(start, end);
        }
        if (address >= start && address <= end) {
          return true;
        }
      }
    } else if (ParseGuestAddress(token, &start) && address == start) {
      return true;
    }

    token_start = token_end;
  }

  return false;
}

bool IsIntegerType(hir::TypeName type) { return type <= hir::INT64_TYPE; }

bool IsRawSlotType(hir::TypeName type) {
  return IsIntegerType(type) || type == hir::FLOAT32_TYPE ||
         type == hir::FLOAT64_TYPE;
}

bool IsMiniJitSlotType(hir::TypeName type) {
  return IsRawSlotType(type) || type == hir::VEC128_TYPE;
}

uint32_t MiniJitSlotSize(hir::TypeName type) {
  return type == hir::VEC128_TYPE ? 16 : 8;
}

uint32_t MiniJitSlotAlign(hir::TypeName type) {
  return type == hir::VEC128_TYPE ? 16 : 8;
}

size_t EstimateMiniJitStackSize(const Program& program) {
  uint32_t offset = 0;
  auto append = [&](hir::TypeName type) {
    offset = static_cast<uint32_t>(
        xe::round_up(offset, MiniJitSlotAlign(type)));
    offset += MiniJitSlotSize(type);
  };
  for (auto type : program.value_types) {
    append(type);
  }
  for (uint32_t i = 0; i < program.local_count; ++i) {
    append(i < program.local_types.size() ? program.local_types[i]
                                          : hir::INT64_TYPE);
  }
  return xe::round_up(static_cast<size_t>(offset), size_t(16));
}

const char* HirTypeName(hir::TypeName type) {
  switch (type) {
    case hir::INT8_TYPE:
      return "int8";
    case hir::INT16_TYPE:
      return "int16";
    case hir::INT32_TYPE:
      return "int32";
    case hir::INT64_TYPE:
      return "int64";
    case hir::FLOAT32_TYPE:
      return "float32";
    case hir::FLOAT64_TYPE:
      return "float64";
    case hir::VEC128_TYPE:
      return "vec128";
    default:
      return "unknown";
  }
}

uint64_t IntegerMask(hir::TypeName type) {
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

uint64_t RawSlotMask(hir::TypeName type) {
  if (type == hir::FLOAT32_TYPE) {
    return 0xFFFFFFFFu;
  }
  return IntegerMask(type);
}

uint64_t MaskInteger(hir::TypeName type, uint64_t value) {
  return value & IntegerMask(type);
}

uint64_t MaskRawSlot(hir::TypeName type, uint64_t value) {
  return value & RawSlotMask(type);
}

int64_t SignExtendInteger(hir::TypeName type, uint64_t value) {
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

long double ApplyRoundMode(hir::RoundMode round_mode, long double value) {
  switch (round_mode) {
    case hir::ROUND_TO_ZERO:
      return std::trunc(value);
    case hir::ROUND_TO_NEAREST:
    case hir::ROUND_DYNAMIC:
      return std::round(value);
    case hir::ROUND_TO_MINUS_INFINITY:
      return std::floor(value);
    case hir::ROUND_TO_POSITIVE_INFINITY:
      return std::ceil(value);
    default:
      return std::round(value);
  }
}

std::pair<int64_t, int64_t> SignedIntegerRange(hir::TypeName type) {
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

float FloatFromBits(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

double DoubleFromBits(uint64_t bits) {
  double value = 0.0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

uint32_t FloatBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

uint64_t DoubleBits(double value) {
  uint64_t bits = 0;
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

void SetVecU8(vec128_t* value, uint32_t index, uint8_t part) {
  value->u8[index ^ 0x3] = part;
}

void SetVecU16(vec128_t* value, uint32_t index, uint16_t part) {
  value->u16[index ^ 0x1] = part;
}

extern "C" uint64_t Arm64JitConvertRaw(uint64_t raw_value,
                                        uint32_t source_type_value,
                                        uint32_t target_type_value,
                                        uint32_t round_mode_value) {
  auto source_type = static_cast<hir::TypeName>(source_type_value);
  auto target_type = static_cast<hir::TypeName>(target_type_value);
  auto round_mode = static_cast<hir::RoundMode>(round_mode_value);

  if (target_type <= hir::INT64_TYPE && source_type <= hir::INT64_TYPE) {
    return MaskInteger(target_type, raw_value);
  }

  if (target_type <= hir::INT64_TYPE &&
      (source_type == hir::FLOAT32_TYPE || source_type == hir::FLOAT64_TYPE)) {
    long double input =
        source_type == hir::FLOAT32_TYPE
            ? static_cast<long double>(
                  FloatFromBits(static_cast<uint32_t>(raw_value)))
            : static_cast<long double>(DoubleFromBits(raw_value));
    if (std::isnan(input)) {
      return 0;
    }
    long double rounded = ApplyRoundMode(round_mode, input);
    auto range = SignedIntegerRange(target_type);
    long double min_value = static_cast<long double>(range.first);
    long double max_value = static_cast<long double>(range.second);
    if (rounded <= min_value) {
      return MaskInteger(target_type, static_cast<uint64_t>(range.first));
    }
    if (rounded >= max_value) {
      return MaskInteger(target_type, static_cast<uint64_t>(range.second));
    }
    return MaskInteger(
        target_type,
        static_cast<uint64_t>(static_cast<int64_t>(rounded)));
  }

  if (target_type == hir::FLOAT32_TYPE) {
    if (source_type == hir::FLOAT64_TYPE) {
      return FloatBits(static_cast<float>(DoubleFromBits(raw_value)));
    }
    if (source_type <= hir::INT64_TYPE) {
      return FloatBits(
          static_cast<float>(SignExtendInteger(source_type, raw_value)));
    }
  }

  if (target_type == hir::FLOAT64_TYPE) {
    if (source_type == hir::FLOAT32_TYPE) {
      return DoubleBits(static_cast<double>(
          FloatFromBits(static_cast<uint32_t>(raw_value))));
    }
    if (source_type <= hir::INT64_TYPE) {
      return DoubleBits(
          static_cast<double>(SignExtendInteger(source_type, raw_value)));
    }
  }

  return MaskRawSlot(target_type, raw_value);
}

extern "C" uint64_t Arm64JitRoundRaw(uint64_t raw_value,
                                      uint32_t type_value,
                                      uint32_t round_mode_value) {
  auto type = static_cast<hir::TypeName>(type_value);
  auto round_mode = static_cast<hir::RoundMode>(round_mode_value);
  switch (type) {
    case hir::FLOAT32_TYPE:
      return FloatBits(static_cast<float>(ApplyRoundMode(
          round_mode,
          static_cast<long double>(FloatFromBits(
              static_cast<uint32_t>(raw_value))))));
    case hir::FLOAT64_TYPE:
      return DoubleBits(static_cast<double>(ApplyRoundMode(
          round_mode, static_cast<long double>(DoubleFromBits(raw_value)))));
    default:
      return MaskRawSlot(type, raw_value);
  }
}

extern "C" uint64_t Arm64JitCastRaw(uint64_t raw_value,
                                     uint32_t source_type_value,
                                     uint32_t target_type_value) {
  auto source_type = static_cast<hir::TypeName>(source_type_value);
  auto target_type = static_cast<hir::TypeName>(target_type_value);

  if (target_type <= hir::INT64_TYPE) {
    return MaskInteger(target_type, raw_value);
  }

  if (target_type == hir::FLOAT32_TYPE) {
    if (source_type == hir::FLOAT64_TYPE) {
      return FloatBits(static_cast<float>(DoubleFromBits(raw_value)));
    }
    if (source_type <= hir::INT64_TYPE) {
      return FloatBits(static_cast<float>(MaskInteger(source_type, raw_value)));
    }
    return MaskRawSlot(target_type, raw_value);
  }

  if (target_type == hir::FLOAT64_TYPE) {
    if (source_type == hir::FLOAT32_TYPE) {
      return DoubleBits(static_cast<double>(
          FloatFromBits(static_cast<uint32_t>(raw_value))));
    }
    if (source_type <= hir::INT64_TYPE) {
      return DoubleBits(
          static_cast<double>(MaskInteger(source_type, raw_value)));
    }
    return MaskRawSlot(target_type, raw_value);
  }

  return MaskRawSlot(target_type, raw_value);
}

extern "C" uint64_t Arm64JitFloatBinaryRaw(uint64_t lhs_raw,
                                            uint64_t rhs_raw,
                                            uint32_t type_value,
                                            uint32_t opcode_value) {
  auto type = static_cast<hir::TypeName>(type_value);
  auto opcode = static_cast<hir::Opcode>(opcode_value);

  if (type == hir::FLOAT32_TYPE) {
    float lhs = FloatFromBits(static_cast<uint32_t>(lhs_raw));
    float rhs = FloatFromBits(static_cast<uint32_t>(rhs_raw));
    float result = 0.0f;
    switch (opcode) {
      case hir::OPCODE_ADD:
        result = lhs + rhs;
        break;
      case hir::OPCODE_SUB:
        result = lhs - rhs;
        break;
      case hir::OPCODE_MUL:
        result = lhs * rhs;
        break;
      case hir::OPCODE_DIV:
        result = lhs / rhs;
        break;
      default:
        break;
    }
    return FloatBits(result);
  }

  if (type == hir::FLOAT64_TYPE) {
    double lhs = DoubleFromBits(lhs_raw);
    double rhs = DoubleFromBits(rhs_raw);
    double result = 0.0;
    switch (opcode) {
      case hir::OPCODE_ADD:
        result = lhs + rhs;
        break;
      case hir::OPCODE_SUB:
        result = lhs - rhs;
        break;
      case hir::OPCODE_MUL:
        result = lhs * rhs;
        break;
      case hir::OPCODE_DIV:
        result = lhs / rhs;
        break;
      default:
        break;
    }
    return DoubleBits(result);
  }

  return 0;
}

extern "C" uint64_t Arm64JitFloatTernaryRaw(uint64_t lhs_raw,
                                             uint64_t rhs_raw,
                                             uint64_t third_raw,
                                             uint32_t type_value,
                                             uint32_t opcode_value) {
  auto type = static_cast<hir::TypeName>(type_value);
  auto opcode = static_cast<hir::Opcode>(opcode_value);

  if (type == hir::FLOAT32_TYPE) {
    float lhs = FloatFromBits(static_cast<uint32_t>(lhs_raw));
    float rhs = FloatFromBits(static_cast<uint32_t>(rhs_raw));
    float third = FloatFromBits(static_cast<uint32_t>(third_raw));
    float product = lhs * rhs;
    switch (opcode) {
      case hir::OPCODE_MUL_ADD:
        return FloatBits(product + third);
      case hir::OPCODE_MUL_SUB:
        return FloatBits(product - third);
      default:
        return 0;
    }
  }

  if (type == hir::FLOAT64_TYPE) {
    double lhs = DoubleFromBits(lhs_raw);
    double rhs = DoubleFromBits(rhs_raw);
    double third = DoubleFromBits(third_raw);
    double product = lhs * rhs;
    switch (opcode) {
      case hir::OPCODE_MUL_ADD:
        return DoubleBits(product + third);
      case hir::OPCODE_MUL_SUB:
        return DoubleBits(product - third);
      default:
        return 0;
    }
  }

  return 0;
}

extern "C" uint64_t Arm64JitUnaryRaw(uint64_t raw_value,
                                      uint32_t type_value,
                                      uint32_t opcode_value) {
  auto type = static_cast<hir::TypeName>(type_value);
  auto opcode = static_cast<hir::Opcode>(opcode_value);

  switch (opcode) {
    case hir::OPCODE_NEG:
      if (type == hir::FLOAT32_TYPE) {
        return FloatBits(-FloatFromBits(static_cast<uint32_t>(raw_value)));
      }
      if (type == hir::FLOAT64_TYPE) {
        return DoubleBits(-DoubleFromBits(raw_value));
      }
      if (IsIntegerType(type)) {
        return MaskInteger(
            type,
            static_cast<uint64_t>(-SignExtendInteger(type, raw_value)));
      }
      break;
    case hir::OPCODE_ABS:
      if (type == hir::FLOAT32_TYPE) {
        return FloatBits(std::fabs(FloatFromBits(
            static_cast<uint32_t>(raw_value))));
      }
      if (type == hir::FLOAT64_TYPE) {
        return DoubleBits(std::fabs(DoubleFromBits(raw_value)));
      }
      if (IsIntegerType(type)) {
        int64_t signed_value = SignExtendInteger(type, raw_value);
        return MaskInteger(
            type,
            signed_value < 0
                ? (~static_cast<uint64_t>(signed_value) + 1)
                : static_cast<uint64_t>(signed_value));
      }
      break;
    case hir::OPCODE_SQRT:
      if (type == hir::FLOAT32_TYPE) {
        return FloatBits(std::sqrt(FloatFromBits(
            static_cast<uint32_t>(raw_value))));
      }
      if (type == hir::FLOAT64_TYPE) {
        return DoubleBits(std::sqrt(DoubleFromBits(raw_value)));
      }
      break;
    case hir::OPCODE_RSQRT:
      if (type == hir::FLOAT32_TYPE) {
        return FloatBits(1.0f / std::sqrt(FloatFromBits(
                                     static_cast<uint32_t>(raw_value))));
      }
      if (type == hir::FLOAT64_TYPE) {
        return DoubleBits(1.0 / std::sqrt(DoubleFromBits(raw_value)));
      }
      break;
    case hir::OPCODE_RECIP:
      if (type == hir::FLOAT32_TYPE) {
        return FloatBits(1.0f / FloatFromBits(
                                    static_cast<uint32_t>(raw_value)));
      }
      if (type == hir::FLOAT64_TYPE) {
        return DoubleBits(1.0 / DoubleFromBits(raw_value));
      }
      break;
    case hir::OPCODE_POW2:
      if (type == hir::FLOAT32_TYPE) {
        return FloatBits(std::exp2(FloatFromBits(
            static_cast<uint32_t>(raw_value))));
      }
      if (type == hir::FLOAT64_TYPE) {
        return DoubleBits(std::exp2(DoubleFromBits(raw_value)));
      }
      break;
    case hir::OPCODE_LOG2:
      if (type == hir::FLOAT32_TYPE) {
        return FloatBits(std::log2(FloatFromBits(
            static_cast<uint32_t>(raw_value))));
      }
      if (type == hir::FLOAT64_TYPE) {
        return DoubleBits(std::log2(DoubleFromBits(raw_value)));
      }
      break;
    default:
      break;
  }

  return MaskRawSlot(type, raw_value);
}

extern "C" uint64_t Arm64JitIsNanRaw(uint64_t raw_value,
                                      uint32_t type_value) {
  auto type = static_cast<hir::TypeName>(type_value);
  switch (type) {
    case hir::FLOAT32_TYPE:
      return std::isnan(FloatFromBits(static_cast<uint32_t>(raw_value))) ? 1
                                                                         : 0;
    case hir::FLOAT64_TYPE:
      return std::isnan(DoubleFromBits(raw_value)) ? 1 : 0;
    default:
      return 0;
  }
}

extern "C" uint64_t Arm64JitCompareFloatRaw(uint64_t lhs_raw,
                                             uint64_t rhs_raw,
                                             uint32_t type_value,
                                             uint32_t opcode_value) {
  auto type = static_cast<hir::TypeName>(type_value);
  auto opcode = static_cast<hir::Opcode>(opcode_value);
  long double lhs = 0.0;
  long double rhs = 0.0;
  if (type == hir::FLOAT32_TYPE) {
    lhs = static_cast<long double>(FloatFromBits(
        static_cast<uint32_t>(lhs_raw)));
    rhs = static_cast<long double>(FloatFromBits(
        static_cast<uint32_t>(rhs_raw)));
  } else if (type == hir::FLOAT64_TYPE) {
    lhs = static_cast<long double>(DoubleFromBits(lhs_raw));
    rhs = static_cast<long double>(DoubleFromBits(rhs_raw));
  } else {
    return 0;
  }

  switch (opcode) {
    case hir::OPCODE_COMPARE_EQ:
      return lhs == rhs ? 1 : 0;
    case hir::OPCODE_COMPARE_NE:
      return lhs != rhs ? 1 : 0;
    case hir::OPCODE_COMPARE_SLT:
    case hir::OPCODE_COMPARE_ULT:
      return lhs < rhs ? 1 : 0;
    case hir::OPCODE_COMPARE_SLE:
    case hir::OPCODE_COMPARE_ULE:
      return lhs <= rhs ? 1 : 0;
    case hir::OPCODE_COMPARE_SGT:
    case hir::OPCODE_COMPARE_UGT:
      return lhs > rhs ? 1 : 0;
    case hir::OPCODE_COMPARE_SGE:
    case hir::OPCODE_COMPARE_UGE:
      return lhs >= rhs ? 1 : 0;
    default:
      return 0;
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

uint64_t ReadIntegerValue(const void* address, hir::TypeName type,
                          uint32_t flags) {
  uint64_t value = 0;
  switch (type) {
    case hir::INT8_TYPE: {
      uint8_t data = 0;
      std::memcpy(&data, address, sizeof(data));
      value = data;
      break;
    }
    case hir::INT16_TYPE: {
      uint16_t data = 0;
      std::memcpy(&data, address, sizeof(data));
      if (flags & hir::LOAD_STORE_BYTE_SWAP) {
        data = xe::byte_swap(data);
      }
      value = data;
      break;
    }
    case hir::INT32_TYPE: {
      uint32_t data = 0;
      std::memcpy(&data, address, sizeof(data));
      if (flags & hir::LOAD_STORE_BYTE_SWAP) {
        data = xe::byte_swap(data);
      }
      value = data;
      break;
    }
    case hir::INT64_TYPE: {
      uint64_t data = 0;
      std::memcpy(&data, address, sizeof(data));
      if (flags & hir::LOAD_STORE_BYTE_SWAP) {
        data = xe::byte_swap(data);
      }
      value = data;
      break;
    }
    default:
      break;
  }
  return MaskInteger(type, value);
}

void WriteIntegerValue(void* address, hir::TypeName type, uint64_t value,
                       uint32_t flags) {
  value = MaskInteger(type, value);
  switch (type) {
    case hir::INT8_TYPE: {
      auto data = static_cast<uint8_t>(value);
      std::memcpy(address, &data, sizeof(data));
      break;
    }
    case hir::INT16_TYPE: {
      auto data = static_cast<uint16_t>(value);
      if (flags & hir::LOAD_STORE_BYTE_SWAP) {
        data = xe::byte_swap(data);
      }
      std::memcpy(address, &data, sizeof(data));
      break;
    }
    case hir::INT32_TYPE: {
      auto data = static_cast<uint32_t>(value);
      if (flags & hir::LOAD_STORE_BYTE_SWAP) {
        data = xe::byte_swap(data);
      }
      std::memcpy(address, &data, sizeof(data));
      break;
    }
    case hir::INT64_TYPE: {
      auto data = value;
      if (flags & hir::LOAD_STORE_BYTE_SWAP) {
        data = xe::byte_swap(data);
      }
      std::memcpy(address, &data, sizeof(data));
      break;
    }
    default:
      break;
  }
}

uint64_t Arm64JitLoadInteger(ThreadState* thread_state, uint64_t address,
                             uint32_t type, uint32_t flags) {
  auto type_name = static_cast<hir::TypeName>(type);
  auto guest_address = NormalizeGuestAddress(address);
  if (type_name == hir::INT32_TYPE) {
    if (auto range = thread_state->memory()->LookupVirtualMappedRange(
            guest_address)) {
      uint32_t value =
          range->read(nullptr, range->callback_context, guest_address);
      if (!(flags & hir::LOAD_STORE_BYTE_SWAP)) {
        value = xe::byte_swap(value);
      }
      return value;
    }
  }

  auto host_address = thread_state->memory()->TranslateVirtual(guest_address);
  return ReadIntegerValue(host_address, type_name, flags);
}

void Arm64JitStoreInteger(ThreadState* thread_state, uint64_t address,
                          uint64_t value, uint32_t type, uint32_t flags) {
  auto type_name = static_cast<hir::TypeName>(type);
  auto guest_address = NormalizeGuestAddress(address);
  if (type_name == hir::INT32_TYPE) {
    if (auto range = thread_state->memory()->LookupVirtualMappedRange(
            guest_address)) {
      uint32_t register_value = static_cast<uint32_t>(value);
      if (!(flags & hir::LOAD_STORE_BYTE_SWAP)) {
        register_value = xe::byte_swap(register_value);
      }
      range->write(nullptr, range->callback_context, guest_address,
                   register_value);
      return;
    }
  }

  auto host_address = thread_state->memory()->TranslateVirtual(guest_address);
  WriteIntegerValue(host_address, type_name, value, flags);
}

uint64_t Arm64JitLoadMmio(ppc::PPCContext* context,
                          xe::cpu::MMIORange* mmio_range,
                          uint32_t read_address) {
  if (!mmio_range || !mmio_range->read) {
    XELOGE("ARM64 JIT invalid MMIO load at {:08X}", read_address);
    return 0;
  }
  uint32_t value = mmio_range->read(context, mmio_range->callback_context,
                                    read_address);
  return xe::byte_swap(value);
}

void Arm64JitStoreMmio(ppc::PPCContext* context,
                       xe::cpu::MMIORange* mmio_range,
                       uint32_t write_address, uint32_t value) {
  if (!mmio_range || !mmio_range->write) {
    XELOGE("ARM64 JIT invalid MMIO store at {:08X}", write_address);
    return;
  }
  mmio_range->write(context, mmio_range->callback_context, write_address,
                    xe::byte_swap(value));
}

uint64_t Arm64JitAtomicCompareExchange(ThreadState* thread_state,
                                       uint64_t address,
                                       uint64_t expected_value,
                                       uint64_t new_value, uint32_t type) {
  auto type_name = static_cast<hir::TypeName>(type);
  auto host_address = thread_state->memory()->TranslateVirtual(
      NormalizeGuestAddress(address));

  switch (type_name) {
    case hir::INT8_TYPE: {
      auto expected = static_cast<uint8_t>(expected_value);
      auto desired = static_cast<uint8_t>(new_value);
      return reinterpret_cast<std::atomic<uint8_t>*>(host_address)
                 ->compare_exchange_strong(expected, desired)
                 ? 1
                 : 0;
    }
    case hir::INT16_TYPE: {
      auto expected = static_cast<uint16_t>(expected_value);
      auto desired = static_cast<uint16_t>(new_value);
      return reinterpret_cast<std::atomic<uint16_t>*>(host_address)
                 ->compare_exchange_strong(expected, desired)
                 ? 1
                 : 0;
    }
    case hir::INT32_TYPE: {
      auto expected = static_cast<uint32_t>(expected_value);
      auto desired = static_cast<uint32_t>(new_value);
      return reinterpret_cast<std::atomic<uint32_t>*>(host_address)
                 ->compare_exchange_strong(expected, desired)
                 ? 1
                 : 0;
    }
    case hir::INT64_TYPE: {
      auto expected = static_cast<uint64_t>(expected_value);
      auto desired = static_cast<uint64_t>(new_value);
      return reinterpret_cast<std::atomic<uint64_t>*>(host_address)
                 ->compare_exchange_strong(expected, desired)
                 ? 1
                 : 0;
    }
    default:
      return 0;
  }
}

void Arm64JitClearStackSlots(void* stack_slots, uint64_t byte_count) {
  std::memset(stack_slots, 0, static_cast<size_t>(byte_count));
}

void Arm64JitCopy16(void* dst, const void* src) {
  std::memcpy(dst, src, sizeof(vec128_t));
}

void Arm64JitLoadVectorShift(uint64_t shift_value, uint32_t left,
                             void* dst) {
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};
  uint32_t shift = static_cast<uint32_t>(shift_value) & 0xF;
  for (uint32_t i = 0; i < 16; ++i) {
    SetVecU8(output, i,
             static_cast<uint8_t>(left ? (i + shift) : (i + 16 - shift)));
  }
}

void Arm64JitUnpackVec128(void* dst, const void* src, uint32_t flags) {
  const auto& input = *reinterpret_cast<const vec128_t*>(src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};

  switch (flags & hir::PACK_TYPE_MODE) {
    case hir::PACK_TYPE_D3DCOLOR: {
      uint32_t packed = input.u32[3];
      output->u32[0] = 0x3F800000u | ((packed >> 16) & 0xFFu);
      output->u32[1] = 0x3F800000u | ((packed >> 8) & 0xFFu);
      output->u32[2] = 0x3F800000u | (packed & 0xFFu);
      output->u32[3] = 0x3F800000u | ((packed >> 24) & 0xFFu);
      break;
    }
    case hir::PACK_TYPE_FLOAT16_2:
      output->u32[0] = FloatBits(half_float::detail::half2float(
          static_cast<uint16_t>(input.u32[3] >> 16)));
      output->u32[1] = FloatBits(half_float::detail::half2float(
          static_cast<uint16_t>(input.u32[3])));
      output->u32[2] = 0;
      output->u32[3] = 0x3F800000u;
      break;
    case hir::PACK_TYPE_FLOAT16_4:
      for (uint32_t i = 0; i < 4; ++i) {
        output->u32[i] = FloatBits(
            half_float::detail::half2float(GetVecU16(input, 4 + i)));
      }
      break;
    case hir::PACK_TYPE_SHORT_2: {
      uint32_t packed = input.u32[3];
      output->u32[0] =
          0x40400000u + static_cast<int32_t>(static_cast<int16_t>(packed >> 16));
      output->u32[1] =
          0x40400000u + static_cast<int32_t>(static_cast<int16_t>(packed));
      output->u32[2] = 0;
      output->u32[3] = 0x3F800000u;
      break;
    }
    case hir::PACK_TYPE_SHORT_4:
      for (uint32_t i = 0; i < 4; ++i) {
        output->u32[i] =
            0x40400000u + static_cast<int32_t>(GetVecI16(input, 4 + i));
      }
      break;
    case hir::PACK_TYPE_UINT_2101010: {
      uint32_t packed = input.u32[3];
      output->u32[0] =
          0x40400000u + SignExtendBits((packed >> 0) & 0x3FFu, 10);
      output->u32[1] =
          0x40400000u + SignExtendBits((packed >> 10) & 0x3FFu, 10);
      output->u32[2] =
          0x40400000u + SignExtendBits((packed >> 20) & 0x3FFu, 10);
      output->u32[3] = 0x3F800000u + ((packed >> 30) & 0x3u);
      break;
    }
    case hir::PACK_TYPE_8_IN_16: {
      uint32_t base = hir::IsPackToHi(flags) ? 0 : 8;
      for (uint32_t i = 0; i < 8; ++i) {
        SetVecU16(output, i, static_cast<uint16_t>(
                                 static_cast<int16_t>(GetVecI8(input, base + i))));
      }
      break;
    }
    case hir::PACK_TYPE_16_IN_32: {
      uint32_t base = hir::IsPackToHi(flags) ? 0 : 4;
      for (uint32_t i = 0; i < 4; ++i) {
        output->i32[i] = static_cast<int32_t>(GetVecI16(input, base + i));
      }
      break;
    }
    default:
      XELOGE("ARM64 JIT unsupported unpack flags {:04X}", flags);
      break;
  }
}

void Arm64JitVec128Binary(void* dst, const void* lhs_src,
                          const void* rhs_src, uint32_t opcode_value) {
  const auto& lhs = *reinterpret_cast<const vec128_t*>(lhs_src);
  const auto& rhs = *reinterpret_cast<const vec128_t*>(rhs_src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};
  auto opcode = static_cast<hir::Opcode>(opcode_value);

  switch (opcode) {
    case hir::OPCODE_ADD:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = lhs.f32[i] + rhs.f32[i];
      }
      break;
    case hir::OPCODE_SUB:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = lhs.f32[i] - rhs.f32[i];
      }
      break;
    case hir::OPCODE_MUL:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = lhs.f32[i] * rhs.f32[i];
      }
      break;
    case hir::OPCODE_DIV:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = lhs.f32[i] / rhs.f32[i];
      }
      break;
    case hir::OPCODE_AND:
      output->low = lhs.low & rhs.low;
      output->high = lhs.high & rhs.high;
      break;
    case hir::OPCODE_AND_NOT:
      output->low = lhs.low & ~rhs.low;
      output->high = lhs.high & ~rhs.high;
      break;
    case hir::OPCODE_OR:
      output->low = lhs.low | rhs.low;
      output->high = lhs.high | rhs.high;
      break;
    case hir::OPCODE_XOR:
      output->low = lhs.low ^ rhs.low;
      output->high = lhs.high ^ rhs.high;
      break;
    default:
      break;
  }
}

void Arm64JitVec128Ternary(void* dst, const void* lhs_src,
                           const void* rhs_src, const void* third_src,
                           uint32_t opcode_value) {
  const auto& lhs = *reinterpret_cast<const vec128_t*>(lhs_src);
  const auto& rhs = *reinterpret_cast<const vec128_t*>(rhs_src);
  const auto& third = *reinterpret_cast<const vec128_t*>(third_src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};
  auto opcode = static_cast<hir::Opcode>(opcode_value);

  switch (opcode) {
    case hir::OPCODE_MUL_ADD:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = lhs.f32[i] * rhs.f32[i] + third.f32[i];
      }
      break;
    case hir::OPCODE_MUL_SUB:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = lhs.f32[i] * rhs.f32[i] - third.f32[i];
      }
      break;
    default:
      break;
  }
}

void Arm64JitVectorConvertVec128(void* dst, const void* src,
                                 uint32_t arithmetic_flags,
                                 uint32_t opcode_value) {
  const auto& input = *reinterpret_cast<const vec128_t*>(src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};
  auto opcode = static_cast<hir::Opcode>(opcode_value);
  bool is_unsigned = (arithmetic_flags & hir::ARITHMETIC_UNSIGNED) != 0;

  switch (opcode) {
    case hir::OPCODE_VECTOR_CONVERT_I2F:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] =
            is_unsigned
                ? static_cast<float>(input.u32[i])
                : static_cast<float>(static_cast<int32_t>(input.u32[i]));
      }
      break;
    case hir::OPCODE_VECTOR_CONVERT_F2I:
      for (uint32_t i = 0; i < 4; ++i) {
        double value = input.f32[i];
        if (is_unsigned) {
          if (std::isnan(value) || value <= 0.0) {
            output->u32[i] = 0;
          } else if (value >=
                     static_cast<double>(
                         std::numeric_limits<uint32_t>::max())) {
            output->u32[i] = std::numeric_limits<uint32_t>::max();
          } else {
            output->u32[i] = static_cast<uint32_t>(value);
          }
        } else {
          if (std::isnan(value)) {
            output->i32[i] = 0;
          } else if (value >=
                     static_cast<double>(
                         std::numeric_limits<int32_t>::max())) {
            output->i32[i] = std::numeric_limits<int32_t>::max();
          } else if (value <=
                     static_cast<double>(
                         std::numeric_limits<int32_t>::min())) {
            output->i32[i] = std::numeric_limits<int32_t>::min();
          } else {
            output->i32[i] = static_cast<int32_t>(value);
          }
        }
      }
      break;
    default:
      break;
  }
}

void Arm64JitRoundVec128(void* dst, const void* src,
                         uint32_t round_mode_value) {
  const auto& input = *reinterpret_cast<const vec128_t*>(src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = input;
  auto round_mode = static_cast<hir::RoundMode>(round_mode_value);
  for (uint32_t i = 0; i < 4; ++i) {
    output->f32[i] = static_cast<float>(ApplyRoundMode(
        round_mode, static_cast<long double>(input.f32[i])));
  }
}

void Arm64JitVec128Unary(void* dst, const void* src,
                         uint32_t opcode_value) {
  const auto& input = *reinterpret_cast<const vec128_t*>(src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};
  auto opcode = static_cast<hir::Opcode>(opcode_value);

  switch (opcode) {
    case hir::OPCODE_NOT:
      output->low = ~input.low;
      output->high = ~input.high;
      break;
    case hir::OPCODE_NEG:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = -input.f32[i];
      }
      break;
    case hir::OPCODE_ABS:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = std::fabs(input.f32[i]);
      }
      break;
    case hir::OPCODE_SQRT:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = std::sqrt(input.f32[i]);
      }
      break;
    case hir::OPCODE_RSQRT:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = 1.0f / std::sqrt(input.f32[i]);
      }
      break;
    case hir::OPCODE_RECIP:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = 1.0f / input.f32[i];
      }
      break;
    case hir::OPCODE_POW2:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = std::exp2(input.f32[i]);
      }
      break;
    case hir::OPCODE_LOG2:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] = std::log2(input.f32[i]);
      }
      break;
    default:
      break;
  }
}

bool Arm64JitCompareSigned(int64_t lhs, int64_t rhs, hir::Opcode opcode) {
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

bool Arm64JitCompareUnsigned(uint64_t lhs, uint64_t rhs,
                             hir::Opcode opcode) {
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

bool Arm64JitCompareFloat(float lhs, float rhs, hir::Opcode opcode) {
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

void Arm64JitVectorCompareVec128(void* dst, const void* lhs_src,
                                 const void* rhs_src,
                                 uint32_t part_type_value,
                                 uint32_t opcode_value) {
  const auto& lhs = *reinterpret_cast<const vec128_t*>(lhs_src);
  const auto& rhs = *reinterpret_cast<const vec128_t*>(rhs_src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};
  auto part_type = static_cast<hir::TypeName>(part_type_value);
  auto opcode = static_cast<hir::Opcode>(opcode_value);

  switch (part_type) {
    case hir::INT8_TYPE:
      for (uint32_t i = 0; i < 16; ++i) {
        bool result =
            opcode == hir::OPCODE_VECTOR_COMPARE_EQ
                ? GetVecU8(lhs, i) == GetVecU8(rhs, i)
                : opcode == hir::OPCODE_VECTOR_COMPARE_UGT ||
                          opcode == hir::OPCODE_VECTOR_COMPARE_UGE
                      ? Arm64JitCompareUnsigned(GetVecU8(lhs, i),
                                                GetVecU8(rhs, i), opcode)
                      : Arm64JitCompareSigned(GetVecI8(lhs, i),
                                              GetVecI8(rhs, i), opcode);
        SetVecU8(output, i, result ? 0xFF : 0x00);
      }
      break;
    case hir::INT16_TYPE:
      for (uint32_t i = 0; i < 8; ++i) {
        bool result =
            opcode == hir::OPCODE_VECTOR_COMPARE_EQ
                ? GetVecU16(lhs, i) == GetVecU16(rhs, i)
                : opcode == hir::OPCODE_VECTOR_COMPARE_UGT ||
                          opcode == hir::OPCODE_VECTOR_COMPARE_UGE
                      ? Arm64JitCompareUnsigned(GetVecU16(lhs, i),
                                                GetVecU16(rhs, i), opcode)
                      : Arm64JitCompareSigned(GetVecI16(lhs, i),
                                              GetVecI16(rhs, i), opcode);
        SetVecU16(output, i, result ? 0xFFFF : 0x0000);
      }
      break;
    case hir::INT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        bool result =
            opcode == hir::OPCODE_VECTOR_COMPARE_EQ
                ? lhs.u32[i] == rhs.u32[i]
                : opcode == hir::OPCODE_VECTOR_COMPARE_UGT ||
                          opcode == hir::OPCODE_VECTOR_COMPARE_UGE
                      ? Arm64JitCompareUnsigned(lhs.u32[i], rhs.u32[i],
                                                opcode)
                      : Arm64JitCompareSigned(
                            static_cast<int32_t>(lhs.u32[i]),
                            static_cast<int32_t>(rhs.u32[i]), opcode);
        output->u32[i] = result ? 0xFFFFFFFFu : 0;
      }
      break;
    case hir::FLOAT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        output->u32[i] =
            Arm64JitCompareFloat(lhs.f32[i], rhs.f32[i], opcode)
                ? 0xFFFFFFFFu
                : 0;
      }
      break;
    default:
      break;
  }
}

void Arm64JitVectorAddSubVec128(void* dst, const void* lhs_src,
                                const void* rhs_src,
                                uint32_t part_type_value,
                                uint32_t arithmetic_flags, uint32_t subtract) {
  const auto& lhs = *reinterpret_cast<const vec128_t*>(lhs_src);
  const auto& rhs = *reinterpret_cast<const vec128_t*>(rhs_src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};
  auto part_type = static_cast<hir::TypeName>(part_type_value);
  bool is_unsigned = (arithmetic_flags & hir::ARITHMETIC_UNSIGNED) != 0;
  bool saturate = (arithmetic_flags & hir::ARITHMETIC_SATURATE) != 0;

  switch (part_type) {
    case hir::FLOAT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        output->f32[i] =
            subtract ? lhs.f32[i] - rhs.f32[i] : lhs.f32[i] + rhs.f32[i];
      }
      break;
    case hir::FLOAT64_TYPE:
      for (uint32_t i = 0; i < 2; ++i) {
        output->f64[i] =
            subtract ? lhs.f64[i] - rhs.f64[i] : lhs.f64[i] + rhs.f64[i];
      }
      break;
    case hir::INT8_TYPE:
      for (uint32_t i = 0; i < 16; ++i) {
        if (saturate) {
          SetVecU8(
              output, i,
              is_unsigned
                  ? (subtract ? xe::sat_sub(GetVecU8(lhs, i),
                                            GetVecU8(rhs, i))
                              : xe::sat_add(GetVecU8(lhs, i),
                                            GetVecU8(rhs, i)))
                  : static_cast<uint8_t>(
                        subtract ? xe::sat_sub(GetVecI8(lhs, i),
                                               GetVecI8(rhs, i))
                                 : xe::sat_add(GetVecI8(lhs, i),
                                               GetVecI8(rhs, i))));
        } else {
          SetVecU8(output, i,
                   subtract ? static_cast<uint8_t>(GetVecU8(lhs, i) -
                                                   GetVecU8(rhs, i))
                            : static_cast<uint8_t>(GetVecU8(lhs, i) +
                                                   GetVecU8(rhs, i)));
        }
      }
      break;
    case hir::INT16_TYPE:
      for (uint32_t i = 0; i < 8; ++i) {
        if (saturate) {
          SetVecU16(
              output, i,
              is_unsigned
                  ? (subtract ? xe::sat_sub(GetVecU16(lhs, i),
                                            GetVecU16(rhs, i))
                              : xe::sat_add(GetVecU16(lhs, i),
                                            GetVecU16(rhs, i)))
                  : static_cast<uint16_t>(
                        subtract ? xe::sat_sub(GetVecI16(lhs, i),
                                               GetVecI16(rhs, i))
                                 : xe::sat_add(GetVecI16(lhs, i),
                                               GetVecI16(rhs, i))));
        } else {
          SetVecU16(output, i,
                    subtract ? static_cast<uint16_t>(GetVecU16(lhs, i) -
                                                     GetVecU16(rhs, i))
                             : static_cast<uint16_t>(GetVecU16(lhs, i) +
                                                     GetVecU16(rhs, i)));
        }
      }
      break;
    case hir::INT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        if (saturate) {
          output->u32[i] =
              is_unsigned
                  ? (subtract ? xe::sat_sub(lhs.u32[i], rhs.u32[i])
                              : xe::sat_add(lhs.u32[i], rhs.u32[i]))
                  : static_cast<uint32_t>(
                        subtract ? xe::sat_sub(lhs.i32[i], rhs.i32[i])
                                 : xe::sat_add(lhs.i32[i], rhs.i32[i]));
        } else {
          output->u32[i] =
              subtract ? lhs.u32[i] - rhs.u32[i] : lhs.u32[i] + rhs.u32[i];
        }
      }
      break;
    case hir::INT64_TYPE:
      for (uint32_t i = 0; i < 2; ++i) {
        if (saturate) {
          output->u64[i] =
              is_unsigned
                  ? (subtract ? xe::sat_sub(lhs.u64[i], rhs.u64[i])
                              : xe::sat_add(lhs.u64[i], rhs.u64[i]))
                  : static_cast<uint64_t>(
                        subtract ? xe::sat_sub(lhs.i64[i], rhs.i64[i])
                                 : xe::sat_add(lhs.i64[i], rhs.i64[i]));
        } else {
          output->u64[i] =
              subtract ? lhs.u64[i] - rhs.u64[i] : lhs.u64[i] + rhs.u64[i];
        }
      }
      break;
    default:
      break;
  }
}

void Arm64JitSplatVec128(void* dst, uint64_t source_raw,
                         uint32_t source_type_value) {
  auto source_type = static_cast<hir::TypeName>(source_type_value);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};

  switch (source_type) {
    case hir::INT8_TYPE:
      for (uint32_t i = 0; i < 16; ++i) {
        SetVecU8(output, i, static_cast<uint8_t>(source_raw));
      }
      break;
    case hir::INT16_TYPE:
      for (uint32_t i = 0; i < 8; ++i) {
        SetVecU16(output, i, static_cast<uint16_t>(source_raw));
      }
      break;
    case hir::INT32_TYPE:
    case hir::FLOAT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        output->u32[i] = static_cast<uint32_t>(source_raw);
      }
      break;
    case hir::INT64_TYPE:
    case hir::FLOAT64_TYPE:
      output->u64[0] = source_raw;
      output->u64[1] = source_raw;
      break;
    default:
      break;
  }
}

uint64_t Arm64JitExtractVec128(const void* src, uint64_t index_value,
                               uint32_t dest_type_value) {
  const auto& input = *reinterpret_cast<const vec128_t*>(src);
  auto dest_type = static_cast<hir::TypeName>(dest_type_value);
  uint32_t index = static_cast<uint32_t>(index_value);
  switch (dest_type) {
    case hir::INT8_TYPE:
      return GetVecU8(input, index & 0xF);
    case hir::INT16_TYPE:
      return GetVecU16(input, index & 0x7);
    case hir::INT32_TYPE:
      return input.u32[index & 0x3];
    case hir::FLOAT32_TYPE:
      return input.u32[index & 0x3];
    default:
      return 0;
  }
}

void Arm64JitSwizzleVec128(void* dst, const void* src,
                           uint32_t swizzle_mask) {
  const auto& input = *reinterpret_cast<const vec128_t*>(src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};
  for (uint32_t i = 0; i < 4; ++i) {
    output->u32[i] = input.u32[(swizzle_mask >> (i * 2)) & 0x3];
  }
}

void Arm64JitPermuteVec128(void* dst, uint32_t part_type_value,
                           uint32_t control_type_value, uint64_t control_raw,
                           const void* control_vec_src, const void* a_src,
                           const void* b_src) {
  auto part_type = static_cast<hir::TypeName>(part_type_value);
  auto control_type = static_cast<hir::TypeName>(control_type_value);
  const auto& a = *reinterpret_cast<const vec128_t*>(a_src);
  const auto& b = *reinterpret_cast<const vec128_t*>(b_src);
  auto* output = reinterpret_cast<vec128_t*>(dst);
  *output = {};

  if (control_type != hir::VEC128_TYPE) {
    uint32_t mask = static_cast<uint32_t>(control_raw);
    for (uint32_t i = 0; i < 4; ++i) {
      uint32_t index = (mask >> (i * 8)) & 0x3;
      bool select_b = ((mask >> (i * 8 + 2)) & 0x1) != 0;
      output->u32[i] = (select_b ? b : a).u32[index];
    }
    return;
  }

  const auto& c = *reinterpret_cast<const vec128_t*>(control_vec_src);
  switch (part_type) {
    case hir::INT8_TYPE:
      for (uint32_t i = 0; i < 16; ++i) {
        uint8_t index = GetVecU8(c, i) & 0x1F;
        SetVecU8(output, i, GetVecU8(index > 15 ? b : a, index & 0xF));
      }
      break;
    case hir::INT16_TYPE:
      for (uint32_t i = 0; i < 8; ++i) {
        uint16_t index = GetVecU16(c, i) & 0xF;
        SetVecU16(output, i, GetVecU16(index > 7 ? b : a, index & 0x7));
      }
      break;
    case hir::INT32_TYPE:
      for (uint32_t i = 0; i < 4; ++i) {
        uint32_t index = c.u32[i] & 0x7;
        output->u32[i] = (index > 3 ? b : a).u32[index & 0x3];
      }
      break;
    default:
      XELOGE("ARM64 JIT unsupported permute part type {}",
             static_cast<uint32_t>(part_type));
      break;
  }
}

uint64_t Arm64JitDotProduct3Vec128(const void* lhs_src, const void* rhs_src) {
  const auto& lhs = *reinterpret_cast<const vec128_t*>(lhs_src);
  const auto& rhs = *reinterpret_cast<const vec128_t*>(rhs_src);
  float result = (lhs.f32[0] * rhs.f32[0] + lhs.f32[1] * rhs.f32[1]) +
                 (lhs.f32[2] * rhs.f32[2] + 0.0f);
  return FloatBits(result);
}

uint64_t Arm64JitDotProduct4Vec128(const void* lhs_src, const void* rhs_src) {
  const auto& lhs = *reinterpret_cast<const vec128_t*>(lhs_src);
  const auto& rhs = *reinterpret_cast<const vec128_t*>(rhs_src);
  float result = 0.0f;
  for (uint32_t i = 0; i < 4; ++i) {
    result += lhs.f32[i] * rhs.f32[i];
  }
  return FloatBits(result);
}

void Arm64JitMemset(ThreadState* thread_state, uint64_t address,
                   uint64_t fill_value, uint64_t length) {
  LogArm64GuestMemoryRangeWatch(thread_state, 0, 0, address, length,
                                "jit_memset", fill_value);
  std::memset(thread_state->memory()->TranslateVirtual(
                  NormalizeGuestAddress(address)),
              static_cast<uint8_t>(fill_value), static_cast<size_t>(length));
}

uint64_t Arm64JitLoadClock() { return Clock::QueryGuestTickCount(); }

uint64_t Arm64JitRotateLeftInteger(uint64_t value, uint64_t amount,
                                   uint32_t type) {
  auto type_name = static_cast<hir::TypeName>(type);
  switch (type_name) {
    case hir::INT8_TYPE: {
      amount &= 0x7;
      if (!amount) {
        return MaskInteger(type_name, value);
      }
      return xe::rotate_left(static_cast<uint8_t>(value),
                             static_cast<uint8_t>(amount));
    }
    case hir::INT16_TYPE: {
      amount &= 0xF;
      if (!amount) {
        return MaskInteger(type_name, value);
      }
      return xe::rotate_left(static_cast<uint16_t>(value),
                             static_cast<uint8_t>(amount));
    }
    case hir::INT32_TYPE: {
      amount &= 0x1F;
      if (!amount) {
        return MaskInteger(type_name, value);
      }
      return xe::rotate_left(static_cast<uint32_t>(value),
                             static_cast<uint8_t>(amount));
    }
    case hir::INT64_TYPE: {
      amount &= 0x3F;
      if (!amount) {
        return MaskInteger(type_name, value);
      }
      return xe::rotate_left(static_cast<uint64_t>(value),
                             static_cast<uint8_t>(amount));
    }
    default:
      return MaskInteger(type_name, value);
  }
}

bool Arm64JitInvokeHostFunction(Function* function, ThreadState* thread_state) {
  if (!function) {
    return false;
  }

  if (function->behavior() == Function::Behavior::kBuiltin) {
    auto builtin_function = static_cast<BuiltinFunction*>(function);
    if (builtin_function->handler()) {
      return function->Call(thread_state, 0);
    }
    if (cvars::arm64_ignore_undefined_externs) {
      XELOGE("ARM64 JIT undefined builtin call to {:08X} {} ignored",
             function->address(), function->name());
      return true;
    }
    XELOGE("ARM64 JIT missing builtin handler for {:08X} {}",
           function->address(), function->name());
    return false;
  }

  if (function->behavior() == Function::Behavior::kExtern) {
    auto guest_function = static_cast<GuestFunction*>(function);
    if (guest_function->extern_handler()) {
      guest_function->extern_handler()(thread_state->context(),
                                       thread_state->context()->kernel_state);
      return true;
    }
    if (cvars::arm64_ignore_undefined_externs) {
      XELOGE("ARM64 JIT undefined extern call to {:08X} {} ignored",
             function->address(), function->name());
      return true;
    }
    XELOGE("ARM64 JIT missing extern handler for {:08X} {}",
           function->address(), function->name());
    return false;
  }

  return false;
}

bool Arm64JitInvokeGuestFunction(Function* function, ThreadState* thread_state,
                                 uint32_t return_address) {
  if (!function) {
    return false;
  }
  if (function->behavior() == Function::Behavior::kBuiltin ||
      function->behavior() == Function::Behavior::kExtern) {
    return Arm64JitInvokeHostFunction(function, thread_state);
  }
  auto resolved = thread_state->processor()->ResolveFunction(function->address());
  if (!resolved) {
    XELOGE("ARM64 JIT failed to resolve direct call target {:08X}",
           function->address());
    return false;
  }
  return resolved->Call(thread_state, return_address);
}

bool Arm64JitInvokeGuestAddress(ThreadState* thread_state, uint32_t address,
                                uint32_t return_address) {
  auto target = thread_state->processor()->ResolveFunction(address);
  if (!target) {
    XELOGE("ARM64 JIT failed to resolve call target {:08X}", address);
    return false;
  }
  return Arm64JitInvokeGuestFunction(target, thread_state, return_address);
}

uint64_t Arm64JitDivInteger(uint64_t lhs, uint64_t rhs, uint32_t type,
                            uint32_t flags) {
  auto type_name = static_cast<hir::TypeName>(type);
  rhs = MaskInteger(type_name, rhs);
  if (!rhs) {
    return 0;
  }
  if (flags & hir::ARITHMETIC_UNSIGNED) {
    return MaskInteger(type_name, MaskInteger(type_name, lhs) / rhs);
  }
  auto signed_result = SignExtendInteger(type_name, lhs) /
                       SignExtendInteger(type_name, rhs);
  return MaskInteger(type_name, static_cast<uint64_t>(signed_result));
}

uint64_t Arm64JitMulHighInteger(uint64_t lhs, uint64_t rhs, uint32_t type,
                                uint32_t flags) {
  auto type_name = static_cast<hir::TypeName>(type);
  uint32_t bits = static_cast<uint32_t>(hir::GetTypeSize(type_name) * 8);
  if (flags & hir::ARITHMETIC_UNSIGNED) {
    auto product = static_cast<unsigned __int128>(MaskInteger(type_name, lhs)) *
                   MaskInteger(type_name, rhs);
    return MaskInteger(type_name, static_cast<uint64_t>(product >> bits));
  }

  auto product =
      static_cast<__int128>(SignExtendInteger(type_name, lhs)) *
      SignExtendInteger(type_name, rhs);
  return MaskInteger(
      type_name,
      static_cast<uint64_t>(static_cast<unsigned __int128>(product) >> bits));
}

bool ConsumeLogBudget(std::atomic<int>* budget) {
  int value = budget->load();
  while (value > 0) {
    if (budget->compare_exchange_strong(value, value - 1)) {
      return true;
    }
  }
  return false;
}

bool Reject(std::string* reject_reason, const std::string& reason) {
  if (reject_reason) {
    *reject_reason = reason;
  }
  return false;
}

class MiniArm64JitEmitter : public Xbyak_aarch64::CodeGenerator {
 public:
  MiniArm64JitEmitter() : CodeGenerator(kMaxCodeSize) {}

  bool Emit(Arm64Function* function, const Program& program,
            std::string* reject_reason) {
    function_ = function;
    program_ = &program;
    if (!BuildStackLayout(reject_reason)) {
      return false;
    }
    const size_t max_stack_bytes =
        static_cast<size_t>(cvars::arm64_mini_jit_max_stack_bytes);
    if (max_stack_bytes && stack_size_ > max_stack_bytes) {
      return Fail(reject_reason, "stack frame too large for mini JIT");
    }
    if (program.blocks.empty()) {
      return Fail(reject_reason, "no blocks");
    }

    block_labels_.clear();
    block_labels_.reserve(program.blocks.size());
    for (size_t i = 0; i < program.blocks.size(); ++i) {
      block_labels_.push_back(std::make_unique<Xbyak_aarch64::Label>());
    }

    EmitPrologue();

    for (size_t block_index = 0; block_index < program.blocks.size();
         ++block_index) {
      L(*block_labels_[block_index]);
      const auto& block = program.blocks[block_index];
      for (uint32_t i = block.instruction_start; i < block.instruction_end;
           ++i) {
        if (i >= program.instructions.size()) {
          return Fail(reject_reason, "instruction index out of range");
        }
        EmitSourceMapForInstruction(program.instructions[i], i);
        if (!EmitInstruction(program.instructions[i], reject_reason)) {
          return false;
        }
      }
    }

    mov(w0, 1);
    EmitEpilogue();
    ready(Xbyak_aarch64::CodeArray::PROTECT_RW);
    return true;
  }

 private:
  struct StackSlot {
    uint32_t offset = 0;
    uint32_t size = 0;
    hir::TypeName type = hir::INT64_TYPE;
  };

  bool Fail(std::string* reject_reason, const std::string& reason) {
    if (reject_reason) {
      *reject_reason = reason;
    }
    return false;
  }

  bool BuildStackLayout(std::string* reject_reason) {
    value_slots_.clear();
    local_slots_.clear();
    value_slots_.reserve(program_->value_types.size());
    local_slots_.reserve(program_->local_count);

    uint32_t offset = 0;
    auto append_slot = [&](hir::TypeName type,
                           std::vector<StackSlot>* slots) -> bool {
      if (!IsMiniJitSlotType(type)) {
        return false;
      }
      uint32_t align = MiniJitSlotAlign(type);
      offset = static_cast<uint32_t>(xe::round_up(offset, align));
      uint32_t size = MiniJitSlotSize(type);
      slots->push_back({offset, size, type});
      offset += size;
      return true;
    };

    for (auto type : program_->value_types) {
      if (!append_slot(type, &value_slots_)) {
        return Fail(reject_reason, "program has unsupported value slot type " +
                                       std::string(HirTypeName(type)));
      }
    }

    for (uint32_t i = 0; i < program_->local_count; ++i) {
      auto type = i < program_->local_types.size() ? program_->local_types[i]
                                                   : hir::INT64_TYPE;
      if (!append_slot(type, &local_slots_)) {
        return Fail(reject_reason, "program has unsupported local slot type " +
                                       std::string(HirTypeName(type)));
      }
    }

    stack_size_ = xe::round_up(static_cast<size_t>(offset), size_t(16));
    return true;
  }

  uint32_t SlotOffsetForValue(uint32_t ordinal) const {
    return value_slots_[ordinal].offset;
  }

  uint32_t SlotOffsetForLocal(uint32_t local_index) const {
    return local_slots_[local_index].offset;
  }

  WReg AsW(const XReg& reg) const { return WReg(reg.getIdx()); }

  bool CheckIntegerType(hir::TypeName type, std::string* reject_reason,
                        const char* opcode_name) {
    if (!IsIntegerType(type)) {
      return Fail(reject_reason, std::string(opcode_name) +
                                     " has non-integer type " +
                                     HirTypeName(type));
    }
    return true;
  }

  bool CheckRawSlotType(hir::TypeName type, std::string* reject_reason,
                        const char* opcode_name) {
    if (!IsRawSlotType(type)) {
      return Fail(reject_reason, std::string(opcode_name) +
                                     " has unsupported slot type " +
                                     HirTypeName(type));
    }
    return true;
  }

  bool CheckDest(const Instruction& instr, std::string* reject_reason) {
    if (instr.dest_ordinal == UINT32_MAX ||
        instr.dest_ordinal >= program_->value_types.size()) {
      return Fail(reject_reason, "invalid destination value");
    }
    return true;
  }

  bool CheckBlockIndex(uint32_t block_index, std::string* reject_reason) {
    if (block_index >= block_labels_.size()) {
      return Fail(reject_reason, "branch target out of range");
    }
    return true;
  }

  void EmitSourceMapForInstruction(const Instruction& instr,
                                   uint32_t instruction_index) {
    if (!function_ || !instr.source_offset ||
        instr.source_offset == last_source_offset_) {
      return;
    }
    last_source_offset_ = instr.source_offset;
    function_->source_map().push_back(
        {instr.source_offset,
         (instr.block_index << 16) | (instruction_index & 0xFFFF),
         static_cast<uint32_t>(getSize())});
  }

  void EmitAdjustSp(bool subtract) {
    if (!stack_size_) {
      return;
    }
    if (stack_size_ <= 4095) {
      if (subtract) {
        sub(sp, sp, static_cast<uint32_t>(stack_size_));
      } else {
        add(sp, sp, static_cast<uint32_t>(stack_size_));
      }
    } else {
      mov(x17, static_cast<uint64_t>(stack_size_));
      if (subtract) {
        sub(sp, sp, x17, Xbyak_aarch64::UXTX);
      } else {
        add(sp, sp, x17, Xbyak_aarch64::UXTX);
      }
    }
  }

  void EmitPrologue() {
    stp(x29, x30, Xbyak_aarch64::pre_ptr(sp, -16));
    mov(x29, sp);
    stp(x19, x20, Xbyak_aarch64::pre_ptr(sp, -16));
    stp(x21, x22, Xbyak_aarch64::pre_ptr(sp, -16));
    stp(x23, x24, Xbyak_aarch64::pre_ptr(sp, -16));

    mov(x19, x0);  // PPCContext*
    mov(x20, x1);  // ThreadState*
    mov(w22, w2);  // guest return address, reserved for call support
    mov(w23, uint64_t(0));  // guest call return address
    ldr(x24, ptr(x19, static_cast<uint32_t>(
                          offsetof(ppc::PPCContext, virtual_membase))));

    EmitAdjustSp(true);
    mov(x21, sp);
    mov(x0, x21);
    mov(x1, static_cast<uint64_t>(stack_size_));
    EmitCall(reinterpret_cast<void*>(&Arm64JitClearStackSlots));
  }

  void EmitEpilogue() {
    L(epilog_label_);
    EmitAdjustSp(false);
    ldp(x23, x24, Xbyak_aarch64::post_ptr(sp, 16));
    ldp(x21, x22, Xbyak_aarch64::post_ptr(sp, 16));
    ldp(x19, x20, Xbyak_aarch64::post_ptr(sp, 16));
    ldp(x29, x30, Xbyak_aarch64::post_ptr(sp, 16));
    ret();
  }

  void EmitReturnTrue() {
    mov(w0, 1);
    b(epilog_label_);
  }

  void EmitAddOffset(const XReg& dst, const XReg& base, uint64_t offset) {
    if (!offset) {
      if (dst.getIdx() != base.getIdx()) {
        mov(dst, base);
      }
      return;
    }
    if (offset <= 4095) {
      add(dst, base, static_cast<uint32_t>(offset));
      return;
    }
    mov(dst, offset);
    add(dst, base, dst);
  }

  void EmitCall(void* helper) {
    mov(x17, reinterpret_cast<uint64_t>(helper));
    blr(x17);
  }

  void EmitLoadStackSlot(const XReg& dst, uint32_t offset) {
    if (offset <= 32760) {
      ldr(dst, ptr(x21, offset));
      return;
    }
    mov(x16, static_cast<uint64_t>(offset));
    add(x16, x21, x16);
    ldr(dst, ptr(x16));
  }

  void EmitStoreStackSlot(uint32_t offset, const XReg& src) {
    if (offset <= 32760) {
      str(src, ptr(x21, offset));
      return;
    }
    mov(x16, static_cast<uint64_t>(offset));
    add(x16, x21, x16);
    str(src, ptr(x16));
  }

  void EmitStackSlotAddress(const XReg& dst, uint32_t offset) {
    if (!offset) {
      if (dst.getIdx() != x21.getIdx()) {
        mov(dst, x21);
      }
      return;
    }
    if (offset <= 4095) {
      add(dst, x21, offset);
      return;
    }
    mov(dst, static_cast<uint64_t>(offset));
    add(dst, x21, dst);
  }

  bool EmitLoadVec128OperandAddress(const Operand& operand, const XReg& dst,
                                    std::string* reject_reason) {
    if (operand.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "vec128 operand has non-vector type");
    }
    if (operand.kind == Operand::Kind::kValue &&
        operand.value_ordinal < value_slots_.size()) {
      EmitStackSlotAddress(dst, SlotOffsetForValue(operand.value_ordinal));
      return true;
    }
    if (operand.kind == Operand::Kind::kConstant) {
      mov(dst, reinterpret_cast<uint64_t>(&operand.constant.v128));
      return true;
    }
    return Fail(reject_reason, "unsupported vec128 operand kind");
  }

  bool EmitCopyVec128ToDest(const Instruction& instr, const XReg& src_address,
                            std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "vec128 copy has non-vector destination");
    }
    EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
    if (src_address.getIdx() != x1.getIdx()) {
      mov(x1, src_address);
    }
    EmitCall(reinterpret_cast<void*>(&Arm64JitCopy16));
    return true;
  }

  void EmitCopyVec128Addresses(const XReg& dst_address,
                               const XReg& src_address) {
    if (dst_address.getIdx() != x0.getIdx()) {
      mov(x0, dst_address);
    }
    if (src_address.getIdx() != x1.getIdx()) {
      mov(x1, src_address);
    }
    EmitCall(reinterpret_cast<void*>(&Arm64JitCopy16));
  }

  void EmitGuestStoreWatch(const Instruction& instr, hir::TypeName value_type) {
    if (cvars::arm64_guest_store_watch.empty()) {
      return;
    }

    sub(sp, sp, uint32_t(16));
    str(x1, ptr(sp));
    str(x8, ptr(sp, uint32_t(8)));

    mov(x0, x20);
    mov(w1, function_ ? function_->address() : uint64_t(0));
    mov(w2, instr.source_offset);
    ldr(x3, ptr(sp));
    mov(w4, static_cast<uint64_t>(value_type));
    ldr(x5, ptr(sp, uint32_t(8)));
    EmitCall(reinterpret_cast<void*>(&LogArm64GuestStoreWatch));

    ldr(x1, ptr(sp));
    ldr(x8, ptr(sp, uint32_t(8)));
    add(sp, sp, uint32_t(16));
  }

  void EmitMaskValue(const XReg& reg, hir::TypeName type) {
    switch (type) {
      case hir::INT8_TYPE:
        and_(reg, reg, uint64_t(0xFF));
        break;
      case hir::INT16_TYPE:
        and_(reg, reg, uint64_t(0xFFFF));
        break;
      case hir::INT32_TYPE:
      case hir::FLOAT32_TYPE:
        mov(AsW(reg), AsW(reg));
        break;
      case hir::INT64_TYPE:
      case hir::FLOAT64_TYPE:
      default:
        break;
    }
  }

  void EmitSignExtend(const XReg& reg, hir::TypeName type) {
    switch (type) {
      case hir::INT8_TYPE:
        sxtb(reg, AsW(reg));
        break;
      case hir::INT16_TYPE:
        sxth(reg, AsW(reg));
        break;
      case hir::INT32_TYPE:
        sxtw(reg, AsW(reg));
        break;
      case hir::INT64_TYPE:
      default:
        break;
    }
  }

  void EmitComputeMemoryAddress(const XReg& dst, const XReg& guest_address) {
    mov(w9, AsW(guest_address));
    if (xe::memory::allocation_granularity() > 0x1000) {
      lsr(w10, w9, 29);
      cmp(w10, uint32_t(7));
      cset(w10, EQ);
      lsl(w10, w10, 12);
      add(x9, x9, x10);
    }
    add(dst, x24, x9);
  }

  void EmitBranchIfKnownMmio(const XReg& guest_address,
                             Xbyak_aarch64::Label& mmio_label) {
    mov(w10, AsW(guest_address));
    lsr(w10, w10, 16);
    mov(w11, uint32_t(0x7FC8));
    cmp(w10, w11);
    b(EQ, mmio_label);
    mov(w11, uint32_t(0x7FEA));
    cmp(w10, w11);
    b(EQ, mmio_label);
  }

  bool EmitLoadOperand(const Operand& operand, const XReg& dst,
                       std::string* reject_reason) {
    switch (operand.kind) {
      case Operand::Kind::kNone:
        mov(dst, uint64_t(0));
        return true;
      case Operand::Kind::kValue:
        if (operand.value_ordinal >= program_->value_types.size()) {
          return Fail(reject_reason, "value operand out of range");
        }
        EmitLoadStackSlot(dst, SlotOffsetForValue(operand.value_ordinal));
        return true;
      case Operand::Kind::kConstant:
        if (!IsRawSlotType(operand.type)) {
          return Fail(reject_reason, "unsupported constant slot type");
        }
        mov(dst, MaskRawSlot(operand.type, operand.constant.u64));
        return true;
      case Operand::Kind::kOffset:
        mov(dst, operand.offset);
        return true;
      default:
        return Fail(reject_reason, "unsupported operand kind");
    }
  }

  bool EmitStoreValue(const Instruction& instr, const XReg& src,
                      std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (!CheckRawSlotType(instr.dest_type, reject_reason, "store")) {
      return false;
    }
    EmitMaskValue(src, instr.dest_type);
    EmitStoreStackSlot(SlotOffsetForValue(instr.dest_ordinal), src);
    return true;
  }

  bool EmitLoadLocal(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.src1.kind != Operand::Kind::kOffset ||
        instr.src1.offset >= program_->local_count) {
      return Fail(reject_reason, "local load out of range");
    }
    if (instr.dest_type == hir::VEC128_TYPE) {
      EmitStackSlotAddress(x1, SlotOffsetForLocal(static_cast<uint32_t>(
                                   instr.src1.offset)));
      return EmitCopyVec128ToDest(instr, x1, reject_reason);
    }
    if (!CheckRawSlotType(instr.dest_type, reject_reason, "load_local")) {
      return false;
    }
    EmitLoadStackSlot(x8, SlotOffsetForLocal(static_cast<uint32_t>(
                              instr.src1.offset)));
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitStoreLocal(const Instruction& instr, std::string* reject_reason) {
    if (instr.src1.kind != Operand::Kind::kOffset ||
        instr.src1.offset >= program_->local_count) {
      return Fail(reject_reason, "local store out of range");
    }
    if (instr.src2.type == hir::VEC128_TYPE) {
      if (!EmitLoadVec128OperandAddress(instr.src2, x1, reject_reason)) {
        return false;
      }
      EmitStackSlotAddress(x0, SlotOffsetForLocal(static_cast<uint32_t>(
                                   instr.src1.offset)));
      EmitCopyVec128Addresses(x0, x1);
      return true;
    }
    if (!CheckRawSlotType(instr.src2.type, reject_reason, "store_local")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src2, x8, reject_reason)) {
      return false;
    }
    EmitMaskValue(x8, instr.src2.type);
    EmitStoreStackSlot(SlotOffsetForLocal(static_cast<uint32_t>(
                           instr.src1.offset)),
                       x8);
    return true;
  }

  bool EmitLoadContext(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type == hir::VEC128_TYPE) {
      EmitAddOffset(x1, x19, instr.src1.offset);
      return EmitCopyVec128ToDest(instr, x1, reject_reason);
    }
    if (!CheckRawSlotType(instr.dest_type, reject_reason, "load_context")) {
      return false;
    }
    EmitAddOffset(x17, x19, instr.src1.offset);
    switch (instr.dest_type) {
      case hir::INT8_TYPE:
        ldrb(w8, ptr(x17));
        break;
      case hir::INT16_TYPE:
        ldrh(w8, ptr(x17));
        break;
      case hir::INT32_TYPE:
      case hir::FLOAT32_TYPE:
        ldr(w8, ptr(x17));
        break;
      case hir::INT64_TYPE:
      case hir::FLOAT64_TYPE:
        ldr(x8, ptr(x17));
        break;
      default:
        return Fail(reject_reason, "unsupported context load type");
    }
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitStoreContext(const Instruction& instr, std::string* reject_reason) {
    if (instr.src2.type == hir::VEC128_TYPE) {
      if (!EmitLoadVec128OperandAddress(instr.src2, x1, reject_reason)) {
        return false;
      }
      EmitAddOffset(x0, x19, instr.src1.offset);
      EmitCopyVec128Addresses(x0, x1);
      return true;
    }
    if (!CheckRawSlotType(instr.src2.type, reject_reason, "store_context")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src2, x8, reject_reason)) {
      return false;
    }
    EmitAddOffset(x17, x19, instr.src1.offset);
    switch (instr.src2.type) {
      case hir::INT8_TYPE:
        strb(w8, ptr(x17));
        break;
      case hir::INT16_TYPE:
        strh(w8, ptr(x17));
        break;
      case hir::INT32_TYPE:
      case hir::FLOAT32_TYPE:
        str(w8, ptr(x17));
        break;
      case hir::INT64_TYPE:
      case hir::FLOAT64_TYPE:
        str(x8, ptr(x17));
        break;
      default:
        return Fail(reject_reason, "unsupported context store type");
    }
    return true;
  }

  bool EmitLoadMmio(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::INT32_TYPE) {
      return Fail(reject_reason, "load_mmio has unsupported type " +
                                     std::string(HirTypeName(instr.dest_type)));
    }
    mov(x0, x19);
    mov(x1, instr.src1.offset);
    mov(w2, static_cast<uint64_t>(instr.src2.offset));
    EmitCall(reinterpret_cast<void*>(&Arm64JitLoadMmio));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitStoreMmio(const Instruction& instr, std::string* reject_reason) {
    if (!CheckIntegerType(instr.src3.type, reject_reason, "store_mmio")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src3, x3, reject_reason)) {
      return false;
    }
    mov(x0, x19);
    mov(x1, instr.src1.offset);
    mov(w2, static_cast<uint64_t>(instr.src2.offset));
    EmitCall(reinterpret_cast<void*>(&Arm64JitStoreMmio));
    return true;
  }

  bool EmitLoadMemory(const Instruction& instr, bool with_offset,
                      std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type == hir::VEC128_TYPE) {
      if (!EmitLoadOperand(instr.src1, x1, reject_reason)) {
        return false;
      }
      if (with_offset) {
        if (!EmitLoadOperand(instr.src2, x2, reject_reason)) {
          return false;
        }
        EmitSignExtend(x2, instr.src2.type);
        add(x1, x1, x2);
      }
      EmitComputeMemoryAddress(x1, x1);
      return EmitCopyVec128ToDest(instr, x1, reject_reason);
    }
    if (!CheckRawSlotType(instr.dest_type, reject_reason, "load_memory")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x1, reject_reason)) {
      return false;
    }
    if (with_offset) {
      if (!EmitLoadOperand(instr.src2, x2, reject_reason)) {
        return false;
      }
      EmitSignExtend(x2, instr.src2.type);
      add(x1, x1, x2);
    }

    Xbyak_aarch64::Label helper_path;
    Xbyak_aarch64::Label done;
    EmitBranchIfKnownMmio(x1, helper_path);

    EmitComputeMemoryAddress(x17, x1);
    switch (instr.dest_type) {
      case hir::INT8_TYPE:
        ldrb(w8, ptr(x17));
        break;
      case hir::INT16_TYPE:
        ldrh(w8, ptr(x17));
        if (instr.flags & hir::LOAD_STORE_BYTE_SWAP) {
          rev16(w8, w8);
        }
        break;
      case hir::INT32_TYPE:
      case hir::FLOAT32_TYPE:
        ldr(w8, ptr(x17));
        if (instr.flags & hir::LOAD_STORE_BYTE_SWAP) {
          rev(w8, w8);
        }
        break;
      case hir::INT64_TYPE:
      case hir::FLOAT64_TYPE:
        ldr(x8, ptr(x17));
        if (instr.flags & hir::LOAD_STORE_BYTE_SWAP) {
          rev(x8, x8);
        }
        break;
      default:
        return Fail(reject_reason, "unsupported memory load type");
    }
    b(done);

    L(helper_path);
    mov(x0, x20);
    mov(w2, static_cast<uint64_t>(instr.dest_type));
    mov(w3, static_cast<uint64_t>(instr.flags));
    EmitCall(reinterpret_cast<void*>(&Arm64JitLoadInteger));
    mov(x8, x0);

    L(done);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitStoreMemory(const Instruction& instr, bool with_offset,
                       std::string* reject_reason) {
    auto value_type = with_offset ? instr.src3.type : instr.src2.type;
    if (value_type == hir::VEC128_TYPE) {
      if (!EmitLoadOperand(instr.src1, x1, reject_reason)) {
        return false;
      }
      if (with_offset) {
        if (!EmitLoadOperand(instr.src2, x2, reject_reason)) {
          return false;
        }
        EmitSignExtend(x2, instr.src2.type);
        add(x1, x1, x2);
      }
      EmitComputeMemoryAddress(x0, x1);
      if (!EmitLoadVec128OperandAddress(with_offset ? instr.src3 : instr.src2,
                                        x1, reject_reason)) {
        return false;
      }
      EmitCopyVec128Addresses(x0, x1);
      return true;
    }
    if (!CheckRawSlotType(with_offset ? instr.src3.type : instr.src2.type,
                          reject_reason, "store_memory")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x1, reject_reason)) {
      return false;
    }
    if (with_offset) {
      if (!EmitLoadOperand(instr.src2, x2, reject_reason)) {
        return false;
      }
      EmitSignExtend(x2, instr.src2.type);
      add(x1, x1, x2);
      if (!EmitLoadOperand(instr.src3, x2, reject_reason)) {
        return false;
      }
      mov(w3, static_cast<uint64_t>(instr.src3.type));
    } else {
      if (!EmitLoadOperand(instr.src2, x2, reject_reason)) {
        return false;
      }
      mov(w3, static_cast<uint64_t>(instr.src2.type));
    }

    Xbyak_aarch64::Label helper_path;
    Xbyak_aarch64::Label done;
    EmitBranchIfKnownMmio(x1, helper_path);

    mov(x8, x2);
    if (instr.flags & hir::LOAD_STORE_BYTE_SWAP) {
      switch (value_type) {
        case hir::INT16_TYPE:
          rev16(w8, w8);
          break;
        case hir::INT32_TYPE:
        case hir::FLOAT32_TYPE:
          rev(w8, w8);
          break;
        case hir::INT64_TYPE:
        case hir::FLOAT64_TYPE:
          rev(x8, x8);
          break;
        case hir::INT8_TYPE:
          break;
        default:
          return Fail(reject_reason, "unsupported swapped memory store type");
      }
    }
    EmitGuestStoreWatch(instr, value_type);
    EmitComputeMemoryAddress(x17, x1);
    switch (value_type) {
      case hir::INT8_TYPE:
        strb(w8, ptr(x17));
        break;
      case hir::INT16_TYPE:
        strh(w8, ptr(x17));
        break;
      case hir::INT32_TYPE:
      case hir::FLOAT32_TYPE:
        str(w8, ptr(x17));
        break;
      case hir::INT64_TYPE:
      case hir::FLOAT64_TYPE:
        str(x8, ptr(x17));
        break;
      default:
        return Fail(reject_reason, "unsupported memory store type");
    }
    b(done);

    L(helper_path);
    mov(x0, x20);
    mov(w4, static_cast<uint64_t>(instr.flags));
    EmitCall(reinterpret_cast<void*>(&Arm64JitStoreInteger));
    L(done);
    return true;
  }

  bool EmitUnaryAssign(const Instruction& instr, bool sign_extend,
                       std::string* reject_reason) {
    if (!sign_extend && instr.dest_type == hir::VEC128_TYPE &&
        instr.src1.type == hir::VEC128_TYPE) {
      if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
        return false;
      }
      return EmitCopyVec128ToDest(instr, x1, reject_reason);
    }
    const bool is_raw_copy_opcode =
        instr.opcode == hir::OPCODE_ASSIGN ||
        instr.opcode == hir::OPCODE_CAST ||
        instr.opcode == hir::OPCODE_TRUNCATE;
    if (is_raw_copy_opcode && !sign_extend) {
      if (!IsRawSlotType(instr.dest_type) || !IsRawSlotType(instr.src1.type)) {
        return Fail(reject_reason,
                    "assign has unsupported slot type src " +
                        std::string(HirTypeName(instr.src1.type)) + " dst " +
                        HirTypeName(instr.dest_type));
      }
      if (instr.dest_type != instr.src1.type) {
        if (!EmitLoadOperand(instr.src1, x0, reject_reason)) {
          return false;
        }
        mov(w1, static_cast<uint64_t>(instr.src1.type));
        mov(w2, static_cast<uint64_t>(instr.dest_type));
        EmitCall(reinterpret_cast<void*>(&Arm64JitCastRaw));
        mov(x8, x0);
        return EmitStoreValue(instr, x8, reject_reason);
      }
    } else {
      if (!CheckIntegerType(instr.dest_type, reject_reason, "assign") ||
          !CheckIntegerType(instr.src1.type, reject_reason, "assign")) {
        return false;
      }
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
      return false;
    }
    if (sign_extend) {
      EmitSignExtend(x8, instr.src1.type);
    }
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitConvert(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (!CheckRawSlotType(instr.dest_type, reject_reason, "convert") ||
        !CheckRawSlotType(instr.src1.type, reject_reason, "convert")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x0, reject_reason)) {
      return false;
    }
    mov(w1, static_cast<uint64_t>(instr.src1.type));
    mov(w2, static_cast<uint64_t>(instr.dest_type));
    mov(w3, static_cast<uint64_t>(instr.flags));
    EmitCall(reinterpret_cast<void*>(&Arm64JitConvertRaw));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitRound(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type == hir::VEC128_TYPE &&
        instr.src1.type == hir::VEC128_TYPE) {
      EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
      if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
        return false;
      }
      mov(w2, static_cast<uint64_t>(instr.flags));
      EmitCall(reinterpret_cast<void*>(&Arm64JitRoundVec128));
      return true;
    }
    if (!CheckRawSlotType(instr.dest_type, reject_reason, "round") ||
        !CheckRawSlotType(instr.src1.type, reject_reason, "round")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x0, reject_reason)) {
      return false;
    }
    mov(w1, static_cast<uint64_t>(instr.dest_type));
    mov(w2, static_cast<uint64_t>(instr.flags));
    EmitCall(reinterpret_cast<void*>(&Arm64JitRoundRaw));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitVectorConvert(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::VEC128_TYPE ||
        instr.src1.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "vector_convert has non-vector slot type");
    }
    EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
    if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
      return false;
    }
    mov(w2, static_cast<uint64_t>(instr.flags));
    mov(w3, static_cast<uint64_t>(instr.opcode));
    EmitCall(reinterpret_cast<void*>(&Arm64JitVectorConvertVec128));
    return true;
  }

  bool EmitVectorCompare(const Instruction& instr,
                         std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::VEC128_TYPE ||
        instr.src1.type != hir::VEC128_TYPE ||
        instr.src2.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "vector_compare has non-vector slot type");
    }
    EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
    if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason) ||
        !EmitLoadVec128OperandAddress(instr.src2, x2, reject_reason)) {
      return false;
    }
    mov(w3, static_cast<uint64_t>(instr.flags));
    mov(w4, static_cast<uint64_t>(instr.opcode));
    EmitCall(reinterpret_cast<void*>(&Arm64JitVectorCompareVec128));
    return true;
  }

  bool EmitCompare(const Instruction& instr, Cond cond, bool sign_extend,
                   std::string* reject_reason) {
    if (!CheckIntegerType(instr.dest_type, reject_reason, "compare")) {
      return false;
    }
    if ((instr.src1.type == hir::FLOAT32_TYPE ||
         instr.src1.type == hir::FLOAT64_TYPE) &&
        instr.src2.type == instr.src1.type) {
      if (!EmitLoadOperand(instr.src1, x0, reject_reason) ||
          !EmitLoadOperand(instr.src2, x1, reject_reason)) {
        return false;
      }
      mov(w2, static_cast<uint64_t>(instr.src1.type));
      mov(w3, static_cast<uint64_t>(instr.opcode));
      EmitCall(reinterpret_cast<void*>(&Arm64JitCompareFloatRaw));
      mov(x8, x0);
      return EmitStoreValue(instr, x8, reject_reason);
    }
    if (!CheckIntegerType(instr.src1.type, reject_reason, "compare") ||
        !CheckIntegerType(instr.src2.type, reject_reason, "compare")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason) ||
        !EmitLoadOperand(instr.src2, x9, reject_reason)) {
      return false;
    }
    if (sign_extend) {
      EmitSignExtend(x8, instr.src1.type);
      EmitSignExtend(x9, instr.src2.type);
    }
    cmp(x8, x9);
    cset(w8, cond);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitIsNan(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (!CheckIntegerType(instr.dest_type, reject_reason, "is_nan") ||
        !CheckRawSlotType(instr.src1.type, reject_reason, "is_nan")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x0, reject_reason)) {
      return false;
    }
    mov(w1, static_cast<uint64_t>(instr.src1.type));
    EmitCall(reinterpret_cast<void*>(&Arm64JitIsNanRaw));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitBinaryArithmetic(const Instruction& instr, hir::Opcode opcode,
                            std::string* reject_reason) {
    if (instr.dest_type == hir::VEC128_TYPE &&
        instr.src1.type == hir::VEC128_TYPE &&
        instr.src2.type == hir::VEC128_TYPE) {
      if (!CheckDest(instr, reject_reason)) {
        return false;
      }
      EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
      if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason) ||
          !EmitLoadVec128OperandAddress(instr.src2, x2, reject_reason)) {
        return false;
      }
      mov(w3, static_cast<uint64_t>(opcode));
      EmitCall(reinterpret_cast<void*>(&Arm64JitVec128Binary));
      return true;
    }

    if ((instr.dest_type == hir::FLOAT32_TYPE ||
         instr.dest_type == hir::FLOAT64_TYPE) &&
        instr.src1.type == instr.dest_type &&
        instr.src2.type == instr.dest_type) {
      if (opcode != hir::OPCODE_ADD && opcode != hir::OPCODE_SUB &&
          opcode != hir::OPCODE_MUL) {
        return Fail(reject_reason, "unsupported float binary opcode");
      }
      if (!EmitLoadOperand(instr.src1, x0, reject_reason) ||
          !EmitLoadOperand(instr.src2, x1, reject_reason)) {
        return false;
      }
      mov(w2, static_cast<uint64_t>(instr.dest_type));
      mov(w3, static_cast<uint64_t>(opcode));
      EmitCall(reinterpret_cast<void*>(&Arm64JitFloatBinaryRaw));
      mov(x8, x0);
      return EmitStoreValue(instr, x8, reject_reason);
    }

    if (!CheckIntegerType(instr.dest_type, reject_reason, "binary")) {
      return false;
    }
    if (!CheckIntegerType(instr.src1.type, reject_reason, "binary") ||
        !CheckIntegerType(instr.src2.type, reject_reason, "binary")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason) ||
        !EmitLoadOperand(instr.src2, x9, reject_reason)) {
      return false;
    }

    switch (opcode) {
      case hir::OPCODE_ADD:
        add(x8, x8, x9);
        break;
      case hir::OPCODE_SUB:
        sub(x8, x8, x9);
        break;
      case hir::OPCODE_MUL:
        mul(x8, x8, x9);
        break;
      case hir::OPCODE_AND:
        and_(x8, x8, x9);
        break;
      case hir::OPCODE_AND_NOT:
        bic(x8, x8, x9);
        break;
      case hir::OPCODE_OR:
        orr(x8, x8, x9);
        break;
      case hir::OPCODE_XOR:
        eor(x8, x8, x9);
        break;
      case hir::OPCODE_SHL:
        and_(x9, x9, uint64_t(0x3F));
        lsl(x8, x8, x9);
        break;
      case hir::OPCODE_SHR:
        and_(x9, x9, uint64_t(0x3F));
        lsr(x8, x8, x9);
        break;
      case hir::OPCODE_SHA:
        EmitSignExtend(x8, instr.src1.type);
        and_(x9, x9, uint64_t(0x3F));
        asr(x8, x8, x9);
        break;
      default:
        return Fail(reject_reason, "unsupported binary opcode");
    }

    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitVectorAddSub(const Instruction& instr, bool subtract,
                        std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::VEC128_TYPE ||
        instr.src1.type != hir::VEC128_TYPE ||
        instr.src2.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "vector_add/sub has non-vector slot type");
    }
    EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
    if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason) ||
        !EmitLoadVec128OperandAddress(instr.src2, x2, reject_reason)) {
      return false;
    }
    mov(w3, static_cast<uint64_t>(instr.flags & 0xFF));
    mov(w4, static_cast<uint64_t>(instr.flags >> 8));
    mov(w5, subtract ? uint64_t(1) : uint64_t(0));
    EmitCall(reinterpret_cast<void*>(&Arm64JitVectorAddSubVec128));
    return true;
  }

  bool EmitAddCarry(const Instruction& instr, std::string* reject_reason) {
    if (!CheckIntegerType(instr.dest_type, reject_reason, "add_carry")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason) ||
        !EmitLoadOperand(instr.src2, x9, reject_reason) ||
        !EmitLoadOperand(instr.src3, x10, reject_reason)) {
      return false;
    }
    add(x8, x8, x9);
    cmp(x10, uint32_t(0));
    cset(w10, NE);
    add(x8, x8, x10);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitDivOrMulHigh(const Instruction& instr, bool high,
                        std::string* reject_reason) {
    if (!high &&
        (instr.dest_type == hir::FLOAT32_TYPE ||
         instr.dest_type == hir::FLOAT64_TYPE) &&
        instr.src1.type == instr.dest_type &&
        instr.src2.type == instr.dest_type) {
      if (!EmitLoadOperand(instr.src1, x0, reject_reason) ||
          !EmitLoadOperand(instr.src2, x1, reject_reason)) {
        return false;
      }
      mov(w2, static_cast<uint64_t>(instr.dest_type));
      mov(w3, static_cast<uint64_t>(hir::OPCODE_DIV));
      EmitCall(reinterpret_cast<void*>(&Arm64JitFloatBinaryRaw));
      mov(x8, x0);
      return EmitStoreValue(instr, x8, reject_reason);
    }

    if (!CheckIntegerType(instr.dest_type, reject_reason, "div/mul_hi")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x0, reject_reason) ||
        !EmitLoadOperand(instr.src2, x1, reject_reason)) {
      return false;
    }
    mov(w2, static_cast<uint64_t>(instr.dest_type));
    mov(w3, static_cast<uint64_t>(instr.flags));
    EmitCall(high ? reinterpret_cast<void*>(&Arm64JitMulHighInteger)
                  : reinterpret_cast<void*>(&Arm64JitDivInteger));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitMulAddSub(const Instruction& instr, std::string* reject_reason) {
    if (instr.dest_type == hir::VEC128_TYPE &&
        instr.src1.type == hir::VEC128_TYPE &&
        instr.src2.type == hir::VEC128_TYPE &&
        instr.src3.type == hir::VEC128_TYPE) {
      if (!CheckDest(instr, reject_reason)) {
        return false;
      }
      EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
      if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason) ||
          !EmitLoadVec128OperandAddress(instr.src2, x2, reject_reason) ||
          !EmitLoadVec128OperandAddress(instr.src3, x3, reject_reason)) {
        return false;
      }
      mov(w4, static_cast<uint64_t>(instr.opcode));
      EmitCall(reinterpret_cast<void*>(&Arm64JitVec128Ternary));
      return true;
    }

    if ((instr.dest_type != hir::FLOAT32_TYPE &&
         instr.dest_type != hir::FLOAT64_TYPE) ||
        instr.src1.type != instr.dest_type ||
        instr.src2.type != instr.dest_type ||
        instr.src3.type != instr.dest_type) {
      return Fail(reject_reason,
                  "mul_add/sub has unsupported slot type " +
                      std::string(HirTypeName(instr.dest_type)));
    }
    if (!EmitLoadOperand(instr.src1, x0, reject_reason) ||
        !EmitLoadOperand(instr.src2, x1, reject_reason) ||
        !EmitLoadOperand(instr.src3, x2, reject_reason)) {
      return false;
    }
    mov(w3, static_cast<uint64_t>(instr.dest_type));
    mov(w4, static_cast<uint64_t>(instr.opcode));
    EmitCall(reinterpret_cast<void*>(&Arm64JitFloatTernaryRaw));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitMinMax(const Instruction& instr, bool maximum,
                  std::string* reject_reason) {
    if (!CheckIntegerType(instr.dest_type, reject_reason, "min/max")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason) ||
        !EmitLoadOperand(instr.src2, x9, reject_reason)) {
      return false;
    }
    EmitSignExtend(x8, instr.src1.type);
    EmitSignExtend(x9, instr.src2.type);
    cmp(x8, x9);
    csel(x8, x8, x9, maximum ? GE : LE);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitNotNeg(const Instruction& instr, bool negate,
                  std::string* reject_reason) {
    if (instr.dest_type == hir::VEC128_TYPE &&
        instr.src1.type == hir::VEC128_TYPE) {
      if (!CheckDest(instr, reject_reason)) {
        return false;
      }
      EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
      if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
        return false;
      }
      mov(w2, static_cast<uint64_t>(negate ? hir::OPCODE_NEG
                                           : hir::OPCODE_NOT));
      EmitCall(reinterpret_cast<void*>(&Arm64JitVec128Unary));
      return true;
    }
    if (negate && (instr.dest_type == hir::FLOAT32_TYPE ||
                   instr.dest_type == hir::FLOAT64_TYPE) &&
        instr.src1.type == instr.dest_type) {
      if (!EmitLoadOperand(instr.src1, x0, reject_reason)) {
        return false;
      }
      mov(w1, static_cast<uint64_t>(instr.dest_type));
      mov(w2, static_cast<uint64_t>(hir::OPCODE_NEG));
      EmitCall(reinterpret_cast<void*>(&Arm64JitUnaryRaw));
      mov(x8, x0);
      return EmitStoreValue(instr, x8, reject_reason);
    }
    if (!CheckIntegerType(instr.dest_type, reject_reason, "not/neg")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
      return false;
    }
    if (negate) {
      neg(x8, x8);
    } else {
      mvn(x8, x8);
    }
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitAbs(const Instruction& instr, std::string* reject_reason) {
    if (instr.dest_type == hir::VEC128_TYPE &&
        instr.src1.type == hir::VEC128_TYPE) {
      if (!CheckDest(instr, reject_reason)) {
        return false;
      }
      EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
      if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
        return false;
      }
      mov(w2, static_cast<uint64_t>(hir::OPCODE_ABS));
      EmitCall(reinterpret_cast<void*>(&Arm64JitVec128Unary));
      return true;
    }
    if (!CheckRawSlotType(instr.dest_type, reject_reason, "abs") ||
        !CheckRawSlotType(instr.src1.type, reject_reason, "abs")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x0, reject_reason)) {
      return false;
    }
    mov(w1, static_cast<uint64_t>(instr.dest_type));
    mov(w2, static_cast<uint64_t>(hir::OPCODE_ABS));
    EmitCall(reinterpret_cast<void*>(&Arm64JitUnaryRaw));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitSqrt(const Instruction& instr, std::string* reject_reason) {
    if (instr.dest_type == hir::VEC128_TYPE &&
        instr.src1.type == hir::VEC128_TYPE) {
      if (!CheckDest(instr, reject_reason)) {
        return false;
      }
      EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
      if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
        return false;
      }
      mov(w2, static_cast<uint64_t>(hir::OPCODE_SQRT));
      EmitCall(reinterpret_cast<void*>(&Arm64JitVec128Unary));
      return true;
    }
    if ((instr.dest_type != hir::FLOAT32_TYPE &&
         instr.dest_type != hir::FLOAT64_TYPE) ||
        instr.src1.type != instr.dest_type) {
      return Fail(reject_reason,
                  "sqrt has unsupported slot type " +
                      std::string(HirTypeName(instr.dest_type)));
    }
    if (!EmitLoadOperand(instr.src1, x0, reject_reason)) {
      return false;
    }
    mov(w1, static_cast<uint64_t>(instr.dest_type));
    mov(w2, static_cast<uint64_t>(hir::OPCODE_SQRT));
    EmitCall(reinterpret_cast<void*>(&Arm64JitUnaryRaw));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitFloatOrVecUnary(const Instruction& instr, hir::Opcode opcode,
                           const char* name, std::string* reject_reason) {
    if (instr.dest_type == hir::VEC128_TYPE &&
        instr.src1.type == hir::VEC128_TYPE) {
      if (!CheckDest(instr, reject_reason)) {
        return false;
      }
      EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
      if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
        return false;
      }
      mov(w2, static_cast<uint64_t>(opcode));
      EmitCall(reinterpret_cast<void*>(&Arm64JitVec128Unary));
      return true;
    }
    if ((instr.dest_type != hir::FLOAT32_TYPE &&
         instr.dest_type != hir::FLOAT64_TYPE) ||
        instr.src1.type != instr.dest_type) {
      return Fail(reject_reason, std::string(name) +
                                     " has unsupported slot type " +
                                     HirTypeName(instr.dest_type));
    }
    if (!EmitLoadOperand(instr.src1, x0, reject_reason)) {
      return false;
    }
    mov(w1, static_cast<uint64_t>(instr.dest_type));
    mov(w2, static_cast<uint64_t>(opcode));
    EmitCall(reinterpret_cast<void*>(&Arm64JitUnaryRaw));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitLoadVectorShift(const Instruction& instr, bool left,
                           std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "load_vector_shift has non-vector dest");
    }
    if (!EmitLoadOperand(instr.src1, x0, reject_reason)) {
      return false;
    }
    mov(w1, left ? uint64_t(1) : uint64_t(0));
    EmitStackSlotAddress(x2, SlotOffsetForValue(instr.dest_ordinal));
    EmitCall(reinterpret_cast<void*>(&Arm64JitLoadVectorShift));
    return true;
  }

  bool EmitUnpack(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::VEC128_TYPE ||
        instr.src1.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "unpack has non-vector slot type");
    }
    EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
    if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
      return false;
    }
    mov(w2, static_cast<uint64_t>(instr.flags));
    EmitCall(reinterpret_cast<void*>(&Arm64JitUnpackVec128));
    return true;
  }

  bool EmitSplat(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::VEC128_TYPE ||
        !IsRawSlotType(instr.src1.type)) {
      return Fail(reject_reason, "splat has unsupported slot type");
    }
    EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
    if (!EmitLoadOperand(instr.src1, x1, reject_reason)) {
      return false;
    }
    mov(w2, static_cast<uint64_t>(instr.src1.type));
    EmitCall(reinterpret_cast<void*>(&Arm64JitSplatVec128));
    return true;
  }

  bool EmitExtract(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.src1.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "extract has non-vector source");
    }
    if (!EmitLoadVec128OperandAddress(instr.src1, x0, reject_reason) ||
        !EmitLoadOperand(instr.src2, x1, reject_reason)) {
      return false;
    }
    mov(w2, static_cast<uint64_t>(instr.dest_type));
    EmitCall(reinterpret_cast<void*>(&Arm64JitExtractVec128));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitSwizzle(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::VEC128_TYPE ||
        instr.src1.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "swizzle has non-vector slot type");
    }
    EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
    if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
      return false;
    }
    mov(w2, static_cast<uint64_t>(instr.src2.offset));
    EmitCall(reinterpret_cast<void*>(&Arm64JitSwizzleVec128));
    return true;
  }

  bool EmitPermute(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::VEC128_TYPE ||
        instr.src2.type != hir::VEC128_TYPE ||
        instr.src3.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "permute has unsupported slot type");
    }
    EmitStackSlotAddress(x0, SlotOffsetForValue(instr.dest_ordinal));
    mov(w1, static_cast<uint64_t>(instr.flags));
    mov(w2, static_cast<uint64_t>(instr.src1.type));
    if (instr.src1.type == hir::VEC128_TYPE) {
      mov(x3, uint64_t(0));
      if (!EmitLoadVec128OperandAddress(instr.src1, x4, reject_reason)) {
        return false;
      }
    } else {
      if (!EmitLoadOperand(instr.src1, x3, reject_reason)) {
        return false;
      }
      mov(x4, uint64_t(0));
    }
    if (!EmitLoadVec128OperandAddress(instr.src2, x5, reject_reason) ||
        !EmitLoadVec128OperandAddress(instr.src3, x6, reject_reason)) {
      return false;
    }
    EmitCall(reinterpret_cast<void*>(&Arm64JitPermuteVec128));
    return true;
  }

  bool EmitDotProduct3(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::FLOAT32_TYPE ||
        instr.src1.type != hir::VEC128_TYPE ||
        instr.src2.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "dot_product_3 has unsupported slot type");
    }
    if (!EmitLoadVec128OperandAddress(instr.src1, x0, reject_reason) ||
        !EmitLoadVec128OperandAddress(instr.src2, x1, reject_reason)) {
      return false;
    }
    EmitCall(reinterpret_cast<void*>(&Arm64JitDotProduct3Vec128));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitDotProduct4(const Instruction& instr, std::string* reject_reason) {
    if (!CheckDest(instr, reject_reason)) {
      return false;
    }
    if (instr.dest_type != hir::FLOAT32_TYPE ||
        instr.src1.type != hir::VEC128_TYPE ||
        instr.src2.type != hir::VEC128_TYPE) {
      return Fail(reject_reason, "dot_product_4 has unsupported slot type");
    }
    if (!EmitLoadVec128OperandAddress(instr.src1, x0, reject_reason) ||
        !EmitLoadVec128OperandAddress(instr.src2, x1, reject_reason)) {
      return false;
    }
    EmitCall(reinterpret_cast<void*>(&Arm64JitDotProduct4Vec128));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitByteSwap(const Instruction& instr, std::string* reject_reason) {
    if (instr.dest_type == hir::VEC128_TYPE &&
        instr.src1.type == hir::VEC128_TYPE) {
      if (!EmitLoadVec128OperandAddress(instr.src1, x1, reject_reason)) {
        return false;
      }
      return EmitCopyVec128ToDest(instr, x1, reject_reason);
    }
    if (!CheckIntegerType(instr.dest_type, reject_reason, "byte_swap")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
      return false;
    }
    switch (instr.dest_type) {
      case hir::INT8_TYPE:
        break;
      case hir::INT16_TYPE:
        rev16(w8, w8);
        break;
      case hir::INT32_TYPE:
        rev(w8, w8);
        break;
      case hir::INT64_TYPE:
        rev(x8, x8);
        break;
      default:
        return Fail(reject_reason, "unsupported byte_swap type");
    }
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitRotateLeft(const Instruction& instr, std::string* reject_reason) {
    if (!CheckIntegerType(instr.dest_type, reject_reason, "rotate_left")) {
      return false;
    }
    if (!CheckIntegerType(instr.src1.type, reject_reason, "rotate_left") ||
        !CheckIntegerType(instr.src2.type, reject_reason, "rotate_left")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
      return false;
    }

    if (instr.src2.kind == Operand::Kind::kConstant &&
        (instr.dest_type == hir::INT32_TYPE ||
         instr.dest_type == hir::INT64_TYPE)) {
      if (instr.dest_type == hir::INT32_TYPE) {
        uint32_t amount =
            static_cast<uint32_t>(instr.src2.constant.u64) & 0x1F;
        if (amount) {
          ror(w8, w8, (32 - amount) & 0x1F);
        }
      } else {
        uint32_t amount =
            static_cast<uint32_t>(instr.src2.constant.u64) & 0x3F;
        if (amount) {
          ror(x8, x8, (64 - amount) & 0x3F);
        }
      }
      return EmitStoreValue(instr, x8, reject_reason);
    }

    if (!EmitLoadOperand(instr.src2, x1, reject_reason)) {
      return false;
    }
    mov(x0, x8);
    mov(w2, static_cast<uint64_t>(instr.dest_type));
    EmitCall(reinterpret_cast<void*>(&Arm64JitRotateLeftInteger));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitCountLeadingZeros(const Instruction& instr,
                             std::string* reject_reason) {
    if (!CheckIntegerType(instr.dest_type, reject_reason, "cntlz")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
      return false;
    }
    switch (instr.src1.type) {
      case hir::INT8_TYPE:
        and_(w8, w8, uint64_t(0xFF));
        clz(w8, w8);
        sub(w8, w8, 24);
        break;
      case hir::INT16_TYPE:
        and_(w8, w8, uint64_t(0xFFFF));
        clz(w8, w8);
        sub(w8, w8, 16);
        break;
      case hir::INT32_TYPE:
        clz(w8, w8);
        break;
      case hir::INT64_TYPE:
        clz(x8, x8);
        break;
      default:
        return Fail(reject_reason, "unsupported cntlz type");
    }
    return EmitStoreValue(instr, x8, reject_reason);
  }

  bool EmitSelect(const Instruction& instr, std::string* reject_reason) {
    if (instr.dest_type == hir::VEC128_TYPE &&
        instr.src2.type == hir::VEC128_TYPE &&
        instr.src3.type == hir::VEC128_TYPE) {
      if (!CheckDest(instr, reject_reason)) {
        return false;
      }
      if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
        return false;
      }
      Xbyak_aarch64::Label use_false;
      Xbyak_aarch64::Label done;
      cbz(x8, use_false);
      if (!EmitLoadVec128OperandAddress(instr.src2, x1, reject_reason) ||
          !EmitCopyVec128ToDest(instr, x1, reject_reason)) {
        return false;
      }
      b(done);
      L(use_false);
      if (!EmitLoadVec128OperandAddress(instr.src3, x1, reject_reason) ||
          !EmitCopyVec128ToDest(instr, x1, reject_reason)) {
        return false;
      }
      L(done);
      return true;
    }

    if (!CheckIntegerType(instr.dest_type, reject_reason, "select")) {
      return false;
    }
    if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
      return false;
    }
    Xbyak_aarch64::Label use_false;
    Xbyak_aarch64::Label done;
    cbz(x8, use_false);
    if (!EmitLoadOperand(instr.src2, x8, reject_reason)) {
      return false;
    }
    if (!EmitStoreValue(instr, x8, reject_reason)) {
      return false;
    }
    b(done);
    L(use_false);
    if (!EmitLoadOperand(instr.src3, x8, reject_reason)) {
      return false;
    }
    if (!EmitStoreValue(instr, x8, reject_reason)) {
      return false;
    }
    L(done);
    return true;
  }

  bool EmitMemorySet(const Instruction& instr, std::string* reject_reason) {
    if (!EmitLoadOperand(instr.src1, x1, reject_reason) ||
        !EmitLoadOperand(instr.src2, x2, reject_reason) ||
        !EmitLoadOperand(instr.src3, x3, reject_reason)) {
      return false;
    }
    mov(x0, x20);
    EmitCall(reinterpret_cast<void*>(&Arm64JitMemset));
    return true;
  }

  bool EmitAtomicCompareExchange(const Instruction& instr,
                                 std::string* reject_reason) {
    if (!CheckIntegerType(instr.dest_type, reject_reason,
                          "atomic_compare_exchange") ||
        !CheckIntegerType(instr.src2.type, reject_reason,
                          "atomic_compare_exchange") ||
        !CheckIntegerType(instr.src3.type, reject_reason,
                          "atomic_compare_exchange")) {
      return false;
    }

    if (!EmitLoadOperand(instr.src3, x3, reject_reason)) {
      return false;
    }
    if (instr.src3.type != instr.src2.type) {
      mov(x0, x3);
      mov(w1, static_cast<uint64_t>(instr.src3.type));
      mov(w2, static_cast<uint64_t>(instr.src2.type));
      EmitCall(reinterpret_cast<void*>(&Arm64JitCastRaw));
      mov(x3, x0);
    }
    if (!EmitLoadOperand(instr.src1, x1, reject_reason) ||
        !EmitLoadOperand(instr.src2, x2, reject_reason)) {
      return false;
    }
    mov(x0, x20);
    mov(w4, static_cast<uint64_t>(instr.src2.type));
    EmitCall(reinterpret_cast<void*>(&Arm64JitAtomicCompareExchange));
    mov(x8, x0);
    return EmitStoreValue(instr, x8, reject_reason);
  }

  void EmitCheckCallResult(uint16_t flags) {
    if (flags & hir::CALL_TAIL) {
      b(epilog_label_);
      return;
    }
    Xbyak_aarch64::Label call_ok;
    cbnz(w0, call_ok);
    b(epilog_label_);
    L(call_ok);
  }

  bool EmitSetReturnAddress(const Instruction& instr,
                            std::string* reject_reason) {
    if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
      return false;
    }
    mov(w23, w8);
    return true;
  }

  bool EmitDirectCall(const Instruction& instr, bool conditional,
                      std::string* reject_reason) {
    const Operand& symbol_operand = conditional ? instr.src2 : instr.src1;
    if (symbol_operand.kind != Operand::Kind::kSymbol ||
        !symbol_operand.symbol) {
      return Fail(reject_reason, "direct call missing symbol");
    }

    Xbyak_aarch64::Label skip_call;
    if (conditional) {
      if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
        return false;
      }
      cbz(x8, skip_call);
    }

    mov(x0, reinterpret_cast<uint64_t>(symbol_operand.symbol));
    mov(x1, x20);
    if (instr.flags & hir::CALL_TAIL) {
      mov(w2, w22);
    } else {
      mov(w2, w23);
    }
    EmitCall(reinterpret_cast<void*>(&Arm64JitInvokeGuestFunction));
    EmitCheckCallResult(instr.flags);

    if (conditional) {
      L(skip_call);
    }
    return true;
  }

  bool EmitExternCall(const Instruction& instr, std::string* reject_reason) {
    if (instr.src1.kind != Operand::Kind::kSymbol || !instr.src1.symbol) {
      return Fail(reject_reason, "extern call missing symbol");
    }
    mov(x0, reinterpret_cast<uint64_t>(instr.src1.symbol));
    mov(x1, x20);
    EmitCall(reinterpret_cast<void*>(&Arm64JitInvokeHostFunction));
    EmitCheckCallResult(instr.flags);
    return true;
  }

  bool EmitIndirectCall(const Instruction& instr, bool conditional,
                        std::string* reject_reason) {
    Xbyak_aarch64::Label skip_call;
    const Operand& target_operand = conditional ? instr.src2 : instr.src1;
    if (conditional) {
      if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
        return false;
      }
      cbz(x8, skip_call);
    }

    if (!EmitLoadOperand(target_operand, x8, reject_reason)) {
      return false;
    }

    if (instr.flags & hir::CALL_POSSIBLE_RETURN) {
      Xbyak_aarch64::Label not_return;
      cmp(w8, w22);
      b(NE, not_return);
      EmitReturnTrue();
      L(not_return);
    }

    mov(x0, x20);
    mov(w1, w8);
    if (instr.flags & hir::CALL_TAIL) {
      mov(w2, w22);
    } else {
      mov(w2, w23);
    }
    EmitCall(reinterpret_cast<void*>(&Arm64JitInvokeGuestAddress));
    EmitCheckCallResult(instr.flags);

    if (conditional) {
      L(skip_call);
    }
    return true;
  }

  bool EmitInstruction(const Instruction& instr, std::string* reject_reason) {
    switch (instr.opcode) {
      case hir::OPCODE_COMMENT:
      case hir::OPCODE_NOP:
      case hir::OPCODE_SOURCE_OFFSET:
      case hir::OPCODE_CONTEXT_BARRIER:
      case hir::OPCODE_MEMORY_BARRIER:
      case hir::OPCODE_CACHE_CONTROL:
      case hir::OPCODE_DEBUG_BREAK:
      case hir::OPCODE_TRAP:
      case hir::OPCODE_DEBUG_BREAK_TRUE:
      case hir::OPCODE_TRAP_TRUE:
      case hir::OPCODE_SET_ROUNDING_MODE:
        return true;

      case hir::OPCODE_SET_RETURN_ADDRESS:
        return EmitSetReturnAddress(instr, reject_reason);
      case hir::OPCODE_CALL:
        return EmitDirectCall(instr, false, reject_reason);
      case hir::OPCODE_CALL_TRUE:
        return EmitDirectCall(instr, true, reject_reason);
      case hir::OPCODE_CALL_INDIRECT:
        return EmitIndirectCall(instr, false, reject_reason);
      case hir::OPCODE_CALL_INDIRECT_TRUE:
        return EmitIndirectCall(instr, true, reject_reason);
      case hir::OPCODE_CALL_EXTERN:
        return EmitExternCall(instr, reject_reason);

      case hir::OPCODE_RETURN:
        EmitReturnTrue();
        return true;
      case hir::OPCODE_RETURN_TRUE: {
        if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
          return false;
        }
        Xbyak_aarch64::Label no_return_label;
        cbz(x8, no_return_label);
        EmitReturnTrue();
        L(no_return_label);
        return true;
      }
      case hir::OPCODE_BRANCH:
        if (!CheckBlockIndex(instr.src1.block_index, reject_reason)) {
          return false;
        }
        b(*block_labels_[instr.src1.block_index]);
        return true;
      case hir::OPCODE_BRANCH_TRUE:
        if (!CheckBlockIndex(instr.src2.block_index, reject_reason) ||
            !EmitLoadOperand(instr.src1, x8, reject_reason)) {
          return false;
        }
        cbnz(x8, *block_labels_[instr.src2.block_index]);
        return true;
      case hir::OPCODE_BRANCH_FALSE:
        if (!CheckBlockIndex(instr.src2.block_index, reject_reason) ||
            !EmitLoadOperand(instr.src1, x8, reject_reason)) {
          return false;
        }
        cbz(x8, *block_labels_[instr.src2.block_index]);
        return true;

      case hir::OPCODE_ASSIGN:
      case hir::OPCODE_CAST:
      case hir::OPCODE_TRUNCATE:
      case hir::OPCODE_ZERO_EXTEND:
        return EmitUnaryAssign(instr, false, reject_reason);
      case hir::OPCODE_SIGN_EXTEND:
        return EmitUnaryAssign(instr, true, reject_reason);
      case hir::OPCODE_CONVERT:
        return EmitConvert(instr, reject_reason);
      case hir::OPCODE_ROUND:
        return EmitRound(instr, reject_reason);
      case hir::OPCODE_VECTOR_CONVERT_I2F:
      case hir::OPCODE_VECTOR_CONVERT_F2I:
        return EmitVectorConvert(instr, reject_reason);
      case hir::OPCODE_LOAD_VECTOR_SHL:
        return EmitLoadVectorShift(instr, true, reject_reason);
      case hir::OPCODE_LOAD_VECTOR_SHR:
        return EmitLoadVectorShift(instr, false, reject_reason);

      case hir::OPCODE_LOAD_LOCAL:
        return EmitLoadLocal(instr, reject_reason);
      case hir::OPCODE_STORE_LOCAL:
        return EmitStoreLocal(instr, reject_reason);
      case hir::OPCODE_LOAD_CONTEXT:
        return EmitLoadContext(instr, reject_reason);
      case hir::OPCODE_STORE_CONTEXT:
        return EmitStoreContext(instr, reject_reason);
      case hir::OPCODE_LOAD_MMIO:
        return EmitLoadMmio(instr, reject_reason);
      case hir::OPCODE_STORE_MMIO:
        return EmitStoreMmio(instr, reject_reason);
      case hir::OPCODE_LOAD:
        return EmitLoadMemory(instr, false, reject_reason);
      case hir::OPCODE_LOAD_OFFSET:
        return EmitLoadMemory(instr, true, reject_reason);
      case hir::OPCODE_STORE:
        return EmitStoreMemory(instr, false, reject_reason);
      case hir::OPCODE_STORE_OFFSET:
        return EmitStoreMemory(instr, true, reject_reason);
      case hir::OPCODE_MEMSET:
        return EmitMemorySet(instr, reject_reason);
      case hir::OPCODE_ATOMIC_COMPARE_EXCHANGE:
        return EmitAtomicCompareExchange(instr, reject_reason);

      case hir::OPCODE_LOAD_CLOCK:
        if (!CheckDest(instr, reject_reason)) {
          return false;
        }
        EmitCall(reinterpret_cast<void*>(&Arm64JitLoadClock));
        mov(x8, x0);
        return EmitStoreValue(instr, x8, reject_reason);

      case hir::OPCODE_SELECT:
        return EmitSelect(instr, reject_reason);
      case hir::OPCODE_IS_TRUE:
      case hir::OPCODE_IS_FALSE:
        if (!EmitLoadOperand(instr.src1, x8, reject_reason)) {
          return false;
        }
        cmp(x8, uint32_t(0));
        cset(w8, instr.opcode == hir::OPCODE_IS_TRUE ? NE : EQ);
        return EmitStoreValue(instr, x8, reject_reason);
      case hir::OPCODE_IS_NAN:
        return EmitIsNan(instr, reject_reason);

      case hir::OPCODE_COMPARE_EQ:
        return EmitCompare(instr, EQ, false, reject_reason);
      case hir::OPCODE_COMPARE_NE:
        return EmitCompare(instr, NE, false, reject_reason);
      case hir::OPCODE_COMPARE_SLT:
        return EmitCompare(instr, LT, true, reject_reason);
      case hir::OPCODE_COMPARE_SLE:
        return EmitCompare(instr, LE, true, reject_reason);
      case hir::OPCODE_COMPARE_SGT:
        return EmitCompare(instr, GT, true, reject_reason);
      case hir::OPCODE_COMPARE_SGE:
        return EmitCompare(instr, GE, true, reject_reason);
      case hir::OPCODE_COMPARE_ULT:
        return EmitCompare(instr, LO, false, reject_reason);
      case hir::OPCODE_COMPARE_ULE:
        return EmitCompare(instr, LS, false, reject_reason);
      case hir::OPCODE_COMPARE_UGT:
        return EmitCompare(instr, HI, false, reject_reason);
      case hir::OPCODE_COMPARE_UGE:
        return EmitCompare(instr, HS, false, reject_reason);
      case hir::OPCODE_VECTOR_COMPARE_EQ:
      case hir::OPCODE_VECTOR_COMPARE_SGT:
      case hir::OPCODE_VECTOR_COMPARE_SGE:
      case hir::OPCODE_VECTOR_COMPARE_UGT:
      case hir::OPCODE_VECTOR_COMPARE_UGE:
        return EmitVectorCompare(instr, reject_reason);

      case hir::OPCODE_ADD:
      case hir::OPCODE_SUB:
      case hir::OPCODE_MUL:
      case hir::OPCODE_AND:
      case hir::OPCODE_AND_NOT:
      case hir::OPCODE_OR:
      case hir::OPCODE_XOR:
      case hir::OPCODE_SHL:
      case hir::OPCODE_SHR:
      case hir::OPCODE_SHA:
        return EmitBinaryArithmetic(instr, instr.opcode, reject_reason);
      case hir::OPCODE_VECTOR_ADD:
        return EmitVectorAddSub(instr, false, reject_reason);
      case hir::OPCODE_VECTOR_SUB:
        return EmitVectorAddSub(instr, true, reject_reason);
      case hir::OPCODE_ROTATE_LEFT:
        return EmitRotateLeft(instr, reject_reason);
      case hir::OPCODE_ADD_CARRY:
        return EmitAddCarry(instr, reject_reason);
      case hir::OPCODE_DIV:
        return EmitDivOrMulHigh(instr, false, reject_reason);
      case hir::OPCODE_MUL_HI:
        return EmitDivOrMulHigh(instr, true, reject_reason);
      case hir::OPCODE_MUL_ADD:
      case hir::OPCODE_MUL_SUB:
        return EmitMulAddSub(instr, reject_reason);
      case hir::OPCODE_MAX:
        return EmitMinMax(instr, true, reject_reason);
      case hir::OPCODE_MIN:
        return EmitMinMax(instr, false, reject_reason);
      case hir::OPCODE_NOT:
        return EmitNotNeg(instr, false, reject_reason);
      case hir::OPCODE_NEG:
        return EmitNotNeg(instr, true, reject_reason);
      case hir::OPCODE_ABS:
        return EmitAbs(instr, reject_reason);
      case hir::OPCODE_SQRT:
        return EmitSqrt(instr, reject_reason);
      case hir::OPCODE_RSQRT:
        return EmitFloatOrVecUnary(instr, hir::OPCODE_RSQRT, "rsqrt",
                                   reject_reason);
      case hir::OPCODE_RECIP:
        return EmitFloatOrVecUnary(instr, hir::OPCODE_RECIP, "recip",
                                   reject_reason);
      case hir::OPCODE_POW2:
        return EmitFloatOrVecUnary(instr, hir::OPCODE_POW2, "pow2",
                                   reject_reason);
      case hir::OPCODE_LOG2:
        return EmitFloatOrVecUnary(instr, hir::OPCODE_LOG2, "log2",
                                   reject_reason);
      case hir::OPCODE_BYTE_SWAP:
        return EmitByteSwap(instr, reject_reason);
      case hir::OPCODE_CNTLZ:
        return EmitCountLeadingZeros(instr, reject_reason);
      case hir::OPCODE_DOT_PRODUCT_3:
        return EmitDotProduct3(instr, reject_reason);
      case hir::OPCODE_DOT_PRODUCT_4:
        return EmitDotProduct4(instr, reject_reason);
      case hir::OPCODE_EXTRACT:
        return EmitExtract(instr, reject_reason);
      case hir::OPCODE_SPLAT:
        return EmitSplat(instr, reject_reason);
      case hir::OPCODE_PERMUTE:
        return EmitPermute(instr, reject_reason);
      case hir::OPCODE_SWIZZLE:
        return EmitSwizzle(instr, reject_reason);
      case hir::OPCODE_UNPACK:
        return EmitUnpack(instr, reject_reason);

      default:
        return Fail(reject_reason,
                    "unsupported opcode " +
                        std::to_string(static_cast<uint32_t>(instr.opcode)));
    }
  }

  const Program* program_ = nullptr;
  Arm64Function* function_ = nullptr;
  uint32_t last_source_offset_ = 0;
  size_t stack_size_ = 0;
  std::vector<StackSlot> value_slots_;
  std::vector<StackSlot> local_slots_;
  std::vector<std::unique_ptr<Xbyak_aarch64::Label>> block_labels_;
  Xbyak_aarch64::Label epilog_label_;
};

}  // namespace

bool TryCompileArm64Program(Arm64Backend* backend, Arm64Function* function,
                            const Arm64Function::Program& program,
                            std::string* reject_reason) {
  if (reject_reason) {
    reject_reason->clear();
  }

#if !XE_ARCH_ARM64
  (void)backend;
  (void)function;
  (void)program;
  return false;
#else
  if (!backend || !function || !backend->code_cache()) {
    return Reject(reject_reason, "missing ARM64 backend/code cache");
  }
  if (!cvars::arm64_enable_mini_jit) {
    return false;
  }
  if (GuestAddressMatchesList(function->address(),
                              cvars::arm64_mini_jit_blacklist) ||
      GuestAddressMatchesList(function->address(),
                              cvars::arm64_force_interpreter_guest_ranges)) {
    return Reject(reject_reason,
                  "guest function forced to interpreter by ARM64 cvar");
  }

  MiniArm64JitEmitter emitter;
  std::string local_reject_reason;
  try {
    function->source_map().clear();
    if (!emitter.Emit(function, program, &local_reject_reason)) {
      function->source_map().clear();
      return Reject(reject_reason, local_reject_reason);
    }
  } catch (const Xbyak_aarch64::Error& e) {
    return Reject(reject_reason,
                  std::string("xbyak_aarch64 error: ") + e.what());
  } catch (const std::exception& e) {
    return Reject(reject_reason,
                  std::string("AArch64 mini JIT exception: ") + e.what());
  }

  void* code_address = nullptr;
  if (!backend->code_cache()->PlaceGuestCode(
          function->address(), emitter.getCode(), emitter.getSize(), function,
          &code_address)) {
    return Reject(reject_reason, "failed to place generated ARM64 code");
  }

  function->SetupCompiledProgram(
      reinterpret_cast<Arm64Function::CompiledProgram>(code_address),
      emitter.getSize());

  if (ConsumeLogBudget(&g_jit_compile_log_budget)) {
    XELOGI(
        "ARM64 mini JIT compiled guest {:08X}: {} instructions, {} bytes, {} "
        "stack bytes",
        function->address(), program.instructions.size(), emitter.getSize(),
        EstimateMiniJitStackSize(program));
  }
  return true;
#endif
}

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
