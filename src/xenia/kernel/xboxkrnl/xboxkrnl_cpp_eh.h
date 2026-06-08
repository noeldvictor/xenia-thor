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

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_XBOXKRNL_CPP_EH_H_
