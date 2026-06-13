/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// The lean ARM64 (qemu) cpu-test build links xenia-cpu but NOT
// xenia-kernel; these tests exercise xboxkrnl guest-EH helpers that
// live there, so they are host-(x64-)only.
#include "xenia/base/platform.h"
#if !XE_ARCH_ARM64

#include "xenia/cpu/testing/util.h"

#include <cstdint>
#include <map>

#include "xenia/kernel/xboxkrnl/xboxkrnl_cpp_eh.h"

using xe::kernel::xboxkrnl::DecodeGuestFuncInfo;
using xe::kernel::xboxkrnl::DecodeGuestHandlerType;
using xe::kernel::xboxkrnl::DecodeGuestTryBlockMapEntry;
using xe::kernel::xboxkrnl::DecodeGuestTypeDescriptor;
using xe::kernel::xboxkrnl::GuestFuncInfo;
using xe::kernel::xboxkrnl::GuestHandlerType;
using xe::kernel::xboxkrnl::GuestTryBlockMapEntry;
using xe::kernel::xboxkrnl::GuestTypeDescriptor;
using xe::kernel::xboxkrnl::kGuestEhFuncInfoMagic;
using xe::kernel::xboxkrnl::kGuestEhFuncInfoMagicMask;
using xe::kernel::xboxkrnl::ResolveGuestFuncInfoAddr;

// Unit 4 of the guest C++ exception-dispatch build: validate the MSVC EH
// descriptor decode (field offsets / struct sizes / fail-closed). The reader
// already yields host-endian words (the real byte-swap is in the runtime
// load_and_swap wrapper, validated on-device), so this test pins the layout
// logic the unwinder depends on. A wrong offset here would silently send U5/U6
// to the wrong catch funclet.

namespace {
struct FakeMem {
  std::map<uint32_t, uint32_t> words;  // guest addr -> host-endian u32
  std::function<bool(uint32_t, uint32_t&)> reader() {
    return [this](uint32_t addr, uint32_t& out) -> bool {
      auto it = words.find(addr);
      if (it == words.end()) {
        return false;
      }
      out = it->second;
      return true;
    };
  }
};
}  // namespace

TEST_CASE("EH descriptors decode at the right offsets", "[guest-eh]") {
  // A function at 0x82001000 whose FuncInfo VA (0x82010000) is stored at
  // func_start - 4, with one try block, one catch of a typed exception.
  FakeMem m;
  const uint32_t kFunc = 0x82001000u;
  const uint32_t kFuncInfo = 0x82010000u;
  const uint32_t kTryMap = 0x82010100u;
  const uint32_t kHandlers = 0x82010200u;
  const uint32_t kTypeDesc = 0x82010300u;
  const uint32_t kCatchFunclet = 0x82005000u;

  m.words[kFunc - 4u] = kFuncInfo;  // FuncStart-4 holds the FuncInfo VA

  // FuncInfo (0x1C): magic, maxState, pUnwindMap, nTryBlocks, pTryBlockMap,
  // nIPMapEntries, pIPtoStateMap.
  m.words[kFuncInfo + 0x00u] = kGuestEhFuncInfoMagic;
  m.words[kFuncInfo + 0x04u] = 4u;
  m.words[kFuncInfo + 0x08u] = 0x82010400u;
  m.words[kFuncInfo + 0x0Cu] = 1u;
  m.words[kFuncInfo + 0x10u] = kTryMap;
  m.words[kFuncInfo + 0x14u] = 2u;
  m.words[kFuncInfo + 0x18u] = 0x82010500u;

  // TryBlockMapEntry (0x14): tryLow, tryHigh, catchHigh, nCatches, pHandlers.
  m.words[kTryMap + 0x00u] = 0u;
  m.words[kTryMap + 0x04u] = 2u;
  m.words[kTryMap + 0x08u] = 3u;
  m.words[kTryMap + 0x0Cu] = 1u;
  m.words[kTryMap + 0x10u] = kHandlers;

  // HandlerType (0x10): adjectives, pType, dispCatchObj, addressOfHandler.
  m.words[kHandlers + 0x00u] = 0u;
  m.words[kHandlers + 0x04u] = kTypeDesc;
  m.words[kHandlers + 0x08u] = 0x40u;  // caught object at establisher + 0x40
  m.words[kHandlers + 0x0Cu] = kCatchFunclet;

  // TypeDescriptor: vftable, spare, name[] (name at +8).
  m.words[kTypeDesc + 0x00u] = 0x82800000u;
  m.words[kTypeDesc + 0x04u] = 0u;

  auto read = m.reader();

  uint32_t fi_addr = 0;
  REQUIRE(ResolveGuestFuncInfoAddr(read, kFunc, &fi_addr));
  REQUIRE(fi_addr == kFuncInfo);

  GuestFuncInfo fi{};
  REQUIRE(DecodeGuestFuncInfo(read, fi_addr, &fi));
  REQUIRE((fi.magic & kGuestEhFuncInfoMagicMask) == kGuestEhFuncInfoMagic);
  REQUIRE(fi.max_state == 4);
  REQUIRE(fi.unwind_map == 0x82010400u);
  REQUIRE(fi.num_try_blocks == 1u);
  REQUIRE(fi.try_block_map == kTryMap);
  REQUIRE(fi.num_ip_map_entries == 2u);
  REQUIRE(fi.ip_to_state_map == 0x82010500u);

  GuestTryBlockMapEntry tb{};
  REQUIRE(DecodeGuestTryBlockMapEntry(read, fi.try_block_map, &tb));
  REQUIRE(tb.try_low == 0);
  REQUIRE(tb.try_high == 2);
  REQUIRE(tb.catch_high == 3);
  REQUIRE(tb.num_catches == 1);
  REQUIRE(tb.handler_array == kHandlers);

  GuestHandlerType ht{};
  REQUIRE(DecodeGuestHandlerType(read, tb.handler_array, &ht));
  REQUIRE(ht.adjectives == 0u);
  REQUIRE(ht.type_descriptor == kTypeDesc);
  REQUIRE(ht.disp_catch_obj == 0x40);
  REQUIRE(ht.address_of_handler == kCatchFunclet);

  GuestTypeDescriptor td{};
  REQUIRE(DecodeGuestTypeDescriptor(read, ht.type_descriptor, &td));
  REQUIRE(td.vftable == 0x82800000u);
  REQUIRE(td.name_addr == kTypeDesc + 8u);
}

TEST_CASE("EH descriptor decode fails closed on unreadable memory",
          "[guest-eh]") {
  FakeMem empty;
  auto read = empty.reader();

  GuestFuncInfo fi{};
  REQUIRE_FALSE(DecodeGuestFuncInfo(read, 0x82010000u, &fi));
  GuestTryBlockMapEntry tb{};
  REQUIRE_FALSE(DecodeGuestTryBlockMapEntry(read, 0x82010100u, &tb));
  GuestHandlerType ht{};
  REQUIRE_FALSE(DecodeGuestHandlerType(read, 0x82010200u, &ht));
  GuestTypeDescriptor td{};
  REQUIRE_FALSE(DecodeGuestTypeDescriptor(read, 0x82010300u, &td));
  uint32_t fi_addr = 0;
  REQUIRE_FALSE(ResolveGuestFuncInfoAddr(read, 0x82001000u, &fi_addr));

  // A FuncInfo missing only its last word still fails (all-or-nothing).
  FakeMem partial;
  for (uint32_t off = 0; off < 0x18u; off += 4u) {
    partial.words[0x82010000u + off] = 0u;
  }
  auto read2 = partial.reader();
  GuestFuncInfo fi2{};
  REQUIRE_FALSE(DecodeGuestFuncInfo(read2, 0x82010000u, &fi2));
}

#endif  // !XE_ARCH_ARM64
