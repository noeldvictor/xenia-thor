/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_XBOXKRNL_CPP_EH_H_
#define XENIA_KERNEL_XBOXKRNL_XBOXKRNL_CPP_EH_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xe {
namespace kernel {
namespace xboxkrnl {

// Guest C++ exception dispatch (cvar guest_cpp_exception_dispatch). One guest
// PowerPC call frame discovered while unwinding the stack to find a catch
// handler. See xboxkrnl_debug.cc / the guest-eh-build-plan.

struct GuestEhFrame {
  uint32_t pc;  // a PC within this frame: the throw/fault PC for the starting
                // frame, otherwise the return address into this frame
  uint32_t sp;  // this frame's stack pointer (guest r1)
};

// Read a big-endian u32 from guest memory, returning false if the address is
// unmapped/unreadable. Shared by the EH stack walk and the descriptor decoders
// so both are host cpu-testable via a synthetic map; the runtime passes a
// wrapper over memory()->TranslateVirtual + xe::load_and_swap.
using GuestBe32Reader = std::function<bool(uint32_t, uint32_t&)>;

// Walk the guest PowerPC back-chain from (start_pc, start_sp) toward the stack
// base, appending each frame (the starting frame included) to *out_frames.
//
// Xbox 360 PPC linkage: a function stores its caller's SP at [sp] (the
// back-chain word) and saves its own return address (LR) at [caller_sp - 8]
// (the gate confirmed the -0x8 slot). So from a frame at sp:
//   caller_sp     = *(be32)[sp]
//   return_pc(->) = *(be32)[caller_sp - 8]   (the PC in the caller)
//
// read_be32(addr, out) reads a big-endian u32 from guest memory, returning
// false if the address is unmapped/unreadable. [stack_min, stack_max) bounds
// the valid guest stack. The walk FAILS CLOSED -- it stops on a zero, backward,
// misaligned, or out-of-range back-chain, an unreadable slot, or max_frames --
// so a corrupt stack yields a short, safe chain instead of wandering into
// garbage (the EH dispatcher must never longjmp based on a bad walk). Returns
// the number of frames appended.
size_t WalkGuestStack(
    const std::function<bool(uint32_t, uint32_t&)>& read_be32,
    uint32_t start_pc, uint32_t start_sp, uint32_t stack_min,
    uint32_t stack_max, size_t max_frames,
    std::vector<GuestEhFrame>* out_frames);

// ---- Unit 4: MSVC C++ EH descriptors (stored BIG-ENDIAN in guest memory) ----
//
// The catch-handler tables are the standard 32-bit MSVC structures. We decode
// them field-by-field (each decoder byte-swaps every u32 via GuestBe32Reader)
// into host-endian copies. Layouts/offsets are gate-confirmed on Project
// Sylpheed; sizes: FuncInfo 0x1C, TryBlockMapEntry 0x14, HandlerType 0x10.

// EH4 FuncInfo magic (gate-confirmed). The high 3 bits hold bbaOpts, so compare
// the low 29 bits.
constexpr uint32_t kGuestEhFuncInfoMagic = 0x19930522u;
constexpr uint32_t kGuestEhFuncInfoMagicMask = 0x1FFFFFFFu;

// _s_FuncInfo (per function; located via FuncStart - 4).
struct GuestFuncInfo {
  uint32_t magic;              // 0x00 (& mask) == kGuestEhFuncInfoMagic
  int32_t max_state;           // 0x04
  uint32_t unwind_map;         // 0x08 -> UnwindMapEntry[]
  uint32_t num_try_blocks;     // 0x0C
  uint32_t try_block_map;      // 0x10 -> TryBlockMapEntry[num_try_blocks]
  uint32_t num_ip_map_entries; // 0x14
  uint32_t ip_to_state_map;    // 0x18 -> IpToStateMapEntry[]
};

// _s_TryBlockMapEntry (per try block).
struct GuestTryBlockMapEntry {
  int32_t try_low;         // 0x00 state range [try_low, try_high] the try covers
  int32_t try_high;        // 0x04
  int32_t catch_high;      // 0x08 last state reachable from the catch funclets
  int32_t num_catches;     // 0x0C
  uint32_t handler_array;  // 0x10 -> HandlerType[num_catches]
};

// _s_HandlerType (per catch clause).
struct GuestHandlerType {
  uint32_t adjectives;          // 0x00 const/volatile/ref + 0x40 = catch(...)
  uint32_t type_descriptor;     // 0x04 -> TypeDescriptor (0 for catch(...))
  int32_t disp_catch_obj;       // 0x08 caught-object offset from the establisher
  uint32_t address_of_handler;  // 0x0C guest VA of the catch funclet
};

// TypeDescriptor header; the mangled type name is the C string at name_addr.
struct GuestTypeDescriptor {
  uint32_t vftable;    // 0x00
  uint32_t spare;      // 0x04
  uint32_t name_addr;  // address of the null-terminated mangled name (= addr+8)
};

// Resolve a function's FuncInfo pointer: the word at (func_start - 4) holds the
// FuncInfo VA (gate finding; device-validated at U6). False on a bad read.
bool ResolveGuestFuncInfoAddr(const GuestBe32Reader& read_be32,
                              uint32_t func_start, uint32_t* out_func_info_addr);

// Decode one descriptor at a guest address. Each returns false (touching no
// output) if any constituent word is unreadable.
bool DecodeGuestFuncInfo(const GuestBe32Reader& read_be32, uint32_t addr,
                         GuestFuncInfo* out);
bool DecodeGuestTryBlockMapEntry(const GuestBe32Reader& read_be32, uint32_t addr,
                                 GuestTryBlockMapEntry* out);
bool DecodeGuestHandlerType(const GuestBe32Reader& read_be32, uint32_t addr,
                            GuestHandlerType* out);
bool DecodeGuestTypeDescriptor(const GuestBe32Reader& read_be32, uint32_t addr,
                               GuestTypeDescriptor* out);

// ---- Unit 5: type-match personality (catch selection + PMD this-adjust) ----
//
// Given the throw's ThrowInfo->CatchableTypeArray (the set of types the thrown
// object can bind to, base classes included) and a catch clause's HandlerType,
// decide whether the catch handles the throw -- matching by the TypeDescriptor
// mangled-name string -- and yield the PMD that converts the thrown pointer to
// the caught base subobject. (Throw-side structs mirror the existing
// x_s__ThrowInfo / x_s__CatchableType / x_PMD in xboxkrnl_debug.cc, re-expressed
// over GuestBe32Reader so the matcher is host cpu-testable.)

// Per-throw magic in the X_EXCEPTION_RECORD (distinct from the FuncInfo magic).
constexpr uint32_t kGuestEhThrowMagic = 0x19930520u;

// catch(...) is a HandlerType with a null type descriptor.
inline bool IsGuestCatchAll(const GuestHandlerType& handler) {
  return handler.type_descriptor == 0;
}

// Read a guest ASCII C-string (a mangled type name). Reads bytes until NUL,
// max_len, or an unreadable byte; returns what was read.
using GuestByteReader = std::function<bool(uint32_t, uint8_t&)>;
std::string ReadGuestCString(const GuestByteReader& read_u8, uint32_t addr,
                             size_t max_len = 512);

// _PMD: this-pointer displacement (member / vbtable-ptr / vbtable-entry).
struct GuestPmd {
  int32_t mdisp;
  int32_t pdisp;
  int32_t vdisp;
};
bool DecodeGuestPmd(const GuestBe32Reader& read_be32, uint32_t addr,
                    GuestPmd* out);

// _s_CatchableType (0x1C): one type the thrown object can be caught as.
struct GuestCatchableType {
  uint32_t properties;          // 0x00
  uint32_t type_descriptor;     // 0x04 -> TypeDescriptor
  GuestPmd this_displacement;   // 0x08..0x13
  int32_t size_or_offset;       // 0x14
  uint32_t copy_function;       // 0x18
};
bool DecodeGuestCatchableType(const GuestBe32Reader& read_be32, uint32_t addr,
                              GuestCatchableType* out);

// Adjust the thrown-object pointer to the base subobject the catch expects, per
// the PMD. pdisp < 0 is the common (non-virtual) case = base + mdisp; pdisp >= 0
// walks the vbtable (rare; device-validated at U6). Fails safe to base + mdisp.
uint32_t AdjustGuestThisPointer(const GuestBe32Reader& read_be32, uint32_t base,
                                const GuestPmd& pmd);

// Does `handler` catch the throw at throw_info_ea? On a typed match, *out_pmd =
// the matching CatchableType's this_displacement; for catch(...), *out_is_catch_all
// = true and *out_pmd = identity (mdisp 0, pdisp -1). Matches by the mangled type
// name, trying each CatchableType in order (most-derived first).
bool GuestHandlerCatchesThrow(const GuestBe32Reader& read_be32,
                              const GuestByteReader& read_u8,
                              const GuestHandlerType& handler,
                              uint32_t throw_info_ea, GuestPmd* out_pmd,
                              bool* out_is_catch_all);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_CPP_EH_H_
