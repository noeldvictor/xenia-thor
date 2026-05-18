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

constexpr size_t kSlotSize = sizeof(uint64_t);
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
    slot_count_ = program.value_types.size() + program.local_count;
    stack_size_ = xe::round_up(slot_count_ * kSlotSize, size_t(16));
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
  bool Fail(std::string* reject_reason, const std::string& reason) {
    if (reject_reason) {
      *reject_reason = reason;
    }
    return false;
  }

  uint32_t SlotOffsetForValue(uint32_t ordinal) const {
    return ordinal * static_cast<uint32_t>(kSlotSize);
  }

  uint32_t SlotOffsetForLocal(uint32_t local_index) const {
    return static_cast<uint32_t>((program_->value_types.size() + local_index) *
                                 kSlotSize);
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

    auto value_type = with_offset ? instr.src3.type : instr.src2.type;
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

  bool EmitByteSwap(const Instruction& instr, std::string* reject_reason) {
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
      case hir::OPCODE_BYTE_SWAP:
        return EmitByteSwap(instr, reject_reason);
      case hir::OPCODE_CNTLZ:
        return EmitCountLeadingZeros(instr, reject_reason);

      default:
        return Fail(reject_reason,
                    "unsupported opcode " +
                        std::to_string(static_cast<uint32_t>(instr.opcode)));
    }
  }

  const Program* program_ = nullptr;
  Arm64Function* function_ = nullptr;
  uint32_t last_source_offset_ = 0;
  size_t slot_count_ = 0;
  size_t stack_size_ = 0;
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
        xe::round_up((program.value_types.size() + program.local_count) *
                         kSlotSize,
                     size_t(16)));
  }
  return true;
#endif
}

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
