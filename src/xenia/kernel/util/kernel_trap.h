/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_KERNEL_TRAP_H_
#define XENIA_KERNEL_UTIL_KERNEL_TRAP_H_

// The export trap (2026-09-21): a breakpoint on a kernel export that needs no
// rebuild. The debug server names an export; when the guest calls it, the shim
// records the thread, every guest register, and 256 words of the guest stack,
// and can hold that thread until the server releases it, so the server can
// read guest memory and disassemble while the state is live. One relaxed
// atomic load per export call when no trap is set.

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

#include "xenia/cpu/ppc/ppc_context.h"

namespace xe {
namespace cpu {
class Export;
}  // namespace cpu
namespace kernel {

// (module id + 1) << 16 | ordinal; 0 means no trap.
extern std::atomic<uint32_t> g_kernel_trap_key;
// When non-zero, only a call whose guest lr equals this value is a hit, so a
// trap on a frequent export (RtlEnterCriticalSection) can name one call site.
extern std::atomic<uint32_t> g_kernel_trap_lr;

// Called by the shim trampoline when its key matches.
void KernelTrapHit(cpu::Export* export_entry, cpu::ppc::PPCContext* ctx);

// Arms a trap on the export with this name (searched in xboxkrnl and xam).
// pause holds the calling guest thread at the hit until KernelTrapRelease.
// Returns "" on success or the reason.
std::string KernelTrapSet(std::string_view export_name, bool pause,
                          uint32_t lr_filter);
// A dump spec applied at every hit and written to the log as hex: a comma
// list of "rN:len" (memory at the guest address in register N) or
// "addr:len" (hex address). Empty clears it. Both the PC (cvars) and the
// device (the trap tool) use it; the log lines make a PC-versus-device diff.
void KernelTrapSetDump(std::string_view spec);
// Arms the trap from the kernel_trap_* cvars (the PC has no debug server).
// Called once the export tables exist.
void KernelTrapArmFromCvars();
void KernelTrapClear();
void KernelTrapRelease();
// The last hit as JSON: {"armed":..,"hits":N,"paused":bool,"export":..,
// "tid":..,"r":[32 hex],"lr":..,"ctr":..,"stack_base":..,"stack":[256 hex],
// "chain":[return addresses],"mem":{"rN":{"at","words","deref"}},
// "stack_text":["+off:text"]}. The last three come from live guest memory,
// so the picture the Banjo dirty-disc needed (2026-09-21) is one call.
std::string KernelTrapReportJson();

// The guest picture behind a stack pointer, from live guest memory. The
// threads walk and the trap report both use these; a client needs no
// memory reads of its own.
// The PowerPC back chain: the frame at [sp], the return address at
// [frame - 8]. A JSON array of hex return addresses, lr first.
std::string GuestChainJson(uint32_t sp, uint32_t lr, int max_frames = 16);
// Printable ASCII runs of 6 or more characters in [sp, sp + length):
// a JSON array of "+offset:text".
std::string GuestStackTextJson(uint32_t sp, uint32_t length, int max_items = 16);
// 16 words at each of r24 to r31 that points into the guest heap, and one
// dereference of the first 8 words that point below the code: a JSON
// object keyed "r24".."r31".
std::string GuestRegisterMemoryJson(const uint64_t* r);

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_KERNEL_TRAP_H_
