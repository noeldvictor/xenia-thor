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
#include <string>

#include "xenia/kernel/xboxkrnl/xboxkrnl_cpp_eh.h"

using xe::kernel::xboxkrnl::AdjustGuestThisPointer;
using xe::kernel::xboxkrnl::DecodeGuestCatchableType;
using xe::kernel::xboxkrnl::DecodeGuestPmd;
using xe::kernel::xboxkrnl::GuestCatchableType;
using xe::kernel::xboxkrnl::GuestHandlerCatchesThrow;
using xe::kernel::xboxkrnl::GuestHandlerType;
using xe::kernel::xboxkrnl::GuestPmd;
using xe::kernel::xboxkrnl::ReadGuestCString;

// Unit 5 of the guest C++ exception-dispatch build: validate catch selection
// (mangled-name type match, catch(...), base-class match) + the PMD this-pointer
// adjustment. Pure logic over the readers; a wrong match here would run the
// wrong catch funclet or bind the wrong object subobject at U6.

namespace {
struct FakeMem {
  std::map<uint32_t, uint32_t> words;  // for GuestBe32Reader (host-endian)
  std::map<uint32_t, uint8_t> bytes;   // for GuestByteReader

  void put_string(uint32_t addr, const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
      bytes[addr + static_cast<uint32_t>(i)] = static_cast<uint8_t>(s[i]);
    }
    bytes[addr + static_cast<uint32_t>(s.size())] = 0;  // NUL
  }
  // Write a TypeDescriptor header at td_ea and its name at td_ea + 8.
  void put_type_descriptor(uint32_t td_ea, const std::string& name) {
    words[td_ea + 0x00u] = 0xC0DEC0DEu;  // vftable (unused by the matcher)
    words[td_ea + 0x04u] = 0u;           // spare
    put_string(td_ea + 0x08u, name);
  }
  // Write a CatchableType (0x1C) at ct_ea referencing type descriptor td_ea.
  void put_catchable(uint32_t ct_ea, uint32_t td_ea, int32_t mdisp,
                     int32_t pdisp, int32_t vdisp) {
    words[ct_ea + 0x00u] = 1u;       // properties
    words[ct_ea + 0x04u] = td_ea;    // type descriptor
    words[ct_ea + 0x08u] = static_cast<uint32_t>(mdisp);
    words[ct_ea + 0x0Cu] = static_cast<uint32_t>(pdisp);
    words[ct_ea + 0x10u] = static_cast<uint32_t>(vdisp);
    words[ct_ea + 0x14u] = 4u;       // size
    words[ct_ea + 0x18u] = 0u;       // copy function
  }

  std::function<bool(uint32_t, uint32_t&)> word_reader() {
    return [this](uint32_t a, uint32_t& o) -> bool {
      auto it = words.find(a);
      if (it == words.end()) return false;
      o = it->second;
      return true;
    };
  }
  std::function<bool(uint32_t, uint8_t&)> byte_reader() {
    return [this](uint32_t a, uint8_t& o) -> bool {
      auto it = bytes.find(a);
      if (it == bytes.end()) return false;
      o = it->second;
      return true;
    };
  }
};

GuestHandlerType Handler(uint32_t type_descriptor) {
  GuestHandlerType h{};
  h.adjectives = 0;
  h.type_descriptor = type_descriptor;
  h.disp_catch_obj = 0x30;
  h.address_of_handler = 0x82004000u;
  return h;
}

// A throw of Derived (derives from Base): CatchableTypeArray = [Derived, Base].
constexpr uint32_t kThrowInfo = 0x82020000u;
constexpr uint32_t kCta = 0x82020100u;
constexpr uint32_t kCtDerived = 0x82020200u;
constexpr uint32_t kCtBase = 0x82020220u;
constexpr uint32_t kTdDerived = 0x82020300u;
constexpr uint32_t kTdBase = 0x82020340u;
const std::string kNameDerived = ".?AVDerived@@";
const std::string kNameBase = ".?AVBase@@";

FakeMem MakeThrowOfDerived() {
  FakeMem m;
  // ThrowInfo: attributes, unwind, forward_compat, catchable_type_array_ptr.
  m.words[kThrowInfo + 0x00u] = 0u;
  m.words[kThrowInfo + 0x04u] = 0u;
  m.words[kThrowInfo + 0x08u] = 0u;
  m.words[kThrowInfo + 0x0Cu] = kCta;
  // CatchableTypeArray: count then pointers.
  m.words[kCta + 0x00u] = 2u;
  m.words[kCta + 0x04u] = kCtDerived;
  m.words[kCta + 0x08u] = kCtBase;
  // Derived binds with identity; Base sits at +0x10 within Derived.
  m.put_catchable(kCtDerived, kTdDerived, /*mdisp=*/0, /*pdisp=*/-1, 0);
  m.put_catchable(kCtBase, kTdBase, /*mdisp=*/0x10, /*pdisp=*/-1, 0);
  m.put_type_descriptor(kTdDerived, kNameDerived);
  m.put_type_descriptor(kTdBase, kNameBase);
  return m;
}
}  // namespace

TEST_CASE("ReadGuestCString reads a mangled name and stops at NUL",
          "[guest-eh]") {
  FakeMem m;
  m.put_string(0x82030000u, kNameBase);
  REQUIRE(ReadGuestCString(m.byte_reader(), 0x82030000u) == kNameBase);
  // Unreadable start -> empty.
  REQUIRE(ReadGuestCString(m.byte_reader(), 0x82039999u).empty());
}

TEST_CASE("GuestHandlerCatchesThrow matches exact, base, and catch-all",
          "[guest-eh]") {
  FakeMem m = MakeThrowOfDerived();
  auto rw = m.word_reader();
  auto rb = m.byte_reader();

  // A TypeDescriptor for the catch clauses (reuse the throw's descriptors).
  GuestPmd pmd{};
  bool catch_all = false;

  // catch (Derived&) -> exact match, identity PMD.
  REQUIRE(GuestHandlerCatchesThrow(rw, rb, Handler(kTdDerived), kThrowInfo, &pmd,
                                   &catch_all));
  REQUIRE_FALSE(catch_all);
  REQUIRE(pmd.mdisp == 0);

  // catch (Base&) -> base-class match, PMD adjusts by +0x10.
  REQUIRE(GuestHandlerCatchesThrow(rw, rb, Handler(kTdBase), kThrowInfo, &pmd,
                                   &catch_all));
  REQUIRE_FALSE(catch_all);
  REQUIRE(pmd.mdisp == 0x10);

  // catch (Unrelated&) -> no match.
  FakeMem m2 = MakeThrowOfDerived();
  m2.put_type_descriptor(0x82021000u, ".?AVUnrelated@@");
  REQUIRE_FALSE(GuestHandlerCatchesThrow(m2.word_reader(), m2.byte_reader(),
                                         Handler(0x82021000u), kThrowInfo, &pmd,
                                         &catch_all));

  // catch (...) -> always matches, identity PMD.
  REQUIRE(GuestHandlerCatchesThrow(rw, rb, Handler(0u), kThrowInfo, &pmd,
                                   &catch_all));
  REQUIRE(catch_all);
  REQUIRE(pmd.mdisp == 0);
  REQUIRE(pmd.pdisp == -1);
}

TEST_CASE("AdjustGuestThisPointer handles non-virtual and virtual bases",
          "[guest-eh]") {
  FakeMem m;
  auto rw = m.word_reader();

  // Non-virtual: base + mdisp.
  GuestPmd nv{/*mdisp=*/0x10, /*pdisp=*/-1, /*vdisp=*/0};
  REQUIRE(AdjustGuestThisPointer(rw, 0x40000000u, nv) == 0x40000010u);

  // Virtual base: vbtable ptr at base + pdisp; the offset is the vbtable entry
  // at vbtable + vdisp; result = base + pdisp + entry + mdisp.
  const uint32_t base = 0x40000000u;
  const uint32_t vbtable = 0x40001000u;
  m.words[base + 0x20u] = vbtable;   // vbtable pointer at base + pdisp(0x20)
  m.words[vbtable + 0x04u] = 0x08u;  // entry at vbtable + vdisp(4) = +8
  GuestPmd v{/*mdisp=*/0x4, /*pdisp=*/0x20, /*vdisp=*/0x4};
  REQUIRE(AdjustGuestThisPointer(rw, base, v) ==
          base + 0x20u + 0x08u + 0x04u);
}

TEST_CASE("DecodeGuestPmd and DecodeGuestCatchableType field offsets",
          "[guest-eh]") {
  FakeMem m;
  m.put_catchable(0x82022000u, 0x82022100u, /*mdisp=*/0x11, /*pdisp=*/0x22,
                  /*vdisp=*/0x33);
  auto rw = m.word_reader();

  GuestCatchableType ct{};
  REQUIRE(DecodeGuestCatchableType(rw, 0x82022000u, &ct));
  REQUIRE(ct.type_descriptor == 0x82022100u);
  REQUIRE(ct.this_displacement.mdisp == 0x11);
  REQUIRE(ct.this_displacement.pdisp == 0x22);
  REQUIRE(ct.this_displacement.vdisp == 0x33);
  REQUIRE(ct.size_or_offset == 4);

  m.words[0x82023000u] = static_cast<uint32_t>(-5);
  m.words[0x82023004u] = 7u;
  m.words[0x82023008u] = 9u;
  GuestPmd pmd{};
  REQUIRE(DecodeGuestPmd(rw, 0x82023000u, &pmd));
  REQUIRE(pmd.mdisp == -5);
  REQUIRE(pmd.pdisp == 7);
  REQUIRE(pmd.vdisp == 9);
}

#endif  // !XE_ARCH_ARM64
