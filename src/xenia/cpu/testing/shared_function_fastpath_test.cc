/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdint>
#include <vector>

#include "xenia/cpu/ppc/ppc_frontend.h"

#include "third_party/catch/include/catch.hpp"

using xe::cpu::ppc::CanonicalFunctionHashRaw;
using xe::cpu::ppc::LookupSharedFunction;
using xe::cpu::ppc::RegisterSharedFunctionForTesting;
using xe::cpu::ppc::SharedFunctionKind;

// Validates the NOVEL part of the shared-function fast-path: the canonical,
// relocation-invariant function hash. The same statically-linked XDK kernel is
// duplicated in every title but relocated, so its b/bl targets differ; the hash
// must be invariant to that (so the kernel is recognized cross-title) yet stay
// sensitive to every other byte (so it never collides a different function onto
// a native handler). The native handlers themselves are byte-trivial; the hash
// is where correctness lives.

namespace {
// Pack host-order PPC instruction words as a big-endian byte buffer (guest code
// is big-endian; CanonicalFunctionHashRaw load_and_swaps each word).
uint64_t Hash(const std::vector<uint32_t>& words) {
  std::vector<uint8_t> buf(words.size() * 4);
  for (size_t i = 0; i < words.size(); ++i) {
    buf[i * 4 + 0] = static_cast<uint8_t>(words[i] >> 24);
    buf[i * 4 + 1] = static_cast<uint8_t>(words[i] >> 16);
    buf[i * 4 + 2] = static_cast<uint8_t>(words[i] >> 8);
    buf[i * 4 + 3] = static_cast<uint8_t>(words[i]);
  }
  return CanonicalFunctionHashRaw(buf.data(),
                                  static_cast<uint32_t>(buf.size()));
}
// bl with a given word displacement (opcode 18, AA=0, LK=1). LI = bits 6-29.
uint32_t Bl(int32_t disp) {
  return 0x48000000u | (static_cast<uint32_t>(disp) & 0x03FFFFFCu) | 1u;
}
// bc conditional branch (opcode 16, AA=0, LK=0). BD = bits 16-29.
uint32_t Bc(int32_t disp) {
  return 0x40000000u | (static_cast<uint32_t>(disp) & 0x0000FFFCu);
}
// addi rD,rA,SIMM (opcode 14).
uint32_t Addi(uint32_t rD, uint32_t rA, uint16_t simm) {
  return (14u << 26) | (rD << 21) | (rA << 16) | simm;
}
}  // namespace

TEST_CASE("SHAREDFN_CANON_HASH_BL_TARGET_INVARIANT", "[instr]") {
  // Identical bodies except the bl TARGET (the relocation) -> SAME hash.
  std::vector<uint32_t> a = {Addi(3, 0, 0x10), Bl(0x100), Addi(4, 3, 0x20)};
  std::vector<uint32_t> b = {Addi(3, 0, 0x10), Bl(0x4000), Addi(4, 3, 0x20)};
  REQUIRE(Hash(a) == Hash(b));
}

TEST_CASE("SHAREDFN_CANON_HASH_BODY_SENSITIVE", "[instr]") {
  // Differ in a non-masked instruction (an addi immediate) -> DIFFERENT hash.
  std::vector<uint32_t> a = {Addi(3, 0, 0x10), Bl(0x100)};
  std::vector<uint32_t> b = {Addi(3, 0, 0x11), Bl(0x100)};
  REQUIRE(Hash(a) != Hash(b));
}

TEST_CASE("SHAREDFN_CANON_HASH_LOOP_BRANCH_PRESERVED", "[instr]") {
  // bc (intra-function loop branch) is NOT masked, so two functions differing in
  // a loop displacement must hash differently - we must not over-mask structure
  // (over-masking risks collapsing distinct functions onto one handler).
  std::vector<uint32_t> a = {Addi(3, 0, 1), Bc(-8)};
  std::vector<uint32_t> b = {Addi(3, 0, 1), Bc(-12)};
  REQUIRE(Hash(a) != Hash(b));
}

TEST_CASE("SHAREDFN_REGISTRY_LOOKUP_AND_SIZE_GUARD", "[instr]") {
  std::vector<uint32_t> fn = {Addi(3, 0, 0x10), Bl(0x100), Bc(-8)};
  uint64_t h = Hash(fn);
  uint32_t size = static_cast<uint32_t>(fn.size() * 4);
  REQUIRE(LookupSharedFunction(h, size) == SharedFunctionKind::kNone);
  RegisterSharedFunctionForTesting(h, size, SharedFunctionKind::kMemcpy);
  REQUIRE(LookupSharedFunction(h, size) == SharedFunctionKind::kMemcpy);
  // Size mismatch -> none (hash-collision defense: never run a handler with the
  // wrong byte count).
  REQUIRE(LookupSharedFunction(h, size + 4) == SharedFunctionKind::kNone);
}
