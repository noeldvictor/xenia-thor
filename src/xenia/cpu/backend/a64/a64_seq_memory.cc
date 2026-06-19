/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_sequences.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>

#include "xenia/base/byte_order.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/memory.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"
#include "xenia/cpu/backend/a64/a64_op.h"
#include "xenia/cpu/backend/a64/a64_seq_util.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/xex_module.h"

DEFINE_bool(emit_mmio_aware_stores_for_recorded_exception_addresses, false,
            "Use recorded MMIO exception addresses for A64 store emission.",
            "a64");
DEFINE_bool(emit_inline_mmio_checks, false,
            "Emit inline A64 MMIO checks for memory accesses.", "a64");
DEFINE_bool(arm64_offset_memory_address_fastpath, false,
            "Use offset-aware A64 guest memory address lowering for normal "
            "LOAD_OFFSET/STORE_OFFSET paths.",
            "a64");
DEFINE_string(arm64_guest_store_watch, "",
              "Comma-separated guest address/range watch list for A64 stores.",
              "a64");
DEFINE_int32(arm64_guest_store_watch_budget, 128,
             "Maximum A64 guest store watch log lines.", "a64");
DEFINE_bool(arm64_global_reservation_helpers, false,
            "Use Edge-style global reservation helpers for A64 "
            "RESERVED_LOAD/STORE instead of the legacy inline CAS path.",
            "a64");
DEFINE_bool(arm64_use_flat_membase, true,
            "R3: fold the 32-bit guest address into [membase, Wn, UXTW] indexed "
            "guest loads instead of materializing membase+addr through a scratch "
            "register, saving a `mov` (and freeing x0) per guest load on "
            "platforms that need no large-page +0x1000 fixup (e.g. Android). "
            "Default-on (matches the opt_flat_membase toggle, device-validated "
            "pixel-correct on Blue Dragon). No effect on Windows (64K granularity "
            "requires the fixup).",
            "a64");
// Spin-loop-yield (cross-title generalization of the proven Blue Dragon
// draw-wait fastpath, +27% fps). CPU-bound 360 titles (Lost Odyssey, Gears 2)
// burn ~12-25% CPU in a shared XDK spin-wait primitive that busy-polls mftb in a
// tight loop; the spinner never yields, starving the lock-holder thread. The
// LOAD_CLOCK helper detects the tight poll and yields. Default-OFF (a single
// bool check on the default path); earns default-on after a device A/B on
// LO/Gears2. See docs/research/20260606-thor-spin-loop-yield-cpu-lever-scope.md.
DEFINE_bool(a64_clock_spin_yield, false,
            "Thor ARM64: when a guest thread busy-polls mftb in a tight loop "
            "(the XDK spin-wait primitive), yield the host core so the "
            "lock-holder thread runs sooner. Cross-title generalization of the "
            "Blue Dragon draw-wait fastpath. Default off; device-validate.",
            "a64");
DEFINE_uint32(a64_clock_spin_yield_stride, 64,
              "Thor ARM64: yield after this many consecutive tight (back-to-"
              "back) mftb reads from one thread. Higher = only deeper spins "
              "yield (fewer false positives on legit timing code).",
              "a64");
DEFINE_uint32(a64_clock_spin_yield_sleep_us, 0,
              "Thor ARM64: sleep this many host microseconds at each spin-yield. "
              "0 = sched_yield only (safest, no sleep latency). The BD fastpath "
              "uses ~100us; tune on-device.",
              "a64");
DEFINE_uint32(a64_clock_spin_yield_window_us, 50,
              "Thor ARM64: two consecutive mftb reads within this many host us "
              "count as a tight spin (else the counter resets). Wider catches "
              "slower spin loops; legit per-frame mftb (~16ms apart) still "
              "resets. Was hardcoded 2us (too tight - caught nothing at a "
              "GPU-bound Gears2 menu); tune on-device.",
              "a64");
DECLARE_bool(arm64_blue_dragon_draw_wait_probe);
DECLARE_uint32(arm64_blue_dragon_draw_wait_probe_stride);
DECLARE_uint32(arm64_blue_dragon_draw_wait_inline_tick_step);

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

volatile int anchor_memory = 0;

namespace {

std::atomic<int32_t> guest_store_watch_log_count{0};

template <typename OffsetOp>
XReg ComputeOffsetMemoryAddress(A64Emitter& e, const I64Op& guest,
                                const OffsetOp& offset) {
  if (cvars::arm64_offset_memory_address_fastpath) {
    return ComputeMemoryAddressOffset(e, guest, offset);
  }
  return AddGuestMemoryOffset(e, ComputeMemoryAddress(e, guest), offset);
}

void SkipStoreWatchDelimiters(const char*& p) {
  while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';') {
    ++p;
  }
}

bool ParseStoreWatchHex(const char*& p, uint32_t* out_value) {
  SkipStoreWatchDelimiters(p);
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
  }
  char* end = nullptr;
  unsigned long value = std::strtoul(p, &end, 16);
  if (end == p) {
    return false;
  }
  *out_value = static_cast<uint32_t>(value);
  p = end;
  return true;
}

bool StoreWatchMatches(uint32_t guest_address, uint32_t size) {
  if (cvars::arm64_guest_store_watch.empty()) {
    return false;
  }
  uint32_t store_end = guest_address + std::max<uint32_t>(size, 1) - 1;
  const char* p = cvars::arm64_guest_store_watch.c_str();
  while (*p) {
    uint32_t start = 0;
    if (!ParseStoreWatchHex(p, &start)) {
      break;
    }
    uint32_t end = start;
    if (*p == '-' || *p == ':') {
      ++p;
      if (!ParseStoreWatchHex(p, &end)) {
        end = start;
      }
    } else if (*p == '+') {
      ++p;
      uint32_t length = 0;
      if (ParseStoreWatchHex(p, &length) && length != 0) {
        end = start + length - 1;
      }
    }
    if (guest_address <= end && store_end >= start) {
      return true;
    }
    SkipStoreWatchDelimiters(p);
  }
  return false;
}

void TraceGuestStoreWatch(void* raw_context, uint32_t guest_pc,
                          uint32_t guest_address, uint32_t size) {
  if (!StoreWatchMatches(guest_address, size)) {
    return;
  }
  int32_t log_index = guest_store_watch_log_count.fetch_add(1);
  if (log_index >= cvars::arm64_guest_store_watch_budget) {
    return;
  }
  auto ctx = reinterpret_cast<ppc::PPCContext*>(raw_context);
  uint32_t raw_value = 0;
  uint32_t swapped_value = 0;
  if (ctx && size == 4) {
    raw_value =
        *reinterpret_cast<uint32_t*>(ctx->virtual_membase + guest_address);
    swapped_value = xe::byte_swap(raw_value);
  }
  XELOGI(
      "ARM64 guest memory watch hit: fn 00000000 guest {:08X} range {:08X} "
      "size {} op STORE thid {:08X} raw {:08X} be {:08X}",
      guest_pc, guest_address, size, ctx ? ctx->thread_id : 0, raw_value,
      swapped_value);
}

void EmitGuestStoreWatch(A64Emitter& e, const hir::Instr* instr,
                         const XReg& guest_address, uint32_t size) {
  if (cvars::arm64_guest_store_watch.empty()) {
    return;
  }
  e.mov(e.w2, WReg(guest_address.getIdx()));
  e.mov(e.w1, static_cast<uint64_t>(instr->GuestAddressFor()));
  e.mov(e.w3, size);
  e.CallNativeSafe(reinterpret_cast<void*>(&TraceGuestStoreWatch));
}

}  // namespace

static bool IsPossibleMMIOInstruction(A64Emitter& e, const hir::Instr* i) {
  (void)e;
  (void)i;
  return false;
}

static void UpdateCurrentThreadKernelTime(void* raw_context) {
  auto ctx = reinterpret_cast<ppc::PPCContext*>(raw_context);
  if (!ctx || !ctx->processor) {
    return;
  }
  auto memory = ctx->processor->memory();
  uint32_t pcr_address = static_cast<uint32_t>(ctx->r[13]);
  auto pcr = memory->TranslateVirtual(pcr_address);
  if (!pcr) {
    return;
  }
  uint32_t current_thread = xe::load_and_swap<uint32_t>(pcr + 0x100);
  auto thread = memory->TranslateVirtual(current_thread);
  if (!thread) {
    return;
  }
  xe::store_and_swap<uint32_t>(thread + 0x58, Clock::QueryGuestUptimeMillis());
}

static bool ShouldUpdateBlueDragonDrawWaitKernelTime(const hir::Instr* instr) {
  // NOTE: Gears' draw-wait KTHREAD+0x58 load site is 0x8222F4CC (same XDK D3D
  // draw-wait primitive). It was tried here but is DELIBERATELY NOT added:
  // advancing kernel-time makes Gears' watchdog fire, but Gears' GPU-hung
  // handler ends in an unconditional trap (guest 0x8222FE14 twui) -> the emulator
  // crashes instead of recovering (device-tested 2026-06-19). BD recovers without
  // trapping; Gears does not. Unblocking Gears needs the clean Gap-A fix (advance
  // Gears' ring read-pointer so the drain routine 0x82221980 sets +0x2A39 bit 0x2)
  // or a game-patch NOPing the trap. See docs/research/20260619-gears-spinwait-re.md.
  return cvars::arm64_blue_dragon_draw_wait_probe &&
         instr->GuestAddressFor() == 0x8246B474;
}

static void EmitBlueDragonDrawWaitKernelTimeUpdateBody(A64Emitter& e) {
  uint32_t inline_step =
      cvars::arm64_blue_dragon_draw_wait_inline_tick_step;
  if (inline_step != 0) {
    auto& done = e.NewCachedLabel();
    inline_step = std::min<uint32_t>(inline_step, 0xFFFFu);

    e.ldr(e.x16,
          ptr(e.GetContextReg(),
              static_cast<uint32_t>(offsetof(ppc::PPCContext, r) +
                                    sizeof(uint64_t) * 13)));
    e.cbz(e.x16, done);
    e.add(e.x16, e.GetMembaseReg(), e.x16);
    e.ldr(e.w17, ptr(e.x16, 0x100));
    e.rev(e.w17, e.w17);
    e.cbz(e.w17, done);

    e.add(e.x16, e.GetMembaseReg(), e.x17);
    e.ldr(e.w17, ptr(e.x16, 0x58));
    e.rev(e.w17, e.w17);
    if (inline_step <= 4095) {
      e.add(e.w17, e.w17, inline_step);
    } else {
      e.mov(e.w0, inline_step);
      e.add(e.w17, e.w17, e.w0);
    }
    e.rev(e.w17, e.w17);
    e.str(e.w17, ptr(e.x16, 0x58));
    e.L(done);
    return;
  }

  e.CallNativeSafe(reinterpret_cast<void*>(&UpdateCurrentThreadKernelTime));
}

static void EmitBlueDragonDrawWaitKernelTimeUpdate(A64Emitter& e) {
  uint32_t stride = std::max<uint32_t>(
      cvars::arm64_blue_dragon_draw_wait_probe_stride, 1);
  if (stride > 1 && (stride & (stride - 1)) == 0) {
    auto& skip_update = e.NewCachedLabel();
    auto bctx = e.GetBackendCtxReg();
    e.ldr(e.w17, ptr(bctx, static_cast<uint32_t>(offsetof(
                                A64BackendContext,
                                blue_dragon_draw_wait_probe_counter))));
    e.add(e.w17, e.w17, 1);
    e.str(e.w17, ptr(bctx, static_cast<uint32_t>(offsetof(
                                A64BackendContext,
                                blue_dragon_draw_wait_probe_counter))));
    e.and_(e.w17, e.w17, stride - 1);
    e.cbnz(e.w17, skip_update);
    EmitBlueDragonDrawWaitKernelTimeUpdateBody(e);
    e.L(skip_update);
    return;
  }

  EmitBlueDragonDrawWaitKernelTimeUpdateBody(e);
}

// ============================================================================
// OPCODE_DELAY_EXECUTION
// ============================================================================
struct DELAY_EXECUTION
    : Sequence<DELAY_EXECUTION, I<OPCODE_DELAY_EXECUTION, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { e.yield(); }
};
EMITTER_OPCODE_TABLE(OPCODE_DELAY_EXECUTION, DELAY_EXECUTION);

// ============================================================================
// OPCODE_MEMORY_BARRIER
// ============================================================================
struct MEMORY_BARRIER
    : Sequence<MEMORY_BARRIER, I<OPCODE_MEMORY_BARRIER, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.dmb(Xbyak_aarch64::ISH);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MEMORY_BARRIER, MEMORY_BARRIER);

// ============================================================================
// OPCODE_YIELD
// ============================================================================
struct YIELD : Sequence<YIELD, I<OPCODE_YIELD, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Guest spin-wait hint (or rN,rN,rN) -> ARM YIELD: hints the core to back
    // the spinning thread off so a sibling thread (often the lock/condition
    // holder) can progress sooner.
    e.yield();
  }
};
EMITTER_OPCODE_TABLE(OPCODE_YIELD, YIELD);

// ============================================================================
// OPCODE_CACHE_CONTROL
// ============================================================================
struct CACHE_CONTROL
    : Sequence<CACHE_CONTROL,
               I<OPCODE_CACHE_CONTROL, VoidOp, I64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    bool is_clflush = false, is_prefetch = false, is_prefetchw = false;
    switch (CacheControlType(i.instr->flags)) {
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH:
        is_prefetch = true;
        break;
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH_FOR_STORE:
        is_prefetchw = true;
        break;
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE:
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE_AND_FLUSH:
        is_clflush = true;
        break;
      default:
        return;
    }
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.add(e.x0, e.GetMembaseReg(), addr);
    size_t cache_line_size = i.src2.value;
    if (is_clflush) {
      // dc civac, x0
      e.sys(0b011, 0b0111, 0b1110, 0b001, e.x0);
    }
    if (is_prefetch) {
      e.prfm(Xbyak_aarch64::PLDL1KEEP, ptr(e.x0));
    } else if (is_prefetchw) {
      e.prfm(Xbyak_aarch64::PSTL1KEEP, ptr(e.x0));
    }
    if (cache_line_size >= 128) {
      e.eor(e.x0, e.x0, 64);
      if (is_clflush) {
        // dc civac, x0
        e.sys(0b011, 0b0111, 0b1110, 0b001, e.x0);
      }
      if (is_prefetch) {
        e.prfm(Xbyak_aarch64::PLDL1KEEP, ptr(e.x0));
      } else if (is_prefetchw) {
        e.prfm(Xbyak_aarch64::PSTL1KEEP, ptr(e.x0));
      }
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CACHE_CONTROL, CACHE_CONTROL);

template <typename T, bool swap>
static void MMIOAwareStore(void* _ctx, unsigned int guestaddr, T value) {
  if (swap) {
    value = xe::byte_swap(value);
  }
  if (guestaddr >= 0xE0000000) {
    guestaddr += 0x1000;
  }
  auto ctx = reinterpret_cast<ppc::PPCContext*>(_ctx);
  auto gaddr = ctx->processor->memory()->LookupVirtualMappedRange(guestaddr);
  if (!gaddr) {
    *reinterpret_cast<T*>(ctx->virtual_membase + guestaddr) = value;
  } else {
    value = xe::byte_swap(value);
    gaddr->write(nullptr, gaddr->callback_context, guestaddr, value);
  }
}

template <typename T, bool swap>
static T MMIOAwareLoad(void* _ctx, unsigned int guestaddr) {
  T value;
  if (guestaddr >= 0xE0000000) {
    guestaddr += 0x1000;
  }
  auto ctx = reinterpret_cast<ppc::PPCContext*>(_ctx);
  auto gaddr = ctx->processor->memory()->LookupVirtualMappedRange(guestaddr);
  if (!gaddr) {
    value = *reinterpret_cast<T*>(ctx->virtual_membase + guestaddr);
    if (swap) {
      value = xe::byte_swap(value);
    }
  } else {
    value = gaddr->read(nullptr, gaddr->callback_context, guestaddr);
  }
  return value;
}

// ============================================================================
// OPCODE_LOAD
// ============================================================================
struct LOAD_I8 : Sequence<LOAD_I8, I<OPCODE_LOAD, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WithGuestMemAddress(e, i.src1, [&](auto&& mem) { e.ldrb(i.dest, mem); });
  }
};
struct LOAD_I16 : Sequence<LOAD_I16, I<OPCODE_LOAD, I16Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WithGuestMemAddress(e, i.src1, [&](auto&& mem) { e.ldrh(i.dest, mem); });
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev16(i.dest, i.dest);
    }
  }
};
struct LOAD_I32 : Sequence<LOAD_I32, I<OPCODE_LOAD, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (IsPossibleMMIOInstruction(e, i.instr)) {
      void* mmio_fn = (void*)&MMIOAwareLoad<uint32_t, false>;
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        mmio_fn = (void*)&MMIOAwareLoad<uint32_t, true>;
      }
      if (i.src1.is_constant) {
        e.mov(e.w1,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else {
        e.mov(e.w1, WReg(i.src1.reg().getIdx()));
      }
      e.CallNativeSafe(mmio_fn);
      e.mov(i.dest, e.w0);
      return;
    }
    if (cvars::emit_inline_mmio_checks) {
      if (i.src1.is_constant) {
        e.mov(e.w17,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else {
        e.mov(e.w17, WReg(i.src1.reg().getIdx()));
      }
      auto& normal_access = e.NewCachedLabel();
      auto& done = e.NewCachedLabel();
      e.mov(e.w0, 0x7FC00000u);
      e.cmp(e.w17, e.w0);
      e.b(LO, normal_access);
      e.mov(e.w0, 0x7FFFFFFFu);
      e.cmp(e.w17, e.w0);
      e.b(HI, normal_access);
      // MMIO path
      void* mmio_fn = (void*)&MMIOAwareLoad<uint32_t, false>;
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        mmio_fn = (void*)&MMIOAwareLoad<uint32_t, true>;
      }
      e.mov(e.w1, e.w17);
      e.CallNativeSafe(mmio_fn);
      e.mov(i.dest, e.w0);
      e.b(done);
      e.L(normal_access);
      {
        WithGuestMemAddress(e, i.src1,
                             [&](auto&& mem) { e.ldr(i.dest, mem); });
        if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
          e.rev(i.dest, i.dest);
        }
      }
      e.L(done);
    } else {
      WithGuestMemAddress(e, i.src1,
                           [&](auto&& mem) { e.ldr(i.dest, mem); });
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        e.rev(i.dest, i.dest);
      }
    }
  }
};
struct LOAD_I64 : Sequence<LOAD_I64, I<OPCODE_LOAD, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WithGuestMemAddress(e, i.src1, [&](auto&& mem) { e.ldr(i.dest, mem); });
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev(i.dest, i.dest);
    }
  }
};
struct LOAD_F32 : Sequence<LOAD_F32, I<OPCODE_LOAD, F32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      WithGuestMemAddress(e, i.src1, [&](auto&& mem) { e.ldr(e.w0, mem); });
      e.rev(e.w0, e.w0);
      e.fmov(i.dest, e.w0);
    } else {
      WithGuestMemAddress(e, i.src1, [&](auto&& mem) { e.ldr(i.dest, mem); });
    }
  }
};
struct LOAD_F64 : Sequence<LOAD_F64, I<OPCODE_LOAD, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      WithGuestMemAddress(e, i.src1, [&](auto&& mem) { e.ldr(e.x0, mem); });
      e.rev(e.x0, e.x0);
      e.fmov(i.dest, e.x0);
    } else {
      WithGuestMemAddress(e, i.src1, [&](auto&& mem) { e.ldr(i.dest, mem); });
    }
  }
};
struct LOAD_V128 : Sequence<LOAD_V128, I<OPCODE_LOAD, V128Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WithGuestMemAddress(e, i.src1, [&](auto&& mem) { e.ldr(i.dest, mem); });
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      // Reverse bytes within each 32-bit word (PPC BE -> ARM64 LE).
      auto idx = i.dest.reg().getIdx();
      e.rev32(VReg16B(idx), VReg16B(idx));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD, LOAD_I8, LOAD_I16, LOAD_I32, LOAD_I64,
                     LOAD_F32, LOAD_F64, LOAD_V128);

// ============================================================================
// OPCODE_STORE
// ============================================================================
struct STORE_I8 : Sequence<STORE_I8, I<OPCODE_STORE, VoidOp, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WithGuestMemAddress(e, i.src1, [&](auto&& mem) {
      if (i.src2.is_constant) {
        e.mov(e.w17, static_cast<uint64_t>(i.src2.constant() & 0xFF));
        e.strb(e.w17, mem);
      } else {
        e.strb(i.src2, mem);
      }
    });
  }
};
struct STORE_I16 : Sequence<STORE_I16, I<OPCODE_STORE, VoidOp, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WithGuestMemAddress(e, i.src1, [&](auto&& mem) {
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        if (i.src2.is_constant) {
          uint16_t val = xe::byte_swap(static_cast<uint16_t>(i.src2.constant()));
          e.mov(e.w17, static_cast<uint64_t>(val));
        } else {
          e.rev16(e.w17, i.src2);
        }
        e.strh(e.w17, mem);
      } else {
        if (i.src2.is_constant) {
          e.mov(e.w17, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
          e.strh(e.w17, mem);
        } else {
          e.strh(i.src2, mem);
        }
      }
    });
  }
};
struct STORE_I32 : Sequence<STORE_I32, I<OPCODE_STORE, VoidOp, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (IsPossibleMMIOInstruction(e, i.instr)) {
      void* mmio_fn = (void*)&MMIOAwareStore<uint32_t, false>;
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        mmio_fn = (void*)&MMIOAwareStore<uint32_t, true>;
      }
      if (i.src1.is_constant) {
        e.mov(e.w1,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else {
        e.mov(e.w1, WReg(i.src1.reg().getIdx()));
      }
      if (i.src2.is_constant) {
        e.mov(e.w2,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      } else {
        e.mov(e.w2, i.src2);
      }
      e.CallNativeSafe(mmio_fn);
      return;
    }
    if (cvars::emit_inline_mmio_checks) {
      if (i.src1.is_constant) {
        e.mov(e.w17,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else {
        e.mov(e.w17, WReg(i.src1.reg().getIdx()));
      }
      auto& normal_access = e.NewCachedLabel();
      auto& done = e.NewCachedLabel();
      e.mov(e.w0, 0x7FC00000u);
      e.cmp(e.w17, e.w0);
      e.b(LO, normal_access);
      e.mov(e.w0, 0x7FFFFFFFu);
      e.cmp(e.w17, e.w0);
      e.b(HI, normal_access);
      // MMIO path: copy value to w2 before w1 in case src2 is in w1.
      void* mmio_fn = (void*)&MMIOAwareStore<uint32_t, false>;
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        mmio_fn = (void*)&MMIOAwareStore<uint32_t, true>;
      }
      if (i.src2.is_constant) {
        e.mov(e.w2,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      } else {
        e.mov(e.w2, i.src2);
      }
      e.mov(e.w1, e.w17);
      e.CallNativeSafe(mmio_fn);
      e.b(done);
      e.L(normal_access);
      {
        WithGuestStoreAddress(e, i.src1, [&](auto&& mem, XReg watch_addr) {
          if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
            if (i.src2.is_constant) {
              uint32_t val =
                  xe::byte_swap(static_cast<uint32_t>(i.src2.constant()));
              e.mov(e.w17, static_cast<uint64_t>(val));
            } else {
              e.rev(e.w17, i.src2);
            }
            e.str(e.w17, mem);
          } else {
            if (i.src2.is_constant) {
              e.mov(e.w17, static_cast<uint64_t>(
                               static_cast<uint32_t>(i.src2.constant())));
              e.str(e.w17, mem);
            } else {
              e.str(i.src2, mem);
            }
          }
          EmitGuestStoreWatch(e, i.instr, watch_addr, 4);
        });
      }
      e.L(done);
    } else {
      WithGuestStoreAddress(e, i.src1, [&](auto&& mem, XReg watch_addr) {
        if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
          if (i.src2.is_constant) {
            uint32_t val =
                xe::byte_swap(static_cast<uint32_t>(i.src2.constant()));
            e.mov(e.w17, static_cast<uint64_t>(val));
          } else {
            e.rev(e.w17, i.src2);
          }
          e.str(e.w17, mem);
        } else {
          if (i.src2.is_constant) {
            e.mov(e.w17, static_cast<uint64_t>(
                             static_cast<uint32_t>(i.src2.constant())));
            e.str(e.w17, mem);
          } else {
            e.str(i.src2, mem);
          }
        }
        EmitGuestStoreWatch(e, i.instr, watch_addr, 4);
      });
    }
  }
};
struct STORE_I64 : Sequence<STORE_I64, I<OPCODE_STORE, VoidOp, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WithGuestMemAddress(e, i.src1, [&](auto&& mem) {
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        if (i.src2.is_constant) {
          uint64_t val = xe::byte_swap(static_cast<uint64_t>(i.src2.constant()));
          e.mov(e.x17, val);
        } else {
          e.rev(e.x17, i.src2);
        }
        e.str(e.x17, mem);
      } else {
        if (i.src2.is_constant) {
          e.mov(e.x17, static_cast<uint64_t>(i.src2.constant()));
          e.str(e.x17, mem);
        } else {
          e.str(i.src2, mem);
        }
      }
    });
  }
};
struct STORE_F32 : Sequence<STORE_F32, I<OPCODE_STORE, VoidOp, I64Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WithGuestMemAddress(e, i.src1, [&](auto&& mem) {
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        if (i.src2.is_constant) {
          uint32_t val =
              xe::byte_swap(static_cast<uint32_t>(i.src2.value->constant.i32));
          e.mov(e.w17, static_cast<uint64_t>(val));
        } else {
          e.fmov(e.w17, i.src2);
          e.rev(e.w17, e.w17);
        }
        e.str(e.w17, mem);
      } else {
        if (i.src2.is_constant) {
          e.mov(e.w17, static_cast<uint64_t>(i.src2.value->constant.i32));
          e.str(e.w17, mem);
        } else {
          e.str(i.src2, mem);
        }
      }
    });
  }
};
struct STORE_F64 : Sequence<STORE_F64, I<OPCODE_STORE, VoidOp, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WithGuestMemAddress(e, i.src1, [&](auto&& mem) {
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        if (i.src2.is_constant) {
          uint64_t val =
              xe::byte_swap(static_cast<uint64_t>(i.src2.value->constant.i64));
          e.mov(e.x17, val);
        } else {
          e.fmov(e.x17, i.src2);
          e.rev(e.x17, e.x17);
        }
        e.str(e.x17, mem);
      } else {
        if (i.src2.is_constant) {
          e.mov(e.x17, static_cast<uint64_t>(i.src2.value->constant.i64));
          e.str(e.x17, mem);
        } else {
          e.str(i.src2, mem);
        }
      }
    });
  }
};
struct STORE_V128
    : Sequence<STORE_V128, I<OPCODE_STORE, VoidOp, I64Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Prepare the source value in a register BEFORE computing the address, so
    // SrcVReg/LoadV128Const (which clobber x0) can't corrupt a classic-path
    // address materialized into x0. The folded [membase, Wn, UXTW] path never
    // touches x0, so this ordering also drops the old x17 address-save dance.
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      // Reverse bytes within each 32-bit word, store via scratch v0.
      int idx = SrcVReg(e, i.src2, 0);
      e.rev32(VReg16B(0), VReg16B(idx));
      WithGuestMemAddress(e, i.src1,
                          [&](auto&& mem) { e.str(QReg(0), mem); });
    } else if (i.src2.is_constant) {
      LoadV128Const(e, 0, i.src2.constant());
      WithGuestMemAddress(e, i.src1,
                          [&](auto&& mem) { e.str(QReg(0), mem); });
    } else {
      WithGuestMemAddress(e, i.src1,
                          [&](auto&& mem) { e.str(i.src2, mem); });
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE, STORE_I8, STORE_I16, STORE_I32, STORE_I64,
                     STORE_F32, STORE_F64, STORE_V128);

// ============================================================================
// OPCODE_LOAD_CLOCK
// ============================================================================
struct LOAD_CLOCK : Sequence<LOAD_CLOCK, I<OPCODE_LOAD_CLOCK, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Call QueryGuestTickCount which updates the clock from host ticks.
    // Reading the cached pointer directly would return stale values for
    // consecutive mftb instructions.
    e.CallNative(reinterpret_cast<void*>(LoadClock));
    e.mov(i.dest, e.x0);
  }
  static uint64_t LoadClock(void* raw_context) {
    const uint64_t guest_tick = Clock::QueryGuestTickCount();
    if (cvars::a64_clock_spin_yield) {
      // Observability (so a device A/B can tell "enabled but not firing" from
      // "not enabled"): log once on first reach, and the firing count every 4096
      // yields. Grep logcat for "a64_clock_spin_yield".
      static std::atomic<bool> logged_enabled{false};
      if (!logged_enabled.exchange(true)) {
        XELOGI("a64_clock_spin_yield ENABLED (stride={}, sleep_us={})",
               uint32_t(cvars::a64_clock_spin_yield_stride),
               uint32_t(cvars::a64_clock_spin_yield_sleep_us));
      }
      // Per-thread tight-poll detector. A real mftb spin reads the clock far
      // faster (every few guest instructions) than legitimate timing code
      // (a handful of reads per frame), so only count reads that arrive within
      // ~2us of the previous one as "spinning"; any larger gap resets.
      thread_local uint64_t last_host_ticks = 0;
      thread_local uint32_t consecutive_rapid = 0;
      const uint64_t now = Clock::QueryHostTickCount();
      const uint64_t rapid_ticks =
          Clock::QueryHostTickFrequency() *
          uint64_t(cvars::a64_clock_spin_yield_window_us) / 1000000u;
      if (last_host_ticks != 0 && (now - last_host_ticks) <= rapid_ticks) {
        if (++consecutive_rapid >= cvars::a64_clock_spin_yield_stride) {
          consecutive_rapid = 0;
          static std::atomic<uint64_t> fired{0};
          const uint64_t n = fired.fetch_add(1, std::memory_order_relaxed) + 1;
          if ((n & 0xFFFu) == 0) {
            XELOGI("a64_clock_spin_yield fired {} times", n);
          }
          const uint32_t sleep_us = cvars::a64_clock_spin_yield_sleep_us;
          if (sleep_us) {
            xe::threading::Sleep(std::chrono::microseconds(sleep_us));
          } else {
            xe::threading::MaybeYield();
          }
        }
      } else {
        consecutive_rapid = 0;
      }
      last_host_ticks = now;
    }
    return guest_tick;
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_CLOCK, LOAD_CLOCK);

// ============================================================================
// OPCODE_LOAD_OFFSET / OPCODE_STORE_OFFSET
// ============================================================================
struct LOAD_OFFSET_I8
    : Sequence<LOAD_OFFSET_I8, I<OPCODE_LOAD_OFFSET, I8Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    ComputeOffsetMemoryAddress(e, i.src1, i.src2);
    e.ldrb(i.dest, ptr(e.GetMembaseReg(), e.x0));
  }
};
struct LOAD_OFFSET_I16
    : Sequence<LOAD_OFFSET_I16, I<OPCODE_LOAD_OFFSET, I16Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    ComputeOffsetMemoryAddress(e, i.src1, i.src2);
    e.ldrh(i.dest, ptr(e.GetMembaseReg(), e.x0));
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev16(i.dest, i.dest);
    }
  }
};
struct LOAD_OFFSET_I32
    : Sequence<LOAD_OFFSET_I32, I<OPCODE_LOAD_OFFSET, I32Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (ShouldUpdateBlueDragonDrawWaitKernelTime(i.instr)) {
      EmitBlueDragonDrawWaitKernelTimeUpdate(e);
    }
    if (IsPossibleMMIOInstruction(e, i.instr)) {
      void* mmio_fn = (void*)&MMIOAwareLoad<uint32_t, false>;
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        mmio_fn = (void*)&MMIOAwareLoad<uint32_t, true>;
      }
      if (i.src1.is_constant) {
        e.mov(e.w1,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else {
        e.mov(e.w1, WReg(i.src1.reg().getIdx()));
      }
      if (i.src2.is_constant) {
        e.mov(e.w17,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      } else {
        e.mov(e.w17, WReg(i.src2.reg().getIdx()));
      }
      e.add(e.w1, e.w1, e.w17);
      e.CallNativeSafe(mmio_fn);
      e.mov(i.dest, e.w0);
      return;
    }
    if (cvars::emit_inline_mmio_checks) {
      // Compute raw guest address (src1 + src2) in w17 for range check.
      if (i.src1.is_constant) {
        e.mov(e.w17,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else {
        e.mov(e.w17, WReg(i.src1.reg().getIdx()));
      }
      if (i.src2.is_constant) {
        uint32_t offset = static_cast<uint32_t>(i.src2.constant());
        if (offset != 0) {
          e.mov(e.w0, static_cast<uint64_t>(offset));
          e.add(e.w17, e.w17, e.w0);
        }
      } else {
        e.add(e.w17, e.w17, WReg(i.src2.reg().getIdx()));
      }
      auto& normal_access = e.NewCachedLabel();
      auto& done = e.NewCachedLabel();
      e.mov(e.w0, 0x7FC00000u);
      e.cmp(e.w17, e.w0);
      e.b(LO, normal_access);
      e.mov(e.w0, 0x7FFFFFFFu);
      e.cmp(e.w17, e.w0);
      e.b(HI, normal_access);
      // MMIO path
      void* mmio_fn = (void*)&MMIOAwareLoad<uint32_t, false>;
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        mmio_fn = (void*)&MMIOAwareLoad<uint32_t, true>;
      }
      e.mov(e.w1, e.w17);
      e.CallNativeSafe(mmio_fn);
      e.mov(i.dest, e.w0);
      e.b(done);
      e.L(normal_access);
      {
        ComputeOffsetMemoryAddress(e, i.src1, i.src2);
        e.ldr(i.dest, ptr(e.GetMembaseReg(), e.x0));
        if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
          e.rev(i.dest, i.dest);
        }
      }
      e.L(done);
    } else {
      ComputeOffsetMemoryAddress(e, i.src1, i.src2);
      e.ldr(i.dest, ptr(e.GetMembaseReg(), e.x0));
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        e.rev(i.dest, i.dest);
      }
    }
  }
};
struct LOAD_OFFSET_I64
    : Sequence<LOAD_OFFSET_I64, I<OPCODE_LOAD_OFFSET, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    ComputeOffsetMemoryAddress(e, i.src1, i.src2);
    e.ldr(i.dest, ptr(e.GetMembaseReg(), e.x0));
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.rev(i.dest, i.dest);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_OFFSET, LOAD_OFFSET_I8, LOAD_OFFSET_I16,
                     LOAD_OFFSET_I32, LOAD_OFFSET_I64);

struct STORE_OFFSET_I8
    : Sequence<STORE_OFFSET_I8,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    ComputeOffsetMemoryAddress(e, i.src1, i.src2);
    if (i.src3.is_constant) {
      e.mov(e.w17, static_cast<uint64_t>(i.src3.constant() & 0xFF));
      e.strb(e.w17, ptr(e.GetMembaseReg(), e.x0));
    } else {
      e.strb(i.src3, ptr(e.GetMembaseReg(), e.x0));
    }
  }
};
struct STORE_OFFSET_I16
    : Sequence<STORE_OFFSET_I16,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    ComputeOffsetMemoryAddress(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (i.src3.is_constant) {
        uint16_t val = xe::byte_swap(static_cast<uint16_t>(i.src3.constant()));
        e.mov(e.w17, static_cast<uint64_t>(val));
      } else {
        e.rev16(e.w17, i.src3);
      }
      e.strh(e.w17, ptr(e.GetMembaseReg(), e.x0));
    } else {
      if (i.src3.is_constant) {
        e.mov(e.w17, static_cast<uint64_t>(i.src3.constant() & 0xFFFF));
        e.strh(e.w17, ptr(e.GetMembaseReg(), e.x0));
      } else {
        e.strh(i.src3, ptr(e.GetMembaseReg(), e.x0));
      }
    }
  }
};
struct STORE_OFFSET_I32
    : Sequence<STORE_OFFSET_I32,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (IsPossibleMMIOInstruction(e, i.instr)) {
      void* mmio_fn = (void*)&MMIOAwareStore<uint32_t, false>;
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        mmio_fn = (void*)&MMIOAwareStore<uint32_t, true>;
      }
      if (i.src1.is_constant) {
        e.mov(e.w1,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else {
        e.mov(e.w1, WReg(i.src1.reg().getIdx()));
      }
      if (i.src2.is_constant) {
        e.mov(e.w17,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      } else {
        e.mov(e.w17, WReg(i.src2.reg().getIdx()));
      }
      e.add(e.w1, e.w1, e.w17);
      if (i.src3.is_constant) {
        e.mov(e.w2,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src3.constant())));
      } else {
        e.mov(e.w2, i.src3);
      }
      e.CallNativeSafe(mmio_fn);
      return;
    }
    if (cvars::emit_inline_mmio_checks) {
      // Compute raw guest address (src1 + src2) in w17 for range check.
      if (i.src1.is_constant) {
        e.mov(e.w17,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else {
        e.mov(e.w17, WReg(i.src1.reg().getIdx()));
      }
      if (i.src2.is_constant) {
        uint32_t offset = static_cast<uint32_t>(i.src2.constant());
        if (offset != 0) {
          e.mov(e.w0, static_cast<uint64_t>(offset));
          e.add(e.w17, e.w17, e.w0);
        }
      } else {
        e.add(e.w17, e.w17, WReg(i.src2.reg().getIdx()));
      }
      auto& normal_access = e.NewCachedLabel();
      auto& done = e.NewCachedLabel();
      e.mov(e.w0, 0x7FC00000u);
      e.cmp(e.w17, e.w0);
      e.b(LO, normal_access);
      e.mov(e.w0, 0x7FFFFFFFu);
      e.cmp(e.w17, e.w0);
      e.b(HI, normal_access);
      // MMIO path: copy value to w2 before w1 in case src3 is in w1.
      void* mmio_fn = (void*)&MMIOAwareStore<uint32_t, false>;
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        mmio_fn = (void*)&MMIOAwareStore<uint32_t, true>;
      }
      if (i.src3.is_constant) {
        e.mov(e.w2,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src3.constant())));
      } else {
        e.mov(e.w2, i.src3);
      }
      e.mov(e.w1, e.w17);
      e.CallNativeSafe(mmio_fn);
      e.b(done);
      e.L(normal_access);
      {
        ComputeOffsetMemoryAddress(e, i.src1, i.src2);
        if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
          if (i.src3.is_constant) {
            uint32_t val =
                xe::byte_swap(static_cast<uint32_t>(i.src3.constant()));
            e.mov(e.w17, static_cast<uint64_t>(val));
          } else {
            e.rev(e.w17, i.src3);
          }
          e.str(e.w17, ptr(e.GetMembaseReg(), e.x0));
        } else {
          if (i.src3.is_constant) {
            e.mov(e.w17, static_cast<uint64_t>(
                             static_cast<uint32_t>(i.src3.constant())));
            e.str(e.w17, ptr(e.GetMembaseReg(), e.x0));
          } else {
            e.str(i.src3, ptr(e.GetMembaseReg(), e.x0));
          }
        }
      }
      EmitGuestStoreWatch(e, i.instr, e.x0, 4);
      e.L(done);
    } else {
      ComputeOffsetMemoryAddress(e, i.src1, i.src2);
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        if (i.src3.is_constant) {
          uint32_t val =
              xe::byte_swap(static_cast<uint32_t>(i.src3.constant()));
          e.mov(e.w17, static_cast<uint64_t>(val));
        } else {
          e.rev(e.w17, i.src3);
        }
        e.str(e.w17, ptr(e.GetMembaseReg(), e.x0));
      } else {
        if (i.src3.is_constant) {
          e.mov(e.w17, static_cast<uint64_t>(
                           static_cast<uint32_t>(i.src3.constant())));
          e.str(e.w17, ptr(e.GetMembaseReg(), e.x0));
        } else {
          e.str(i.src3, ptr(e.GetMembaseReg(), e.x0));
        }
      }
      EmitGuestStoreWatch(e, i.instr, e.x0, 4);
    }
  }
};
struct STORE_OFFSET_I64
    : Sequence<STORE_OFFSET_I64,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    ComputeOffsetMemoryAddress(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (i.src3.is_constant) {
        uint64_t val = xe::byte_swap(static_cast<uint64_t>(i.src3.constant()));
        e.mov(e.x17, val);
      } else {
        e.rev(e.x17, i.src3);
      }
      e.str(e.x17, ptr(e.GetMembaseReg(), e.x0));
    } else {
      if (i.src3.is_constant) {
        e.mov(e.x17, static_cast<uint64_t>(i.src3.constant()));
        e.str(e.x17, ptr(e.GetMembaseReg(), e.x0));
      } else {
        e.str(i.src3, ptr(e.GetMembaseReg(), e.x0));
      }
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_OFFSET, STORE_OFFSET_I8, STORE_OFFSET_I16,
                     STORE_OFFSET_I32, STORE_OFFSET_I64);

// ============================================================================
// OPCODE_MEMSET
// ============================================================================
static const bool zva_enable = (xe_cpu_mrs(DCZID_EL0) & 0b1'0000) == 0;
static const uint64_t zva_length = (4ULL << (xe_cpu_mrs(DCZID_EL0) & 0b0'1111));

struct MEMSET_I64
    : Sequence<MEMSET_I64, I<OPCODE_MEMSET, VoidOp, I64Op, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    assert_true(i.src3.is_constant);
    assert_true(i.src2.constant() == 0);
    // memset(membase + guest_addr, 0, length)
    // Only used by dcbz/dcbz128: constant zero value, constant aligned size.
    auto addr = ComputeMemoryAddress(e, i.src1);
    EmitGuestStoreWatch(e, i.instr, addr,
                        static_cast<uint32_t>(i.src3.constant()));
    addr = ComputeMemoryAddress(e, i.src1);
    e.add(e.x0, e.GetMembaseReg(), addr);
    const uint64_t len = i.src3.constant();
    uint64_t off = 0;

    // Use `dc zva` if it writes more bytes at a time than STP
    if (zva_enable && len >= zva_length && zva_length > 16) {
      for (; off + zva_length <= len; off += zva_length) {
        // dc zva, x0
        e.sys(0b011, 0b0111, 0b0100, 0b001, e.x0);
        if (off + zva_length < len) {
          e.add(e.x0, e.x0, zva_length);
        }
      }
    }

    // Inline with STP xzr, xzr pairs (16 bytes each)
    for (; off + 16 <= len; off += 16) {
      e.stp(e.xzr, e.xzr, AdrPostImm(e.x0, 16));
    }
    // Handle remaining bytes (0-15)
    if (off + 8 <= len) {
      e.str(e.xzr, AdrPostImm(e.x0, 8));
      off += 8;
    }
    if (off + 4 <= len) {
      e.str(e.wzr, AdrPostImm(e.x0, 4));
      off += 4;
    }
    // Byte loop for any remaining 0-3 bytes
    for (; off + 1 <= len; off += 1) {
      e.strb(e.wzr, AdrPostImm(e.x0, 1));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MEMSET, MEMSET_I64);

// ============================================================================
// ============================================================================
// OPCODE_ATOMIC_COMPARE_EXCHANGE
// ============================================================================
struct ATOMIC_COMPARE_EXCHANGE_I32
    : Sequence<ATOMIC_COMPARE_EXCHANGE_I32,
               I<OPCODE_ATOMIC_COMPARE_EXCHANGE, I8Op, I64Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Compute full host address (ldxr/stxr need base-only [Xn] addressing).
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.add(e.x4, e.GetMembaseReg(), addr);
    // src2 = expected (use w5), src3 = desired (use w6).
    if (i.src2.is_constant) {
      e.mov(e.w5,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
    } else {
      e.mov(e.w5, i.src2);
    }
    if (i.src3.is_constant) {
      e.mov(e.w6,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src3.constant())));
    } else {
      e.mov(e.w6, i.src3);
    }

    if (e.IsFeatureEnabled(kA64EmitLSE)) {
      e.mov(e.w0, e.w5);
      e.casal(e.w5, e.w6, ptr(e.x4));
      e.cmp(e.w5, e.w0);
      e.cset(i.dest, Xbyak_aarch64::EQ);
      return;
    }

    auto& retry = e.NewCachedLabel();
    auto& fail = e.NewCachedLabel();
    auto& done = e.NewCachedLabel();
    e.L(retry);
    e.ldaxr(e.w2, ptr(e.x4));
    e.cmp(e.w2, e.w5);
    e.b(Xbyak_aarch64::NE, fail);
    e.stlxr(e.w3, e.w6, ptr(e.x4));
    e.cbnz(e.w3, retry);
    e.mov(i.dest, 1);
    e.b(done);
    e.L(fail);
    e.clrex(15);
    e.mov(i.dest, 0);
    e.L(done);
  }
};
struct ATOMIC_COMPARE_EXCHANGE_I64
    : Sequence<ATOMIC_COMPARE_EXCHANGE_I64,
               I<OPCODE_ATOMIC_COMPARE_EXCHANGE, I8Op, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.add(e.x4, e.GetMembaseReg(), addr);
    if (i.src2.is_constant) {
      e.mov(e.x5, static_cast<uint64_t>(i.src2.constant()));
    } else {
      e.mov(e.x5, i.src2);
    }
    if (i.src3.is_constant) {
      e.mov(e.x6, static_cast<uint64_t>(i.src3.constant()));
    } else {
      e.mov(e.x6, i.src3);
    }

    if (e.IsFeatureEnabled(kA64EmitLSE)) {
      e.mov(e.x0, e.x5);
      e.casal(e.x5, e.x6, ptr(e.x4));
      e.cmp(e.x5, e.x0);
      e.cset(i.dest, Xbyak_aarch64::EQ);
      return;
    }

    auto& retry = e.NewCachedLabel();
    auto& fail = e.NewCachedLabel();
    auto& done = e.NewCachedLabel();
    e.L(retry);
    e.ldaxr(e.x2, ptr(e.x4));
    e.cmp(e.x2, e.x5);
    e.b(Xbyak_aarch64::NE, fail);
    e.stlxr(e.w3, e.x6, ptr(e.x4));
    e.cbnz(e.w3, retry);
    e.mov(i.dest, 1);
    e.b(done);
    e.L(fail);
    e.clrex(15);
    e.mov(i.dest, 0);
    e.L(done);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ATOMIC_COMPARE_EXCHANGE,
                     ATOMIC_COMPARE_EXCHANGE_I32, ATOMIC_COMPARE_EXCHANGE_I64);

// ============================================================================
// OPCODE_LOAD_MMIO / OPCODE_STORE_MMIO
// ============================================================================
struct LOAD_MMIO_I32
    : Sequence<LOAD_MMIO_I32, I<OPCODE_LOAD_MMIO, I32Op, OffsetOp, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto mmio_range = reinterpret_cast<MMIORange*>(i.src1.value);
    auto read_address = uint32_t(i.src2.value);
    // CallNativeSafe: thunk sets x0=PPCContext*, x1/x2/x3 pass through.
    // MMIOReadCallback(void* ppc_ctx, void* callback_ctx, uint32_t addr).
    e.mov(e.x1, uint64_t(mmio_range->callback_context));
    e.mov(e.w2, static_cast<uint64_t>(read_address));
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->read));
    e.rev(e.w0, e.w0);
    e.mov(i.dest, e.w0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_MMIO, LOAD_MMIO_I32);

struct STORE_MMIO_I32
    : Sequence<STORE_MMIO_I32,
               I<OPCODE_STORE_MMIO, VoidOp, OffsetOp, OffsetOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto mmio_range = reinterpret_cast<MMIORange*>(i.src1.value);
    auto write_address = uint32_t(i.src2.value);
    // CallNativeSafe: thunk sets x0=PPCContext*, x1/x2/x3 pass through.
    // MMIOWriteCallback(void* ppc_ctx, void* callback_ctx, uint32_t addr,
    //                   uint32_t value).
    e.mov(e.x1, uint64_t(mmio_range->callback_context));
    e.mov(e.w2, static_cast<uint64_t>(write_address));
    if (i.src3.is_constant) {
      e.mov(e.w3, static_cast<uint64_t>(
                      xe::byte_swap(static_cast<uint32_t>(i.src3.constant()))));
    } else {
      e.mov(e.w3, i.src3);
      e.rev(e.w3, e.w3);
    }
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->write));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_MMIO, STORE_MMIO_I32);

// ============================================================================
// OPCODE_RESERVED_LOAD / OPCODE_RESERVED_STORE
// ============================================================================
// Helper: get pointer to A64BackendContext.
// x19 is the dedicated backend context register, so this is a no-op
// accessor for readability. The returned register is x19.
static const Xbyak_aarch64::XReg& LoadBackendCtxPtr(A64Emitter& e) {
  return e.GetBackendCtxReg();
}

struct RESERVED_LOAD_I32
    : Sequence<RESERVED_LOAD_I32, I<OPCODE_RESERVED_LOAD, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (cvars::arm64_global_reservation_helpers) {
      if (i.src1.is_constant) {
        e.mov(e.w1, static_cast<uint32_t>(i.src1.constant()));
      } else {
        e.mov(e.w1, WReg(i.src1.reg().getIdx()));
      }
      e.CallNativeSafe(e.backend()->try_acquire_reservation_helper_);
      auto addr = ComputeMemoryAddress(e, i.src1);
      e.ldr(i.dest, ptr(e.GetMembaseReg(), addr));
      auto bctx = LoadBackendCtxPtr(e);
      e.mov(e.w0, i.dest);
      e.str(e.x0, ptr(bctx, static_cast<uint32_t>(offsetof(
                                A64BackendContext, cached_reserve_value_))));
      return;
    }

    auto addr = ComputeMemoryAddress(e, i.src1);
    // Save guest address before load; dest may alias addr register.
    e.mov(e.w0, WReg(addr.getIdx()));
    // Load the value (may clobber addr if dest == addr).
    e.ldr(i.dest, ptr(e.GetMembaseReg(), addr));
    // Save reservation: address and value in backend context.
    auto bctx = LoadBackendCtxPtr(e);
    // Store the guest address (already saved in x0).
    e.str(e.x0, ptr(bctx, static_cast<uint32_t>(offsetof(
                              A64BackendContext, cached_reserve_offset))));
    // Store the loaded value (zero-extended to 64-bit).
    e.mov(e.w1, i.dest);
    e.str(e.x1, ptr(bctx, static_cast<uint32_t>(offsetof(
                              A64BackendContext, cached_reserve_value_))));
    // Set the "has reserve" flag (bit 1).
    e.ldr(e.w1,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    e.orr(e.w1, e.w1, static_cast<uint32_t>(1u << kA64BackendHasReserveBit));
    e.str(e.w1,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
  }
};
struct RESERVED_LOAD_I64
    : Sequence<RESERVED_LOAD_I64, I<OPCODE_RESERVED_LOAD, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (cvars::arm64_global_reservation_helpers) {
      if (i.src1.is_constant) {
        e.mov(e.w1, static_cast<uint32_t>(i.src1.constant()));
      } else {
        e.mov(e.w1, WReg(i.src1.reg().getIdx()));
      }
      e.CallNativeSafe(e.backend()->try_acquire_reservation_helper_);
      auto addr = ComputeMemoryAddress(e, i.src1);
      e.ldr(i.dest, ptr(e.GetMembaseReg(), addr));
      auto bctx = LoadBackendCtxPtr(e);
      e.str(i.dest, ptr(bctx, static_cast<uint32_t>(offsetof(
                                  A64BackendContext, cached_reserve_value_))));
      return;
    }

    auto addr = ComputeMemoryAddress(e, i.src1);
    // Save guest address before load; dest may alias addr register.
    e.mov(e.w0, WReg(addr.getIdx()));
    // Load the value (may clobber addr if dest == addr).
    e.ldr(i.dest, ptr(e.GetMembaseReg(), addr));
    // Save reservation in backend context.
    auto bctx = LoadBackendCtxPtr(e);
    e.str(e.x0, ptr(bctx, static_cast<uint32_t>(offsetof(
                              A64BackendContext, cached_reserve_offset))));
    e.str(i.dest, ptr(bctx, static_cast<uint32_t>(offsetof(
                                A64BackendContext, cached_reserve_value_))));
    e.ldr(e.w1,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    e.orr(e.w1, e.w1, static_cast<uint32_t>(1u << kA64BackendHasReserveBit));
    e.str(e.w1,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RESERVED_LOAD, RESERVED_LOAD_I32,
                     RESERVED_LOAD_I64);

struct RESERVED_STORE_I32
    : Sequence<RESERVED_STORE_I32,
               I<OPCODE_RESERVED_STORE, I8Op, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (cvars::arm64_global_reservation_helpers) {
      e.add(e.x2, e.GetMembaseReg(), addr);
      if (i.src2.is_constant) {
        e.mov(e.w3,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      } else {
        e.mov(e.w3, WReg(i.src2.reg().getIdx()));
      }
      if (i.src1.is_constant) {
        e.mov(e.w1, static_cast<uint32_t>(i.src1.constant()));
      } else {
        e.mov(e.w1, WReg(i.src1.reg().getIdx()));
      }
      e.CallNativeSafe(e.backend()->reserved_store_32_helper);
      e.mov(i.dest, e.w0);
      return;
    }

    auto& no_reserve = e.NewCachedLabel();
    auto& done = e.NewCachedLabel();
    // Check if we have a reservation.
    auto bctx = LoadBackendCtxPtr(e);
    e.ldr(e.w4,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    e.tbz(e.w4, kA64BackendHasReserveBit, no_reserve);
    // Clear the reserve flag.
    e.and_(e.w4, e.w4,
           static_cast<uint32_t>(~(1u << kA64BackendHasReserveBit)));
    e.str(e.w4,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    // Check if address matches.
    e.ldr(e.x4, ptr(bctx, static_cast<uint32_t>(offsetof(
                              A64BackendContext, cached_reserve_offset))));
    e.mov(e.w5, WReg(addr.getIdx()));
    e.cmp(e.x4, e.x5);
    e.b(Xbyak_aarch64::NE, no_reserve);
    // Address matches. Do atomic compare-exchange.
    // Expected value from cached_reserve_value_.
    e.ldr(e.w5, ptr(bctx, static_cast<uint32_t>(offsetof(
                              A64BackendContext, cached_reserve_value_))));
    // Desired value.
    if (i.src2.is_constant) {
      e.mov(e.w6,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
    } else {
      e.mov(e.w6, WReg(i.src2.reg().getIdx()));
    }
    // Compute host address.
    e.add(e.x4, e.GetMembaseReg(), addr);

    if (e.IsFeatureEnabled(kA64EmitLSE)) {
      e.mov(e.w0, e.w5);
      e.casal(e.w5, e.w6, ptr(e.x4));
      e.cmp(e.w5, e.w0);
      e.cset(i.dest, Xbyak_aarch64::EQ);
      e.b(done);
    } else {
      // LDXR/STXR loop.
      auto& cas_loop = e.NewCachedLabel();
      auto& cas_fail = e.NewCachedLabel();
      e.L(cas_loop);
      e.ldaxr(e.w7, ptr(e.x4));
      e.cmp(e.w7, e.w5);
      e.b(Xbyak_aarch64::NE, cas_fail);
      e.stlxr(e.w7, e.w6, ptr(e.x4));
      e.cbnz(e.w7, cas_loop);
      // Success.
      e.mov(i.dest, 1);
      e.b(done);
      e.L(cas_fail);
      e.clrex(15);
    }
    e.L(no_reserve);
    e.mov(i.dest, 0);
    e.L(done);
  }
};
struct RESERVED_STORE_I64
    : Sequence<RESERVED_STORE_I64,
               I<OPCODE_RESERVED_STORE, I8Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (cvars::arm64_global_reservation_helpers) {
      e.add(e.x2, e.GetMembaseReg(), addr);
      if (i.src2.is_constant) {
        e.mov(e.x3, static_cast<uint64_t>(i.src2.constant()));
      } else {
        e.mov(e.x3, XReg(i.src2.reg().getIdx()));
      }
      if (i.src1.is_constant) {
        e.mov(e.w1, static_cast<uint32_t>(i.src1.constant()));
      } else {
        e.mov(e.w1, WReg(i.src1.reg().getIdx()));
      }
      e.CallNativeSafe(e.backend()->reserved_store_64_helper);
      e.mov(i.dest, e.w0);
      return;
    }

    auto& no_reserve = e.NewCachedLabel();
    auto& done = e.NewCachedLabel();
    auto bctx = LoadBackendCtxPtr(e);
    e.ldr(e.w4,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    e.tbz(e.w4, kA64BackendHasReserveBit, no_reserve);
    e.and_(e.w4, e.w4,
           static_cast<uint32_t>(~(1u << kA64BackendHasReserveBit)));
    e.str(e.w4,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    e.ldr(e.x4, ptr(bctx, static_cast<uint32_t>(offsetof(
                              A64BackendContext, cached_reserve_offset))));
    e.mov(e.w5, WReg(addr.getIdx()));
    e.cmp(e.x4, e.x5);
    e.b(Xbyak_aarch64::NE, no_reserve);
    // 64-bit compare-exchange.
    e.ldr(e.x5, ptr(bctx, static_cast<uint32_t>(offsetof(
                              A64BackendContext, cached_reserve_value_))));
    if (i.src2.is_constant) {
      e.mov(e.x6, static_cast<uint64_t>(i.src2.constant()));
    } else {
      e.mov(e.x6, XReg(i.src2.reg().getIdx()));
    }
    e.add(e.x4, e.GetMembaseReg(), addr);

    if (e.IsFeatureEnabled(kA64EmitLSE)) {
      e.mov(e.x0, e.x5);
      e.casal(e.x5, e.x6, ptr(e.x4));
      e.cmp(e.x5, e.x0);
      e.cset(i.dest, Xbyak_aarch64::EQ);
      e.b(done);
    } else {
      auto& cas_loop = e.NewCachedLabel();
      auto& cas_fail = e.NewCachedLabel();
      e.L(cas_loop);
      e.ldaxr(e.x7, ptr(e.x4));
      e.cmp(e.x7, e.x5);
      e.b(Xbyak_aarch64::NE, cas_fail);
      e.stlxr(e.w7, e.x6, ptr(e.x4));
      e.cbnz(e.w7, cas_loop);
      e.mov(i.dest, 1);
      e.b(done);
      e.L(cas_fail);
      e.clrex(15);
    }
    e.L(no_reserve);
    e.mov(i.dest, 0);
    e.L(done);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RESERVED_STORE, RESERVED_STORE_I32,
                     RESERVED_STORE_I64);

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
