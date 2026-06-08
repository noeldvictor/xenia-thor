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
#include <map>
#include <string>
#include <vector>

#include "xenia/kernel/xboxkrnl/xboxkrnl_cpp_eh.h"

using xe::kernel::xboxkrnl::FindGuestCatchForThrow;
using xe::kernel::xboxkrnl::GuestCatchResolution;
using xe::kernel::xboxkrnl::GuestEhFrame;

// Unit 6 of the guest C++ exception-dispatch build: validate the catch-found
// DECISION (FindGuestCatchForThrow) -- pick the right frame/funclet/establisher,
// adjust the this-pointer, and select the caught-object slot. The on-device
// Sylpheed fires proved parse/walk/decode but its throw is UNCAUGHT, so this
// path (a frame whose function actually has a matching try/catch) was never
// exercised on hardware. Here a synthetic throw-and-catch scenario covers it.

namespace {
struct FakeMem {
  std::map<uint32_t, uint32_t> words;
  std::map<uint32_t, uint8_t> bytes;

  void put_string(uint32_t addr, const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
      bytes[addr + static_cast<uint32_t>(i)] = static_cast<uint8_t>(s[i]);
    }
    bytes[addr + static_cast<uint32_t>(s.size())] = 0;
  }
  void put_type_descriptor(uint32_t td, const std::string& name) {
    words[td + 0x00u] = 0xC0DEC0DEu;
    words[td + 0x04u] = 0u;
    put_string(td + 0x08u, name);
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

// Addresses for the synthetic scenario.
constexpr uint32_t kThrowPc = 0x82000040u;   // in funcA (no catch)
constexpr uint32_t kFuncA = 0x82000000u;
constexpr uint32_t kCatchPc = 0x82001100u;   // in funcB (has the catch)
constexpr uint32_t kFuncB = 0x82001000u;
constexpr uint32_t kSpA = 0x70001000u;
constexpr uint32_t kSpB = 0x70002000u;
constexpr uint32_t kFuncInfoB = 0x82010000u;
constexpr uint32_t kTryMapB = 0x82010100u;
constexpr uint32_t kHandlersB = 0x82010200u;
constexpr uint32_t kTdCatch = 0x82010300u;
constexpr uint32_t kFuncletB = 0x82005000u;
constexpr uint32_t kThrowInfo = 0x82020000u;
constexpr uint32_t kCta = 0x82020100u;
constexpr uint32_t kCtThrow = 0x82020200u;
constexpr uint32_t kTdThrow = 0x82020300u;
constexpr uint32_t kThrownObj = 0x70003000u;
const std::string kName = ".?AVMyExc@@";

// funcB catches `catch_type` by reference; the throw is of `throw_type`.
FakeMem MakeScenario(uint32_t catch_type_descriptor, const std::string& catch_name,
                     const std::string& throw_name) {
  FakeMem m;
  m.words[kFuncB - 4u] = kFuncInfoB;  // func_start-4 -> FuncInfo VA
  // FuncInfo: magic, maxState, unwindMap, nTry, tryMap, nIp, ipMap.
  m.words[kFuncInfoB + 0x00u] = 0x19930522u;
  m.words[kFuncInfoB + 0x04u] = 4u;
  m.words[kFuncInfoB + 0x08u] = 0u;
  m.words[kFuncInfoB + 0x0Cu] = 1u;
  m.words[kFuncInfoB + 0x10u] = kTryMapB;
  m.words[kFuncInfoB + 0x14u] = 0u;
  m.words[kFuncInfoB + 0x18u] = 0u;
  // TryBlockMapEntry: tryLow, tryHigh, catchHigh, nCatches, handlers.
  m.words[kTryMapB + 0x00u] = 0u;
  m.words[kTryMapB + 0x04u] = 2u;
  m.words[kTryMapB + 0x08u] = 3u;
  m.words[kTryMapB + 0x0Cu] = 1u;
  m.words[kTryMapB + 0x10u] = kHandlersB;
  // HandlerType: adjectives (0x08 = by-reference), type, dispCatchObj, funclet.
  m.words[kHandlersB + 0x00u] = 0x08u;
  m.words[kHandlersB + 0x04u] = catch_type_descriptor;
  m.words[kHandlersB + 0x08u] = 0x40u;
  m.words[kHandlersB + 0x0Cu] = kFuncletB;
  if (catch_type_descriptor == kTdCatch) {
    m.put_type_descriptor(kTdCatch, catch_name);
  }
  // ThrowInfo + CatchableTypeArray + CatchableType (identity PMD).
  m.words[kThrowInfo + 0x0Cu] = kCta;
  m.words[kCta + 0x00u] = 1u;
  m.words[kCta + 0x04u] = kCtThrow;
  m.words[kCtThrow + 0x00u] = 0u;       // properties
  m.words[kCtThrow + 0x04u] = kTdThrow;  // type descriptor
  m.words[kCtThrow + 0x08u] = 0u;        // pmd.mdisp
  m.words[kCtThrow + 0x0Cu] = static_cast<uint32_t>(-1);  // pmd.pdisp (non-virtual)
  m.words[kCtThrow + 0x10u] = 0u;        // pmd.vdisp
  m.words[kCtThrow + 0x14u] = 4u;        // size
  m.words[kCtThrow + 0x18u] = 0u;        // copy fn
  m.put_type_descriptor(kTdThrow, throw_name);
  return m;
}

xe::kernel::xboxkrnl::GuestRuntimeFunctionLookup MakeLookup() {
  return [](uint32_t pc, uint32_t& func_start, bool& has_eh) -> bool {
    if (pc == kCatchPc) {
      func_start = kFuncB;
      has_eh = true;
      return true;
    }
    if (pc == kThrowPc) {
      func_start = kFuncA;
      has_eh = false;  // throwing frame has no exception handler
      return true;
    }
    return false;
  };
}

std::vector<GuestEhFrame> MakeFrames() {
  return {GuestEhFrame{kThrowPc, kSpA}, GuestEhFrame{kCatchPc, kSpB}};
}
}  // namespace

TEST_CASE("FindGuestCatchForThrow selects a matching typed catch", "[guest-eh]") {
  FakeMem m = MakeScenario(kTdCatch, kName, kName);  // catch type == throw type
  GuestCatchResolution res =
      FindGuestCatchForThrow(m.word_reader(), m.byte_reader(), MakeLookup(),
                             MakeFrames(), kThrowInfo, kThrownObj);
  REQUIRE(res.found);
  REQUIRE(res.frame_index == 1);  // the catch is in the 2nd (outer) frame
  REQUIRE(res.funclet == kFuncletB);
  REQUIRE(res.establisher == kSpB);
  REQUIRE_FALSE(res.is_catch_all);
  REQUIRE(res.disp_catch_obj == 0x40);
  REQUIRE((res.adjectives & 0x08u) != 0u);  // by-reference
  REQUIRE(res.adjusted_this == kThrownObj);  // identity PMD (pdisp < 0, mdisp 0)
}

TEST_CASE("FindGuestCatchForThrow returns not-found on a type mismatch",
          "[guest-eh]") {
  FakeMem m = MakeScenario(kTdCatch, ".?AVCatchType@@", ".?AVThrowType@@");
  GuestCatchResolution res =
      FindGuestCatchForThrow(m.word_reader(), m.byte_reader(), MakeLookup(),
                             MakeFrames(), kThrowInfo, kThrownObj);
  REQUIRE_FALSE(res.found);
}

TEST_CASE("FindGuestCatchForThrow matches catch(...) regardless of type",
          "[guest-eh]") {
  // type_descriptor == 0 => catch(...).
  FakeMem m = MakeScenario(0u, "", ".?AVAnything@@");
  GuestCatchResolution res =
      FindGuestCatchForThrow(m.word_reader(), m.byte_reader(), MakeLookup(),
                             MakeFrames(), kThrowInfo, kThrownObj);
  REQUIRE(res.found);
  REQUIRE(res.frame_index == 1);
  REQUIRE(res.is_catch_all);
  REQUIRE(res.adjusted_this == kThrownObj);  // catch-all binds the raw object
}

TEST_CASE("FindGuestCatchForThrow returns not-found when no frame has EH",
          "[guest-eh]") {
  FakeMem m = MakeScenario(kTdCatch, kName, kName);
  // A lookup where no frame reports an exception handler.
  auto no_eh = [](uint32_t, uint32_t& fs, bool& eh) -> bool {
    fs = 0;
    eh = false;
    return true;
  };
  GuestCatchResolution res =
      FindGuestCatchForThrow(m.word_reader(), m.byte_reader(), no_eh,
                             MakeFrames(), kThrowInfo, kThrownObj);
  REQUIRE_FALSE(res.found);
}
