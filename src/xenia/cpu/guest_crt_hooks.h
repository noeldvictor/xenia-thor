/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_GUEST_CRT_HOOKS_H_
#define XENIA_CPU_GUEST_CRT_HOOKS_H_

#include <cstdint>

#include "xenia/cpu/function.h"

namespace xe {
namespace cpu {

// Host replacements for a title's statically linked C runtime, planted on
// the guest addresses of those functions at declare time (the same extern
// dispatch the kernel imports use). The tables come from the static
// recompilations of the titles, which replace exactly these functions with
// native ones (reNut's renut_crt.toml for Banjo-Kazooie: Nuts & Bolts;
// re:Blue for Blue Dragon): the game's own heap, memcpy, and memset run as
// host code instead of translated PowerPC. The heap replacement is also a
// correctness fix: Banjo's heap ran with a null critical section under the
// emulator, two threads corrupted its free list, and RtlpDeCommitFreeBlock
// read at null - 8 (the puzzle-transition stall of 2026-09-21).
struct GuestCrtHook {
  uint32_t address;
  const char* name;
  GuestFunction::ExternHandler handler;
};

// The hook for a guest address in the module whose code section hashes to
// code_hash (XXH3, the same hash the game-patch files match), or nullptr.
const GuestCrtHook* LookupGuestCrtHook(uint64_t code_hash, uint32_t address);

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_GUEST_CRT_HOOKS_H_
