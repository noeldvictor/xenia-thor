/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/byte_order.h"
#include "xenia/base/debugging.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_cpp_eh.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DEFINE_bool(xboxkrnl_ignore_guest_debug_breakpoints, false,
            "Experimental Android bring-up: log and ignore guest "
            "DbgBreakPoint calls instead of trapping the host process.",
            "Kernel");

// The master switch (guest_cpp_exception_dispatch) is DEFINEd in the cpu lib
// (xex_module.cc) because the XEX loader needs it to parse the exception
// directory; HandleCppException only reads it here.
DECLARE_bool(guest_cpp_exception_dispatch);

DEFINE_bool(
    guest_cpp_exception_dispatch_transfer, false,
    "Experimental (Project Sylpheed): when guest_cpp_exception_dispatch is also "
    "on, actually transfer control to the matched guest catch funclet (set the "
    "establisher frame + Reenter) instead of only walking/matching and logging. "
    "Default-off so the first device run validates the unwind+match "
    "diagnostically before risking a control transfer. See the "
    "guest-eh-build-plan memory.",
    "Kernel");

namespace xe {
namespace kernel {
namespace xboxkrnl {

// Unit 3 of the guest C++ exception-dispatch build (cvar
// guest_cpp_exception_dispatch). Walk the guest PowerPC back-chain to enumerate
// call frames for the unwinder; declared in xboxkrnl_cpp_eh.h. Pure logic over a
// guest-memory read callback so it is host cpu-testable (walk_guest_stack_test).
// FAILS CLOSED on any corrupt step -- a bad walk must never feed a longjmp.
size_t WalkGuestStack(
    const std::function<bool(uint32_t, uint32_t&)>& read_be32,
    uint32_t start_pc, uint32_t start_sp, uint32_t stack_min,
    uint32_t stack_max, size_t max_frames,
    std::vector<GuestEhFrame>* out_frames) {
  uint32_t cur_pc = start_pc;
  uint32_t cur_sp = start_sp;
  for (size_t i = 0; i < max_frames; ++i) {
    out_frames->push_back({cur_pc, cur_sp});

    // The caller's stack pointer is the back-chain word at [sp].
    uint32_t caller_sp = 0;
    if (!read_be32(cur_sp, caller_sp)) {
      break;  // back-chain word unreadable
    }
    if (caller_sp == 0) {
      break;  // top of the stack (null back-chain)
    }
    if (caller_sp <= cur_sp) {
      break;  // the stack grows down; the caller must sit at a higher address
    }
    if ((caller_sp & 0x7u) != 0) {
      break;  // back-chain must be at least 8-byte aligned
    }
    if (caller_sp < stack_min || caller_sp >= stack_max) {
      break;  // outside the valid guest stack region
    }

    // The return address into the caller is the LR this frame saved at
    // [caller_sp - 8]. If it is unreadable, stop WITHOUT appending a frame we
    // cannot give a PC -- the next iteration would have no valid pc.
    uint32_t ret_pc = 0;
    if (!read_be32(caller_sp - 8u, ret_pc)) {
      break;
    }

    cur_pc = ret_pc;
    cur_sp = caller_sp;
  }
  return out_frames->size();
}

// Unit 4: decode the big-endian MSVC C++ EH descriptors. Each decoder reads its
// fixed set of u32 words via read_be32 (which byte-swaps), so a wrong offset or
// struct size is caught host-side (eh_descriptors_test). All-or-nothing: an
// unreadable word fails the whole decode without touching *out.
bool ResolveGuestFuncInfoAddr(const GuestBe32Reader& read_be32,
                              uint32_t func_start, uint32_t* out_func_info_addr) {
  return read_be32(func_start - 4u, *out_func_info_addr);
}

bool DecodeGuestFuncInfo(const GuestBe32Reader& read_be32, uint32_t addr,
                         GuestFuncInfo* out) {
  uint32_t w[7];
  for (int i = 0; i < 7; ++i) {
    if (!read_be32(addr + static_cast<uint32_t>(i) * 4u, w[i])) {
      return false;
    }
  }
  out->magic = w[0];
  out->max_state = static_cast<int32_t>(w[1]);
  out->unwind_map = w[2];
  out->num_try_blocks = w[3];
  out->try_block_map = w[4];
  out->num_ip_map_entries = w[5];
  out->ip_to_state_map = w[6];
  return true;
}

bool DecodeGuestTryBlockMapEntry(const GuestBe32Reader& read_be32, uint32_t addr,
                                 GuestTryBlockMapEntry* out) {
  uint32_t w[5];
  for (int i = 0; i < 5; ++i) {
    if (!read_be32(addr + static_cast<uint32_t>(i) * 4u, w[i])) {
      return false;
    }
  }
  out->try_low = static_cast<int32_t>(w[0]);
  out->try_high = static_cast<int32_t>(w[1]);
  out->catch_high = static_cast<int32_t>(w[2]);
  out->num_catches = static_cast<int32_t>(w[3]);
  out->handler_array = w[4];
  return true;
}

bool DecodeGuestHandlerType(const GuestBe32Reader& read_be32, uint32_t addr,
                            GuestHandlerType* out) {
  uint32_t w[4];
  for (int i = 0; i < 4; ++i) {
    if (!read_be32(addr + static_cast<uint32_t>(i) * 4u, w[i])) {
      return false;
    }
  }
  out->adjectives = w[0];
  out->type_descriptor = w[1];
  out->disp_catch_obj = static_cast<int32_t>(w[2]);
  out->address_of_handler = w[3];
  return true;
}

bool DecodeGuestTypeDescriptor(const GuestBe32Reader& read_be32, uint32_t addr,
                               GuestTypeDescriptor* out) {
  uint32_t w[2];
  for (int i = 0; i < 2; ++i) {
    if (!read_be32(addr + static_cast<uint32_t>(i) * 4u, w[i])) {
      return false;
    }
  }
  out->vftable = w[0];
  out->spare = w[1];
  out->name_addr = addr + 8u;  // the mangled name C string follows the header
  return true;
}

// Unit 5: type-match personality (catch selection + PMD this-adjust). Pure logic
// over the readers, host-tested by eh_type_match_test.
std::string ReadGuestCString(const GuestByteReader& read_u8, uint32_t addr,
                             size_t max_len) {
  std::string s;
  for (size_t i = 0; i < max_len; ++i) {
    uint8_t b = 0;
    if (!read_u8(addr + static_cast<uint32_t>(i), b) || b == 0) {
      break;
    }
    s.push_back(static_cast<char>(b));
  }
  return s;
}

bool DecodeGuestPmd(const GuestBe32Reader& read_be32, uint32_t addr,
                    GuestPmd* out) {
  uint32_t w[3];
  for (int i = 0; i < 3; ++i) {
    if (!read_be32(addr + static_cast<uint32_t>(i) * 4u, w[i])) {
      return false;
    }
  }
  out->mdisp = static_cast<int32_t>(w[0]);
  out->pdisp = static_cast<int32_t>(w[1]);
  out->vdisp = static_cast<int32_t>(w[2]);
  return true;
}

bool DecodeGuestCatchableType(const GuestBe32Reader& read_be32, uint32_t addr,
                              GuestCatchableType* out) {
  uint32_t w[7];
  for (int i = 0; i < 7; ++i) {
    if (!read_be32(addr + static_cast<uint32_t>(i) * 4u, w[i])) {
      return false;
    }
  }
  out->properties = w[0];
  out->type_descriptor = w[1];
  out->this_displacement.mdisp = static_cast<int32_t>(w[2]);
  out->this_displacement.pdisp = static_cast<int32_t>(w[3]);
  out->this_displacement.vdisp = static_cast<int32_t>(w[4]);
  out->size_or_offset = static_cast<int32_t>(w[5]);
  out->copy_function = w[6];
  return true;
}

uint32_t AdjustGuestThisPointer(const GuestBe32Reader& read_be32, uint32_t base,
                                const GuestPmd& pmd) {
  if (pmd.pdisp < 0) {
    return base + static_cast<uint32_t>(pmd.mdisp);  // non-virtual base
  }
  // Virtual base: base + pdisp holds the vbtable pointer; the base subobject
  // offset is the vbtable entry at vbtable + vdisp.
  uint32_t vbtable = 0;
  if (!read_be32(base + static_cast<uint32_t>(pmd.pdisp), vbtable)) {
    return base + static_cast<uint32_t>(pmd.mdisp);
  }
  uint32_t vbentry = 0;
  if (!read_be32(vbtable + static_cast<uint32_t>(pmd.vdisp), vbentry)) {
    return base + static_cast<uint32_t>(pmd.mdisp);
  }
  return base + static_cast<uint32_t>(pmd.pdisp) +
         static_cast<uint32_t>(static_cast<int32_t>(vbentry)) +
         static_cast<uint32_t>(pmd.mdisp);
}

bool GuestHandlerCatchesThrow(const GuestBe32Reader& read_be32,
                              const GuestByteReader& read_u8,
                              const GuestHandlerType& handler,
                              uint32_t throw_info_ea, GuestPmd* out_pmd,
                              bool* out_is_catch_all) {
  *out_is_catch_all = false;
  out_pmd->mdisp = 0;
  out_pmd->pdisp = -1;  // identity (no adjustment)
  out_pmd->vdisp = 0;

  if (IsGuestCatchAll(handler)) {
    *out_is_catch_all = true;
    return true;
  }

  GuestTypeDescriptor catch_td;
  if (!DecodeGuestTypeDescriptor(read_be32, handler.type_descriptor, &catch_td)) {
    return false;
  }
  std::string catch_name = ReadGuestCString(read_u8, catch_td.name_addr);
  if (catch_name.empty()) {
    return false;
  }

  // ThrowInfo.catchable_type_array_ptr is at +0x0C; the array is {count;
  // ptrs[count]}.
  uint32_t cta_ea = 0;
  if (!read_be32(throw_info_ea + 0x0Cu, cta_ea)) {
    return false;
  }
  uint32_t count = 0;
  if (!read_be32(cta_ea + 0x00u, count)) {
    return false;
  }
  constexpr uint32_t kMaxCatchableTypes = 64u;
  for (uint32_t i = 0; i < count && i < kMaxCatchableTypes; ++i) {
    uint32_t ct_ea = 0;
    if (!read_be32(cta_ea + 0x04u + i * 4u, ct_ea)) {
      return false;
    }
    GuestCatchableType ct;
    if (!DecodeGuestCatchableType(read_be32, ct_ea, &ct)) {
      continue;
    }
    GuestTypeDescriptor ct_td;
    if (!DecodeGuestTypeDescriptor(read_be32, ct.type_descriptor, &ct_td)) {
      continue;
    }
    if (ReadGuestCString(read_u8, ct_td.name_addr) == catch_name) {
      *out_pmd = ct.this_displacement;
      return true;
    }
  }
  return false;
}

void DbgBreakPoint_entry() {
  if (cvars::xboxkrnl_ignore_guest_debug_breakpoints) {
    XELOGW("DbgBreakPoint suppressed by xboxkrnl_ignore_guest_debug_breakpoints");
    return;
  }
  xe::debugging::Break();
}
DECLARE_XBOXKRNL_EXPORT2(DbgBreakPoint, kDebug, kStub, kImportant);

// https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
typedef struct {
  xe::be<uint32_t> type;
  xe::be<uint32_t> name_ptr;
  xe::be<uint32_t> thread_id;
  xe::be<uint32_t> flags;
} X_THREADNAME_INFO;
static_assert_size(X_THREADNAME_INFO, 0x10);

void HandleSetThreadName(pointer_t<X_EXCEPTION_RECORD> record) {
  // SetThreadName. FFS.
  // https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx

  // TODO(benvanik): check record->number_parameters to make sure it's a
  // correct size.
  auto thread_info =
      reinterpret_cast<X_THREADNAME_INFO*>(&record->exception_information[0]);

  assert_true(thread_info->type == 0x1000);

  if (!thread_info->name_ptr) {
    XELOGD("SetThreadName called with null name_ptr");
    return;
  }

  // 4D5307D6 (and its demo) has a bug where it ends up passing freed memory for
  // the name, so at the point of SetThreadName it's filled with junk.

  // TODO(gibbed): cvar for thread name encoding for conversion, some games use
  // SJIS and there's no way to automatically know this.
  auto name = std::string(
      kernel_memory()->TranslateVirtual<const char*>(thread_info->name_ptr));
  std::replace_if(
      name.begin(), name.end(), [](auto c) { return c < 32 || c > 127; }, '?');

  object_ref<XThread> thread;
  if (thread_info->thread_id == -1) {
    // Current thread.
    thread = retain_object(XThread::GetCurrentThread());
  } else {
    // Lookup thread by ID.
    thread = kernel_state()->GetThreadByID(thread_info->thread_id);
  }

  if (thread) {
    XELOGD("SetThreadName({}, {})", thread->thread_id(), name);
    thread->set_name(name);
  }

  // TODO(benvanik): unwinding required here?
}

typedef struct {
  xe::be<int32_t> mdisp;
  xe::be<int32_t> pdisp;
  xe::be<int32_t> vdisp;
} x_PMD;

typedef struct {
  xe::be<uint32_t> properties;
  xe::be<uint32_t> type_ptr;
  x_PMD this_displacement;
  xe::be<int32_t> size_or_offset;
  xe::be<uint32_t> copy_function_ptr;
} x_s__CatchableType;

typedef struct {
  xe::be<int32_t> number_catchable_types;
  xe::be<uint32_t> catchable_type_ptrs[1];
} x_s__CatchableTypeArray;

typedef struct {
  xe::be<uint32_t> attributes;
  xe::be<uint32_t> unwind_ptr;
  xe::be<uint32_t> forward_compat_ptr;
  xe::be<uint32_t> catchable_type_array_ptr;
} x_s__ThrowInfo;

// Unit 6 of the guest C++ exception-dispatch build: actually dispatch a guest
// C++ throw to its catch handler. Returns true once it has transferred control
// (via Reenter, which does not return here on the Android longjmp path); returns
// false on any failure or if no handler is found, so the caller falls back to
// the historical log+return stub. Walk/decode/match are host-validated
// (xboxkrnl_cpp_eh.* + the [guest-eh] cpu-tests); this wires them to the live
// context. Gated by cvars::guest_cpp_exception_dispatch; the control transfer
// itself is additionally gated by guest_cpp_exception_dispatch_transfer so the
// first device run can validate the unwind+match diagnostically.
static bool TryDispatchGuestCppException(pointer_t<X_EXCEPTION_RECORD> record) {
  auto current_thread = XThread::GetCurrentThread();
  if (!current_thread || !current_thread->thread_state()) {
    return false;
  }
  auto context = current_thread->thread_state()->context();
  if (!context) {
    return false;
  }
  auto memory = kernel_memory();
  if (!memory) {
    return false;
  }

  // Fault-safe guest readers: refuse unmapped / non-readable pages so a corrupt
  // back-chain or EH pointer can never fault the host (which would be worse than
  // the stub). Page protection is checked via the owning heap.
  auto read_be32 = [memory](uint32_t addr, uint32_t& out) -> bool {
    if (addr < 0x1000u) return false;
    auto* heap = memory->LookupHeap(addr);
    uint32_t protect = 0;
    if (!heap || !heap->QueryProtect(addr, &protect) ||
        !(protect & kMemoryProtectRead)) {
      return false;
    }
    out = xe::load_and_swap<uint32_t>(memory->TranslateVirtual<uint8_t*>(addr));
    return true;
  };
  auto read_u8 = [memory](uint32_t addr, uint8_t& out) -> bool {
    if (addr < 0x1000u) return false;
    auto* heap = memory->LookupHeap(addr);
    uint32_t protect = 0;
    if (!heap || !heap->QueryProtect(addr, &protect) ||
        !(protect & kMemoryProtectRead)) {
      return false;
    }
    out = *memory->TranslateVirtual<uint8_t*>(addr);
    return true;
  };

  const uint32_t thrown_ptr =
      static_cast<uint32_t>(record->exception_information[1]);
  const uint32_t throw_info_ea =
      static_cast<uint32_t>(record->exception_information[2]);
  if (!throw_info_ea) {
    return false;
  }

  // RtlRaiseException is HLE-called from the guest throw machinery
  // (_CxxThrowException); the throw site is the guest return address (lr). Use
  // the recorded exception_address if the guest populated it.
  const uint32_t lr = static_cast<uint32_t>(context->lr);
  uint32_t fault_pc = static_cast<uint32_t>(record->exception_address);
  if (!fault_pc) {
    fault_pc = lr;
  }
  const uint32_t sp = static_cast<uint32_t>(context->r[1]);

  auto exec_module = kernel_state()->GetExecutableModule();
  if (!exec_module) {
    return false;
  }
  auto* xex = exec_module->xex_module();
  if (!xex) {
    return false;
  }

  uint32_t stack_min = current_thread->stack_limit();
  uint32_t stack_max = current_thread->stack_base();
  if (!stack_max) {
    stack_max = 0xFFFFFFFFu;  // bounds unknown: rely on the walk's other gates
  }

  std::vector<GuestEhFrame> frames;
  WalkGuestStack(read_be32, fault_pc, sp, stack_min, stack_max,
                 /*max_frames=*/256, &frames);
  XELOGI(
      "guest-eh: throw exc_addr={:08X} lr={:08X} sp={:08X} thrown={:08X} "
      "throw_info={:08X} frames={} stack=[{:08X}-{:08X}]",
      static_cast<uint32_t>(record->exception_address), lr, sp, thrown_ptr,
      throw_info_ea, frames.size(), stack_min, stack_max);

  for (size_t i = 0; i < frames.size(); ++i) {
    const uint32_t frame_pc = frames[i].pc;
    const uint32_t frame_sp = frames[i].sp;
    const auto* rf = xex->FindRuntimeFunction(frame_pc);
    if (!rf || !rf->has_exception_handler) {
      continue;
    }
    uint32_t func_info_ea = 0;
    if (!ResolveGuestFuncInfoAddr(read_be32, rf->func_start, &func_info_ea)) {
      continue;
    }
    GuestFuncInfo fi;
    if (!DecodeGuestFuncInfo(read_be32, func_info_ea, &fi)) {
      continue;
    }
    if ((fi.magic & kGuestEhFuncInfoMagicMask) != kGuestEhFuncInfoMagic) {
      continue;
    }
    if (!fi.num_try_blocks || !fi.try_block_map) {
      continue;
    }
    for (uint32_t t = 0; t < fi.num_try_blocks && t < 256u; ++t) {
      GuestTryBlockMapEntry tb;
      if (!DecodeGuestTryBlockMapEntry(read_be32, fi.try_block_map + t * 0x14u,
                                       &tb)) {
        continue;
      }
      if (tb.num_catches <= 0 || !tb.handler_array) {
        continue;
      }
      for (int32_t h = 0; h < tb.num_catches && h < 256; ++h) {
        GuestHandlerType handler;
        if (!DecodeGuestHandlerType(
                read_be32, tb.handler_array + static_cast<uint32_t>(h) * 0x10u,
                &handler)) {
          continue;
        }
        GuestPmd pmd;
        bool catch_all = false;
        if (!GuestHandlerCatchesThrow(read_be32, read_u8, handler, throw_info_ea,
                                      &pmd, &catch_all)) {
          continue;
        }

        const uint32_t establisher = frame_sp;
        const uint32_t adjusted =
            catch_all ? thrown_ptr
                      : AdjustGuestThisPointer(read_be32, thrown_ptr, pmd);
        // First-cut continuation = the establisher's return address. The true
        // post-catch resume point lives inside the establisher; this is refined
        // after the diagnostic fire reveals the funclet structure.
        const uint32_t continuation =
            (i + 1 < frames.size()) ? frames[i + 1].pc : lr;

        XELOGI(
            "guest-eh: MATCH frame[{}] func={:08X} try={} catch={} "
            "funclet={:08X} establisher={:08X} disp_catch_obj={:08X} "
            "adjusted_this={:08X} catch_all={} adjectives={:08X} cont={:08X}",
            i, rf->func_start, t, h, handler.address_of_handler, establisher,
            static_cast<uint32_t>(handler.disp_catch_obj), adjusted,
            catch_all ? 1 : 0, handler.adjectives, continuation);

        if (!cvars::guest_cpp_exception_dispatch_transfer) {
          // Diagnostic mode: handler found and logged, but do not risk the
          // control transfer yet. Fall back to the stub.
          XELOGW(
              "guest-eh: transfer disabled "
              "(guest_cpp_exception_dispatch_transfer off); not transferring");
          return false;
        }

        // Store the caught object for typed catches (catch(...) binds nothing).
        if (!catch_all) {
          constexpr uint32_t kHandlerIsReference = 0x08u;
          if (!(handler.adjectives & kHandlerIsReference)) {
            XELOGW(
                "guest-eh: catch-by-value not yet supported (adj={:08X}); "
                "declining",
                handler.adjectives);
            return false;
          }
          const uint32_t slot =
              establisher + static_cast<uint32_t>(handler.disp_catch_obj);
          auto* slot_heap = memory->LookupHeap(slot);
          uint32_t slot_protect = 0;
          if (!slot_heap || !slot_heap->QueryProtect(slot, &slot_protect) ||
              !(slot_protect & kMemoryProtectWrite)) {
            XELOGW("guest-eh: catch-object slot {:08X} not writable; declining",
                   slot);
            return false;
          }
          xe::store_and_swap<uint32_t>(memory->TranslateVirtual<uint8_t*>(slot),
                                       adjusted);
        }

        // Transfer to the catch funclet. Mirrors KeSetCurrentStackPointers: set
        // the establisher frame + funclet ABI (r12 = establisher frame), seed lr
        // with the continuation, and Reenter at the funclet.
        // A64Backend::PrepareForReentry resets the stackpoint depth on reentry,
        // so no manual reconciliation is required.
        XELOGI("guest-eh: TRANSFER -> funclet {:08X} (r1=r12={:08X} lr={:08X})",
               handler.address_of_handler, establisher, continuation);
        context->r[1] = establisher;
        context->r[12] = establisher;
        context->lr = continuation;
        current_thread->Reenter(handler.address_of_handler);
        return true;  // unreachable on the longjmp path
      }
    }
  }
  XELOGW("guest-eh: no matching catch handler found in {} frames",
         frames.size());
  return false;
}

void HandleCppException(pointer_t<X_EXCEPTION_RECORD> record) {
  // C++ exception.
  // https://blogs.msdn.com/b/oldnewthing/archive/2010/07/30/10044061.aspx
  // http://www.drdobbs.com/visual-c-exception-handling-instrumentat/184416600
  // http://www.openrce.org/articles/full_view/21

  assert_true(record->number_parameters == 3);
  assert_true(record->exception_information[0] == 0x19930520);

  if (cvars::guest_cpp_exception_dispatch) {
    if (TryDispatchGuestCppException(record)) {
      return;  // transferred to the guest catch handler
    }
    // Otherwise fall through to the historical stub.
  }

  // Without dispatch (or on any failure above): log and return so
  // RtlRaiseException is non-fatal, matching upstream canary/edge. Otherwise ANY
  // guest throw (e.g. std::bad_alloc from a failed allocation -- Project
  // Sylpheed's heap allocator does exactly this) hard-crashes the emulator at
  // the raise site.
  XELOGE("Guest attempted to throw a C++ exception!");
}

void RtlRaiseException_entry(pointer_t<X_EXCEPTION_RECORD> record) {
  switch (record->code) {
    case 0x406D1388: {
      HandleSetThreadName(record);
      return;
    }
    case 0xE06D7363: {
      HandleCppException(record);
      return;
    }
  }

  // TODO(benvanik): unwinding.
  // This is going to suck.
  // RtlRaiseException is not a noreturn function for unhandled codes; return
  // safe-ish instead of aborting the whole emulator (port of upstream
  // canary/edge).
  // xe::debugging::Break();
  XELOGE("Guest attempted to trigger a breakpoint!");
}
DECLARE_XBOXKRNL_EXPORT2(RtlRaiseException, kDebug, kStub, kImportant);

void KeBugCheckEx_entry(dword_t code, dword_t param1, dword_t param2,
                        dword_t param3, dword_t param4) {
  XELOGD("*** STOP: 0x{:08X} (0x{:08X}, 0x{:08X}, 0x{:08X}, 0x{:08X})", code,
         param1, param2, param3, param4);
  fflush(stdout);
  xe::debugging::Break();
  assert_always();
}
DECLARE_XBOXKRNL_EXPORT2(KeBugCheckEx, kDebug, kStub, kImportant);

void KeBugCheck_entry(dword_t code) { KeBugCheckEx_entry(code, 0, 0, 0, 0); }
DECLARE_XBOXKRNL_EXPORT2(KeBugCheck, kDebug, kImplemented, kImportant);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Debug);
