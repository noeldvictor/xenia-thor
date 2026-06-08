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
#include <vector>

#include "xenia/cpu/xex_module.h"

using namespace xe::cpu;

// Unit 2 of the guest C++ exception-dispatch build: validate the sorted-table
// PC->function lookup used by the unwinder. The XEX loader parses the PE
// exception directory into a vector<GuestRuntimeFunction> sorted by func_start
// with end_address = the next entry's func_start (last entry gets a fallback
// span); FindRuntimeFunction maps a guest PC to its containing entry. This is
// pure logic, host-testable without a real XEX (the BIG-ENDIAN on-disk decode +
// FuncStart-VA assumption are validated separately on-device via the load log).

namespace {
XexModule::GuestRuntimeFunction RF(uint32_t start, uint32_t end, bool has_eh) {
  XexModule::GuestRuntimeFunction f;
  f.func_start = start;
  f.end_address = end;
  f.has_exception_handler = has_eh;
  return f;
}
}  // namespace

TEST_CASE("FindRuntimeFunction maps a PC to its sorted entry", "[xex]") {
  // Sorted-by-func_start as the loader builds it; a gap between the 2nd and 3rd
  // functions exercises the [func_start, end_address) upper bound.
  std::vector<XexModule::GuestRuntimeFunction> t = {
      RF(0x82001000, 0x82001100, true),
      RF(0x82001100, 0x82001200, false),
      RF(0x82002000, 0x82012000, true),  // gap precedes this entry
  };

  // Below the first function -> no match.
  REQUIRE(XexModule::FindRuntimeFunction(t, 0x82000FFFu) == nullptr);

  // Start and last byte of the first function resolve to it.
  const auto* a = XexModule::FindRuntimeFunction(t, 0x82001000u);
  REQUIRE(a != nullptr);
  REQUIRE(a->func_start == 0x82001000u);
  REQUIRE(a->has_exception_handler);
  REQUIRE(XexModule::FindRuntimeFunction(t, 0x820010FFu) == a);

  // The adjacent-function boundary lands in the next entry, not the first.
  const auto* b = XexModule::FindRuntimeFunction(t, 0x82001100u);
  REQUIRE(b != nullptr);
  REQUIRE(b->func_start == 0x82001100u);
  REQUIRE_FALSE(b->has_exception_handler);

  // In the gap (>= 2nd end, < 3rd start) -> no match.
  REQUIRE(XexModule::FindRuntimeFunction(t, 0x82001500u) == nullptr);

  // The third function, including its last byte within the fallback span.
  const auto* c = XexModule::FindRuntimeFunction(t, 0x82002000u);
  REQUIRE(c != nullptr);
  REQUIRE(c->func_start == 0x82002000u);
  REQUIRE(XexModule::FindRuntimeFunction(t, 0x82011FFFu) == c);
  // Past the fallback span -> no match.
  REQUIRE(XexModule::FindRuntimeFunction(t, 0x82012000u) == nullptr);

  // Empty table is safe.
  std::vector<XexModule::GuestRuntimeFunction> empty;
  REQUIRE(XexModule::FindRuntimeFunction(empty, 0x82001000u) == nullptr);
}
