/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_backend.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cctype>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "xenia/base/atomic.h"
#include "xenia/base/clock.h"
#include "xenia/base/exception_handler.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/mutex.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "third_party/fmt/include/fmt/format.h"
#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif
#if XE_ARCH_ARM64 && XE_COMPILER_MSVC
#include <intrin.h>
#endif
#include "xenia/cpu/backend/a64/a64_assembler.h"
#include "xenia/cpu/backend/a64/a64_code_cache.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/a64/a64_sequences.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/stack_walker.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/cpu/xex_module.h"

DEFINE_uint32(a64_max_stackpoints, 65536,
              "Max number of host->guest stack mappings we can record.",
              "a64");

DEFINE_bool(a64_enable_host_guest_stack_synchronization, true,
            "Records entries for guest/host stack mappings at function starts "
            "and checks for reentry at return sites. Has slight performance "
            "impact, but fixes crashes in games that use setjmp/longjmp.",
            "a64");

DEFINE_uint32(
    arm64_compiled_call_trace_interval, 0,
    "Thor ARM64 bring-up: log every Nth A64 guest function entry after "
    "filtering. 0 disables function-entry trace generation.",
    "a64");
DEFINE_uint32(
    arm64_compiled_call_trace_min_count, 0,
    "Thor ARM64 bring-up: suppress A64 guest function-entry trace lines until "
    "a function has been entered this many times.",
    "a64");
DEFINE_uint32(
    arm64_compiled_call_trace_budget, 0,
    "Thor ARM64 bring-up: maximum A64 guest function-entry trace lines to "
    "emit.",
    "a64");
DEFINE_string(
    arm64_compiled_call_trace_functions, "",
    "Thor ARM64 bring-up: optional comma/semicolon/space separated guest "
    "function addresses or inclusive ranges to trace.",
    "a64");
DEFINE_string(
    arm64_compiled_call_trace_guest_tids, "",
    "Thor ARM64 bring-up: optional comma/semicolon/space separated guest "
    "thread ids or inclusive ranges to trace.",
    "a64");
DEFINE_uint32(
    arm64_compiled_call_trace_after_ms, 0,
    "Thor ARM64 bring-up: suppress A64 guest function-entry trace lines until "
    "this many host milliseconds after the first traced function entry.",
    "a64");
DEFINE_bool(
    arm64_blue_dragon_draw_wait_probe, false,
    "Thor ARM64 bring-up: update current KTHREAD+0x58 at Blue Dragon's known "
    "draw-thread wait timeout load site. Research-only title probe.",
    "a64");
DEFINE_uint32(
    arm64_blue_dragon_draw_wait_probe_stride, 1,
    "Thor ARM64 bring-up: only call the Blue Dragon draw-wait KTHREAD+0x58 "
    "host updater every power-of-two N wait-loop hits. 1 updates every hit.",
    "a64");
DEFINE_uint32(
    arm64_blue_dragon_draw_wait_inline_tick_step, 0,
    "Thor ARM64 bring-up: if non-zero, update Blue Dragon's draw-wait "
    "KTHREAD+0x58 inline by this many milliseconds instead of calling the "
    "host clock updater. Research-only wait accelerator.",
    "a64");
DEFINE_bool(
    arm64_blue_dragon_draw_wait_fastpath, false,
    "Thor ARM64 bring-up: replace Blue Dragon's known 8246B408 draw-wait "
    "function body with a narrow hand-emitted A64 path. Research-only and "
    "title-specific.",
    "a64");
DEFINE_bool(
    arm64_blue_dragon_draw_wait_fastpath_host_counter_time, false,
    "Thor ARM64 bring-up: in the Blue Dragon draw-wait fastpath, derive "
    "KTHREAD+0x58 from CNTVCT_EL0/CNTFRQ_EL0 instead of a native clock call. "
    "Research-only and title-specific.",
    "a64");
DEFINE_uint32(
    arm64_blue_dragon_draw_wait_fastpath_native_yield_stride, 0,
    "Thor ARM64 bring-up: in the Blue Dragon draw-wait fastpath, call the "
    "host scheduler yield helper every N wait-return hits. 0 disables it. "
    "Research-only and title-specific.",
    "a64");
DEFINE_uint32(
    arm64_blue_dragon_draw_wait_fastpath_native_sleep_us, 0,
    "Thor ARM64 bring-up: sleep this many host microseconds in the Blue Dragon "
    "draw-wait native yield helper. 0 uses sched_yield only. Research-only and "
    "title-specific.",
    "a64");
DEFINE_uint32(
    arm64_blue_dragon_draw_wait_fastpath_timeout_ms, 5000,
    "Thor ARM64 bring-up: timeout threshold for the Blue Dragon draw-wait "
    "fastpath. 5000 matches the observed guest predicate. Research-only and "
    "title-specific.",
    "a64");
DEFINE_bool(
    arm64_blue_dragon_draw_wait_inline_in_caller, false,
    "Thor ARM64 speed lane: inline Blue Dragon's known 8246B408 draw-wait "
    "fastpath at the hot 8246E618 caller call site. Research-only and "
    "title-specific.",
    "a64");
DEFINE_bool(
    arm64_blue_dragon_draw_wait_caller_profile, false,
    "Thor ARM64 speed lane: sample guest LR callers entering Blue Dragon's "
    "known 8246B408 draw-wait function. Research-only and title-specific.",
    "a64");
DEFINE_uint32(
    arm64_blue_dragon_draw_wait_caller_profile_stride, 65536,
    "Thor ARM64 speed lane: sample every N entries to Blue Dragon's 8246B408 "
    "draw-wait function when caller profiling is enabled.",
    "a64");
DEFINE_uint32(
    arm64_blue_dragon_draw_wait_caller_profile_budget, 64,
    "Thor ARM64 speed lane: maximum Blue Dragon draw-wait caller sample lines "
    "to emit per process.",
    "a64");
DEFINE_bool(
    arm64_blue_dragon_memcpy_fastpath, false,
    "Thor ARM64 speed lane: replace Blue Dragon's hot 826BF770 byte-copy "
    "helper with a native host memmove over translated guest pointers. "
    "Research-only and title-specific.",
    "a64");
DEFINE_bool(
    arm64_blue_dragon_stricmp_fastpath, false,
    "Thor ARM64 speed lane: replace Blue Dragon's hot 826C5620 ASCII "
    "case-insensitive string-compare helper with a hand-emitted A64 loop that "
    "preserves PPC-visible CR and volatile register side effects. "
    "Research-only and title-specific.",
    "a64");
DEFINE_bool(
    arm64_blue_dragon_stricmp_return_profile, false,
    "Thor ARM64 speed lane: sample Blue Dragon's normal generated 826C5620 "
    "ASCII string-compare return state. Research-only and title-specific.",
    "a64");
DEFINE_uint32(
    arm64_blue_dragon_stricmp_return_profile_stride, 4096,
    "Thor ARM64 speed lane: sample every N returns from Blue Dragon's 826C5620 "
    "string-compare helper when return profiling is enabled.",
    "a64");
DEFINE_uint32(
    arm64_blue_dragon_stricmp_return_profile_budget, 64,
    "Thor ARM64 speed lane: maximum Blue Dragon 826C5620 return-state sample "
    "lines to emit per process.",
    "a64");
DEFINE_bool(
    arm64_blue_dragon_jump_table_fastpath, false,
    "Thor ARM64 speed lane: replace Blue Dragon's hot 827294CC jump-table "
    "tail-dispatch helper with a hand-emitted A64 path. Research-only and "
    "title-specific.",
    "a64");
DEFINE_bool(
    arm64_add_sub_imm_audit, false,
    "Thor ARM64 speed lane: log wrapped ADD/SUB immediate candidates without "
    "changing generated code. Research-only lowering audit.",
    "a64");
DEFINE_uint32(
    arm64_add_sub_imm_audit_function, 0,
    "Thor ARM64 speed lane: optional exact guest function start address for "
    "the wrapped ADD/SUB immediate audit. 0 logs all functions.",
    "a64");
DEFINE_uint32(
    arm64_add_sub_imm_audit_budget, 128,
    "Thor ARM64 speed lane: maximum wrapped ADD/SUB immediate audit lines to "
    "emit per process.",
    "a64");
DEFINE_bool(
    arm64_add_i64_wrapped_imm_fastpath, true,
    "Thor ARM64 speed lane: lower I64 ADD with a wrapped small negative "
    "constant as SUB #imm. Research-only; pass false to disable.",
    "a64");
DEFINE_uint32(
    arm64_add_i64_wrapped_imm_fastpath_function, 0,
    "Thor ARM64 speed lane: optional exact guest function start address for "
    "the wrapped I64 ADD immediate fastpath. 0 enables all functions.",
    "a64");
DEFINE_bool(
    arm64_immediate_lowering_audit, false,
    "Thor ARM64 speed lane: log logical-immediate lowering choices without "
    "changing generated code. Research-only lowering audit.",
    "a64");
DEFINE_uint32(
    arm64_immediate_lowering_audit_function, 0,
    "Thor ARM64 speed lane: optional exact guest function start address for "
    "the immediate-lowering audit. 0 logs all functions.",
    "a64");
DEFINE_uint32(
    arm64_immediate_lowering_audit_budget, 256,
    "Thor ARM64 speed lane: maximum immediate-lowering audit lines to emit "
    "per process.",
    "a64");
DEFINE_bool(
    arm64_context_traffic_audit, false,
    "Thor ARM64 speed lane: log HIR context/local/memory traffic summaries "
    "for compiled functions. Research-only lowering audit.",
    "a64");
DEFINE_uint32(
    arm64_context_traffic_audit_function, 0,
    "Thor ARM64 speed lane: optional exact guest function start address for "
    "the context-traffic audit. 0 logs all functions.",
    "a64");
DEFINE_uint32(
    arm64_context_traffic_audit_budget, 32,
    "Thor ARM64 speed lane: maximum context-traffic audit function summaries "
    "to emit per process.",
    "a64");
DEFINE_uint32(
    arm64_speed_profile_interval_ms, 0,
    "Thor ARM64 speed lane: interval for low-noise A64 profile summaries. "
    "0 disables inline counters and periodic top-function reports.",
    "a64");
DEFINE_uint32(
    arm64_speed_profile_top_functions, 12,
    "Thor ARM64 speed lane: number of functions to print in each profile "
    "summary.",
    "a64");
DEFINE_uint32(
    arm64_speed_profile_min_delta, 1,
    "Thor ARM64 speed lane: minimum entry-count delta before a function can "
    "appear in the periodic top-function report.",
    "a64");
DEFINE_string(
    arm64_speed_profile_body_time_filter, "",
    "Thor ARM64 speed lane: optional comma/semicolon/space separated guest "
    "function addresses or inclusive ranges for CNTVCT body-time profiling. "
    "Requires arm64_speed_profile_interval_ms.",
    "a64");
DEFINE_string(
    arm64_speed_profile_block_filter, "",
    "Thor ARM64 speed lane: optional comma/semicolon/space separated guest "
    "function start addresses or inclusive start-address ranges for "
    "selected-function block-entry profiling. Requires "
    "arm64_speed_profile_interval_ms.",
    "a64");
DEFINE_bool(
    arm64_speed_profile_thread_snapshot, false,
    "Thor ARM64 speed lane: log each guest thread's last A64 function and "
    "PPC context registers on every speed-profile interval.",
    "a64");
DEFINE_bool(
    arm64_speed_profile_thread_snapshot_on_idle, false,
    "Thor ARM64 speed lane: after A64 counters have been active, log one "
    "guest thread snapshot when a later profile interval goes idle.",
    "a64");

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

namespace {

std::string_view TrimAddressToken(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return value;
}

bool ParseAddressToken(std::string_view value, uint32_t* out_address) {
  value = TrimAddressToken(value);
  if (value.empty()) {
    return false;
  }

  int base = 10;
  if (value.size() > 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
    base = 16;
  } else {
    if (value.size() >= 8) {
      base = 16;
    }
    for (char c : value) {
      if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
        base = 16;
        break;
      }
    }
  }

  uint64_t parsed = 0;
  auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed, base);
  if (result.ec != std::errc() || result.ptr != value.data() + value.size() ||
      parsed > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  *out_address = static_cast<uint32_t>(parsed);
  return true;
}

bool FunctionMatchesAddressFilter(A64Function* function,
                                  std::string_view filter) {
  if (!function || filter.empty()) {
    return false;
  }

  uint32_t function_start = function->address();
  uint32_t function_end = function->end_address();
  if (function_end < function_start) {
    function_end = function_start;
  }

  size_t token_start = 0;
  while (token_start < filter.size()) {
    while (token_start < filter.size() &&
           (std::isspace(static_cast<unsigned char>(filter[token_start])) ||
            filter[token_start] == ',' || filter[token_start] == ';')) {
      ++token_start;
    }
    if (token_start >= filter.size()) {
      break;
    }

    size_t token_end = token_start;
    while (token_end < filter.size() && filter[token_end] != ',' &&
           filter[token_end] != ';' &&
           !std::isspace(static_cast<unsigned char>(filter[token_end]))) {
      ++token_end;
    }

    std::string_view token =
        TrimAddressToken(filter.substr(token_start, token_end - token_start));
    size_t range_separator = token.find('-');
    uint32_t start = 0;
    uint32_t end = 0;
    if (range_separator != std::string_view::npos) {
      if (ParseAddressToken(token.substr(0, range_separator), &start) &&
          ParseAddressToken(token.substr(range_separator + 1), &end)) {
        if (start > end) {
          std::swap(start, end);
        }
        if (start <= function_end && end >= function_start) {
          return true;
        }
      }
    } else if (ParseAddressToken(token, &start) && start >= function_start &&
               start <= function_end) {
      return true;
    }

    token_start = token_end;
  }

  return false;
}

bool FunctionStartMatchesAddressFilter(A64Function* function,
                                       std::string_view filter) {
  if (!function || filter.empty()) {
    return false;
  }

  uint32_t function_start = function->address();
  size_t token_start = 0;
  while (token_start < filter.size()) {
    while (token_start < filter.size() &&
           (std::isspace(static_cast<unsigned char>(filter[token_start])) ||
            filter[token_start] == ',' || filter[token_start] == ';')) {
      ++token_start;
    }
    if (token_start >= filter.size()) {
      break;
    }

    size_t token_end = token_start;
    while (token_end < filter.size() && filter[token_end] != ',' &&
           filter[token_end] != ';' &&
           !std::isspace(static_cast<unsigned char>(filter[token_end]))) {
      ++token_end;
    }

    std::string_view token =
        TrimAddressToken(filter.substr(token_start, token_end - token_start));
    size_t range_separator = token.find('-');
    uint32_t start = 0;
    uint32_t end = 0;
    if (range_separator != std::string_view::npos) {
      if (ParseAddressToken(token.substr(0, range_separator), &start) &&
          ParseAddressToken(token.substr(range_separator + 1), &end)) {
        if (start > end) {
          std::swap(start, end);
        }
        if (function_start >= start && function_start <= end) {
          return true;
        }
      }
    } else if (ParseAddressToken(token, &start) && function_start == start) {
      return true;
    }

    token_start = token_end;
  }

  return false;
}

const char* ThreadDebugStateName(ThreadDebugInfo::State state) {
  switch (state) {
    case ThreadDebugInfo::State::kAlive:
      return "alive";
    case ThreadDebugInfo::State::kWaiting:
      return "waiting";
    case ThreadDebugInfo::State::kExited:
      return "exited";
    case ThreadDebugInfo::State::kZombie:
      return "zombie";
    default:
      return "unknown";
  }
}

}  // namespace

// Resolve a guest function at runtime. Called by the resolve thunk when
// a guest address has not yet been compiled.
uint64_t ResolveFunction(void* raw_context, uint64_t target_address);

// ==========================================================================
// A64HelperEmitter — generates thunks using xbyak_aarch64.
// ==========================================================================
class A64HelperEmitter : public A64Emitter {
 public:
  A64HelperEmitter(A64Backend* backend, XbyakA64Allocator* allocator);

  HostToGuestThunk EmitHostToGuestThunk();
  GuestToHostThunk EmitGuestToHostThunk();
  ResolveFunctionThunk EmitResolveFunctionThunk();
  void* EmitGuestAndHostSynchronizeStackHelper();
};

A64HelperEmitter::A64HelperEmitter(A64Backend* backend,
                                   XbyakA64Allocator* allocator)
    : A64Emitter(backend, allocator) {}

// ==========================================================================
// Reservation helpers - implement PPC lwarx/stwcx semantics with a global
// per-cache-line bitmap so cross-thread stores invalidate other threads'
// reservations. Data-only CAS is ABA-vulnerable and can silently accept a
// stale PPC reservation.
// ==========================================================================
namespace {

A64BackendContext* BackendContextFromRawContext(void* raw_context) {
  return reinterpret_cast<A64BackendContext*>(
      reinterpret_cast<uint8_t*>(raw_context) - sizeof(A64BackendContext));
}

void ReserveOffsetAndBit(ReserveHelper* reserve_helper, uint32_t guest_address,
                         volatile uint64_t*& out_block, uint32_t& out_bit) {
  const uint32_t block_idx = guest_address >> A64_RESERVE_BLOCK_SHIFT;
  out_block = &reserve_helper->blocks[block_idx >> 6];
  out_bit = block_idx & 63;
}

extern "C" uint64_t TryAcquireReservationHelper(void* raw_context,
                                                uint64_t guest_address) {
  auto* bctx = BackendContextFromRawContext(raw_context);
  const uint32_t reserve_flag = 1u << kA64BackendHasReserveBit;
  // PPC lwarx drops any previous reservation.
  bctx->flags &= ~reserve_flag;

  volatile uint64_t* block = nullptr;
  uint32_t bit = 0;
  ReserveOffsetAndBit(bctx->reserve_helper_, uint32_t(guest_address), block,
                      bit);
  const uint64_t mask = uint64_t(1) << bit;

  bool acquired = false;
  while (true) {
    const uint64_t old = *block;
    if (old & mask) {
      break;
    }
    if (xe::atomic_cas(old, old | mask,
                       reinterpret_cast<volatile uint64_t*>(block))) {
      acquired = true;
      break;
    }
  }

  bctx->cached_reserve_offset = reinterpret_cast<uintptr_t>(block);
  bctx->cached_reserve_bit = bit;
  if (acquired) {
    bctx->flags |= reserve_flag;
  }
  return acquired ? 1 : 0;
}

template <typename T>
uint64_t ReservedStoreImpl(void* raw_context, uint64_t guest_address,
                           uint64_t host_address, uint64_t value) {
  auto* bctx = BackendContextFromRawContext(raw_context);
  const uint32_t reserve_flag = 1u << kA64BackendHasReserveBit;
  const bool had_reservation = (bctx->flags & reserve_flag) != 0;
  // PPC stwcx. always clears the reservation.
  bctx->flags &= ~reserve_flag;
  if (!had_reservation) {
    return 0;
  }

  volatile uint64_t* block = nullptr;
  uint32_t bit = 0;
  ReserveOffsetAndBit(bctx->reserve_helper_, uint32_t(guest_address), block,
                      bit);
  if (bctx->cached_reserve_offset != reinterpret_cast<uintptr_t>(block) ||
      bctx->cached_reserve_bit != bit) {
    assert_always();
    return 0;
  }

  bool exchange_ok = false;
  if constexpr (sizeof(T) == sizeof(uint64_t)) {
    exchange_ok = xe::atomic_cas(
        bctx->cached_reserve_value_, uint64_t(value),
        reinterpret_cast<volatile uint64_t*>(uintptr_t(host_address)));
  } else {
    exchange_ok = xe::atomic_cas(
        uint32_t(bctx->cached_reserve_value_), uint32_t(value),
        reinterpret_cast<volatile uint32_t*>(uintptr_t(host_address)));
  }

  const uint64_t mask = uint64_t(1) << bit;
  while (true) {
    const uint64_t old = *block;
    if ((old & mask) == 0) {
      break;
    }
    if (xe::atomic_cas(old, old & ~mask,
                       reinterpret_cast<volatile uint64_t*>(block))) {
      break;
    }
  }

  return exchange_ok ? 1 : 0;
}

extern "C" uint64_t ReservedStore32Helper(void* raw_context,
                                          uint64_t guest_address,
                                          uint64_t host_address,
                                          uint64_t value) {
  return ReservedStoreImpl<uint32_t>(raw_context, guest_address, host_address,
                                     value);
}

extern "C" uint64_t ReservedStore64Helper(void* raw_context,
                                          uint64_t guest_address,
                                          uint64_t host_address,
                                          uint64_t value) {
  return ReservedStoreImpl<uint64_t>(raw_context, guest_address, host_address,
                                     value);
}

}  // namespace

// --------------------------------------------------------------------------
// HostToGuestThunk
// --------------------------------------------------------------------------
// Called from host C++ code to enter JIT'd guest code.
//   x0 = target machine code address
//   x1 = PPCContext* (arg0)
//   x2 = return address value (arg1)
//
// ARM64 AAPCS64 calling convention:
//   Caller-saved: x0-x18, v0-v7, v16-v31
//   Callee-saved: x19-x28, x29(FP), x30(LR), d8-d15
//
// We save all callee-saved regs, set up context (x20) and membase (x21),
// then call the target. On return, restore and return to host.
HostToGuestThunk A64HelperEmitter::EmitHostToGuestThunk() {
  struct {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  code_offsets.prolog = getSize();

  // Allocate thunk stack frame.
  // Save x29(FP) and x30(LR) first, then callee-saved GPRs and NEON regs.
  const size_t thunk_stack = StackLayout::THUNK_STACK_SIZE;

  // sub sp, sp, #thunk_stack
  sub(sp, sp, static_cast<uint32_t>(thunk_stack));
  code_offsets.prolog_stack_alloc = getSize();

  // Save callee-saved GPRs: x19-x28, x29, x30
  stp(x19, x20, ptr(sp, 0x00));
  stp(x21, x22, ptr(sp, 0x10));
  stp(x23, x24, ptr(sp, 0x20));
  stp(x25, x26, ptr(sp, 0x30));
  stp(x27, x28, ptr(sp, 0x40));
  stp(x29, x30, ptr(sp, 0x50));

  // Save callee-saved NEON regs: full q8-q15 (JIT uses all 128 bits).
  stp(Xbyak_aarch64::QReg(8), Xbyak_aarch64::QReg(9), ptr(sp, 0x60));
  stp(Xbyak_aarch64::QReg(10), Xbyak_aarch64::QReg(11), ptr(sp, 0x80));
  stp(Xbyak_aarch64::QReg(12), Xbyak_aarch64::QReg(13), ptr(sp, 0xA0));
  stp(Xbyak_aarch64::QReg(14), Xbyak_aarch64::QReg(15), ptr(sp, 0xC0));

  code_offsets.body = getSize();

  if (backend()->speed_profile_enabled()) {
    EmitAtomicIncrement64(backend()->speed_profile_host_to_guest_entries());
  }

  // Set up guest execution state.
  // x20 = context (PPCContext*)
  mov(x20, x1);
  // x19 = backend context (immediately before PPCContext in memory)
  sub(x19, x20, static_cast<uint32_t>(sizeof(A64BackendContext)));
  // x21 = virtual_membase (loaded from context)
  ldr(x21, ptr(x20, static_cast<int32_t>(
                        offsetof(ppc::PPCContext, virtual_membase))));
  // Restore the guest scalar FPCR on every host->guest entry so host-side
  // work done before the call can't leak a stale rounding / non-IEEE mode.
  ldr(w11,
      ptr(x19, static_cast<uint32_t>(offsetof(A64BackendContext, fpcr_fpu))));
  msr(3, 3, 4, 4, 0, x11);
  // x0 still holds target, x2 holds return address.
  // The guest function's prolog stores x0 to GUEST_RET_ADDR on its stack
  // frame. Move the target to a scratch reg and put the guest return
  // address into x0.
  mov(x9, x0);  // x9 = target (scratch reg)
  // Pass guest return address in x0 (convention for guest function entry).
  mov(x0, x2);  // x0 = guest return address

  // Call the guest function.
  blr(x9);

  code_offsets.epilog = getSize();

  // Restore callee-saved NEON regs (full q8-q15).
  ldp(Xbyak_aarch64::QReg(14), Xbyak_aarch64::QReg(15), ptr(sp, 0xC0));
  ldp(Xbyak_aarch64::QReg(12), Xbyak_aarch64::QReg(13), ptr(sp, 0xA0));
  ldp(Xbyak_aarch64::QReg(10), Xbyak_aarch64::QReg(11), ptr(sp, 0x80));
  ldp(Xbyak_aarch64::QReg(8), Xbyak_aarch64::QReg(9), ptr(sp, 0x60));

  // Restore callee-saved GPRs.
  ldp(x29, x30, ptr(sp, 0x50));
  ldp(x27, x28, ptr(sp, 0x40));
  ldp(x25, x26, ptr(sp, 0x30));
  ldp(x23, x24, ptr(sp, 0x20));
  ldp(x21, x22, ptr(sp, 0x10));
  ldp(x19, x20, ptr(sp, 0x00));

  // Deallocate stack.
  add(sp, sp, static_cast<uint32_t>(thunk_stack));
  ret();

  code_offsets.tail = getSize();

  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = thunk_stack;
  func_info.lr_save_offset = 0x058;  // stp x29, x30, [sp, #0x50]

  void* fn = Emplace(func_info);
  return reinterpret_cast<HostToGuestThunk>(fn);
}

// --------------------------------------------------------------------------
// GuestToHostThunk
// --------------------------------------------------------------------------
// Called from guest JIT code to transition into a host (C++) function.
//   x0 = target host function
//   x1 = arg0
//   x2 = arg1
//
// We save volatile guest registers that we need to preserve across the
// host call, then call the host function with context as the first arg.
GuestToHostThunk A64HelperEmitter::EmitGuestToHostThunk() {
  struct {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  code_offsets.prolog = getSize();

  // The guest JIT uses v4-v15, v16-v31 as allocatable VEC regs.
  // v0-v7, v16-v31 are caller-saved in AAPCS64 (fully clobbered by C).
  // v8-v15 lower 64 bits are callee-saved, but upper 64 bits are not.
  // We must save all guest-allocated VEC regs (full 128-bit Q regs).
  // GPRs x19-x28 are callee-saved in AAPCS64, so the C function preserves them.
  //
  // Stack layout:
  //   q4, q5       sp + 0x000  (32 bytes)
  //   q6, q7       sp + 0x020
  //   q8, q9       sp + 0x040
  //   q10, q11     sp + 0x060
  //   q12, q13     sp + 0x080
  //   q14, q15     sp + 0x0A0
  //   q16, q17     sp + 0x0C0
  //   q18, q19     sp + 0x0E0
  //   q20, q21     sp + 0x100
  //   q22, q23     sp + 0x120
  //   q24, q25     sp + 0x140
  //   q26, q27     sp + 0x160
  //   q28, q29     sp + 0x180
  //   q30, q31     sp + 0x1A0
  //   x29, x30     sp + 0x1C0
  //   Total: 0x1D0 = 464 bytes (16-byte aligned)
  const size_t g2h_stack = 464;
  sub(sp, sp, static_cast<uint32_t>(g2h_stack));
  code_offsets.prolog_stack_alloc = getSize();

  // Save guest-allocated VEC regs (full Q = 128-bit).
  stp(Xbyak_aarch64::QReg(4), Xbyak_aarch64::QReg(5), ptr(sp, 0x000));
  stp(Xbyak_aarch64::QReg(6), Xbyak_aarch64::QReg(7), ptr(sp, 0x020));
  stp(Xbyak_aarch64::QReg(8), Xbyak_aarch64::QReg(9), ptr(sp, 0x040));
  stp(Xbyak_aarch64::QReg(10), Xbyak_aarch64::QReg(11), ptr(sp, 0x060));
  stp(Xbyak_aarch64::QReg(12), Xbyak_aarch64::QReg(13), ptr(sp, 0x080));
  stp(Xbyak_aarch64::QReg(14), Xbyak_aarch64::QReg(15), ptr(sp, 0x0A0));
  stp(Xbyak_aarch64::QReg(16), Xbyak_aarch64::QReg(17), ptr(sp, 0x0C0));
  stp(Xbyak_aarch64::QReg(18), Xbyak_aarch64::QReg(19), ptr(sp, 0x0E0));
  stp(Xbyak_aarch64::QReg(20), Xbyak_aarch64::QReg(21), ptr(sp, 0x100));
  stp(Xbyak_aarch64::QReg(22), Xbyak_aarch64::QReg(23), ptr(sp, 0x120));
  stp(Xbyak_aarch64::QReg(24), Xbyak_aarch64::QReg(25), ptr(sp, 0x140));
  stp(Xbyak_aarch64::QReg(26), Xbyak_aarch64::QReg(27), ptr(sp, 0x160));
  stp(Xbyak_aarch64::QReg(28), Xbyak_aarch64::QReg(29), ptr(sp, 0x180));
  stp(Xbyak_aarch64::QReg(30), Xbyak_aarch64::QReg(31), ptr(sp, 0x1A0));
  // Save x29/x30 (FP/LR).
  stp(x29, x30, ptr(sp, 0x1C0));

  code_offsets.body = getSize();

  if (backend()->speed_profile_enabled()) {
    EmitAtomicIncrement64(backend()->speed_profile_guest_to_host_calls());
  }

  // Call host function.
  // AAPCS64: x0=first arg. We set x0=context (from x20).
  mov(x9, x0);   // x9 = target function (scratch)
  mov(x0, x20);  // x0 = PPCContext* (our context reg)
  // x1, x2, x3 already hold args from the caller.
  blr(x9);

  // Host callbacks may change FPCR. Restore the guest scalar FPCR before
  // resuming the JIT so later guest ops observe the cached PPC mode.
  // x19 (backend context) is callee-saved, so it survives the host call.
  ldr(w11,
      ptr(x19, static_cast<uint32_t>(offsetof(A64BackendContext, fpcr_fpu))));
  msr(3, 3, 4, 4, 0, x11);

  code_offsets.epilog = getSize();

  // Restore.
  ldp(x29, x30, ptr(sp, 0x1C0));
  ldp(Xbyak_aarch64::QReg(30), Xbyak_aarch64::QReg(31), ptr(sp, 0x1A0));
  ldp(Xbyak_aarch64::QReg(28), Xbyak_aarch64::QReg(29), ptr(sp, 0x180));
  ldp(Xbyak_aarch64::QReg(26), Xbyak_aarch64::QReg(27), ptr(sp, 0x160));
  ldp(Xbyak_aarch64::QReg(24), Xbyak_aarch64::QReg(25), ptr(sp, 0x140));
  ldp(Xbyak_aarch64::QReg(22), Xbyak_aarch64::QReg(23), ptr(sp, 0x120));
  ldp(Xbyak_aarch64::QReg(20), Xbyak_aarch64::QReg(21), ptr(sp, 0x100));
  ldp(Xbyak_aarch64::QReg(18), Xbyak_aarch64::QReg(19), ptr(sp, 0x0E0));
  ldp(Xbyak_aarch64::QReg(16), Xbyak_aarch64::QReg(17), ptr(sp, 0x0C0));
  ldp(Xbyak_aarch64::QReg(14), Xbyak_aarch64::QReg(15), ptr(sp, 0x0A0));
  ldp(Xbyak_aarch64::QReg(12), Xbyak_aarch64::QReg(13), ptr(sp, 0x080));
  ldp(Xbyak_aarch64::QReg(10), Xbyak_aarch64::QReg(11), ptr(sp, 0x060));
  ldp(Xbyak_aarch64::QReg(8), Xbyak_aarch64::QReg(9), ptr(sp, 0x040));
  ldp(Xbyak_aarch64::QReg(6), Xbyak_aarch64::QReg(7), ptr(sp, 0x020));
  ldp(Xbyak_aarch64::QReg(4), Xbyak_aarch64::QReg(5), ptr(sp, 0x000));

  add(sp, sp, static_cast<uint32_t>(g2h_stack));
  ret();

  code_offsets.tail = getSize();

  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = g2h_stack;
  func_info.lr_save_offset = 0x1C8;  // stp x29, x30, [sp, #0x1C0]

  void* fn = Emplace(func_info);
  return reinterpret_cast<GuestToHostThunk>(fn);
}

// --------------------------------------------------------------------------
// ResolveFunctionThunk
// --------------------------------------------------------------------------
// Called when guest code calls an unresolved function address.
// The indirection table initially points all entries here.
// We call ResolveFunction to compile/lookup the target, then jump to it.
//
// On entry from the indirection table:
//   w16 = guest PPC address (loaded by the call sequence)
//   x20 = context
//   x30 = return address (from the BLR that got us here)
ResolveFunctionThunk A64HelperEmitter::EmitResolveFunctionThunk() {
  struct {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  code_offsets.prolog = getSize();

  const size_t thunk_stack = StackLayout::THUNK_STACK_SIZE;
  sub(sp, sp, static_cast<uint32_t>(thunk_stack));
  code_offsets.prolog_stack_alloc = getSize();

  // Save x29/x30 and x0 (guest return address, needed by the resolved
  // function's prolog). x19 is callee-saved so it survives the C call.
  stp(x29, x30, ptr(sp, 0x50));
  stp(x0, x19, ptr(sp, 0x00));  // save x0 (guest ret addr) and x19

  code_offsets.body = getSize();

  // Call ResolveFunction(context, target_address).
  mov(x0, x20);  // x0 = PPCContext*
  mov(x1, x16);  // x1 = guest address (32-bit in w16)
  // Load address of ResolveFunction.
  mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
  blr(x9);
  // x0 now holds the resolved host machine code address.
  mov(x9, x0);

  code_offsets.epilog = getSize();

  // Restore x0 (guest return address) and saved regs.
  ldp(x0, x19, ptr(sp, 0x00));
  ldp(x29, x30, ptr(sp, 0x50));
  add(sp, sp, static_cast<uint32_t>(thunk_stack));

  cbz(x9, 8);   // skip br x9 if null, fall through to brk
  br(x9);       // Jump to the resolved function (tail call — preserves LR).
  brk(0xF000);  // Resolution failed — trap for debugging.

  code_offsets.tail = getSize();

  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = thunk_stack;
  func_info.lr_save_offset = 0x058;  // stp x29, x30, [sp, #0x50]

  void* fn = Emplace(func_info);
  return reinterpret_cast<ResolveFunctionThunk>(fn);
}

// --------------------------------------------------------------------------
// GuestAndHostSynchronizeStackHelper
// --------------------------------------------------------------------------
// Called when longjmp is detected (guest r1 changed after a call returned).
// Walks the stackpoint array backward to find the matching host SP, restores
// it, and jumps back to the caller.
//
// On entry (set by the tail-emitted sync check in the guest function):
//   x8  = return address (where to jump after fixup)
//   x9  = caller's stack size (to subtract from restored SP)
//   x19 = A64BackendContext*
//   x20 = PPCContext*
void* A64HelperEmitter::EmitGuestAndHostSynchronizeStackHelper() {
  using namespace Xbyak_aarch64;
  struct {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  code_offsets.prolog = getSize();
  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();

  // x19 = backend context pointer (already set up by HostToGuestThunk)

  // x10 = stackpoints array pointer
  ldr(x10, ptr(x19, static_cast<uint32_t>(
                        offsetof(A64BackendContext, stackpoints))));
  // w11 = current_stackpoint_depth
  ldr(w11, ptr(x19, static_cast<uint32_t>(offsetof(A64BackendContext,
                                                   current_stackpoint_depth))));

  // w12 = current guest r1
  ldr(w12, ptr(x20, static_cast<int32_t>(offsetof(ppc::PPCContext, r[1]))));

  // Search backward through stackpoints for the first entry where
  // guest_stack_ >= current r1 (guest stack was unwound past that frame).
  // ecx = loop index, starting at depth - 1
  sub(w13, w11, 1);

  auto& loop = NewCachedLabel();
  auto& found = NewCachedLabel();
  auto& underflow = NewCachedLabel();

  L(loop);
  // Bounds check
  tbnz(w13, 31, underflow);  // if index went negative, bail

  // x14 = &stackpoints[w13] = x10 + w13 * sizeof(A64BackendStackpoint)
  mov(w14, static_cast<uint32_t>(sizeof(A64BackendStackpoint)));
  umull(x14, w13, w14);
  add(x14, x10, x14);

  // w15 = stackpoints[index].guest_stack_
  ldr(w15, ptr(x14, static_cast<uint32_t>(
                        offsetof(A64BackendStackpoint, guest_stack_))));

  // If guest_stack_ >= current r1, we found our target frame.
  cmp(w15, w12);
  b(GE, found);

  // Not found yet, go to previous entry.
  sub(w13, w13, 1);
  b(loop);

  L(found);
  // x14 points to the matching stackpoint entry.
  // Restore host SP from stackpoints[index].host_stack_
  ldr(x16, ptr(x14, static_cast<uint32_t>(
                        offsetof(A64BackendStackpoint, host_stack_))));

  // Adjust for the caller's stack frame: SP = host_stack_ - stack_size
  sub(x16, x16, x9);
  mov(sp, x16);

  // Update current_stackpoint_depth = index + 1
  // (the entry we restored to has been consumed)
  add(w13, w13, 1);
  str(w13, ptr(x19, static_cast<uint32_t>(offsetof(A64BackendContext,
                                                   current_stackpoint_depth))));

  // Jump back to the caller.
  br(x8);

  L(underflow);
  // Should be impossible — stackpoint array underflowed.
  brk(0xF001);  // assertion failure

  code_offsets.epilog = getSize();
  code_offsets.tail = getSize();

  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = 0;

  return Emplace(func_info);
}

// ==========================================================================
// ResolveFunction — runtime function resolution.
// ==========================================================================
uint64_t ResolveFunction(void* raw_context, uint64_t target_address) {
  auto guest_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  auto thread_state = guest_context->thread_state;
  assert_not_zero(target_address);
  auto backend =
      reinterpret_cast<A64Backend*>(thread_state->processor()->backend());

  auto fn = thread_state->processor()->ResolveFunction(
      static_cast<uint32_t>(target_address));
  if (!fn) {
    backend->RecordResolveFunction(false);
    // Unresolvable — return 0 which will fault.
    return 0;
  }

  auto guest_fn = static_cast<GuestFunction*>(fn);
  auto code = guest_fn->machine_code();
  if (!code) {
    backend->RecordResolveFunction(false);
    return 0;
  }
  backend->RecordResolveFunction(true);
  return reinterpret_cast<uint64_t>(code);
}

// ==========================================================================
// A64Backend
// ==========================================================================

bool A64Backend::speed_profile_enabled() const {
  return cvars::arm64_speed_profile_interval_ms != 0;
}

bool A64Backend::BodyTimeProfileEnabledForFunction(
    A64Function* function) const {
  if (!speed_profile_enabled() ||
      cvars::arm64_speed_profile_body_time_filter.empty()) {
    return false;
  }
  return FunctionMatchesAddressFilter(
      function, cvars::arm64_speed_profile_body_time_filter);
}

bool A64Backend::BlockProfileEnabledForFunction(A64Function* function) const {
  if (!speed_profile_enabled() ||
      cvars::arm64_speed_profile_block_filter.empty()) {
    return false;
  }
  return FunctionStartMatchesAddressFilter(
      function, cvars::arm64_speed_profile_block_filter);
}

void A64Backend::RecordResolveFunction(bool success) {
  if (!speed_profile_enabled()) {
    return;
  }
  speed_profile_resolve_calls_.fetch_add(1, std::memory_order_relaxed);
  if (!success) {
    speed_profile_resolve_misses_.fetch_add(1, std::memory_order_relaxed);
  }
}

void A64Backend::RegisterProfiledFunction(A64Function* function) {
  if (!speed_profile_enabled() || !function) {
    return;
  }

  std::lock_guard<std::mutex> lock(speed_profile_mutex_);
  auto it = std::find_if(
      speed_profile_functions_.begin(), speed_profile_functions_.end(),
      [function](const ProfiledFunctionEntry& entry) {
        return entry.function == function;
      });
  if (it == speed_profile_functions_.end()) {
    speed_profile_functions_.push_back({function, 0});
  }
}

void A64Backend::UnregisterProfiledFunction(A64Function* function) {
  if (!function) {
    return;
  }

  std::lock_guard<std::mutex> lock(speed_profile_mutex_);
  speed_profile_functions_.erase(
      std::remove_if(
          speed_profile_functions_.begin(), speed_profile_functions_.end(),
          [function](const ProfiledFunctionEntry& entry) {
            return entry.function == function;
          }),
      speed_profile_functions_.end());
}

void A64Backend::StartSpeedProfiler() {
  if (!speed_profile_enabled() || speed_profile_timer_) {
    return;
  }

  uint32_t interval_ms = cvars::arm64_speed_profile_interval_ms;
  uint32_t top_functions = cvars::arm64_speed_profile_top_functions;
  uint32_t min_delta = cvars::arm64_speed_profile_min_delta;
  XELOGW(
      "A64 speed profile enabled: interval_ms={} top_functions={} "
      "min_delta={}",
      interval_ms, top_functions, min_delta);
  if (cvars::arm64_add_sub_imm_audit) {
    XELOGW(
        "A64 ADD/SUB immediate audit enabled: function={:08X} budget={}",
        cvars::arm64_add_sub_imm_audit_function,
        cvars::arm64_add_sub_imm_audit_budget);
  }
  if (cvars::arm64_add_i64_wrapped_imm_fastpath) {
    XELOGW("A64 ADD_I64 wrapped immediate fastpath enabled: function={:08X}",
           cvars::arm64_add_i64_wrapped_imm_fastpath_function);
  }
  if (cvars::arm64_immediate_lowering_audit) {
    XELOGW("A64 immediate lowering audit enabled: function={:08X} budget={}",
           cvars::arm64_immediate_lowering_audit_function,
           cvars::arm64_immediate_lowering_audit_budget);
  }
  if (cvars::arm64_context_traffic_audit) {
    XELOGW("A64 context traffic audit enabled: function={:08X} budget={}",
           cvars::arm64_context_traffic_audit_function,
           cvars::arm64_context_traffic_audit_budget);
  }
  speed_profile_timer_ = threading::HighResolutionTimer::CreateRepeating(
      std::chrono::milliseconds(interval_ms), [this]() { LogSpeedProfile(); });
}

void A64Backend::LogSpeedProfile() {
  if (!speed_profile_enabled()) {
    return;
  }

  struct FunctionSample {
    uint32_t address = 0;
    std::string name;
    uint64_t total = 0;
    uint64_t delta = 0;
    uint64_t body_ticks_total = 0;
    uint64_t body_ticks_delta = 0;
    uint64_t body_ticks_per_entry = 0;
    size_t code_size = 0;
  };
  struct BlockSample {
    uint32_t function_address = 0;
    uint32_t block_address = 0;
    uint16_t block_ordinal = 0;
    std::string function_name;
    uint64_t total = 0;
    uint64_t delta = 0;
  };

  std::vector<FunctionSample> samples;
  std::vector<FunctionSample> body_samples;
  std::vector<BlockSample> block_samples;
  uint64_t entry_delta_total = 0;
  size_t function_count = 0;
  {
    std::lock_guard<std::mutex> lock(speed_profile_mutex_);
    function_count = speed_profile_functions_.size();
    samples.reserve(function_count);
    for (auto& entry : speed_profile_functions_) {
      A64Function* function = entry.function;
      if (!function) {
        continue;
      }
      uint64_t total =
          function->profile_entry_count()->load(std::memory_order_relaxed);
      uint64_t delta = total - entry.last_entry_count;
      entry.last_entry_count = total;
      uint64_t body_total =
          function->profile_body_ticks()->load(std::memory_order_relaxed);
      uint64_t body_delta = body_total - entry.last_body_ticks;
      entry.last_body_ticks = body_total;
      entry_delta_total += delta;
      if (delta < cvars::arm64_speed_profile_min_delta && body_delta == 0) {
        continue;
      }
      std::string name = function->name();
      if (name.empty()) {
        name = fmt::format("sub_{:08X}", function->address());
      }
      uint64_t body_ticks_per_entry = delta ? body_delta / delta : 0;
      FunctionSample sample = {function->address(),
                               name,
                               total,
                               delta,
                               body_total,
                               body_delta,
                               body_ticks_per_entry,
                               function->machine_code_length()};
      if (delta >= cvars::arm64_speed_profile_min_delta) {
        samples.push_back(sample);
      }
      if (body_delta != 0) {
        body_samples.push_back(std::move(sample));
      }

      size_t block_count = function->profile_block_count_count();
      if (block_count != 0) {
        if (entry.last_block_counts.size() != block_count) {
          entry.last_block_counts.assign(block_count, 0);
        }
        for (size_t i = 0; i < block_count; ++i) {
          auto* block_counter = function->profile_block_count(i);
          if (!block_counter) {
            continue;
          }
          uint64_t block_total =
              block_counter->load(std::memory_order_relaxed);
          uint64_t block_delta = block_total - entry.last_block_counts[i];
          entry.last_block_counts[i] = block_total;
          if (block_delta < cvars::arm64_speed_profile_min_delta) {
            continue;
          }
          block_samples.push_back(
              {function->address(), function->profile_block_address(i),
               static_cast<uint16_t>(i), name, block_total, block_delta});
        }
      }
    }
  }

  std::sort(samples.begin(), samples.end(),
            [](const FunctionSample& a, const FunctionSample& b) {
              return a.delta > b.delta;
            });
  std::sort(body_samples.begin(), body_samples.end(),
            [](const FunctionSample& a, const FunctionSample& b) {
              return a.body_ticks_delta > b.body_ticks_delta;
            });
  std::sort(block_samples.begin(), block_samples.end(),
            [](const BlockSample& a, const BlockSample& b) {
              return a.delta > b.delta;
            });

  auto load_delta = [](std::atomic<uint64_t>& counter, uint64_t& last) {
    uint64_t total = counter.load(std::memory_order_relaxed);
    uint64_t delta = total - last;
    last = total;
    return std::pair<uint64_t, uint64_t>{total, delta};
  };
  auto h2g = load_delta(speed_profile_host_to_guest_entries_,
                        last_speed_profile_host_to_guest_entries_);
  auto g2h = load_delta(speed_profile_guest_to_host_calls_,
                        last_speed_profile_guest_to_host_calls_);
  auto direct = load_delta(speed_profile_direct_guest_calls_,
                           last_speed_profile_direct_guest_calls_);
  auto indirect = load_delta(speed_profile_indirect_guest_calls_,
                             last_speed_profile_indirect_guest_calls_);
  auto extern_calls =
      load_delta(speed_profile_extern_calls_, last_speed_profile_extern_calls_);
  auto resolves =
      load_delta(speed_profile_resolve_calls_, last_speed_profile_resolve_calls_);
  auto resolve_misses = load_delta(speed_profile_resolve_misses_,
                                   last_speed_profile_resolve_misses_);

  XELOGW(
      "A64 speed profile summary: funcs={} entry_delta={} h2g={}/{} "
      "g2h={}/{} direct={}/{} indirect={}/{} extern={}/{} resolves={}/{} "
      "resolve_misses={}/{}",
      function_count, entry_delta_total, h2g.second, h2g.first, g2h.second,
      g2h.first, direct.second, direct.first, indirect.second, indirect.first,
      extern_calls.second, extern_calls.first, resolves.second, resolves.first,
      resolve_misses.second, resolve_misses.first);

  const bool interval_had_activity =
      entry_delta_total || h2g.second || g2h.second || direct.second ||
      indirect.second || extern_calls.second || resolves.second ||
      resolve_misses.second;
  bool idle_snapshot_requested = false;
  if (interval_had_activity) {
    speed_profile_seen_activity_ = true;
    speed_profile_idle_snapshot_emitted_ = false;
  } else if (cvars::arm64_speed_profile_thread_snapshot_on_idle &&
             speed_profile_seen_activity_ &&
             !speed_profile_idle_snapshot_emitted_) {
    idle_snapshot_requested = true;
    speed_profile_idle_snapshot_emitted_ = true;
  }

  size_t top_count = std::min<size_t>(
      samples.size(), cvars::arm64_speed_profile_top_functions);
  for (size_t i = 0; i < top_count; ++i) {
    const auto& sample = samples[i];
    XELOGW("A64 speed profile top {:02}: fn {:08X} '{}' delta={} total={} "
           "code_size={}",
           i + 1, sample.address, sample.name, sample.delta, sample.total,
           sample.code_size);
  }

  size_t body_top_count = std::min<size_t>(
      body_samples.size(), cvars::arm64_speed_profile_top_functions);
  for (size_t i = 0; i < body_top_count; ++i) {
    const auto& sample = body_samples[i];
    XELOGW(
        "A64 speed profile body top {:02}: fn {:08X} '{}' "
        "body_ticks_delta={} body_ticks_total={} entries_delta={} "
        "ticks_per_entry={} code_size={}",
        i + 1, sample.address, sample.name, sample.body_ticks_delta,
        sample.body_ticks_total, sample.delta, sample.body_ticks_per_entry,
        sample.code_size);
  }

  size_t block_top_count = std::min<size_t>(
      block_samples.size(), cvars::arm64_speed_profile_top_functions);
  for (size_t i = 0; i < block_top_count; ++i) {
    const auto& sample = block_samples[i];
    XELOGW(
        "A64 speed profile block top {:02}: fn {:08X} '{}' block={} "
        "guest={:08X} delta={} total={}",
        i + 1, sample.function_address, sample.function_name,
        sample.block_ordinal, sample.block_address, sample.delta,
        sample.total);
  }

  const bool should_log_thread_snapshot =
      cvars::arm64_speed_profile_thread_snapshot || idle_snapshot_requested;
  if (should_log_thread_snapshot && processor()) {
    if (idle_snapshot_requested) {
      XELOGW("A64 idle thread snapshot trigger: counters went flat after "
             "previous activity");
    }
    bool acquired_thread_debug_lock = false;
    std::vector<ThreadDebugInfo*> thread_infos;
    for (uint32_t attempt = 0; attempt < 20; ++attempt) {
      thread_infos =
          processor()->TryQueryThreadDebugInfos(&acquired_thread_debug_lock);
      if (acquired_thread_debug_lock) {
        break;
      }
      xe::threading::Sleep(std::chrono::milliseconds(1));
    }
    if (!acquired_thread_debug_lock) {
      auto owner = ppc::QueryGlobalLockOwnerSnapshot();
      XELOGW(
          "A64 thread snapshot skipped: processor debug lock busy "
          "after_retries=20 last_global_owner_sys_tid={} global_lock_count={} "
          "owner_tid={:08X} owner_lr={:08X} owner_ctr={:08X} "
          "owner_r1={:08X} owner_r3={:08X} owner_r4={:08X}",
          xe::global_critical_region::last_owner_system_thread_id(),
          owner.count, owner.thread_id, owner.lr, owner.ctr, owner.r1, owner.r3,
          owner.r4);
      return;
    }
    for (auto* thread_info : thread_infos) {
      if (!thread_info) {
        continue;
      }
      auto* thread = thread_info->thread;
      auto* thread_state = thread ? thread->thread_state() : nullptr;
      auto* context = thread_state ? thread_state->context() : nullptr;
      uint32_t lr = context ? static_cast<uint32_t>(context->lr) : 0;
      uint32_t ctr = context ? static_cast<uint32_t>(context->ctr) : 0;
      uint32_t r1 = context ? static_cast<uint32_t>(context->r[1]) : 0;
      uint32_t r3 = context ? static_cast<uint32_t>(context->r[3]) : 0;
      uint32_t r4 = context ? static_cast<uint32_t>(context->r[4]) : 0;
      uint32_t last_fn = 0;
      uint32_t last_ret = 0;
      if (context) {
        auto* a64_context = BackendContextForGuestContext(context);
        last_fn = a64_context->last_guest_function;
        last_ret = a64_context->last_guest_return_address;
      }
      XELOGW(
          "A64 thread snapshot tid={:08X} handle={:08X} state={} "
          "last_fn={:08X} last_ret={:08X} lr={:08X} ctr={:08X} "
          "r1={:08X} r3={:08X} r4={:08X}",
          thread_info->thread_id, thread_info->thread_handle,
          ThreadDebugStateName(thread_info->state), last_fn, last_ret, lr, ctr,
          r1, r3, r4);
    }
  }
}

// ARM64 guest trampoline template.
// Loads proc, userdata1, userdata2 into x0-x2, then jumps to guest_to_host
// thunk via x9.  Each 64-bit immediate uses movz + 3x movk (16 bytes).
// Total: 4 registers × 16 bytes + 4 bytes (br x9) = 68 bytes.
//
// Template layout (offsets where 64-bit immediates are patched):
//   +0x00: movz x0, #imm16; movk x0, ..., lsl 16/32/48  -> proc
//   +0x10: movz x1, #imm16; movk x1, ..., lsl 16/32/48  -> userdata1
//   +0x20: movz x2, #imm16; movk x2, ..., lsl 16/32/48  -> userdata2
//   +0x30: movz x9, #imm16; movk x9, ..., lsl 16/32/48  -> g2h thunk
//   +0x40: br x9
//
// ARM64 encoding helpers:
//   movz xN, #imm16          = 0xD2800000 | (imm16 << 5) | N
//   movk xN, #imm16, lsl #S  = 0xF2800000 | (hw << 21) | (imm16 << 5) | N
//     where hw = S/16 (0,1,2,3)
static void EncodeMovImm64(uint32_t* out, uint32_t reg, uint64_t imm) {
  out[0] = 0xD2800000 | (static_cast<uint32_t>(imm & 0xFFFF) << 5) | reg;
  out[1] =
      0xF2A00000 | (static_cast<uint32_t>((imm >> 16) & 0xFFFF) << 5) | reg;
  out[2] =
      0xF2C00000 | (static_cast<uint32_t>((imm >> 32) & 0xFFFF) << 5) | reg;
  out[3] =
      0xF2E00000 | (static_cast<uint32_t>((imm >> 48) & 0xFFFF) << 5) | reg;
}

static constexpr size_t kGuestTrampolineSize = 68;  // 17 instructions × 4
static constexpr uint32_t kTrampolineOffsetProc = 0x00;
static constexpr uint32_t kTrampolineOffsetArg1 = 0x10;
static constexpr uint32_t kTrampolineOffsetArg2 = 0x20;
static constexpr uint32_t kTrampolineOffsetThunk = 0x30;

static void BuildGuestTrampoline(uint8_t* buf, void* proc, void* userdata1,
                                 void* userdata2, void* g2h_thunk) {
  auto* code = reinterpret_cast<uint32_t*>(buf);
  // x0 = proc (target function for guest-to-host thunk)
  EncodeMovImm64(&code[0], 0, reinterpret_cast<uint64_t>(proc));
  // x1 = userdata1
  EncodeMovImm64(&code[4], 1, reinterpret_cast<uint64_t>(userdata1));
  // x2 = userdata2
  EncodeMovImm64(&code[8], 2, reinterpret_cast<uint64_t>(userdata2));
  // x9 = guest_to_host_thunk
  EncodeMovImm64(&code[12], 9, reinterpret_cast<uint64_t>(g2h_thunk));
  // br x9
  code[16] = 0xD61F0120;  // br x9
}

A64Backend::A64Backend() {
  code_cache_ = A64CodeCache::Create();

  // Allocate executable memory for guest trampolines.
#if 0
  uint32_t base_address = 0x10000;
  void* buf = nullptr;
  while (base_address < 0x80000000) {
    buf = memory::AllocFixed(
        reinterpret_cast<void*>(static_cast<uintptr_t>(base_address)),
        kGuestTrampolineSize * MAX_GUEST_TRAMPOLINES,
        xe::memory::AllocationType::kReserveCommit,
        xe::memory::PageAccess::kExecuteReadWrite);
    if (!buf) {
      base_address += 65536;
    } else {
      break;
    }
  }
#else
    void* buf = reinterpret_cast<uint8_t*>(memory::AllocFixed(
            (void*)(uintptr_t)code_cache_->execute_address_high(),
            kGuestTrampolineSize * MAX_GUEST_TRAMPOLINES,
            xe::memory::AllocationType::kReserveCommit,
            xe::memory::PageAccess::kExecuteReadWrite));
#endif
  xenia_assert(buf);
  guest_trampoline_memory_ = reinterpret_cast<uint8_t*>(buf);
  guest_trampoline_address_bitmap_.Resize(MAX_GUEST_TRAMPOLINES);
}

A64Backend::~A64Backend() {
  speed_profile_timer_.reset();
  ExceptionHandler::Uninstall(&ExceptionCallbackThunk, this);
  if (guest_trampoline_memory_) {
    memory::DeallocFixed(guest_trampoline_memory_,
                         kGuestTrampolineSize * MAX_GUEST_TRAMPOLINES,
                         memory::DeallocationType::kRelease);
    guest_trampoline_memory_ = nullptr;
  }
}

bool A64Backend::Initialize(Processor* processor) {
  if (!Backend::Initialize(processor)) {
    return false;
  }

  // Initialize the code cache.
  if (!code_cache_->Initialize()) {
    XELOGE("A64Backend: Failed to initialize code cache");
    return false;
  }

  // Expose the code cache to the base Backend class.
  Backend::code_cache_ = code_cache_.get();

  // Set up machine info for the register allocator.
  machine_info_.supports_extended_load_store = true;
  // GPR set: x22-x28 (7 registers; x19=backend ctx, x20=context, x21=membase)
  auto& gpr_set = machine_info_.register_sets[0];
  gpr_set.id = 0;
  std::strcpy(gpr_set.name, "gpr");
  gpr_set.types = MachineInfo::RegisterSet::INT_TYPES;
  gpr_set.count = A64Emitter::GPR_COUNT;
  // VEC set: v4-v15, v16-v31 (28 registers, v0-v3 scratch)
  auto& vec_set = machine_info_.register_sets[1];
  vec_set.id = 1;
  std::strcpy(vec_set.name, "vec");
  vec_set.types = MachineInfo::RegisterSet::FLOAT_TYPES |
                  MachineInfo::RegisterSet::VEC_TYPES;
  vec_set.count = A64Emitter::VEC_COUNT;

  // Generate thunks using ARM64 assembler.
  XbyakA64Allocator allocator;
  A64HelperEmitter thunk_emitter(this, &allocator);

  host_to_guest_thunk_ = thunk_emitter.EmitHostToGuestThunk();
  guest_to_host_thunk_ = thunk_emitter.EmitGuestToHostThunk();
  resolve_function_thunk_ = thunk_emitter.EmitResolveFunctionThunk();

  if (!host_to_guest_thunk_ || !guest_to_host_thunk_ ||
      !resolve_function_thunk_) {
    XELOGE("A64Backend: Failed to generate thunks");
    return false;
  }

  if (cvars::a64_enable_host_guest_stack_synchronization) {
    synchronize_guest_and_host_stack_helper_ =
        thunk_emitter.EmitGuestAndHostSynchronizeStackHelper();
  }

  // Wire up reservation helpers used by RESERVED_LOAD/STORE codegen.
  try_acquire_reservation_helper_ =
      reinterpret_cast<void*>(&TryAcquireReservationHelper);
  reserved_store_32_helper = reinterpret_cast<void*>(&ReservedStore32Helper);
  reserved_store_64_helper = reinterpret_cast<void*>(&ReservedStore64Helper);

  // Set the indirection table default to point at the resolve thunk.
  code_cache_->set_indirection_default(
      uint32_t(reinterpret_cast<uint64_t>(resolve_function_thunk_)));

  // Commit the indirection table range used by guest trampolines so that
  // CreateGuestTrampoline can call AddIndirection without faulting.
  code_cache_->CommitExecutableRange(GUEST_TRAMPOLINE_BASE,
                                     GUEST_TRAMPOLINE_END);

  // Commit special indirection ranges (force return address, etc.).
  code_cache_->CommitExecutableRange(0x9FFF0000, 0x9FFFFFFF);

  // Register exception handler for MMIO access from JIT code.
  ExceptionHandler::Install(ExceptionCallbackThunk, this);
  StartSpeedProfiler();

  return true;
}

void A64Backend::CommitExecutableRange(uint32_t guest_low,
                                       uint32_t guest_high) {
  code_cache_->CommitExecutableRange(guest_low, guest_high);
}

std::unique_ptr<Assembler> A64Backend::CreateAssembler() {
  return std::make_unique<A64Assembler>(this);
}

std::unique_ptr<GuestFunction> A64Backend::CreateGuestFunction(
    Module* module, uint32_t address) {
  return std::make_unique<A64Function>(module, address);
}

uint64_t A64Backend::CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                                  uint64_t current_pc) {
  // ARM64 instructions are fixed 4 bytes.
  return current_pc + 4;
}

// ARM64 BRK #0 encoding (4 bytes, fixed-width instruction).
static constexpr uint32_t kArm64Brk0 = 0xD4200000;
static constexpr uint32_t kArm64BrkMask = 0xFFE0001F;

static bool IsArm64Brk(uint32_t instruction) {
  return (instruction & kArm64BrkMask) == kArm64Brk0;
}

static uint32_t Arm64BrkImmediate(uint32_t instruction) {
  return (instruction >> 5) & 0xFFFF;
}

void A64Backend::InstallBreakpoint(Breakpoint* breakpoint) {
  breakpoint->ForEachHostAddress([breakpoint](uint64_t host_address) {
    auto ptr = reinterpret_cast<void*>(host_address);
    auto original_bytes = xe::load<uint32_t>(ptr);
    assert_true(original_bytes != kArm64Brk0);
    xe::store<uint32_t>(ptr, kArm64Brk0);
    breakpoint->backend_data().emplace_back(host_address, original_bytes);
  });
}

void A64Backend::InstallBreakpoint(Breakpoint* breakpoint, Function* fn) {
  assert_true(breakpoint->address_type() == Breakpoint::AddressType::kGuest);
  assert_true(fn->is_guest());
  auto guest_function = reinterpret_cast<cpu::GuestFunction*>(fn);
  auto host_address =
      guest_function->MapGuestAddressToMachineCode(breakpoint->guest_address());
  if (!host_address) {
    assert_always();
    return;
  }

  auto ptr = reinterpret_cast<void*>(host_address);
  auto original_bytes = xe::load<uint32_t>(ptr);
  assert_true(original_bytes != kArm64Brk0);
  xe::store<uint32_t>(ptr, kArm64Brk0);
  breakpoint->backend_data().emplace_back(host_address, original_bytes);
}

void A64Backend::UninstallBreakpoint(Breakpoint* breakpoint) {
  for (auto& pair : breakpoint->backend_data()) {
    auto ptr = reinterpret_cast<uint8_t*>(pair.first);
    auto instruction_bytes = xe::load<uint32_t>(ptr);
    assert_true(instruction_bytes == kArm64Brk0);
    xe::store<uint32_t>(ptr, static_cast<uint32_t>(pair.second));
  }
  breakpoint->backend_data().clear();
}

void A64Backend::InitializeBackendContext(void* ctx) {
  auto* a64_ctx = BackendContextForGuestContext(ctx);
  std::memset(a64_ctx, 0, sizeof(A64BackendContext));
  a64_ctx->reserve_helper_ = &reserve_helper_;
  a64_ctx->Ox1000 = 0x1000;
  a64_ctx->fpcr_fpu = DEFAULT_FPU_FPCR;
  a64_ctx->fpcr_vmx = DEFAULT_VMX_FPCR;
  a64_ctx->flags = (1U << kA64BackendNJMOn);  // NJM on by default
  a64_ctx->guest_tick_count = Clock::GetGuestTickCountPointer();
  a64_ctx->host_uptime_millis_base = Clock::QueryHostUptimeMillis();
  a64_ctx->processor_irql = processor()->raw_irql_ptr_for_a64();

  // Allocate stackpoints for longjmp detection.
  if (cvars::a64_enable_host_guest_stack_synchronization) {
    uint32_t max_stackpoints = cvars::a64_max_stackpoints;
    if (max_stackpoints > 0) {
      a64_ctx->stackpoints = new A64BackendStackpoint[max_stackpoints]();
    }
  }

  // Reset the live host FPCR for a fresh PPC context so one test's rounding
  // state does not leak into the next on the shared PPC test runner thread.
  SetGuestRoundingMode(ctx, 0);
}

void A64Backend::DeinitializeBackendContext(void* ctx) {
  auto* a64_ctx = BackendContextForGuestContext(ctx);
  if (a64_ctx->stackpoints) {
    delete[] a64_ctx->stackpoints;
    a64_ctx->stackpoints = nullptr;
  }
}

void A64Backend::PrepareForReentry(void* ctx) {
  auto* a64_ctx = BackendContextForGuestContext(ctx);
  a64_ctx->current_stackpoint_depth = 0;
}

uint32_t A64Backend::CreateGuestTrampoline(GuestTrampolineProc proc,
                                           void* userdata1, void* userdata2,
                                           bool long_term) {
  size_t new_index;
  if (long_term) {
    new_index = guest_trampoline_address_bitmap_.AcquireFromBack();
  } else {
    new_index = guest_trampoline_address_bitmap_.Acquire();
  }
  xenia_assert(new_index != static_cast<size_t>(-1));

  uint8_t* write_pos =
      &guest_trampoline_memory_[kGuestTrampolineSize * new_index];

  BuildGuestTrampoline(write_pos, reinterpret_cast<void*>(proc), userdata1,
                       userdata2,
                       reinterpret_cast<void*>(guest_to_host_thunk_));

  // Flush instruction cache for the new trampoline code.
#if XE_PLATFORM_WIN32
  FlushInstructionCache(GetCurrentProcess(), write_pos, kGuestTrampolineSize);
#else
  __builtin___clear_cache(
      reinterpret_cast<char*>(write_pos),
      reinterpret_cast<char*>(write_pos + kGuestTrampolineSize));
#endif

  uint32_t indirection_guest_addr =
      GUEST_TRAMPOLINE_BASE +
      (static_cast<uint32_t>(new_index) * GUEST_TRAMPOLINE_MIN_LEN);

  code_cache()->AddIndirection(
      indirection_guest_addr,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(write_pos)));

  return indirection_guest_addr;
}

void A64Backend::FreeGuestTrampoline(uint32_t trampoline_addr) {
  xenia_assert(trampoline_addr >= GUEST_TRAMPOLINE_BASE &&
               trampoline_addr < GUEST_TRAMPOLINE_END);
  size_t index =
      (trampoline_addr - GUEST_TRAMPOLINE_BASE) / GUEST_TRAMPOLINE_MIN_LEN;
  guest_trampoline_address_bitmap_.Release(index);
}

// PPC rounding mode (3-bit) to ARM64 FPCR value.
// Same table as in a64_sequences.cc SET_ROUNDING_MODE.
static constexpr uint32_t fpcr_table[8] = {
    (0b00 << 22),              // PPC 0: nearest, IEEE
    (0b11 << 22),              // PPC 1: toward zero, IEEE
    (0b01 << 22),              // PPC 2: toward +inf, IEEE
    (0b10 << 22),              // PPC 3: toward -inf, IEEE
    (0b00 << 22) | (1 << 24),  // PPC 4: nearest, flush-to-zero
    (0b11 << 22) | (1 << 24),  // PPC 5: toward zero, flush-to-zero
    (0b01 << 22) | (1 << 24),  // PPC 6: toward +inf, flush-to-zero
    (0b10 << 22) | (1 << 24),  // PPC 7: toward -inf, flush-to-zero
};

void A64Backend::SetGuestRoundingMode(void* ctx, unsigned int mode) {
  A64BackendContext* bctx = BackendContextForGuestContext(ctx);
  uint32_t control = mode & 7;
  uint32_t fpcr_val = fpcr_table[control];
#if XE_COMPILER_MSVC
  // MSVC ARM64 intrinsic: ARM64_FPCR = register ID 0x5A20.
  _WriteStatusReg(0x5A20, static_cast<uint64_t>(fpcr_val));
#else
  __asm__ volatile("msr fpcr, %0" : : "r"(static_cast<uint64_t>(fpcr_val)));
#endif
  bctx->fpcr_fpu = fpcr_val;
  if (control & 0b100) {
    bctx->flags |= (1u << kA64BackendNonIEEEMode);
  } else {
    bctx->flags &= ~(1u << kA64BackendNonIEEEMode);
  }
  auto ppc_context = reinterpret_cast<ppc::PPCContext*>(ctx);
  ppc_context->fpscr.bits.rn = control;
  ppc_context->fpscr.bits.ni = control >> 2;
}

bool A64Backend::PopulatePseudoStacktrace(GuestPseudoStackTrace* st) {
  ThreadState* thrd_state = ThreadState::Get();
  if (!thrd_state) {
    return false;
  }
  ppc::PPCContext* ctx = thrd_state->context();
  A64BackendContext* backend_ctx = BackendContextForGuestContext(ctx);

  uint32_t depth = backend_ctx->current_stackpoint_depth - 1;
  if (static_cast<int32_t>(depth) < 1) {
    return false;
  }
  uint32_t num_entries_to_populate =
      std::min(MAX_GUEST_PSEUDO_STACKTRACE_ENTRIES, depth);

  st->count = num_entries_to_populate;
  st->truncated_flag = num_entries_to_populate < depth ? 1 : 0;

  A64BackendStackpoint* current_stackpoint =
      &backend_ctx->stackpoints[backend_ctx->current_stackpoint_depth - 1];

  for (uint32_t stp_index = 0; stp_index < num_entries_to_populate;
       ++stp_index) {
    st->return_addrs[stp_index] = current_stackpoint->guest_return_address_;
    current_stackpoint--;
  }
  return true;
}

void A64Backend::RecordMMIOExceptionForGuestInstruction(void* host_address) {
  // The donor backend records this through an XexModule info-cache extension.
  // That cache is not imported yet, so keep the callback present while the A64
  // backend is brought up against this fork.
  (void)host_address;
}

bool A64Backend::ExceptionCallbackThunk(Exception* ex, void* data) {
  auto* backend = reinterpret_cast<A64Backend*>(data);
  return backend->ExceptionCallback(ex);
}

bool A64Backend::ExceptionCallback(Exception* ex) {
  if (ex->code() != Exception::Code::kIllegalInstruction) {
    return false;
  }

  auto instruction_bytes =
      xe::load<uint32_t>(reinterpret_cast<void*>(ex->pc()));
  if (IsArm64Brk(instruction_bytes) && instruction_bytes != kArm64Brk0) {
    auto code_cache = processor()->backend()->code_cache();
    auto code_base = code_cache->execute_base_address();
    auto code_end = code_base + code_cache->total_size();
    uint64_t code_offset =
        ex->pc() >= code_base && ex->pc() < code_end ? ex->pc() - code_base
                                                     : UINT64_MAX;
    auto host_context = ex->thread_context();
    XELOGE(
        "A64 backend trap: BRK #{:04X} pc={:016X} code_offset={:X} "
        "x16_guest_target={:08X} x20_context={:016X} x30_lr={:016X}",
        Arm64BrkImmediate(instruction_bytes), ex->pc(), code_offset,
        uint32_t(host_context->x[16]), host_context->x[20],
        host_context->x[30]);
    return false;
  }

  // Verify it's our BRK #0 instruction.
  if (instruction_bytes != kArm64Brk0) {
    return false;
  }

  return processor()->OnThreadBreakpointHit(ex);
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
