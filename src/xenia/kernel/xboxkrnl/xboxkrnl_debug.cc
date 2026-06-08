/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/debugging.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_cpp_eh.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DEFINE_bool(xboxkrnl_ignore_guest_debug_breakpoints, false,
            "Experimental Android bring-up: log and ignore guest "
            "DbgBreakPoint calls instead of trapping the host process.",
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

void HandleCppException(pointer_t<X_EXCEPTION_RECORD> record) {
  // C++ exception.
  // https://blogs.msdn.com/b/oldnewthing/archive/2010/07/30/10044061.aspx
  // http://www.drdobbs.com/visual-c-exception-handling-instrumentat/184416600
  // http://www.openrce.org/articles/full_view/21

  assert_true(record->number_parameters == 3);
  assert_true(record->exception_information[0] == 0x19930520);

  auto thrown_ptr = record->exception_information[1];
  auto thrown = kernel_memory()->TranslateVirtual(thrown_ptr);
  auto vftable_ptr = *reinterpret_cast<xe::be<uint32_t>*>(thrown);

  auto throw_info_ptr = record->exception_information[2];
  auto throw_info =
      kernel_memory()->TranslateVirtual<x_s__ThrowInfo*>(throw_info_ptr);
  auto catchable_types =
      kernel_memory()->TranslateVirtual<x_s__CatchableTypeArray*>(
          throw_info->catchable_type_array_ptr);

  // xenia does not implement guest C++ exception dispatch/unwinding. Don't
  // abort here: log and return so RtlRaiseException is non-fatal, matching
  // upstream canary/edge. Otherwise ANY guest throw (e.g. std::bad_alloc from
  // a failed allocation -- Project Sylpheed's heap allocator does exactly
  // this) hard-crashes the emulator at the raise site.
  // xe::debugging::Break();
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
