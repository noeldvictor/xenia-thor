/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// The lean ARM64 (qemu) cpu-test build links xenia-cpu but NOT
// xenia-kernel; this test exercises the xboxkrnl guest stack walker that
// lives there, so it is host-(x64-)only.
#include "xenia/base/platform.h"
#if !XE_ARCH_ARM64

#include "xenia/cpu/testing/util.h"

#include <cstdint>
#include <map>
#include <vector>

#include "xenia/kernel/xboxkrnl/xboxkrnl_cpp_eh.h"

using xe::kernel::xboxkrnl::GuestEhFrame;
using xe::kernel::xboxkrnl::WalkGuestStack;

// Unit 3 of the guest C++ exception-dispatch build: validate the guest PPC
// back-chain walk used to unwind toward a catch handler. Pure logic, host-
// testable via a synthetic stack: a function stores its caller's SP at [sp] and
// its own return address at [caller_sp - 8]. The walk must follow that chain and
// FAIL CLOSED on any corruption (so the dispatcher never longjmps on a bad walk).

namespace {
// Backs read_be32 with a sparse guest-memory map; unmapped addresses read false.
struct FakeMem {
  std::map<uint32_t, uint32_t> words;
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
constexpr uint32_t kStackMin = 0x70000000u;
constexpr uint32_t kStackMax = 0x70010000u;
}  // namespace

TEST_CASE("WalkGuestStack follows the PPC back-chain", "[guest-eh]") {
  // Three frames growing down (sp increases toward the base). For each frame the
  // back-chain at [sp] points to the caller's sp, and the return address into
  // the caller is stored at [caller_sp - 8].
  FakeMem m;
  m.words[0x70001000u] = 0x70002000u;       // f0 back-chain -> f1 sp
  m.words[0x70002000u - 8u] = 0x82001234u;  // return PC into f1
  m.words[0x70002000u] = 0x70003000u;       // f1 back-chain -> f2 sp
  m.words[0x70003000u - 8u] = 0x82005678u;  // return PC into f2
  m.words[0x70003000u] = 0x00000000u;       // f2 back-chain = 0 (top of stack)

  std::vector<GuestEhFrame> frames;
  size_t n = WalkGuestStack(m.reader(), /*start_pc=*/0x82000010u,
                            /*start_sp=*/0x70001000u, kStackMin, kStackMax,
                            /*max_frames=*/16, &frames);
  REQUIRE(n == 3);
  REQUIRE(frames.size() == 3);
  REQUIRE(frames[0].sp == 0x70001000u);
  REQUIRE(frames[0].pc == 0x82000010u);  // the starting (throw) PC
  REQUIRE(frames[1].sp == 0x70002000u);
  REQUIRE(frames[1].pc == 0x82001234u);
  REQUIRE(frames[2].sp == 0x70003000u);
  REQUIRE(frames[2].pc == 0x82005678u);
}

TEST_CASE("WalkGuestStack fails closed on a corrupt chain", "[guest-eh]") {
  // A backward back-chain (points to a lower address) must stop the walk: the
  // stack grows down, so the caller's sp is always higher.
  {
    FakeMem m;
    m.words[0x70005000u] = 0x70004000u;  // points DOWN -> invalid
    std::vector<GuestEhFrame> frames;
    size_t n = WalkGuestStack(m.reader(), 0x82000000u, 0x70005000u, kStackMin,
                              kStackMax, 16, &frames);
    REQUIRE(n == 1);  // only the starting frame survives
  }

  // A back-chain outside [stack_min, stack_max) must stop the walk.
  {
    FakeMem m;
    m.words[0x70001000u] = 0x80000000u;  // outside the stack region
    std::vector<GuestEhFrame> frames;
    size_t n = WalkGuestStack(m.reader(), 0x82000000u, 0x70001000u, kStackMin,
                              kStackMax, 16, &frames);
    REQUIRE(n == 1);
  }

  // A misaligned back-chain must stop the walk.
  {
    FakeMem m;
    m.words[0x70001000u] = 0x70002003u;  // not 8-byte aligned
    std::vector<GuestEhFrame> frames;
    size_t n = WalkGuestStack(m.reader(), 0x82000000u, 0x70001000u, kStackMin,
                              kStackMax, 16, &frames);
    REQUIRE(n == 1);
  }

  // An unreadable back-chain word (empty memory) stops at the starting frame.
  {
    FakeMem m;
    std::vector<GuestEhFrame> frames;
    size_t n = WalkGuestStack(m.reader(), 0x82000000u, 0x70001000u, kStackMin,
                              kStackMax, 16, &frames);
    REQUIRE(n == 1);
  }

  // A readable back-chain whose saved-LR slot is unreadable stops cleanly: the
  // caller frame is NOT appended (we never invent a frame without its return PC).
  {
    FakeMem m;
    m.words[0x70001000u] = 0x70002000u;  // valid back-chain...
    // ...but [0x70002000 - 8] is absent, so the return PC can't be read.
    std::vector<GuestEhFrame> frames;
    size_t n = WalkGuestStack(m.reader(), 0x82000000u, 0x70001000u, kStackMin,
                              kStackMax, 16, &frames);
    REQUIRE(n == 1);
  }
}

TEST_CASE("WalkGuestStack honors max_frames", "[guest-eh]") {
  // A long valid chain truncates at max_frames rather than running unbounded.
  FakeMem m;
  for (uint32_t sp = 0x70001000u; sp < 0x70009000u; sp += 0x1000u) {
    m.words[sp] = sp + 0x1000u;            // back-chain -> next higher frame
    m.words[sp + 0x1000u - 8u] = 0x82000000u + sp;  // return PC into caller
  }
  std::vector<GuestEhFrame> frames;
  size_t n = WalkGuestStack(m.reader(), 0x82000000u, 0x70001000u, kStackMin,
                            kStackMax, /*max_frames=*/4, &frames);
  REQUIRE(n == 4);
}

#endif  // !XE_ARCH_ARM64
