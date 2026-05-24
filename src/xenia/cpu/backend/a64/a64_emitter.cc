/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_emitter.h"

#include <algorithm>
#include <array>
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
#include <unordered_map>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/clock.h"
#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_code_cache.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/a64/a64_sequences.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"

DECLARE_uint32(a64_max_stackpoints);
DECLARE_bool(a64_enable_host_guest_stack_synchronization);
DECLARE_uint32(arm64_compiled_call_trace_interval);
DECLARE_uint32(arm64_compiled_call_trace_min_count);
DECLARE_uint32(arm64_compiled_call_trace_budget);
DECLARE_string(arm64_compiled_call_trace_functions);
DECLARE_string(arm64_compiled_call_trace_guest_tids);
DECLARE_uint32(arm64_compiled_call_trace_after_ms);
DECLARE_bool(arm64_blue_dragon_draw_wait_probe);
DECLARE_bool(arm64_blue_dragon_draw_wait_fastpath);
DECLARE_bool(arm64_blue_dragon_draw_wait_fastpath_host_counter_time);
DECLARE_uint32(arm64_blue_dragon_draw_wait_fastpath_native_yield_stride);
DECLARE_uint32(arm64_blue_dragon_draw_wait_fastpath_native_sleep_us);
DECLARE_uint32(arm64_blue_dragon_draw_wait_fastpath_timeout_ms);
DECLARE_bool(arm64_blue_dragon_draw_wait_inline_in_caller);
DECLARE_bool(arm64_blue_dragon_draw_wait_caller_profile);
DECLARE_uint32(arm64_blue_dragon_draw_wait_caller_profile_stride);
DECLARE_uint32(arm64_blue_dragon_draw_wait_caller_profile_budget);
DECLARE_bool(arm64_blue_dragon_memcpy_fastpath);
DECLARE_bool(arm64_blue_dragon_stricmp_fastpath);
DECLARE_bool(arm64_blue_dragon_stricmp_deferred_cr_fastpath);
DECLARE_bool(arm64_blue_dragon_stricmp_return_profile);
DECLARE_uint32(arm64_blue_dragon_stricmp_return_profile_stride);
DECLARE_uint32(arm64_blue_dragon_stricmp_return_profile_budget);
DECLARE_bool(arm64_blue_dragon_jump_table_fastpath);
DECLARE_bool(arm64_blue_dragon_jump_table_inline_in_caller);
DECLARE_bool(arm64_blue_dragon_vmx_copy_loop_fastpath);
DECLARE_bool(arm64_blue_dragon_word_copy_loop_fastpath);
DECLARE_uint32(arm64_blue_dragon_draw_wait_probe_stride);
DECLARE_uint32(arm64_blue_dragon_draw_wait_inline_tick_step);
DECLARE_bool(arm64_context_value_cache);
DECLARE_bool(arm64_context_value_cache_fallthrough);
DECLARE_bool(arm64_context_value_cache_preserve_barrier);
DECLARE_bool(arm64_context_pinned_gpr_r1);
DECLARE_bool(arm64_context_pinned_gpr_r1_fallthrough);
DECLARE_uint32(arm64_context_pinned_gpr_r1_function);
DECLARE_bool(arm64_context_pinned_gpr_r1_audit);
DECLARE_bool(arm64_context_traffic_audit);
DECLARE_uint32(arm64_context_traffic_audit_function);
DECLARE_uint32(arm64_context_traffic_audit_budget);
DECLARE_uint32(arm64_speed_profile_body_time_after_ms);
DECLARE_bool(a64_rtl_leave_fastpath_audit);
DEFINE_bool(a64_inline_gprlr_helpers, true,
            "Inline PPC __savegprlr_* / __restgprlr_* ABI helpers in the "
            "A64 backend.",
            "a64");
DEFINE_bool(
    a64_inline_ppc_thread_field_leaf_helpers, true,
    "Inline tiny PPC leaf helpers matching "
    "`lwz r11,D(r13); lwz r3,D(r11); blr` in the A64 backend.",
    "a64");
DEFINE_bool(a64_inline_kernel_high_frequency_exports, true,
            "Inline a small set of high-frequency Xbox kernel exports in the "
            "A64 backend to avoid guest-to-host thunk overhead.",
            "a64");
DEFINE_bool(a64_inline_kernel_spinlock_exports, true,
            "Inline tiny high-frequency Xbox kernel spin-lock exports in the "
            "A64 backend. Experimental.",
            "a64");
DEFINE_bool(a64_lse_kernel_lock_fastpaths, true,
            "Use ARMv8.1 LSE atomics in A64 kernel lock/IRQL fastpaths when "
            "the host CPU supports them. Experimental.",
            "a64");
DEFINE_bool(
    a64_inline_rtl_leave_final_unlock, true,
    "Inline the uncontended final RtlLeaveCriticalSection unlock in the A64 "
    "backend. Experimental.",
    "a64");
DEFINE_bool(
    a64_rtl_enter_free_first, false,
    "Try the free-lock RtlEnterCriticalSection A64 path before the recursive "
    "owner check. Experimental; default-off after Blue Dragon black-idle.",
    "a64");
DEFINE_bool(
    a64_inline_kf_lower_irql, false,
    "Inline KfLowerIrql by restoring the processor IRQL directly. "
    "Experimental; skips the native APC delivery check.",
    "a64");
DEFINE_bool(
    a64_inline_kf_lower_irql_apc_guard, false,
    "Inline KfLowerIrql only when the current thread reports no pending APCs; "
    "falls back to the native export for APC delivery.",
    "a64");
DEFINE_bool(a64_kf_lower_irql_apc_guard_audit, false,
            "Count guarded KfLowerIrql inline and fallback paths in the A64 "
            "speed profile.",
            "a64");
DEFINE_uint32(
    a64_kf_lower_irql_apc_guard_native_poll_interval, 0,
    "When a64_inline_kf_lower_irql_apc_guard is enabled, force every Nth "
    "no-pending KfLowerIrql through the native export. Use a power of two; 0 "
    "disables forced native polling.",
    "a64");
DEFINE_bool(a64_inline_fpr_helpers, true,
            "Inline PPC __savefpr_* / __restfpr_* ABI helpers in the A64 "
            "backend. Experimental.",
            "a64");
DEFINE_bool(a64_inline_vmx_helpers, true,
            "Inline PPC __savevmx_* / __restvmx_* ABI helpers in the A64 "
            "backend. Experimental.",
            "a64");

namespace {
std::atomic<int> g_a64_call_trace_budget{0};
std::atomic<uint32_t> g_a64_call_trace_configured_budget{
    std::numeric_limits<uint32_t>::max()};
std::atomic<uint64_t> g_a64_call_trace_first_host_ms{0};
std::mutex g_a64_call_trace_counts_mutex;
std::unordered_map<uint64_t, uint64_t> g_a64_call_trace_counts;
std::atomic<int> g_blue_dragon_draw_wait_caller_profile_budget{0};
std::atomic<uint32_t> g_blue_dragon_draw_wait_caller_profile_configured_budget{
    std::numeric_limits<uint32_t>::max()};
std::mutex g_blue_dragon_draw_wait_caller_profile_counts_mutex;
std::unordered_map<uint32_t, uint64_t>
    g_blue_dragon_draw_wait_caller_profile_counts;
std::atomic<int> g_blue_dragon_stricmp_return_profile_budget{0};
std::atomic<uint32_t> g_blue_dragon_stricmp_return_profile_configured_budget{
    std::numeric_limits<uint32_t>::max()};
std::atomic<uint64_t> g_blue_dragon_stricmp_return_profile_count{0};
std::mutex g_blue_dragon_stricmp_return_profile_counts_mutex;
std::unordered_map<uint32_t, uint64_t>
    g_blue_dragon_stricmp_return_profile_lr_counts;
std::atomic<uint32_t> g_a64_context_traffic_audit_count{0};

enum class ContextTrafficSlot {
  kOther,
  kThreadState,
  kVirtualMembase,
  kLr,
  kCtr,
  kGpr,
  kFpr,
  kVmx,
  kXer,
  kCr,
  kFpscr,
  kVscr,
  kThreadRuntime,
  kReservation,
};

struct A64ContextTrafficStats {
  uint32_t blocks = 0;
  uint32_t instrs = 0;
  uint32_t context_loads = 0;
  uint32_t context_stores = 0;
  uint32_t cr_loads = 0;
  uint32_t cr_stores = 0;
  uint32_t gpr_loads = 0;
  uint32_t gpr_stores = 0;
  uint32_t lr_loads = 0;
  uint32_t lr_stores = 0;
  uint32_t ctr_loads = 0;
  uint32_t ctr_stores = 0;
  uint32_t xer_loads = 0;
  uint32_t xer_stores = 0;
  uint32_t fpr_loads = 0;
  uint32_t fpr_stores = 0;
  uint32_t vmx_loads = 0;
  uint32_t vmx_stores = 0;
  uint32_t runtime_loads = 0;
  uint32_t runtime_stores = 0;
  uint32_t local_loads = 0;
  uint32_t local_stores = 0;
  uint32_t memory_loads = 0;
  uint32_t memory_stores = 0;
  uint32_t memory_barriers = 0;
  uint32_t context_barriers = 0;
  uint32_t byte_swaps = 0;
  uint32_t integer_compares = 0;
  uint32_t vector_compares = 0;
  uint32_t branches = 0;
  uint32_t calls = 0;
  uint32_t returns = 0;
  uint32_t atomics = 0;
  uint32_t reservation_ops = 0;
  uint32_t context_store_compare_sources = 0;
  uint32_t context_store_constant_sources = 0;
  uint32_t context_store_select_sources = 0;
  uint32_t context_store_is_false_sources = 0;
  uint32_t cr_store_compare_sources = 0;
  uint32_t cr_store_constant_sources = 0;
  uint32_t cr_store_select_sources = 0;
  uint32_t cr_store_is_false_sources = 0;
  uint32_t cr_update_triplets = 0;
  uint32_t cr_update_triplets_strict = 0;
  uint32_t cr_update_triplets_signed = 0;
  uint32_t cr_update_triplets_unsigned = 0;
  uint32_t cr_gt_eq_pairs = 0;
  uint32_t cr_gt_eq_pairs_strict = 0;
  uint32_t cr6_update_shapes = 0;
  uint32_t cr6_update_shapes_strict = 0;
  std::unordered_map<uint32_t, uint32_t> context_load_offsets;
  std::unordered_map<uint32_t, uint32_t> context_store_offsets;
  std::unordered_map<uint64_t, uint32_t> cr_store_source_offsets;
};

struct A64ContextValueCacheStats {
  uint32_t eligible_loads = 0;
  uint32_t load_hits = 0;
  uint32_t eligible_stores = 0;
  uint32_t store_caches = 0;
  uint32_t offset_invalidations = 0;
  uint32_t register_invalidations = 0;
  uint32_t safety_resets = 0;
  uint32_t block_resets = 0;
  uint32_t barrier_resets = 0;
  uint32_t barrier_preserves = 0;
  uint32_t fallthrough_preserves = 0;
  std::array<uint32_t, 32> load_hits_by_slot = {};
  std::array<uint32_t, 32> store_caches_by_slot = {};
  uint32_t pinned_r1_loads = 0;
  uint32_t pinned_r1_hits = 0;
  uint32_t pinned_r1_pin_loads = 0;
  uint32_t pinned_r1_pin_stores = 0;
  uint32_t pinned_r1_block_resets = 0;
  uint32_t pinned_r1_branch_resets = 0;
  uint32_t pinned_r1_volatile_resets = 0;
  uint32_t pinned_r1_overlap_resets = 0;
  uint32_t pinned_r1_barrier_preserves = 0;
  uint32_t pinned_r1_fallthrough_preserves = 0;
};

class A64ContextValueCache {
 public:
  explicit A64ContextValueCache(bool enabled, bool fallthrough_enabled,
                                bool preserve_barrier_enabled,
                                bool pinned_r1_enabled,
                                bool pinned_r1_fallthrough_enabled)
      : enabled_(enabled),
        fallthrough_enabled_(enabled && fallthrough_enabled),
        preserve_barrier_enabled_(enabled && preserve_barrier_enabled),
        pinned_r1_enabled_(pinned_r1_enabled),
        pinned_r1_fallthrough_enabled_(pinned_r1_enabled &&
                                       pinned_r1_fallthrough_enabled) {
    Reset();
  }

  const A64ContextValueCacheStats& stats() const { return stats_; }

  void ResetBlock(const xe::cpu::hir::Block* block) {
    if (!enabled_ && !pinned_r1_enabled_) {
      return;
    }
    if (pinned_r1_enabled_ && pinned_r1_valid_ &&
        pinned_r1_fallthrough_enabled_ && CanPreserveIntoBlock(block)) {
      ++stats_.pinned_r1_fallthrough_preserves;
    } else if (pinned_r1_enabled_) {
      InvalidatePinnedR1(&stats_.pinned_r1_block_resets);
    }
    if (!enabled_) {
      return;
    }
    if (fallthrough_enabled_ && CanPreserveIntoBlock(block)) {
      ++stats_.fallthrough_preserves;
      return;
    }
    ResetWithReason(&stats_.block_resets);
  }

  bool TryEmitLoad(xe::cpu::backend::a64::A64Emitter& e,
                   const xe::cpu::hir::Instr* instr) {
    using namespace xe::cpu::hir;
    if (!instr || instr->GetOpcodeNum() != OPCODE_LOAD_CONTEXT ||
        !instr->dest || instr->dest->type != INT64_TYPE) {
      return false;
    }

    uint32_t offset = static_cast<uint32_t>(instr->src1.offset);
    uint32_t slot = 0;
    if (!TryGetGprSlot(offset, sizeof(uint64_t), &slot)) {
      return false;
    }

    if (pinned_r1_enabled_ && slot == 1) {
      ++stats_.pinned_r1_loads;
      int dest_host_reg = -1;
      if (!TryHostIntegerRegIndex(instr->dest, &dest_host_reg)) {
        return false;
      }
      Xbyak_aarch64::XReg dest_reg(0);
      xe::cpu::backend::a64::A64Emitter::SetupReg(instr->dest, dest_reg);
      if (pinned_r1_valid_) {
        e.mov(dest_reg, e.x29);
        ++stats_.pinned_r1_hits;
        return true;
      }
      e.ldr(e.x29, Xbyak_aarch64::ptr(e.GetContextReg(), offset));
      e.mov(dest_reg, e.x29);
      pinned_r1_valid_ = true;
      ++stats_.pinned_r1_pin_loads;
      return true;
    }

    if (!enabled_) {
      return false;
    }

    ++stats_.eligible_loads;
    Entry& entry = entries_[slot];
    if (!entry.valid) {
      return false;
    }

    int dest_host_reg = -1;
    if (!TryHostIntegerRegIndex(instr->dest, &dest_host_reg)) {
      return false;
    }

    Xbyak_aarch64::XReg dest_reg(0);
    xe::cpu::backend::a64::A64Emitter::SetupReg(instr->dest, dest_reg);
    if (dest_host_reg != entry.host_reg) {
      e.mov(dest_reg, Xbyak_aarch64::XReg(entry.host_reg));
    }
    InvalidateHostReg(dest_host_reg);
    entry.valid = true;
    entry.host_reg = dest_host_reg;
    ++stats_.load_hits;
    ++stats_.load_hits_by_slot[slot];
    return true;
  }

  void ObservePostEmit(const xe::cpu::hir::Instr* instr,
                       const xe::cpu::hir::Instr* new_tail,
                       xe::cpu::backend::a64::A64Emitter& e) {
    if ((!enabled_ && !pinned_r1_enabled_) || !instr) {
      return;
    }

    if (new_tail != instr->next) {
      const xe::cpu::hir::Instr* cursor = instr;
      while (cursor && cursor != new_tail) {
        ObserveInstructionEffects(cursor, e);
        cursor = cursor->next;
      }
      if (!cursor) {
        ResetWithReason(&stats_.safety_resets);
        InvalidatePinnedR1(&stats_.pinned_r1_volatile_resets);
      }
      return;
    }

    ObserveInstructionEffects(instr, e);
  }

 private:
  struct Entry {
    bool valid = false;
    int host_reg = -1;
  };

  void ObserveInstructionEffects(const xe::cpu::hir::Instr* instr,
                                 xe::cpu::backend::a64::A64Emitter& e) {
    using namespace xe::cpu::hir;
    if (!instr) {
      return;
    }

    switch (instr->GetOpcodeNum()) {
      case OPCODE_CONTEXT_BARRIER:
        if (pinned_r1_valid_) {
          ++stats_.pinned_r1_barrier_preserves;
        }
        if (!enabled_) {
          return;
        }
        if (preserve_barrier_enabled_) {
          ++stats_.barrier_preserves;
          return;
        }
        ResetWithReason(&stats_.barrier_resets);
        return;
      case OPCODE_LOAD_CONTEXT: {
        if (!instr->dest || instr->dest->type != INT64_TYPE) {
          break;
        }
        uint32_t slot = 0;
        if (!TryGetGprSlot(static_cast<uint32_t>(instr->src1.offset),
                           sizeof(uint64_t), &slot)) {
          break;
        }
        if (pinned_r1_enabled_ && slot == 1) {
          int host_reg = -1;
          if (TryHostIntegerRegIndex(instr->dest, &host_reg)) {
            e.mov(e.x29, Xbyak_aarch64::XReg(host_reg));
            pinned_r1_valid_ = true;
            ++stats_.pinned_r1_pin_loads;
          } else {
            InvalidatePinnedR1(&stats_.pinned_r1_volatile_resets);
          }
        }
        if (!enabled_) {
          return;
        }
        int host_reg = -1;
        if (TryHostIntegerRegIndex(instr->dest, &host_reg)) {
          InvalidateHostReg(host_reg);
          entries_[slot].valid = true;
          entries_[slot].host_reg = host_reg;
        }
        return;
      }
      case OPCODE_STORE_CONTEXT: {
        uint32_t offset = static_cast<uint32_t>(instr->src1.offset);
        size_t size = instr->src2.value
                          ? xe::cpu::hir::GetTypeSize(instr->src2.value->type)
                          : 1;
        uint32_t access_end =
            offset + static_cast<uint32_t>(std::max<size_t>(size, 1));
        if (pinned_r1_enabled_ && offset < GprSlotOffset(2) &&
            access_end > GprSlotOffset(1)) {
          InvalidatePinnedR1(&stats_.pinned_r1_overlap_resets);
        }
        if (!enabled_) {
          return;
        }
        uint32_t slot = 0;
        if (!TryGetGprSlot(offset, size, &slot)) {
          InvalidateOverlappingGprSlots(offset, size);
          break;
        }

        ++stats_.eligible_stores;
        int host_reg = -1;
        if (instr->src2.value && instr->src2.value->type == INT64_TYPE &&
            TryHostIntegerRegIndex(instr->src2.value, &host_reg)) {
          entries_[slot].valid = true;
          entries_[slot].host_reg = host_reg;
          ++stats_.store_caches;
          ++stats_.store_caches_by_slot[slot];
        } else {
          InvalidateSlot(slot);
        }
        return;
      }
      default:
        break;
    }

    if (instr->dest) {
      int host_reg = -1;
      if (enabled_ && TryHostIntegerRegIndex(instr->dest, &host_reg)) {
        InvalidateHostReg(host_reg);
      }
    }

    if (instr->opcode->flags & OPCODE_FLAG_BRANCH) {
      if (pinned_r1_valid_ && pinned_r1_fallthrough_enabled_ &&
          IsConditionalBranchTail(instr)) {
        ++stats_.pinned_r1_fallthrough_preserves;
      } else {
        InvalidatePinnedR1(&stats_.pinned_r1_branch_resets);
      }
      if (!enabled_) {
        return;
      }
      if (fallthrough_enabled_ &&
          (instr->GetOpcodeNum() == OPCODE_BRANCH_TRUE ||
           instr->GetOpcodeNum() == OPCODE_BRANCH_FALSE)) {
        return;
      }
      ResetWithReason(&stats_.safety_resets);
      return;
    }

    if (instr->opcode->flags & OPCODE_FLAG_VOLATILE) {
      InvalidatePinnedR1(&stats_.pinned_r1_volatile_resets);
      if (enabled_) {
        ResetWithReason(&stats_.safety_resets);
      }
    }
  }

  static bool IsConditionalBranchTail(const xe::cpu::hir::Instr* instr) {
    using namespace xe::cpu::hir;
    return instr &&
           (instr->GetOpcodeNum() == OPCODE_BRANCH_TRUE ||
            instr->GetOpcodeNum() == OPCODE_BRANCH_FALSE);
  }

  static bool CanPreserveIntoBlock(const xe::cpu::hir::Block* block) {
    if (!block || !block->prev || block->label_head ||
        block->incoming_edge_head) {
      return false;
    }
    return IsConditionalBranchTail(block->prev->instr_tail);
  }

  static bool IsIntegerType(xe::cpu::hir::TypeName type) {
    using namespace xe::cpu::hir;
    switch (type) {
      case INT8_TYPE:
      case INT16_TYPE:
      case INT32_TYPE:
      case INT64_TYPE:
        return true;
      default:
        return false;
    }
  }

  static bool TryHostIntegerRegIndex(const xe::cpu::hir::Value* value,
                                     int* host_reg) {
    if (!value || value->IsConstant() || !IsIntegerType(value->type) ||
        value->reg.index < 0 ||
        value->reg.index >= xe::cpu::backend::a64::A64Emitter::GPR_COUNT) {
      return false;
    }
    Xbyak_aarch64::XReg reg(0);
    xe::cpu::backend::a64::A64Emitter::SetupReg(value, reg);
    *host_reg = reg.getIdx();
    return true;
  }

  static bool TryGetGprSlot(uint32_t offset, size_t size, uint32_t* slot) {
    using xe::cpu::ppc::PPCContext;
    const uint32_t gpr_base = static_cast<uint32_t>(offsetof(PPCContext, r));
    const uint32_t gpr_size = 32 * sizeof(uint64_t);
    if (size != sizeof(uint64_t) || offset < gpr_base ||
        offset >= gpr_base + gpr_size || ((offset - gpr_base) & 7)) {
      return false;
    }
    *slot = (offset - gpr_base) >> 3;
    return true;
  }

  static uint32_t GprSlotOffset(uint32_t slot) {
    using xe::cpu::ppc::PPCContext;
    return static_cast<uint32_t>(offsetof(PPCContext, r) +
                                 slot * sizeof(uint64_t));
  }

  void Reset() {
    for (auto& entry : entries_) {
      entry = {};
    }
  }

  void ResetWithReason(uint32_t* counter) {
    bool had_entries = false;
    for (const auto& entry : entries_) {
      had_entries |= entry.valid;
    }
    if (had_entries && counter) {
      ++(*counter);
    }
    Reset();
  }

  void InvalidateSlot(uint32_t slot) {
    if (entries_[slot].valid) {
      entries_[slot] = {};
      ++stats_.offset_invalidations;
    }
  }

  void InvalidateHostReg(int host_reg) {
    for (auto& entry : entries_) {
      if (entry.valid && entry.host_reg == host_reg) {
        entry = {};
        ++stats_.register_invalidations;
      }
    }
  }

  void InvalidateOverlappingGprSlots(uint32_t offset, size_t size) {
    using xe::cpu::ppc::PPCContext;
    const uint32_t gpr_base = static_cast<uint32_t>(offsetof(PPCContext, r));
    const uint32_t gpr_size = 32 * sizeof(uint64_t);
    uint32_t end = offset + static_cast<uint32_t>(std::max<size_t>(size, 1));
    if (offset >= gpr_base + gpr_size || end <= gpr_base) {
      return;
    }
    for (uint32_t slot = 0; slot < entries_.size(); ++slot) {
      uint32_t slot_offset = gpr_base + slot * sizeof(uint64_t);
      uint32_t slot_end = slot_offset + sizeof(uint64_t);
      if (offset < slot_end && end > slot_offset) {
        InvalidateSlot(slot);
      }
    }
  }

  void InvalidatePinnedR1(uint32_t* counter) {
    if (pinned_r1_valid_) {
      pinned_r1_valid_ = false;
      if (counter) {
        ++(*counter);
      }
    }
  }

  bool enabled_ = false;
  bool fallthrough_enabled_ = false;
  bool preserve_barrier_enabled_ = false;
  bool pinned_r1_enabled_ = false;
  bool pinned_r1_fallthrough_enabled_ = false;
  bool pinned_r1_valid_ = false;
  std::array<Entry, 32> entries_;
  A64ContextValueCacheStats stats_;
};

std::string FormatGprSlotCounts(const std::array<uint32_t, 32>& counts) {
  std::string text;
  for (size_t i = 0; i < counts.size(); ++i) {
    if (!counts[i]) {
      continue;
    }
    if (!text.empty()) {
      text += " ";
    }
    text += fmt::format("r[{}]={}", i, counts[i]);
  }
  return text.empty() ? "-" : text;
}

bool ConsumeContextTrafficAuditBudget(uint32_t* out_index) {
  uint32_t budget = cvars::arm64_context_traffic_audit_budget;
  uint32_t value = g_a64_context_traffic_audit_count.load(
      std::memory_order_relaxed);
  while (value < budget) {
    if (g_a64_context_traffic_audit_count.compare_exchange_strong(
            value, value + 1, std::memory_order_acq_rel)) {
      *out_index = value + 1;
      return true;
    }
  }
  return false;
}

bool AccessOverlaps(size_t offset, size_t size, size_t range_start,
                    size_t range_end) {
  size_t end = offset + std::max<size_t>(size, 1);
  return offset < range_end && end > range_start;
}

ContextTrafficSlot ClassifyContextTrafficSlot(size_t offset, size_t size) {
  using xe::cpu::ppc::PPCContext;

  if (AccessOverlaps(offset, size, offsetof(PPCContext, thread_state),
                     offsetof(PPCContext, thread_state) +
                         sizeof(static_cast<PPCContext*>(nullptr)
                                    ->thread_state))) {
    return ContextTrafficSlot::kThreadState;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, virtual_membase),
                     offsetof(PPCContext, virtual_membase) +
                         sizeof(static_cast<PPCContext*>(nullptr)
                                    ->virtual_membase))) {
    return ContextTrafficSlot::kVirtualMembase;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, lr),
                     offsetof(PPCContext, lr) +
                         sizeof(static_cast<PPCContext*>(nullptr)->lr))) {
    return ContextTrafficSlot::kLr;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, ctr),
                     offsetof(PPCContext, ctr) +
                         sizeof(static_cast<PPCContext*>(nullptr)->ctr))) {
    return ContextTrafficSlot::kCtr;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, r),
                     offsetof(PPCContext, f))) {
    return ContextTrafficSlot::kGpr;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, f),
                     offsetof(PPCContext, v))) {
    return ContextTrafficSlot::kFpr;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, v),
                     offsetof(PPCContext, xer_ca))) {
    return ContextTrafficSlot::kVmx;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, xer_ca),
                     offsetof(PPCContext, cr0))) {
    return ContextTrafficSlot::kXer;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, cr0),
                     offsetof(PPCContext, fpscr))) {
    return ContextTrafficSlot::kCr;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, fpscr),
                     offsetof(PPCContext, vscr_sat))) {
    return ContextTrafficSlot::kFpscr;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, vscr_sat),
                     offsetof(PPCContext, thread_id))) {
    return ContextTrafficSlot::kVscr;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, thread_id),
                     offsetof(PPCContext, physical_membase))) {
    return ContextTrafficSlot::kThreadRuntime;
  }
  if (AccessOverlaps(offset, size, offsetof(PPCContext, physical_membase),
                     sizeof(PPCContext))) {
    return ContextTrafficSlot::kReservation;
  }
  return ContextTrafficSlot::kOther;
}

void AddContextTrafficAccess(A64ContextTrafficStats* stats, uint32_t offset,
                             size_t size, bool store) {
  ContextTrafficSlot slot = ClassifyContextTrafficSlot(offset, size);
  if (store) {
    ++stats->context_stores;
    ++stats->context_store_offsets[offset];
  } else {
    ++stats->context_loads;
    ++stats->context_load_offsets[offset];
  }

  auto add_load_store = [store](uint32_t* loads, uint32_t* stores) {
    if (store) {
      ++*stores;
    } else {
      ++*loads;
    }
  };

  switch (slot) {
    case ContextTrafficSlot::kLr:
      add_load_store(&stats->lr_loads, &stats->lr_stores);
      break;
    case ContextTrafficSlot::kCtr:
      add_load_store(&stats->ctr_loads, &stats->ctr_stores);
      break;
    case ContextTrafficSlot::kGpr:
      add_load_store(&stats->gpr_loads, &stats->gpr_stores);
      break;
    case ContextTrafficSlot::kFpr:
      add_load_store(&stats->fpr_loads, &stats->fpr_stores);
      break;
    case ContextTrafficSlot::kVmx:
      add_load_store(&stats->vmx_loads, &stats->vmx_stores);
      break;
    case ContextTrafficSlot::kXer:
      add_load_store(&stats->xer_loads, &stats->xer_stores);
      break;
    case ContextTrafficSlot::kCr:
      add_load_store(&stats->cr_loads, &stats->cr_stores);
      break;
    case ContextTrafficSlot::kThreadState:
    case ContextTrafficSlot::kVirtualMembase:
    case ContextTrafficSlot::kThreadRuntime:
    case ContextTrafficSlot::kReservation:
      add_load_store(&stats->runtime_loads, &stats->runtime_stores);
      break;
    case ContextTrafficSlot::kOther:
    case ContextTrafficSlot::kFpscr:
    case ContextTrafficSlot::kVscr:
      break;
  }
}

bool IsIntegerCompareOpcode(xe::cpu::hir::Opcode opcode) {
  return opcode >= xe::cpu::hir::OPCODE_COMPARE_EQ &&
         opcode <= xe::cpu::hir::OPCODE_COMPARE_UGE;
}

bool IsVectorCompareOpcode(xe::cpu::hir::Opcode opcode) {
  return opcode >= xe::cpu::hir::OPCODE_VECTOR_COMPARE_EQ &&
         opcode <= xe::cpu::hir::OPCODE_VECTOR_COMPARE_UGE;
}

bool IsCrContextOffset(uint32_t offset) {
  using xe::cpu::ppc::PPCContext;
  const uint32_t cr_base = static_cast<uint32_t>(offsetof(PPCContext, cr0));
  const uint32_t cr_end = static_cast<uint32_t>(offsetof(PPCContext, fpscr));
  return offset >= cr_base && offset < cr_end;
}

uint64_t MakeContextTrafficOffsetOpcodeKey(uint32_t offset,
                                           xe::cpu::hir::Opcode opcode) {
  return (static_cast<uint64_t>(offset) << 32) |
         static_cast<uint32_t>(opcode);
}

const char* ContextTrafficOpcodeName(xe::cpu::hir::Opcode opcode) {
  using namespace xe::cpu::hir;
  switch (opcode) {
    case OPCODE_LOAD_CONTEXT:
      return "load_context";
    case OPCODE_STORE_CONTEXT:
      return "store_context";
    case OPCODE_COMPARE_EQ:
      return "cmp_eq";
    case OPCODE_COMPARE_NE:
      return "cmp_ne";
    case OPCODE_COMPARE_SLT:
      return "cmp_slt";
    case OPCODE_COMPARE_SLE:
      return "cmp_sle";
    case OPCODE_COMPARE_SGT:
      return "cmp_sgt";
    case OPCODE_COMPARE_SGE:
      return "cmp_sge";
    case OPCODE_COMPARE_ULT:
      return "cmp_ult";
    case OPCODE_COMPARE_ULE:
      return "cmp_ule";
    case OPCODE_COMPARE_UGT:
      return "cmp_ugt";
    case OPCODE_COMPARE_UGE:
      return "cmp_uge";
    case OPCODE_VECTOR_COMPARE_EQ:
      return "vcmp_eq";
    case OPCODE_VECTOR_COMPARE_SGT:
      return "vcmp_sgt";
    case OPCODE_VECTOR_COMPARE_SGE:
      return "vcmp_sge";
    case OPCODE_VECTOR_COMPARE_UGT:
      return "vcmp_ugt";
    case OPCODE_VECTOR_COMPARE_UGE:
      return "vcmp_uge";
    case OPCODE_IS_TRUE:
      return "is_true";
    case OPCODE_IS_FALSE:
      return "is_false";
    case OPCODE_NOT:
      return "not";
    case OPCODE_AND:
      return "and";
    case OPCODE_AND_NOT:
      return "and_not";
    case OPCODE_OR:
      return "or";
    case OPCODE_XOR:
      return "xor";
    case OPCODE_SELECT:
      return "select";
    case OPCODE_TRUNCATE:
      return "truncate";
    case OPCODE_ZERO_EXTEND:
      return "zero_extend";
    case OPCODE_SHL:
      return "shl";
    case OPCODE_SHR:
      return "shr";
    default:
      return "other";
  }
}

bool AuditIntegerType(xe::cpu::hir::TypeName type) {
  using namespace xe::cpu::hir;
  switch (type) {
    case INT8_TYPE:
    case INT16_TYPE:
    case INT32_TYPE:
    case INT64_TYPE:
      return true;
    default:
      return false;
  }
}

uint64_t AuditIntegerValueBits(const xe::cpu::hir::Value* value) {
  using namespace xe::cpu::hir;
  switch (value->type) {
    case INT8_TYPE:
      return static_cast<uint8_t>(value->constant.i8);
    case INT16_TYPE:
      return static_cast<uint16_t>(value->constant.i16);
    case INT32_TYPE:
      return static_cast<uint32_t>(value->constant.i32);
    case INT64_TYPE:
      return static_cast<uint64_t>(value->constant.i64);
    default:
      return 0;
  }
}

bool AuditValuesMatchForCompare(const xe::cpu::hir::Value* a,
                                const xe::cpu::hir::Value* b) {
  if (a == b) {
    return true;
  }
  if (!a || !b || a->type != b->type || !AuditIntegerType(a->type)) {
    return false;
  }
  if (a->IsConstant() && b->IsConstant()) {
    return AuditIntegerValueBits(a) == AuditIntegerValueBits(b);
  }
  return false;
}

bool AuditCompareOperandsMatch(const xe::cpu::hir::Instr* a,
                               const xe::cpu::hir::Instr* b) {
  return a && b &&
         AuditValuesMatchForCompare(a->src1.value, b->src1.value) &&
         AuditValuesMatchForCompare(a->src2.value, b->src2.value);
}

bool AuditValueUsesOnly(const xe::cpu::hir::Value* value,
                        const xe::cpu::hir::Instr* allowed_a,
                        const xe::cpu::hir::Instr* allowed_b = nullptr) {
  if (!value || !value->use_head) {
    return false;
  }
  for (auto use = value->use_head; use; use = use->next) {
    if (use->instr == allowed_a || use->instr == allowed_b) {
      continue;
    }
    return false;
  }
  return true;
}

bool AuditStoreContextOfValue(const xe::cpu::hir::Instr* instr,
                              const xe::cpu::hir::Value* value,
                              uint32_t* out_offset) {
  if (!instr || instr->GetOpcodeNum() != xe::cpu::hir::OPCODE_STORE_CONTEXT ||
      instr->src2.value != value) {
    return false;
  }
  *out_offset = static_cast<uint32_t>(instr->src1.offset);
  return true;
}

const xe::cpu::hir::Instr* AuditBranchOnValue(
    const xe::cpu::hir::Instr* instr, const xe::cpu::hir::Value* value) {
  if (!instr || instr->src1.value != value) {
    return nullptr;
  }
  switch (instr->GetOpcodeNum()) {
    case xe::cpu::hir::OPCODE_BRANCH_TRUE:
    case xe::cpu::hir::OPCODE_BRANCH_FALSE:
      return instr;
    default:
      return nullptr;
  }
}

bool AuditPpcCrGtEqStorePair(uint32_t gt_offset, uint32_t eq_offset) {
  using xe::cpu::ppc::PPCContext;
  const uint32_t cr_base = static_cast<uint32_t>(offsetof(PPCContext, cr0));
  const uint32_t cr_end = cr_base + 8 * 4;
  return gt_offset + 1 == eq_offset && gt_offset >= cr_base + 1 &&
         gt_offset < cr_end && ((gt_offset - cr_base) & 3) == 1;
}

bool AuditPpcCrLtGtEqStoreTriplet(uint32_t lt_offset, uint32_t gt_offset,
                                  uint32_t eq_offset) {
  using xe::cpu::ppc::PPCContext;
  const uint32_t cr_base = static_cast<uint32_t>(offsetof(PPCContext, cr0));
  const uint32_t cr_end = cr_base + 8 * 4;
  return lt_offset + 1 == gt_offset && gt_offset + 1 == eq_offset &&
         lt_offset >= cr_base && eq_offset < cr_end &&
         ((lt_offset - cr_base) & 3) == 0;
}

bool AuditCrTripletShape(const xe::cpu::hir::Instr* instr, bool* strict,
                         bool* is_signed) {
  using namespace xe::cpu::hir;
  if (!instr || !instr->dest || instr->dest->type != INT8_TYPE) {
    return false;
  }

  if (instr->GetOpcodeNum() == OPCODE_COMPARE_SLT) {
    *is_signed = true;
  } else if (instr->GetOpcodeNum() == OPCODE_COMPARE_ULT) {
    *is_signed = false;
  } else {
    return false;
  }

  const Instr* lt_store = instr->next;
  uint32_t lt_offset = 0;
  if (!AuditStoreContextOfValue(lt_store, instr->dest, &lt_offset)) {
    return false;
  }

  const Instr* gt_compare = lt_store->next;
  Opcode expected_gt = *is_signed ? OPCODE_COMPARE_SGT : OPCODE_COMPARE_UGT;
  if (!gt_compare || gt_compare->GetOpcodeNum() != expected_gt ||
      !gt_compare->dest || gt_compare->dest->type != INT8_TYPE ||
      !AuditCompareOperandsMatch(instr, gt_compare)) {
    return false;
  }

  const Instr* gt_store = gt_compare->next;
  uint32_t gt_offset = 0;
  if (!AuditStoreContextOfValue(gt_store, gt_compare->dest, &gt_offset)) {
    return false;
  }

  const Instr* eq_compare = gt_store->next;
  if (!eq_compare || eq_compare->GetOpcodeNum() != OPCODE_COMPARE_EQ ||
      !eq_compare->dest || eq_compare->dest->type != INT8_TYPE ||
      !AuditCompareOperandsMatch(instr, eq_compare)) {
    return false;
  }

  const Instr* eq_store = eq_compare->next;
  uint32_t eq_offset = 0;
  if (!AuditStoreContextOfValue(eq_store, eq_compare->dest, &eq_offset) ||
      !AuditPpcCrLtGtEqStoreTriplet(lt_offset, gt_offset, eq_offset)) {
    return false;
  }

  const Instr* branch = AuditBranchOnValue(eq_store->next, eq_compare->dest);
  *strict = AuditValueUsesOnly(instr->dest, lt_store) &&
            AuditValueUsesOnly(gt_compare->dest, gt_store) &&
            AuditValueUsesOnly(eq_compare->dest, eq_store, branch);
  return true;
}

bool AuditCrGtEqPairShape(const xe::cpu::hir::Instr* instr, bool* strict) {
  using namespace xe::cpu::hir;
  if (!instr || instr->GetOpcodeNum() != OPCODE_COMPARE_UGT || !instr->dest ||
      instr->dest->type != INT8_TYPE) {
    return false;
  }

  const Instr* gt_store = instr->next;
  uint32_t gt_offset = 0;
  if (!AuditStoreContextOfValue(gt_store, instr->dest, &gt_offset)) {
    return false;
  }

  const Instr* eq_compare = gt_store->next;
  if (!eq_compare || eq_compare->GetOpcodeNum() != OPCODE_COMPARE_EQ ||
      !eq_compare->dest || eq_compare->dest->type != INT8_TYPE ||
      !AuditCompareOperandsMatch(instr, eq_compare)) {
    return false;
  }

  const Instr* eq_store = eq_compare->next;
  uint32_t eq_offset = 0;
  if (!AuditStoreContextOfValue(eq_store, eq_compare->dest, &eq_offset) ||
      !AuditPpcCrGtEqStorePair(gt_offset, eq_offset)) {
    return false;
  }

  const Instr* branch = AuditBranchOnValue(eq_store->next, eq_compare->dest);
  *strict = AuditValueUsesOnly(instr->dest, gt_store) &&
            AuditValueUsesOnly(eq_compare->dest, eq_store, branch);
  return true;
}

bool AuditCr6UpdateShape(const xe::cpu::hir::Instr* instr, bool* strict) {
  using namespace xe::cpu::hir;
  using xe::cpu::ppc::PPCContext;

  if (!instr || instr->GetOpcodeNum() != OPCODE_NOT || !instr->dest) {
    return false;
  }

  const Instr* all_false = instr->next;
  if (!all_false || all_false->GetOpcodeNum() != OPCODE_IS_FALSE ||
      all_false->src1.value != instr->dest || !all_false->dest ||
      all_false->dest->type != INT8_TYPE) {
    return false;
  }

  const Instr* all_store = all_false->next;
  uint32_t all_offset = 0;
  if (!AuditStoreContextOfValue(all_store, all_false->dest, &all_offset) ||
      all_offset !=
          static_cast<uint32_t>(offsetof(PPCContext, cr6.cr6_all_equal))) {
    return false;
  }

  const Instr* none_false = all_store->next;
  if (!none_false || none_false->GetOpcodeNum() != OPCODE_IS_FALSE ||
      none_false->src1.value != instr->src1.value || !none_false->dest ||
      none_false->dest->type != INT8_TYPE) {
    return false;
  }

  const Instr* none_store = none_false->next;
  uint32_t none_offset = 0;
  if (!AuditStoreContextOfValue(none_store, none_false->dest, &none_offset) ||
      none_offset !=
          static_cast<uint32_t>(offsetof(PPCContext, cr6.cr6_none_equal))) {
    return false;
  }

  *strict = AuditValueUsesOnly(instr->dest, all_false) &&
            AuditValueUsesOnly(all_false->dest, all_store) &&
            AuditValueUsesOnly(none_false->dest, none_store);
  return true;
}

std::string FormatContextTrafficTopOffsets(
    const std::unordered_map<uint32_t, uint32_t>& offsets) {
  if (offsets.empty()) {
    return "-";
  }

  std::vector<std::pair<uint32_t, uint32_t>> sorted(offsets.begin(),
                                                    offsets.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) {
              if (a.second != b.second) {
                return a.second > b.second;
              }
              return a.first < b.first;
            });

  std::string text;
  size_t count = std::min<size_t>(sorted.size(), 8);
  for (size_t i = 0; i < count; ++i) {
    if (!text.empty()) {
      text += ",";
    }
    text += fmt::format("0x{:03X}:{}", sorted[i].first, sorted[i].second);
  }
  return text;
}

std::string FormatContextTrafficTopOffsetOpcodes(
    const std::unordered_map<uint64_t, uint32_t>& offsets) {
  if (offsets.empty()) {
    return "-";
  }

  std::vector<std::pair<uint64_t, uint32_t>> sorted(offsets.begin(),
                                                    offsets.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) {
              if (a.second != b.second) {
                return a.second > b.second;
              }
              return a.first < b.first;
            });

  std::string text;
  size_t count = std::min<size_t>(sorted.size(), 10);
  for (size_t i = 0; i < count; ++i) {
    if (!text.empty()) {
      text += ",";
    }
    uint32_t offset = static_cast<uint32_t>(sorted[i].first >> 32);
    auto opcode = static_cast<xe::cpu::hir::Opcode>(
        static_cast<uint32_t>(sorted[i].first));
    text += fmt::format("0x{:03X}:{}:{}", offset,
                        ContextTrafficOpcodeName(opcode), sorted[i].second);
  }
  return text;
}

std::string_view TrimTraceToken(std::string_view value) {
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

bool ParseTraceNumber(std::string_view value, uint32_t* out_value) {
  value = TrimTraceToken(value);
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

  uint32_t parsed = 0;
  auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed, base);
  if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
    return false;
  }

  *out_value = parsed;
  return true;
}

bool TraceFilterMatches(uint32_t value, std::string_view filter) {
  filter = TrimTraceToken(filter);
  if (filter.empty()) {
    return true;
  }

  size_t start = 0;
  while (start < filter.size()) {
    size_t end = filter.find_first_of(",; ", start);
    if (end == std::string_view::npos) {
      end = filter.size();
    }
    std::string_view token = TrimTraceToken(filter.substr(start, end - start));
    if (!token.empty()) {
      size_t dash = token.find('-');
      uint32_t range_start = 0;
      uint32_t range_end = 0;
      if (dash != std::string_view::npos &&
          ParseTraceNumber(token.substr(0, dash), &range_start) &&
          ParseTraceNumber(token.substr(dash + 1), &range_end)) {
        if (range_start > range_end) {
          std::swap(range_start, range_end);
        }
        if (range_start <= value && value <= range_end) {
          return true;
        }
      } else if (ParseTraceNumber(token, &range_start) &&
                 range_start == value) {
        return true;
      }
    }
    start = end + 1;
  }

  return false;
}

void ConfigureA64CallTraceBudget() {
  uint32_t budget = cvars::arm64_compiled_call_trace_budget;
  uint32_t configured_budget =
      g_a64_call_trace_configured_budget.load(std::memory_order_relaxed);
  if (configured_budget == budget) {
    return;
  }

  if (g_a64_call_trace_configured_budget.compare_exchange_strong(
          configured_budget, budget, std::memory_order_acq_rel)) {
    int clamped_budget =
        budget > static_cast<uint32_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(budget);
    g_a64_call_trace_budget.store(clamped_budget, std::memory_order_release);
  }
}

bool ConsumeA64CallTraceBudget() {
  int value = g_a64_call_trace_budget.load(std::memory_order_relaxed);
  while (value > 0) {
    if (g_a64_call_trace_budget.compare_exchange_strong(
            value, value - 1, std::memory_order_acq_rel)) {
      return true;
    }
  }
  return false;
}

void ConfigureBlueDragonDrawWaitCallerProfileBudget() {
  uint32_t budget = cvars::arm64_blue_dragon_draw_wait_caller_profile_budget;
  uint32_t configured_budget =
      g_blue_dragon_draw_wait_caller_profile_configured_budget.load(
          std::memory_order_relaxed);
  if (configured_budget == budget) {
    return;
  }

  if (g_blue_dragon_draw_wait_caller_profile_configured_budget
          .compare_exchange_strong(configured_budget, budget,
                                   std::memory_order_acq_rel)) {
    int clamped_budget =
        budget > static_cast<uint32_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(budget);
    g_blue_dragon_draw_wait_caller_profile_budget.store(
        clamped_budget, std::memory_order_release);
  }
}

bool ConsumeBlueDragonDrawWaitCallerProfileBudget() {
  int value = g_blue_dragon_draw_wait_caller_profile_budget.load(
      std::memory_order_relaxed);
  while (value > 0) {
    if (g_blue_dragon_draw_wait_caller_profile_budget.compare_exchange_strong(
            value, value - 1, std::memory_order_acq_rel)) {
      return true;
    }
  }
  return false;
}

void ConfigureBlueDragonStricmpReturnProfileBudget() {
  uint32_t budget = cvars::arm64_blue_dragon_stricmp_return_profile_budget;
  uint32_t configured_budget =
      g_blue_dragon_stricmp_return_profile_configured_budget.load(
          std::memory_order_relaxed);
  if (configured_budget == budget) {
    return;
  }

  if (g_blue_dragon_stricmp_return_profile_configured_budget
          .compare_exchange_strong(configured_budget, budget,
                                   std::memory_order_acq_rel)) {
    int clamped_budget =
        budget > static_cast<uint32_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(budget);
    g_blue_dragon_stricmp_return_profile_budget.store(
        clamped_budget, std::memory_order_release);
  }
}

bool ConsumeBlueDragonStricmpReturnProfileBudget() {
  int value = g_blue_dragon_stricmp_return_profile_budget.load(
      std::memory_order_relaxed);
  while (value > 0) {
    if (g_blue_dragon_stricmp_return_profile_budget.compare_exchange_strong(
            value, value - 1, std::memory_order_acq_rel)) {
      return true;
    }
  }
  return false;
}

bool A64CallTraceRequested() {
  return cvars::arm64_compiled_call_trace_interval != 0 &&
         cvars::arm64_compiled_call_trace_budget != 0;
}

std::string DescribeTraceFunction(xe::cpu::Processor* processor,
                                  uint32_t address) {
  if (!processor || !address) {
    return {};
  }

  xe::cpu::Function* function = processor->QueryFunction(address);
  if (!function) {
    auto functions = processor->FindFunctionsWithAddress(address);
    if (!functions.empty()) {
      function = functions.front();
    }
  }
  if (!function) {
    return {};
  }

  std::string description = function->name();
  if (description.empty()) {
    description = fmt::format("sub_{:08X}", function->address());
  }
  if (address != function->address()) {
    description += fmt::format("+{:X}", address - function->address());
  }
  return description;
}

void TraceFunctionEntry(void* raw_context, uint64_t function_address) {
  auto ctx = reinterpret_cast<xe::cpu::ppc::PPCContext*>(raw_context);
  if (!ctx || !A64CallTraceRequested()) {
    return;
  }

  uint32_t function_u32 = static_cast<uint32_t>(function_address);
  if (!TraceFilterMatches(
          ctx->thread_id, cvars::arm64_compiled_call_trace_guest_tids) ||
      !TraceFilterMatches(
          function_u32, cvars::arm64_compiled_call_trace_functions)) {
    return;
  }

  uint64_t now_ms = xe::Clock::QueryHostUptimeMillis();
  uint64_t first_ms =
      g_a64_call_trace_first_host_ms.load(std::memory_order_relaxed);
  if (!first_ms &&
      g_a64_call_trace_first_host_ms.compare_exchange_strong(
          first_ms, now_ms, std::memory_order_acq_rel)) {
    first_ms = now_ms;
  }
  uint32_t after_ms = cvars::arm64_compiled_call_trace_after_ms;
  if (after_ms && now_ms - first_ms < after_ms) {
    return;
  }

  uint64_t count = 0;
  {
    std::lock_guard<std::mutex> lock(g_a64_call_trace_counts_mutex);
    count = ++g_a64_call_trace_counts[function_u32];
  }

  uint32_t min_count = cvars::arm64_compiled_call_trace_min_count;
  if (min_count && count < min_count) {
    return;
  }

  uint32_t interval = cvars::arm64_compiled_call_trace_interval;
  if (interval > 1 && count % interval != 0 &&
      cvars::arm64_compiled_call_trace_functions.empty()) {
    return;
  }

  ConfigureA64CallTraceBudget();
  if (!ConsumeA64CallTraceBudget()) {
    return;
  }

  uint32_t lr = static_cast<uint32_t>(ctx->lr);
  uint32_t ctr = static_cast<uint32_t>(ctx->ctr);
  std::string fn_name = DescribeTraceFunction(ctx->processor, function_u32);
  std::string lr_name = DescribeTraceFunction(ctx->processor, lr);
  std::string ctr_name = DescribeTraceFunction(ctx->processor, ctr);

  XELOGI(
      "A64 call trace thid {:08X} fn {:08X} '{}' count {} lr {:08X} '{}' "
      "ctr {:08X} '{}' r1 {:08X} r3 {:08X} r10 {:08X} r11 {:08X} r13 {:08X} "
      "r29 {:08X} r30 {:08X} r31 {:08X}",
      ctx->thread_id, function_u32, fn_name, count, lr, lr_name, ctr, ctr_name,
      static_cast<uint32_t>(ctx->r[1]), static_cast<uint32_t>(ctx->r[3]),
      static_cast<uint32_t>(ctx->r[10]), static_cast<uint32_t>(ctx->r[11]),
      static_cast<uint32_t>(ctx->r[13]), static_cast<uint32_t>(ctx->r[29]),
      static_cast<uint32_t>(ctx->r[30]), static_cast<uint32_t>(ctx->r[31]));
}

void UpdateBlueDragonDrawWaitKernelTimeForFastpath(void* raw_context) {
  auto ctx = reinterpret_cast<xe::cpu::ppc::PPCContext*>(raw_context);
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
  xe::store_and_swap<uint32_t>(thread + 0x58,
                               xe::Clock::QueryGuestUptimeMillis());
}

void YieldBlueDragonDrawWaitFastpath(void* raw_context) {
  (void)raw_context;
  uint32_t sleep_us =
      cvars::arm64_blue_dragon_draw_wait_fastpath_native_sleep_us;
  if (sleep_us != 0) {
    xe::threading::Sleep(std::chrono::microseconds(sleep_us));
    return;
  }
  xe::threading::MaybeYield();
}

void RecordBlueDragonDrawWaitCallerProfile(void* raw_context) {
  auto ctx = reinterpret_cast<xe::cpu::ppc::PPCContext*>(raw_context);
  if (!ctx || !cvars::arm64_blue_dragon_draw_wait_caller_profile) {
    return;
  }

  ConfigureBlueDragonDrawWaitCallerProfileBudget();
  if (!ConsumeBlueDragonDrawWaitCallerProfileBudget()) {
    return;
  }

  uint32_t lr = static_cast<uint32_t>(ctx->lr);
  uint64_t samples_for_lr = 0;
  {
    std::lock_guard<std::mutex> lock(
        g_blue_dragon_draw_wait_caller_profile_counts_mutex);
    samples_for_lr = ++g_blue_dragon_draw_wait_caller_profile_counts[lr];
  }

  std::string lr_name = DescribeTraceFunction(ctx->processor, lr);
  XELOGW(
      "A64 Blue Dragon draw-wait caller sample thid {:08X} lr {:08X} '{}' "
      "samples_for_lr={} r1 {:08X} r3 {:08X} r29 {:08X} r30 {:08X} "
      "r31 {:08X}",
      ctx->thread_id, lr, lr_name, samples_for_lr,
      static_cast<uint32_t>(ctx->r[1]), static_cast<uint32_t>(ctx->r[3]),
      static_cast<uint32_t>(ctx->r[29]), static_cast<uint32_t>(ctx->r[30]),
      static_cast<uint32_t>(ctx->r[31]));
}

void RecordBlueDragonStricmpReturnProfile(void* raw_context) {
  auto ctx = reinterpret_cast<xe::cpu::ppc::PPCContext*>(raw_context);
  if (!ctx || !cvars::arm64_blue_dragon_stricmp_return_profile) {
    return;
  }

  uint64_t sample_count =
      g_blue_dragon_stricmp_return_profile_count.fetch_add(
          1, std::memory_order_relaxed) +
      1;
  uint32_t stride =
      std::max(cvars::arm64_blue_dragon_stricmp_return_profile_stride, 1u);
  if (stride > 1 && sample_count % stride != 0) {
    return;
  }

  ConfigureBlueDragonStricmpReturnProfileBudget();
  if (!ConsumeBlueDragonStricmpReturnProfileBudget()) {
    return;
  }

  uint32_t lr = static_cast<uint32_t>(ctx->lr);
  uint64_t samples_for_lr = 0;
  {
    std::lock_guard<std::mutex> lock(
        g_blue_dragon_stricmp_return_profile_counts_mutex);
    samples_for_lr = ++g_blue_dragon_stricmp_return_profile_lr_counts[lr];
  }

  uint32_t cr_packed = static_cast<uint32_t>(ctx->cr());
  std::string lr_name = DescribeTraceFunction(ctx->processor, lr);
  XELOGW(
      "A64 Blue Dragon stricmp return sample count={} thid {:08X} lr {:08X} "
      "'{}' samples_for_lr={} r1 {:08X} r3 {:08X} r4 {:08X} r5 {:08X} "
      "r6 {:08X} r9 {:08X} cr {:08X} cr0 {:08X} cr5 {:08X} cr6 {:08X} "
      "cr7 {:08X} xer_so {}",
      sample_count, ctx->thread_id, lr, lr_name, samples_for_lr,
      static_cast<uint32_t>(ctx->r[1]), static_cast<uint32_t>(ctx->r[3]),
      static_cast<uint32_t>(ctx->r[4]), static_cast<uint32_t>(ctx->r[5]),
      static_cast<uint32_t>(ctx->r[6]), static_cast<uint32_t>(ctx->r[9]),
      cr_packed, ctx->cr0.value, ctx->cr5.value, ctx->cr6.value,
      ctx->cr7.value, static_cast<uint32_t>(ctx->xer_so));
}

bool ParseGprLrHelper(const xe::cpu::GuestFunction* function, bool* is_save,
                      int* first_gpr) {
  if (!function || !is_save || !first_gpr) {
    return false;
  }

  const std::string& name = function->name();
  constexpr std::string_view kSavePrefix = "__savegprlr_";
  constexpr std::string_view kRestPrefix = "__restgprlr_";
  std::string_view suffix;
  if (function->behavior() == xe::cpu::Function::Behavior::kProlog &&
      name.rfind(kSavePrefix, 0) == 0) {
    *is_save = true;
    suffix = std::string_view(name).substr(kSavePrefix.size());
  } else if (function->behavior() ==
                 xe::cpu::Function::Behavior::kEpilogReturn &&
             name.rfind(kRestPrefix, 0) == 0) {
    *is_save = false;
    suffix = std::string_view(name).substr(kRestPrefix.size());
  } else {
    return false;
  }

  int parsed = 0;
  auto result =
      std::from_chars(suffix.data(), suffix.data() + suffix.size(), parsed);
  if (result.ec != std::errc() || result.ptr != suffix.data() + suffix.size() ||
      parsed < 14 || parsed > 31) {
    return false;
  }
  *first_gpr = parsed;
  return true;
}

bool ParseFprVmxHelper(const xe::cpu::GuestFunction* function, bool* is_save,
                       bool* is_vmx, int* first_reg, int* last_reg) {
  if (!function || !is_save || !is_vmx || !first_reg || !last_reg) {
    return false;
  }

  const std::string& name = function->name();
  constexpr std::string_view kSaveFprPrefix = "__savefpr_";
  constexpr std::string_view kRestFprPrefix = "__restfpr_";
  constexpr std::string_view kSaveVmxPrefix = "__savevmx_";
  constexpr std::string_view kRestVmxPrefix = "__restvmx_";
  std::string_view suffix;
  if (function->behavior() == xe::cpu::Function::Behavior::kProlog &&
      name.rfind(kSaveFprPrefix, 0) == 0) {
    *is_save = true;
    *is_vmx = false;
    suffix = std::string_view(name).substr(kSaveFprPrefix.size());
  } else if (function->behavior() == xe::cpu::Function::Behavior::kEpilog &&
             name.rfind(kRestFprPrefix, 0) == 0) {
    *is_save = false;
    *is_vmx = false;
    suffix = std::string_view(name).substr(kRestFprPrefix.size());
  } else if (function->behavior() == xe::cpu::Function::Behavior::kProlog &&
             name.rfind(kSaveVmxPrefix, 0) == 0) {
    *is_save = true;
    *is_vmx = true;
    suffix = std::string_view(name).substr(kSaveVmxPrefix.size());
  } else if (function->behavior() == xe::cpu::Function::Behavior::kEpilog &&
             name.rfind(kRestVmxPrefix, 0) == 0) {
    *is_save = false;
    *is_vmx = true;
    suffix = std::string_view(name).substr(kRestVmxPrefix.size());
  } else {
    return false;
  }

  int parsed = 0;
  auto result =
      std::from_chars(suffix.data(), suffix.data() + suffix.size(), parsed);
  if (result.ec != std::errc() || result.ptr != suffix.data() + suffix.size()) {
    return false;
  }
  if (*is_vmx) {
    if ((parsed < 14 || parsed > 31) && (parsed < 64 || parsed > 127)) {
      return false;
    }
    *last_reg = parsed < 32 ? 31 : 127;
  } else {
    if (parsed < 14 || parsed > 31) {
      return false;
    }
    *last_reg = 31;
  }
  *first_reg = parsed;
  return true;
}

bool DecodePpcLwz(uint32_t instr, uint32_t* dest_reg, uint32_t* base_reg,
                  int32_t* offset) {
  if (!dest_reg || !base_reg || !offset || (instr >> 26) != 32) {
    return false;
  }
  *dest_reg = (instr >> 21) & 0x1F;
  *base_reg = (instr >> 16) & 0x1F;
  *offset = static_cast<int16_t>(instr & 0xFFFF);
  return true;
}

bool IsScaledU32LoadOffset(int32_t offset) {
  return offset >= 0 && offset <= 16380 && (offset & 3) == 0;
}

bool ParsePpcThreadFieldLeafHelper(xe::Memory* memory,
                                   const xe::cpu::GuestFunction* function,
                                   int32_t* thread_offset,
                                   int32_t* field_offset) {
  if (!memory || !function || !thread_offset || !field_offset ||
      function->behavior() != xe::cpu::Function::Behavior::kDefault) {
    return false;
  }

  const uint32_t address = function->address();
  const uint8_t* code = memory->TranslateVirtual<const uint8_t*>(address);
  const uint32_t instr0 = xe::load_and_swap<uint32_t>(code + 0);
  const uint32_t instr1 = xe::load_and_swap<uint32_t>(code + 4);
  const uint32_t instr2 = xe::load_and_swap<uint32_t>(code + 8);

  uint32_t dest0 = 0;
  uint32_t base0 = 0;
  int32_t offset0 = 0;
  uint32_t dest1 = 0;
  uint32_t base1 = 0;
  int32_t offset1 = 0;
  if (!DecodePpcLwz(instr0, &dest0, &base0, &offset0) ||
      !DecodePpcLwz(instr1, &dest1, &base1, &offset1) ||
      instr2 != 0x4E800020 || dest0 != 11 || base0 != 13 || dest1 != 3 ||
      base1 != 11 || !IsScaledU32LoadOffset(offset0) ||
      !IsScaledU32LoadOffset(offset1)) {
    return false;
  }

  *thread_offset = offset0;
  *field_offset = offset1;
  return true;
}
}  // namespace

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using namespace Xbyak_aarch64;

// Defined in a64_backend.cc.
extern uint64_t ResolveFunction(void* raw_context, uint64_t target_address);

static uint64_t UndefinedCallExtern(void* raw_context, uint64_t function_ptr) {
  auto function = reinterpret_cast<Function*>(function_ptr);
  XELOGE("undefined extern call to {:08X} {}", function->address(),
         function->name());
  return 0;
}

static uint64_t TrapDebugPrint(void* raw_context, uint64_t address) {
  (void)address;
  auto context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  if (!context || !context->virtual_membase) {
    return 0;
  }

  uint32_t str_ptr = static_cast<uint32_t>(context->r[3]);
  auto str =
      reinterpret_cast<const char*>(context->virtual_membase + str_ptr);
  XELOGD("(DebugPrint) {}", str ? str : "");
  return 0;
}

static uint64_t TrapDebugBreak(void* raw_context, uint64_t address) {
  (void)address;
  auto context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  uint32_t thread_id = context ? context->thread_id : 0;
  XELOGE("tw/td forced trap hit on A64 thid {:08X}", thread_id);
  if (cvars::break_on_debugbreak) {
    xe::debugging::Break();
  }
  return 0;
}

static constexpr size_t kMaxCodeSize = 1_MiB;

// Register maps:
// GPR allocatable registers: x22, x23, x24, x25, x26, x27, x28
// (x19=backend context, x20=context, x21=membase are reserved)
const uint32_t A64Emitter::gpr_reg_map_[GPR_COUNT] = {
    22, 23, 24, 25, 26, 27, 28,
};

// VEC allocatable registers: v4-v15, v16-v31
// (v0-v3 are scratch)
const uint32_t A64Emitter::vec_reg_map_[VEC_COUNT] = {
    4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
};

A64Emitter::A64Emitter(A64Backend* backend, XbyakA64Allocator* allocator)
    : CodeGenerator(kMaxCodeSize, Xbyak_aarch64::DontSetProtectRWE, allocator),
      processor_(backend->processor()),
      backend_(backend),
      code_cache_(backend->code_cache()),
      allocator_(allocator),
      feature_flags_(arm64::GetFeatureFlags()) {}

A64Emitter::~A64Emitter() = default;

bool A64Emitter::Emit(GuestFunction* function, hir::HIRBuilder* builder,
                      uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
                      void** out_code_address, size_t* out_code_size,
                      std::vector<SourceMapEntry>* out_source_map) {
  SCOPE_profile_cpu_f("cpu");

  guest_module_ = dynamic_cast<XexModule*>(function->module());

  debug_info_ = debug_info;
  debug_info_flags_ = debug_info_flags;
  trace_data_ = &function->trace_data();

  current_guest_function_ = function->address();
  auto a64_function = static_cast<A64Function*>(function);
  current_a64_function_ = nullptr;
  current_guest_function_entry_count_ = a64_function->profile_entry_count();
  current_guest_function_body_ticks_ =
      backend_->BodyTimeProfileEnabledForFunction(a64_function)
          ? a64_function->profile_body_ticks()
          : nullptr;
  const bool current_guest_function_entry_exit_profile =
      backend_->EntryExitTimeProfileEnabledForFunction(a64_function);
  current_guest_function_prolog_ticks_ =
      current_guest_function_entry_exit_profile
          ? a64_function->profile_prolog_ticks()
          : nullptr;
  current_guest_function_epilog_ticks_ =
      current_guest_function_entry_exit_profile
          ? a64_function->profile_epilog_ticks()
          : nullptr;
  current_guest_function_block_body_ticks_ =
      backend_->BlockBodyTimeProfileEnabledForFunction(a64_function);
  current_guest_function_call_edge_profile_ =
      backend_->CallEdgeProfileEnabledForFunction(a64_function);
  const bool current_guest_function_call_edge_audit_only =
      backend_->CallEdgeAuditOnlyEnabledForFunction(a64_function);
  if (backend_->BlockProfileEnabledForFunction(a64_function) ||
      current_guest_function_block_body_ticks_) {
    size_t block_count = 0;
    for (auto block = builder->first_block(); block; block = block->next) {
      if (block->ordinal == UINT16_MAX) {
        continue;
      }
      block_count =
          std::max(block_count, static_cast<size_t>(block->ordinal) + 1);
    }
    a64_function->SetupProfileBlockCounts(block_count);
    current_a64_function_ = a64_function;
  }
  if (current_guest_function_call_edge_profile_ ||
      current_guest_function_call_edge_audit_only) {
    size_t call_edge_count = 0;
    size_t block_count = 0;
    for (auto block = builder->first_block(); block; block = block->next) {
      if (block->ordinal != UINT16_MAX) {
        block_count =
            std::max(block_count, static_cast<size_t>(block->ordinal) + 1);
      }
      for (auto instr = block->instr_head; instr; instr = instr->next) {
        switch (instr->GetOpcodeNum()) {
          case hir::OPCODE_CALL:
          case hir::OPCODE_CALL_TRUE:
            ++call_edge_count;
            break;
          default:
            break;
        }
      }
    }
    if (current_guest_function_call_edge_audit_only) {
      std::string name = function->name();
      if (name.empty()) {
        name = fmt::format("sub_{:08X}", function->address());
      }
      XELOGW(
          "A64 call-edge compile audit: fn {:08X} '{}' blocks={} "
          "direct_call_edges={} instrumentation=0",
          function->address(), name, block_count, call_edge_count);
    }
    if (current_guest_function_call_edge_profile_) {
      a64_function->SetupProfileCallEdges(call_edge_count);
      current_a64_function_ = a64_function;
    }
  }
  MaybeLogContextTrafficAudit(builder);

  // Reset state.
  stack_size_ = StackLayout::GUEST_STACK_SIZE;
  body_time_start_stack_offset_ = 0;
  entry_exit_time_start_stack_offset_ = 0;
  block_body_time_start_stack_offset_ = 0;
  block_body_time_counter_stack_offset_ = 0;
  call_edge_time_start_stack_offset_ = 0;
  current_call_edge_ordinal_ = 0;
  current_block_guest_address_ = 0;
  source_map_arena_.Reset();
  tail_code_.clear();
  fpcr_mode_ = FPCRMode::Unknown;

  // Try to emit.
  EmitFunctionInfo func_info = {};
  if (!Emit(builder, func_info)) {
    return false;
  }

  // Emplace the code into the code cache.
  *out_code_address = Emplace(func_info, function);
  *out_code_size = func_info.code_size.total;

  // Copy source map.
  source_map_arena_.CloneContents(out_source_map);

  return *out_code_address != nullptr;
}

void A64Emitter::EmitAtomicIncrement64(std::atomic<uint64_t>* counter) {
  if (!counter) {
    return;
  }

  mov(x9, reinterpret_cast<uint64_t>(counter));
  auto& retry = NewCachedLabel();
  L(retry);
  ldxr(x10, ptr(x9));
  add(x10, x10, uint32_t{1});
  stxr(w11, x10, ptr(x9));
  cbnz(w11, retry);
}

void A64Emitter::EmitAtomicAdd64(std::atomic<uint64_t>* counter,
                                 const Xbyak_aarch64::XReg& value_reg) {
  if (!counter) {
    return;
  }

  mov(x12, reinterpret_cast<uint64_t>(counter));
  auto& retry = NewCachedLabel();
  L(retry);
  ldxr(x13, ptr(x12));
  add(x13, x13, value_reg);
  stxr(w14, x13, ptr(x12));
  cbnz(w14, retry);
}

void A64Emitter::MaybeEmitCallEdgeProfileStart(
    std::atomic<uint64_t>* entry_counter) {
  if (!entry_counter) {
    return;
  }

  EmitAtomicIncrement64(entry_counter);
  if (cvars::arm64_speed_profile_body_time_after_ms != 0) {
    str(xzr, ptr(sp, static_cast<uint32_t>(
                         call_edge_time_start_stack_offset_)));
    auto& inactive = NewCachedLabel();
    mov(x12, reinterpret_cast<uint64_t>(
                 backend_->speed_profile_body_time_active()));
    ldr(w11, ptr(x12));
    cbz(w11, inactive);
    mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
    str(x17, ptr(sp, static_cast<uint32_t>(
                         call_edge_time_start_stack_offset_)));
    L(inactive);
    return;
  }

  mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
  str(x17,
      ptr(sp, static_cast<uint32_t>(call_edge_time_start_stack_offset_)));
}

void A64Emitter::MaybeEmitCallEdgeProfileEnd(
    std::atomic<uint64_t>* body_ticks_counter) {
  if (!body_ticks_counter) {
    return;
  }

  if (cvars::arm64_speed_profile_body_time_after_ms != 0) {
    auto& inactive = NewCachedLabel();
    ldr(x11, ptr(sp, static_cast<uint32_t>(
                         call_edge_time_start_stack_offset_)));
    cbz(x11, inactive);
    mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
    sub(x17, x17, x11);
    EmitAtomicAdd64(body_ticks_counter, x17);
    L(inactive);
    return;
  }

  mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
  ldr(x11,
      ptr(sp, static_cast<uint32_t>(call_edge_time_start_stack_offset_)));
  sub(x17, x17, x11);
  EmitAtomicAdd64(body_ticks_counter, x17);
}

void A64Emitter::MaybeEmitBlockBodyTimeProfileInit() {
  if (!current_guest_function_block_body_ticks_) {
    return;
  }
  str(xzr,
      ptr(sp, static_cast<uint32_t>(block_body_time_start_stack_offset_)));
  str(xzr,
      ptr(sp, static_cast<uint32_t>(block_body_time_counter_stack_offset_)));
}

void A64Emitter::MaybeEmitBlockBodyTimeEnd() {
  if (!current_guest_function_block_body_ticks_) {
    return;
  }

  auto& no_active_block = NewCachedLabel();
  ldr(x10,
      ptr(sp, static_cast<uint32_t>(block_body_time_counter_stack_offset_)));
  cbz(x10, no_active_block);
  ldr(x11, ptr(sp, static_cast<uint32_t>(block_body_time_start_stack_offset_)));
  cbz(x11, no_active_block);
  mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
  sub(x17, x17, x11);
  auto& retry = NewCachedLabel();
  L(retry);
  ldxr(x12, ptr(x10));
  add(x12, x12, x17);
  stxr(w13, x12, ptr(x10));
  cbnz(w13, retry);
  str(xzr,
      ptr(sp, static_cast<uint32_t>(block_body_time_start_stack_offset_)));
  str(xzr,
      ptr(sp, static_cast<uint32_t>(block_body_time_counter_stack_offset_)));
  L(no_active_block);
}

void A64Emitter::MaybeEmitBlockBodyTimeTransition(const hir::Block* block) {
  if (!current_guest_function_block_body_ticks_ || !current_a64_function_ ||
      !block || block->ordinal == UINT16_MAX) {
    return;
  }

  auto* counter = current_a64_function_->profile_block_body_ticks(
      static_cast<size_t>(block->ordinal));
  if (!counter) {
    return;
  }

  MaybeEmitBlockBodyTimeEnd();
  if (cvars::arm64_speed_profile_body_time_after_ms != 0) {
    auto& inactive = NewCachedLabel();
    mov(x12, reinterpret_cast<uint64_t>(
                 backend_->speed_profile_body_time_active()));
    ldr(w11, ptr(x12));
    cbz(w11, inactive);
    mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
    str(x17,
        ptr(sp, static_cast<uint32_t>(block_body_time_start_stack_offset_)));
    mov(x17, reinterpret_cast<uint64_t>(counter));
    str(x17,
        ptr(sp, static_cast<uint32_t>(block_body_time_counter_stack_offset_)));
    L(inactive);
    return;
  }

  mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
  str(x17,
      ptr(sp, static_cast<uint32_t>(block_body_time_start_stack_offset_)));
  mov(x17, reinterpret_cast<uint64_t>(counter));
  str(x17,
      ptr(sp, static_cast<uint32_t>(block_body_time_counter_stack_offset_)));
}

void A64Emitter::MaybeEmitBodyTimeProfileStart() {
  if (!current_guest_function_body_ticks_) {
    return;
  }

  if (cvars::arm64_speed_profile_body_time_after_ms != 0) {
    str(xzr, ptr(sp, static_cast<uint32_t>(body_time_start_stack_offset_)));
    auto& inactive = NewCachedLabel();
    mov(x12, reinterpret_cast<uint64_t>(
                 backend_->speed_profile_body_time_active()));
    ldr(w11, ptr(x12));
    cbz(w11, inactive);
    mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
    str(x17, ptr(sp, static_cast<uint32_t>(body_time_start_stack_offset_)));
    L(inactive);
    return;
  }

  mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
  str(x17, ptr(sp, static_cast<uint32_t>(body_time_start_stack_offset_)));
}

void A64Emitter::MaybeEmitBodyTimeProfileEnd() {
  MaybeEmitBlockBodyTimeEnd();
  if (!current_guest_function_body_ticks_) {
    return;
  }

  if (cvars::arm64_speed_profile_body_time_after_ms != 0) {
    auto& inactive = NewCachedLabel();
    ldr(x11, ptr(sp, static_cast<uint32_t>(body_time_start_stack_offset_)));
    cbz(x11, inactive);
    mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
    sub(x17, x17, x11);
    EmitAtomicAdd64(current_guest_function_body_ticks_, x17);
    L(inactive);
    return;
  }

  mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
  ldr(x11, ptr(sp, static_cast<uint32_t>(body_time_start_stack_offset_)));
  sub(x17, x17, x11);
  EmitAtomicAdd64(current_guest_function_body_ticks_, x17);
}

void A64Emitter::MaybeEmitEntryExitTimeProfileStartInX15() {
  if (!current_guest_function_prolog_ticks_ &&
      !current_guest_function_epilog_ticks_) {
    return;
  }

  if (cvars::arm64_speed_profile_body_time_after_ms != 0) {
    auto& inactive = NewCachedLabel();
    auto& done = NewCachedLabel();
    mov(x12,
        reinterpret_cast<uint64_t>(backend_->speed_profile_body_time_active()));
    ldr(w11, ptr(x12));
    cbz(w11, inactive);
    mrs(x15, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
    b(done);
    L(inactive);
    mov(x15, 0);
    L(done);
    return;
  }

  mrs(x15, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
}

void A64Emitter::MaybeEmitEntryExitTimeProfileStoreStartFromX15() {
  if (!current_guest_function_prolog_ticks_ &&
      !current_guest_function_epilog_ticks_) {
    return;
  }
  str(x15,
      ptr(sp, static_cast<uint32_t>(entry_exit_time_start_stack_offset_)));
}

void A64Emitter::MaybeEmitEntryExitTimeProfilePrologEnd() {
  if (!current_guest_function_prolog_ticks_) {
    return;
  }

  auto& inactive = NewCachedLabel();
  ldr(x11,
      ptr(sp, static_cast<uint32_t>(entry_exit_time_start_stack_offset_)));
  cbz(x11, inactive);
  mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
  sub(x17, x17, x11);
  EmitAtomicAdd64(current_guest_function_prolog_ticks_, x17);
  L(inactive);
}

void A64Emitter::MaybeEmitEntryExitTimeProfileEpilogEndFromX15() {
  if (!current_guest_function_epilog_ticks_) {
    return;
  }

  auto& inactive = NewCachedLabel();
  cbz(x15, inactive);
  mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
  sub(x17, x17, x15);
  EmitAtomicAdd64(current_guest_function_epilog_ticks_, x17);
  L(inactive);
}

uint32_t A64Emitter::FindBlockGuestAddress(const hir::Block* block) const {
  if (!block) {
    return 0;
  }
  for (const hir::Instr* instr = block->instr_head; instr; instr = instr->next) {
    if (instr->GetOpcodeNum() == hir::OPCODE_SOURCE_OFFSET) {
      return static_cast<uint32_t>(instr->src1.offset);
    }
    uint32_t address = instr->GuestAddressFor();
    if (address) {
      return address;
    }
  }
  return 0;
}

void A64Emitter::MaybeEmitBlockProfileEntry(const hir::Block* block) {
  if (!current_a64_function_ || !block || block->ordinal == UINT16_MAX) {
    return;
  }
  auto* counter =
      current_a64_function_->profile_block_count(static_cast<size_t>(
          block->ordinal));
  if (!counter) {
    return;
  }
  current_a64_function_->set_profile_block_address(
      static_cast<size_t>(block->ordinal), FindBlockGuestAddress(block));
  EmitAtomicIncrement64(counter);
}

void A64Emitter::MaybeLogContextTrafficAudit(hir::HIRBuilder* builder) {
  if (!cvars::arm64_context_traffic_audit || !builder) {
    return;
  }
  uint32_t function_filter = cvars::arm64_context_traffic_audit_function;
  if (function_filter && function_filter != current_guest_function_) {
    return;
  }

  uint32_t log_index = 0;
  if (!ConsumeContextTrafficAuditBudget(&log_index)) {
    return;
  }

  A64ContextTrafficStats stats;
  for (auto block = builder->first_block(); block; block = block->next) {
    ++stats.blocks;
    for (auto instr = block->instr_head; instr; instr = instr->next) {
      ++stats.instrs;
      auto opcode = instr->GetOpcodeNum();

      if (IsIntegerCompareOpcode(opcode)) {
        ++stats.integer_compares;
      } else if (IsVectorCompareOpcode(opcode)) {
        ++stats.vector_compares;
      }

      switch (opcode) {
        case hir::OPCODE_LOAD_CONTEXT: {
          size_t size = instr->dest ? hir::GetTypeSize(instr->dest->type) : 1;
          AddContextTrafficAccess(&stats, static_cast<uint32_t>(
                                              instr->src1.offset),
                                  size, false);
          break;
        }
        case hir::OPCODE_STORE_CONTEXT: {
          auto* value = instr->src2.value;
          uint32_t offset = static_cast<uint32_t>(instr->src1.offset);
          size_t size = value ? hir::GetTypeSize(value->type) : 1;
          AddContextTrafficAccess(&stats, offset, size, true);
          if (value) {
            if (value->IsConstant()) {
              ++stats.context_store_constant_sources;
              if (IsCrContextOffset(offset)) {
                ++stats.cr_store_constant_sources;
              }
            }
            if (value->def) {
              auto source_opcode = value->def->GetOpcodeNum();
              if (IsCrContextOffset(offset)) {
                ++stats.cr_store_source_offsets
                    [MakeContextTrafficOffsetOpcodeKey(offset, source_opcode)];
              }
              if (IsIntegerCompareOpcode(source_opcode) ||
                  IsVectorCompareOpcode(source_opcode)) {
                ++stats.context_store_compare_sources;
                if (IsCrContextOffset(offset)) {
                  ++stats.cr_store_compare_sources;
                }
              } else if (source_opcode == hir::OPCODE_SELECT) {
                ++stats.context_store_select_sources;
                if (IsCrContextOffset(offset)) {
                  ++stats.cr_store_select_sources;
                }
              } else if (source_opcode == hir::OPCODE_IS_FALSE) {
                ++stats.context_store_is_false_sources;
                if (IsCrContextOffset(offset)) {
                  ++stats.cr_store_is_false_sources;
                }
              }
            }
          }
          break;
        }
        case hir::OPCODE_LOAD_LOCAL:
          ++stats.local_loads;
          break;
        case hir::OPCODE_STORE_LOCAL:
          ++stats.local_stores;
          break;
        case hir::OPCODE_LOAD_MMIO:
        case hir::OPCODE_LOAD_OFFSET:
        case hir::OPCODE_LOAD:
        case hir::OPCODE_LVL:
        case hir::OPCODE_LVR:
          ++stats.memory_loads;
          break;
        case hir::OPCODE_STORE_MMIO:
        case hir::OPCODE_STORE_OFFSET:
        case hir::OPCODE_STORE:
        case hir::OPCODE_MEMSET:
        case hir::OPCODE_STVL:
        case hir::OPCODE_STVR:
          ++stats.memory_stores;
          break;
        case hir::OPCODE_ATOMIC_EXCHANGE:
        case hir::OPCODE_ATOMIC_COMPARE_EXCHANGE:
          ++stats.atomics;
          ++stats.memory_loads;
          ++stats.memory_stores;
          break;
        case hir::OPCODE_RESERVED_LOAD:
          ++stats.reservation_ops;
          ++stats.memory_loads;
          break;
        case hir::OPCODE_RESERVED_STORE:
          ++stats.reservation_ops;
          ++stats.memory_loads;
          ++stats.memory_stores;
          break;
        case hir::OPCODE_CONTEXT_BARRIER:
          ++stats.context_barriers;
          break;
        case hir::OPCODE_MEMORY_BARRIER:
          ++stats.memory_barriers;
          break;
        case hir::OPCODE_BYTE_SWAP:
          ++stats.byte_swaps;
          break;
        case hir::OPCODE_BRANCH:
        case hir::OPCODE_BRANCH_TRUE:
        case hir::OPCODE_BRANCH_FALSE:
          ++stats.branches;
          break;
        case hir::OPCODE_CALL:
        case hir::OPCODE_CALL_TRUE:
        case hir::OPCODE_CALL_INDIRECT:
        case hir::OPCODE_CALL_INDIRECT_TRUE:
        case hir::OPCODE_CALL_EXTERN:
          ++stats.calls;
          break;
        case hir::OPCODE_RETURN:
        case hir::OPCODE_RETURN_TRUE:
          ++stats.returns;
          break;
        default:
          break;
      }

      bool strict = false;
      bool is_signed = false;
      if (AuditCrTripletShape(instr, &strict, &is_signed)) {
        ++stats.cr_update_triplets;
        if (strict) {
          ++stats.cr_update_triplets_strict;
        }
        if (is_signed) {
          ++stats.cr_update_triplets_signed;
        } else {
          ++stats.cr_update_triplets_unsigned;
        }
      }
      if (AuditCrGtEqPairShape(instr, &strict)) {
        ++stats.cr_gt_eq_pairs;
        if (strict) {
          ++stats.cr_gt_eq_pairs_strict;
        }
      }
      if (AuditCr6UpdateShape(instr, &strict)) {
        ++stats.cr6_update_shapes;
        if (strict) {
          ++stats.cr6_update_shapes_strict;
        }
      }
    }
  }

  XELOGW(
      "A64 context traffic audit {:03}: fn {:08X} blocks={} instrs={} "
      "context_loads={} context_stores={} locals={}/{} mem={}/{} "
      "byte_swaps={} cmp={}/{} branches={} calls={} returns={}",
      log_index, current_guest_function_, stats.blocks, stats.instrs,
      stats.context_loads, stats.context_stores, stats.local_loads,
      stats.local_stores, stats.memory_loads, stats.memory_stores,
      stats.byte_swaps, stats.integer_compares, stats.vector_compares,
      stats.branches, stats.calls, stats.returns);
  XELOGW(
      "A64 context traffic audit {:03}: fn {:08X} ppc_loads "
      "lr/ctr/gpr/cr/xer/fpr/vmx/runtime={}/{}/{}/{}/{}/{}/{}/{} "
      "ppc_stores={}/{}/{}/{}/{}/{}/{}/{} "
      "store_src cmp/const/select/is_false={}/{}/{}/{} "
      "barriers ctx/mem={}/{} atomics/resv={}/{}",
      log_index, current_guest_function_, stats.lr_loads, stats.ctr_loads,
      stats.gpr_loads, stats.cr_loads, stats.xer_loads, stats.fpr_loads,
      stats.vmx_loads, stats.runtime_loads, stats.lr_stores, stats.ctr_stores,
      stats.gpr_stores, stats.cr_stores, stats.xer_stores, stats.fpr_stores,
      stats.vmx_stores, stats.runtime_stores,
      stats.context_store_compare_sources,
      stats.context_store_constant_sources, stats.context_store_select_sources,
      stats.context_store_is_false_sources,
      stats.context_barriers, stats.memory_barriers, stats.atomics,
      stats.reservation_ops);
  XELOGW("A64 context traffic audit {:03}: fn {:08X} load_top={} store_top={}",
         log_index, current_guest_function_,
         FormatContextTrafficTopOffsets(stats.context_load_offsets),
         FormatContextTrafficTopOffsets(stats.context_store_offsets));
  XELOGW(
      "A64 context traffic audit {:03}: fn {:08X} cr_store_src "
      "cmp/const/select/is_false={}/{}/{}/{} top={}",
      log_index, current_guest_function_, stats.cr_store_compare_sources,
      stats.cr_store_constant_sources, stats.cr_store_select_sources,
      stats.cr_store_is_false_sources,
      FormatContextTrafficTopOffsetOpcodes(stats.cr_store_source_offsets));
  XELOGW(
      "A64 context traffic audit {:03}: fn {:08X} cr_shapes "
      "triplet={}/{} signed/unsigned={}/{} gt_eq={}/{} cr6={}/{}",
      log_index, current_guest_function_, stats.cr_update_triplets,
      stats.cr_update_triplets_strict, stats.cr_update_triplets_signed,
      stats.cr_update_triplets_unsigned, stats.cr_gt_eq_pairs,
      stats.cr_gt_eq_pairs_strict, stats.cr6_update_shapes,
      stats.cr6_update_shapes_strict);
}

bool A64Emitter::Emit(hir::HIRBuilder* builder, EmitFunctionInfo& func_info) {
  // Calculate local variable stack offsets.
  auto locals = builder->locals();
  size_t stack_offset = StackLayout::GUEST_STACK_SIZE;
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    auto slot = *it;
    size_t type_size = hir::GetTypeSize(slot->type);
    // Align to natural size (at least 4 bytes for ARM64 alignment).
    size_t align_size = xe::round_up(type_size, static_cast<size_t>(4));
    stack_offset = xe::align(stack_offset, align_size);
    slot->set_constant(static_cast<uint32_t>(stack_offset));
    stack_offset += type_size;
  }
  if (current_guest_function_body_ticks_) {
    stack_offset = xe::align(stack_offset, static_cast<size_t>(8));
    body_time_start_stack_offset_ = stack_offset;
    stack_offset += sizeof(uint64_t);
  }
  if (current_guest_function_prolog_ticks_ ||
      current_guest_function_epilog_ticks_) {
    stack_offset = xe::align(stack_offset, static_cast<size_t>(8));
    entry_exit_time_start_stack_offset_ = stack_offset;
    stack_offset += sizeof(uint64_t);
  }
  if (current_guest_function_block_body_ticks_) {
    stack_offset = xe::align(stack_offset, static_cast<size_t>(8));
    block_body_time_start_stack_offset_ = stack_offset;
    stack_offset += sizeof(uint64_t);
    block_body_time_counter_stack_offset_ = stack_offset;
    stack_offset += sizeof(uint64_t);
  }
  if (current_guest_function_call_edge_profile_) {
    stack_offset = xe::align(stack_offset, static_cast<size_t>(8));
    call_edge_time_start_stack_offset_ = stack_offset;
    stack_offset += sizeof(uint64_t);
  }
  // Align total stack offset to 16 bytes (ARM64 ABI requirement).
  stack_offset -= StackLayout::GUEST_STACK_SIZE;
  stack_offset = xe::align(stack_offset, static_cast<size_t>(16));

  const size_t stack_size = StackLayout::GUEST_STACK_SIZE + stack_offset;
  // ARM64 ABI: SP must always be 16-byte aligned.
  assert_true(stack_size % 16 == 0);
  func_info.stack_size = stack_size;
  func_info.lr_save_offset = StackLayout::HOST_RET_ADDR;
  stack_size_ = stack_size;

  struct {
    size_t prolog;
    size_t body;
    size_t epilog;
    size_t tail;
    size_t prolog_stack_alloc;
  } code_offsets = {};

  // ========================================================================
  // PROLOG
  // ========================================================================
  code_offsets.prolog = getSize();

  MaybeEmitEntryExitTimeProfileStartInX15();

  // sub sp, sp, #stack_size
  if (stack_size <= 4095) {
    sub(sp, sp, static_cast<uint32_t>(stack_size));
  } else {
    mov(x17, static_cast<uint64_t>(stack_size));
    sub(sp, sp, x17, UXTX);
  }
  code_offsets.prolog_stack_alloc = getSize();
  MaybeEmitEntryExitTimeProfileStoreStartFromX15();

  // Store host return address (x30/LR) so the epilog can restore it.
  str(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
  // Store guest PPC return address (passed in x0 by convention).
  str(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
  // Store zero for call return address (we haven't made a call yet).
  str(xzr, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));

  // Record stackpoint for longjmp recovery, then save the resulting depth
  // for post-call detection (if depth changes, a longjmp skipped frames).
  PushStackpoint();
  if (cvars::a64_enable_host_guest_stack_synchronization) {
    ldr(w16, ptr(x19, static_cast<uint32_t>(offsetof(
                          A64BackendContext, current_stackpoint_depth))));
    str(w16, ptr(sp, static_cast<uint32_t>(
                         StackLayout::GUEST_SAVED_STACKPOINT_DEPTH)));
  }
  if (A64CallTraceRequested()) {
    mov(x1, static_cast<uint64_t>(current_guest_function_));
    CallNativeSafe(reinterpret_cast<void*>(&TraceFunctionEntry));
  }
  if (backend_->speed_profile_enabled()) {
    EmitAtomicIncrement64(current_guest_function_entry_count_);
    mov(w16, current_guest_function_);
    str(w16, ptr(x19, static_cast<uint32_t>(
                          offsetof(A64BackendContext, last_guest_function))));
    str(w0, ptr(x19, static_cast<uint32_t>(offsetof(
                          A64BackendContext, last_guest_return_address))));
  }
  MaybeEmitEntryExitTimeProfilePrologEnd();
  MaybeEmitBodyTimeProfileStart();
  MaybeEmitBlockBodyTimeProfileInit();
  MaybeEmitBlueDragonDrawWaitCallerProfile();

  // ========================================================================
  // BODY
  // ========================================================================
  code_offsets.body = getSize();

  // Allocate the epilog label (owned by label_cache_ for cleanup).
  auto epilog_label_ptr = new Label();
  label_cache_.push_back(epilog_label_ptr);
  epilog_label_ = epilog_label_ptr;

  if (TryEmitBlueDragonDrawWaitFunctionBody()) {
    b(*epilog_label_);
  } else if (TryEmitBlueDragonMemcpyFunctionBody()) {
    b(*epilog_label_);
  } else if (TryEmitBlueDragonStricmpFunctionBody()) {
    b(*epilog_label_);
  } else if (TryEmitBlueDragonJumpTableFunctionBody()) {
    b(*epilog_label_);
  } else {
  // Walk HIR blocks and emit ARM64 instructions.
  auto block = builder->first_block();
  synchronize_stack_on_next_instruction_ = false;
  A64ContextValueCache context_value_cache(
      cvars::arm64_context_value_cache,
      cvars::arm64_context_value_cache_fallthrough,
      cvars::arm64_context_value_cache_preserve_barrier,
      cvars::arm64_context_pinned_gpr_r1 &&
          (!cvars::arm64_context_pinned_gpr_r1_function ||
           cvars::arm64_context_pinned_gpr_r1_function ==
               current_guest_function_),
      cvars::arm64_context_pinned_gpr_r1_fallthrough);
  while (block) {
    current_block_guest_address_ = FindBlockGuestAddress(block);
    // Reset FPCR tracking on each block entry (we don't know which
    // predecessor ran, so mode is unknown).
    ForgetFpcrMode();
    context_value_cache.ResetBlock(block);

    // Bind all labels targeting this block.
    auto label = block->label_head;
    while (label) {
      L(GetLabel(label->id));
      label = label->next;
    }
    MaybeEmitBlockBodyTimeTransition(block);
    MaybeEmitBlockProfileEntry(block);
    if (TryEmitBlueDragonVmxCopyLoopBlock(block) ||
        TryEmitBlueDragonWordCopyLoopBlock(block)) {
      block = block->next;
      continue;
    }

    // Process each instruction in the block.
    const hir::Instr* instr = block->instr_head;
    while (instr) {
      // After a guest call, check for longjmp on the next real instruction.
      // Skip SOURCE_OFFSET because the return address from the call would
      // point past the check, so it would never execute.
      if (synchronize_stack_on_next_instruction_) {
        if (instr->GetOpcodeNum() != hir::OPCODE_SOURCE_OFFSET) {
          synchronize_stack_on_next_instruction_ = false;
          EnsureSynchronizedGuestAndHostStack();
        }
      }
      if (context_value_cache.TryEmitLoad(*this, instr)) {
        instr = instr->next;
        continue;
      }
      const hir::Instr* new_tail = instr;
      if (!SelectSequence(this, instr, &new_tail)) {
        // No sequence matched — this is expected in Phase 1 before
        // sequences are implemented.
        XELOGE("A64: Unable to process HIR opcode {}",
               hir::GetOpcodeName(instr->GetOpcodeInfo()));
        return false;
      }
      context_value_cache.ObservePostEmit(instr, new_tail, *this);
      instr = new_tail;
    }

    block = block->next;
  }
  const auto& cache_stats = context_value_cache.stats();
  if (cvars::arm64_context_traffic_audit &&
      (!cvars::arm64_context_traffic_audit_function ||
       cvars::arm64_context_traffic_audit_function ==
           current_guest_function_) &&
      cache_stats.eligible_loads) {
    XELOGW(
        "A64 context value cache: fn {:08X} loads/hits={}/{} "
        "stores/cached={}/{} invalid offset/reg={}/{} "
        "resets safety/block/barrier={}/{}/{} barrier_preserves={} "
        "fallthrough_preserves={}",
        current_guest_function_, cache_stats.eligible_loads,
        cache_stats.load_hits, cache_stats.eligible_stores,
        cache_stats.store_caches, cache_stats.offset_invalidations,
        cache_stats.register_invalidations, cache_stats.safety_resets,
        cache_stats.block_resets, cache_stats.barrier_resets,
        cache_stats.barrier_preserves, cache_stats.fallthrough_preserves);
    XELOGW("A64 context value cache slots: fn {:08X} load_hits={} stores={}",
           current_guest_function_,
           FormatGprSlotCounts(cache_stats.load_hits_by_slot),
           FormatGprSlotCounts(cache_stats.store_caches_by_slot));
  }
  if (cvars::arm64_context_pinned_gpr_r1_audit &&
      cvars::arm64_context_pinned_gpr_r1 &&
      (!cvars::arm64_context_pinned_gpr_r1_function ||
       cvars::arm64_context_pinned_gpr_r1_function ==
           current_guest_function_) &&
      cache_stats.pinned_r1_loads) {
    XELOGW(
        "A64 pinned r1 cache: fn {:08X} loads/hits={}/{} pin_loads={} "
        "pin_stores={} resets block/branch/volatile/overlap={}/{}/{}/{} "
        "barrier_preserves={} fallthrough_preserves={}",
        current_guest_function_, cache_stats.pinned_r1_loads,
        cache_stats.pinned_r1_hits, cache_stats.pinned_r1_pin_loads,
        cache_stats.pinned_r1_pin_stores, cache_stats.pinned_r1_block_resets,
        cache_stats.pinned_r1_branch_resets,
        cache_stats.pinned_r1_volatile_resets,
        cache_stats.pinned_r1_overlap_resets,
        cache_stats.pinned_r1_barrier_preserves,
        cache_stats.pinned_r1_fallthrough_preserves);
  }
  }

  // ========================================================================
  // EPILOG
  // ========================================================================
  L(*epilog_label_);
  MaybeEmitBlueDragonStricmpReturnProfile();
  MaybeEmitBodyTimeProfileEnd();
  epilog_label_ = nullptr;
  code_offsets.epilog = getSize();

  MaybeEmitEntryExitTimeProfileStartInX15();
  MaybeEmitEntryExitTimeProfileStoreStartFromX15();

  // Pop stackpoint before leaving.
  PopStackpoint();

  if (current_guest_function_epilog_ticks_) {
    ldr(x15,
        ptr(sp, static_cast<uint32_t>(entry_exit_time_start_stack_offset_)));
  }

  // Restore host return address and deallocate stack.
  ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
  if (stack_size <= 4095) {
    add(sp, sp, static_cast<uint32_t>(stack_size));
  } else {
    mov(x17, static_cast<uint64_t>(stack_size));
    add(sp, sp, x17, UXTX);
  }
  MaybeEmitEntryExitTimeProfileEpilogEndFromX15();
  ret();

  // ========================================================================
  // TAIL CODE
  // ========================================================================
  for (auto& tail_item : tail_code_) {
    // ARM64 instructions are always 4-byte aligned, so alignment is mostly
    // a no-op unless we want cache-line alignment for hot paths.
    L(tail_item.label);
    tail_item.func(*this, tail_item.label);
  }
  code_offsets.tail = getSize();

  // Fill in EmitFunctionInfo metrics.
  assert_zero(code_offsets.prolog);
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;

  return true;
}

void* A64Emitter::Emplace(const EmitFunctionInfo& func_info,
                          GuestFunction* function) {
  assert_true(func_info.code_size.total == getSize());

  void* new_execute_address;
  void* new_write_address;

  if (function) {
    code_cache_->PlaceGuestCode(
        function->address(),
        const_cast<void*>(static_cast<const void*>(getCode())), func_info,
        function, new_execute_address, new_write_address);
  } else {
    code_cache_->PlaceHostCode(
        0, const_cast<void*>(static_cast<const void*>(getCode())), func_info,
        new_execute_address, new_write_address);
  }

  // In xbyak_aarch64, labels are resolved at define time (backpatching),
  // so all relative offsets are already correct. We just need to reset
  // the codegen state for the next function.
  reset();
  tail_code_.clear();
  current_a64_function_ = nullptr;

  // Clean up cached labels.
  for (auto* cached_label : label_cache_) {
    delete cached_label;
  }
  label_cache_.clear();

  // Clean up HIR->xbyak label map.
  for (auto& pair : label_map_) {
    delete pair.second;
  }
  label_map_.clear();

  return new_execute_address;
}

void A64Emitter::MarkSourceOffset(const hir::Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->guest_address = static_cast<uint32_t>(i->src1.offset);
  entry->hir_offset = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset = static_cast<uint32_t>(getSize());
}

void A64Emitter::DebugBreak() {
  if (!cvars::break_on_debugbreak) {
    return;
  }
  brk(0xF000);
}

void A64Emitter::Trap(uint16_t trap_type) {
  switch (trap_type) {
    case 20:
    case 26:
      // 0x0FE00014 is a debug print trap where r3 points at the buffer.
      mov(x1, uint64_t{0});
      CallNativeSafe(reinterpret_cast<void*>(&TrapDebugPrint));
      break;
    case 0:
    case 22:
      mov(x1, uint64_t{0});
      CallNativeSafe(reinterpret_cast<void*>(&TrapDebugBreak));
      break;
    case 25:
      break;
    default:
      XELOGW("A64: Unknown trap type {}", trap_type);
      DebugBreak();
      break;
  }
}

void A64Emitter::UnimplementedInstr(const hir::Instr* i) {
  XELOGE("A64: Unimplemented HIR instruction: {}",
         hir::GetOpcodeName(i->GetOpcodeInfo()));
  DebugBreak();
}

bool A64Emitter::TryEmitGprLrHelperCall(const hir::Instr* instr,
                                        GuestFunction* function) {
  if (!cvars::a64_inline_gprlr_helpers) {
    return false;
  }

  bool is_save = false;
  int first_gpr = 0;
  if (!ParseGprLrHelper(function, &is_save, &first_gpr)) {
    return false;
  }

  const bool is_tail_call = (instr->flags & hir::CALL_TAIL) != 0;
  if (is_save && is_tail_call) {
    return false;
  }
  if (!is_save && !is_tail_call) {
    return false;
  }

  ForgetFpcrMode();

  const int32_t first_stack_offset = -8 * (33 - first_gpr);
  const uint32_t base_subtract =
      static_cast<uint32_t>(-first_stack_offset);

  ldr(w9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[1]))));
  add(x9, GetMembaseReg(), x9);
  if (base_subtract) {
    sub(x9, x9, base_subtract);
  }

  for (int gpr = first_gpr; gpr <= 31; ++gpr) {
    const uint32_t slot_offset =
        static_cast<uint32_t>((-8 * (33 - gpr)) - first_stack_offset);
    const auto context_offset =
        static_cast<int32_t>(offsetof(ppc::PPCContext, r) +
                             sizeof(uint64_t) * static_cast<size_t>(gpr));
    if (is_save) {
      ldr(x10, ptr(GetContextReg(), context_offset));
      rev(x10, x10);
      str(x10, ptr(x9, slot_offset));
    } else {
      ldr(x10, ptr(x9, slot_offset));
      rev(x10, x10);
      str(x10, ptr(GetContextReg(), context_offset));
    }
  }

  const uint32_t lr_slot_offset =
      static_cast<uint32_t>(-8 - first_stack_offset);
  if (is_save) {
    ldr(w10, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[12]))));
    rev(w10, w10);
    str(w10, ptr(x9, lr_slot_offset));
  } else {
    ldr(w10, ptr(x9, lr_slot_offset));
    rev(w10, w10);
    str(x10, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[12]))));
    str(x10, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, lr))));
    mov(w16, w10);
    ldr(w11, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    cmp(w16, w11);
    auto& tail_jump = NewCachedLabel();
    b(NE, tail_jump);
    b(epilog_label());

    L(tail_jump);
    if (backend_->speed_profile_enabled()) {
      EmitAtomicIncrement64(backend_->speed_profile_indirect_guest_calls());
    }
    if (code_cache_->has_indirection_table()) {
      mov(x0, A64CodeCache::execute_address_high());
      orr(x16, x16, x0);
      ldr(w9, ptr(x16, static_cast<uint32_t>(0)));
      orr(x9, x9, x0);
    } else {
      mov(x0, x20);
      mov(x1, x16);
      mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
      blr(x9);
      mov(x9, x0);
    }

    MaybeEmitBodyTimeProfileEnd();
    PopStackpoint();
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
    if (stack_size() <= 4095) {
      add(sp, sp, static_cast<uint32_t>(stack_size()));
    } else {
      mov(x17, static_cast<uint64_t>(stack_size()));
      add(sp, sp, x17, UXTX);
    }
    br(x9);
  }

  return true;
}

bool A64Emitter::TryEmitFprVmxHelperCall(const hir::Instr* instr,
                                         GuestFunction* function) {
  if ((instr->flags & hir::CALL_TAIL) != 0) {
    return false;
  }

  bool is_save = false;
  bool is_vmx = false;
  int first_reg = 0;
  int last_reg = 0;
  if (!ParseFprVmxHelper(function, &is_save, &is_vmx, &first_reg, &last_reg)) {
    return false;
  }
  if ((is_vmx && !cvars::a64_inline_vmx_helpers) ||
      (!is_vmx && !cvars::a64_inline_fpr_helpers)) {
    return false;
  }

  ForgetFpcrMode();

  const int slot_size = is_vmx ? 16 : 8;
  const int stack_limit = is_vmx ? (last_reg + 1) : 32;
  const int32_t first_stack_offset = -slot_size * (stack_limit - first_reg);
  const uint32_t base_subtract =
      static_cast<uint32_t>(-first_stack_offset);

  ldr(w9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[12]))));
  if (base_subtract) {
    sub(w9, w9, base_subtract);
  }
  AddGuestAddressToMembase(w9, x9);
  if (is_vmx) {
    and_(x9, x9, ~0xFull);
  }

  for (int reg = first_reg; reg <= last_reg; ++reg) {
    const uint32_t slot_offset = static_cast<uint32_t>(
        (-slot_size * (stack_limit - reg)) - first_stack_offset);
    if (is_vmx) {
      const auto context_offset =
          static_cast<int32_t>(offsetof(ppc::PPCContext, v) +
                               sizeof(xe::vec128_t) *
                                   static_cast<size_t>(reg));
      if (is_save) {
        ldr(QReg(0), ptr(GetContextReg(), context_offset));
        rev32(VReg16B(0), VReg16B(0));
        str(QReg(0), ptr(x9, slot_offset));
      } else {
        ldr(QReg(0), ptr(x9, slot_offset));
        rev32(VReg16B(0), VReg16B(0));
        str(QReg(0), ptr(GetContextReg(), context_offset));
      }
    } else {
      const auto context_offset =
          static_cast<int32_t>(offsetof(ppc::PPCContext, f) +
                               sizeof(double) * static_cast<size_t>(reg));
      if (is_save) {
        ldr(x10, ptr(GetContextReg(), context_offset));
        rev(x10, x10);
        str(x10, ptr(x9, slot_offset));
      } else {
        ldr(x10, ptr(x9, slot_offset));
        rev(x10, x10);
        str(x10, ptr(GetContextReg(), context_offset));
      }
    }
  }

  if (is_vmx) {
    mov(x10, 0xFFFFFFFFFFFFFFF0ull);
    str(x10, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[11]))));
  }

  return true;
}

void A64Emitter::AddGuestAddressToMembase(Xbyak_aarch64::WReg guest_reg,
                                          Xbyak_aarch64::XReg host_reg) {
  mov(WReg(host_reg.getIdx()), guest_reg);
  if (xe::memory::allocation_granularity() > 0x1000) {
    mov(w12, 0xE0000000u);
    cmp(WReg(host_reg.getIdx()), w12);
    auto& skip_offset = NewCachedLabel();
    b(LO, skip_offset);
    add(WReg(host_reg.getIdx()), WReg(host_reg.getIdx()), 1, 12);
    L(skip_offset);
  }
  add(host_reg, GetMembaseReg(), host_reg);
}

bool A64Emitter::TryEmitPpcThreadFieldLeafHelperCall(const hir::Instr* instr,
                                                     GuestFunction* function) {
  if (!cvars::a64_inline_ppc_thread_field_leaf_helpers ||
      (instr->flags & hir::CALL_TAIL) != 0) {
    return false;
  }

  int32_t thread_offset = 0;
  int32_t field_offset = 0;
  if (!ParsePpcThreadFieldLeafHelper(processor_->memory(), function,
                                     &thread_offset, &field_offset)) {
    return false;
  }

  ForgetFpcrMode();

  // Inline:
  //   lwz r11, D(r13)
  //   lwz r3, D(r11)
  //   blr
  //
  // Store r11 as well as r3 so the volatile-register side effect matches the
  // guest helper for debug traces and any non-ABI caller oddities.
  ldr(w9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[13]))));
  AddGuestAddressToMembase(w9, x9);
  ldr(w10, ptr(x9, static_cast<uint32_t>(thread_offset)));
  rev(w10, w10);
  str(x10, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[11]))));

  AddGuestAddressToMembase(w10, x10);
  ldr(w11, ptr(x10, static_cast<uint32_t>(field_offset)));
  rev(w11, w11);
  str(x11, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));

  return true;
}

void A64Emitter::EmitKernelExternHostCall(const GuestFunction* function) {
  mov(x0, reinterpret_cast<uint64_t>(function->extern_handler()));
  ldr(x1, ptr(GetContextReg(), static_cast<int32_t>(offsetof(
                                   ppc::PPCContext, kernel_state))));
  mov(x9, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
  blr(x9);
}

bool A64Emitter::TryEmitKernelHighFrequencyExternCall(
    const hir::Instr* instr, const GuestFunction* function) {
  (void)instr;
  if (!cvars::a64_inline_kernel_high_frequency_exports || !function ||
      function->behavior() != Function::Behavior::kExtern) {
    return false;
  }

  const cpu::Export* export_data = function->export_data();
  if (!export_data ||
      !(export_data->tags & cpu::ExportTag::kHighFrequency)) {
    return false;
  }

  const std::string_view name(export_data->name);
  const int32_t r3_offset =
      static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]));
  const int32_t r13_offset =
      static_cast<int32_t>(offsetof(ppc::PPCContext, r[13]));
  const int32_t a64_apc_pending_count_offset = static_cast<int32_t>(
      offsetof(ppc::PPCContext, a64_apc_pending_count));
  constexpr uint32_t kKpcrCurrentThreadOffset = 0x100;
  constexpr uint32_t kRtlCriticalSectionLockCountOffset = 0x10;
  constexpr uint32_t kRtlCriticalSectionRecursionCountOffset = 0x14;
  constexpr uint32_t kRtlCriticalSectionOwningThreadOffset = 0x18;

  if (name == "KeRaiseIrqlToDpcLevel") {
    ldr(x9, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                      A64BackendContext, processor_irql))));
    mov(w11, static_cast<uint32_t>(cpu::Irql::DPC));

    if (cvars::a64_lse_kernel_lock_fastpaths &&
        IsFeatureEnabled(kA64EmitLSE)) {
      swpal(w11, w10, ptr(x9));
      sxtw(x10, w10);
      str(x10, ptr(GetContextReg(), r3_offset));
      return true;
    }

    auto& retry = NewCachedLabel();
    L(retry);
    ldaxr(w10, ptr(x9));
    stlxr(w12, w11, ptr(x9));
    cbnz(w12, retry);

    sxtw(x10, w10);
    str(x10, ptr(GetContextReg(), r3_offset));
    return true;
  }

  if (name == "KfLowerIrql" &&
      cvars::a64_inline_kf_lower_irql_apc_guard) {
    if (!function->extern_handler()) {
      return false;
    }
    auto& slow_missing = NewCachedLabel();
    auto& slow_pending = NewCachedLabel();
    auto& slow_poll = NewCachedLabel();
    auto& done = NewCachedLabel();
    const bool audit_kf_lower =
        cvars::a64_kf_lower_irql_apc_guard_audit;
    uint32_t native_poll_interval =
        cvars::a64_kf_lower_irql_apc_guard_native_poll_interval;
    if (native_poll_interval & (native_poll_interval - 1)) {
      native_poll_interval = 0;
    }

    ldr(x12, ptr(GetContextReg(), a64_apc_pending_count_offset));
    cbz(x12, slow_missing);
    ldr(w13, ptr(x12));
    cbnz(w13, slow_pending);

    if (native_poll_interval) {
      const uint32_t counter_offset = static_cast<uint32_t>(offsetof(
          A64BackendContext, kf_lower_irql_apc_guard_counter));
      ldr(w14, ptr(GetBackendCtxReg(), counter_offset));
      add(w14, w14, 1);
      str(w14, ptr(GetBackendCtxReg(), counter_offset));
      tst(w14, native_poll_interval - 1);
      b(EQ, slow_poll);
    }

    ldr(x9, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                      A64BackendContext, processor_irql))));
    ldr(w10, ptr(GetContextReg(), r3_offset));

    if (cvars::a64_lse_kernel_lock_fastpaths &&
        IsFeatureEnabled(kA64EmitLSE)) {
      swpal(w10, w11, ptr(x9));
      if (audit_kf_lower) {
        EmitAtomicIncrement64(backend_->kf_lower_irql_apc_fastpath_count());
      }
      b(done);
    } else {
      auto& retry = NewCachedLabel();
      L(retry);
      ldaxr(w11, ptr(x9));
      stlxr(w12, w10, ptr(x9));
      cbnz(w12, retry);
      if (audit_kf_lower) {
        EmitAtomicIncrement64(backend_->kf_lower_irql_apc_fastpath_count());
      }
      b(done);
    }

    L(slow_pending);
    if (audit_kf_lower) {
      EmitAtomicIncrement64(
          backend_->kf_lower_irql_apc_pending_fallback_count());
    }
    EmitKernelExternHostCall(function);
    b(done);

    L(slow_poll);
    if (audit_kf_lower) {
      EmitAtomicIncrement64(
          backend_->kf_lower_irql_apc_poll_fallback_count());
    }
    EmitKernelExternHostCall(function);
    b(done);

    L(slow_missing);
    if (audit_kf_lower) {
      EmitAtomicIncrement64(
          backend_->kf_lower_irql_apc_missing_fallback_count());
    }
    EmitKernelExternHostCall(function);

    L(done);
    return true;
  }

  if (name == "KfLowerIrql" && cvars::a64_inline_kf_lower_irql) {
    ldr(x9, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                      A64BackendContext, processor_irql))));
    ldr(w10, ptr(GetContextReg(), r3_offset));

    if (cvars::a64_lse_kernel_lock_fastpaths &&
        IsFeatureEnabled(kA64EmitLSE)) {
      swpal(w10, w11, ptr(x9));
      return true;
    }

    auto& retry = NewCachedLabel();
    L(retry);
    ldaxr(w11, ptr(x9));
    stlxr(w12, w10, ptr(x9));
    cbnz(w12, retry);
    return true;
  }

  if (!function->extern_handler()) {
    return false;
  }

  auto emit_load_spin_lock_ptr = [&](Xbyak_aarch64::Label& slow) {
    ldr(w9, ptr(GetContextReg(), r3_offset));
    cbz(w9, slow);
    AddGuestAddressToMembase(w9, x9);
  };

  auto emit_release_spin_lock = [&]() {
    if (cvars::a64_lse_kernel_lock_fastpaths &&
        IsFeatureEnabled(kA64EmitLSE)) {
      mov(w10, 0xFFFFFFFFu);
      ldaddal(w10, w11, ptr(x9));
      return;
    }

    auto& retry = NewCachedLabel();
    L(retry);
    ldaxr(w10, ptr(x9));
    sub(w10, w10, 1);
    stlxr(w11, w10, ptr(x9));
    cbnz(w11, retry);
  };

  if (cvars::a64_inline_kernel_spinlock_exports &&
      name == "KeAcquireSpinLockAtRaisedIrql") {
    auto& slow = NewCachedLabel();
    auto& retry = NewCachedLabel();
    auto& busy = NewCachedLabel();
    auto& done = NewCachedLabel();

    emit_load_spin_lock_ptr(slow);

    if (cvars::a64_lse_kernel_lock_fastpaths &&
        IsFeatureEnabled(kA64EmitLSE)) {
      L(retry);
      mov(w10, 0);
      mov(w11, 1);
      casal(w10, w11, ptr(x9));
      cbz(w10, done);
      yield();
      b(retry);
    } else {
      L(retry);
      ldaxr(w10, ptr(x9));
      cbnz(w10, busy);
      mov(w11, 1);
      stlxr(w12, w11, ptr(x9));
      cbnz(w12, retry);
      b(done);

      L(busy);
      clrex(15);
      yield();
      b(retry);
    }

    L(slow);
    EmitKernelExternHostCall(function);

    L(done);
    return true;
  }

  if (cvars::a64_inline_kernel_spinlock_exports &&
      name == "KeTryToAcquireSpinLockAtRaisedIrql") {
    auto& slow = NewCachedLabel();
    auto& retry = NewCachedLabel();
    auto& fail = NewCachedLabel();
    auto& done = NewCachedLabel();

    emit_load_spin_lock_ptr(slow);

    if (cvars::a64_lse_kernel_lock_fastpaths &&
        IsFeatureEnabled(kA64EmitLSE)) {
      mov(w10, 0);
      mov(w11, 1);
      casal(w10, w11, ptr(x9));
      cmp(w10, 0);
      cset(w10, EQ);
      str(x10, ptr(GetContextReg(), r3_offset));
      b(done);
    } else {
      L(retry);
      ldaxr(w10, ptr(x9));
      cbnz(w10, fail);
      mov(w11, 1);
      stlxr(w12, w11, ptr(x9));
      cbnz(w12, retry);
      mov(x10, 1);
      str(x10, ptr(GetContextReg(), r3_offset));
      b(done);

      L(fail);
      clrex(15);
      str(xzr, ptr(GetContextReg(), r3_offset));
      b(done);
    }

    L(slow);
    EmitKernelExternHostCall(function);

    L(done);
    return true;
  }

  if (cvars::a64_inline_kernel_spinlock_exports &&
      name == "KeReleaseSpinLockFromRaisedIrql") {
    auto& slow = NewCachedLabel();
    auto& done = NewCachedLabel();

    emit_load_spin_lock_ptr(slow);
    emit_release_spin_lock();
    b(done);

    L(slow);
    EmitKernelExternHostCall(function);

    L(done);
    return true;
  }

  auto emit_load_critical_section_and_thread = [&](
                                                   Xbyak_aarch64::Label& slow) {
    ldr(w9, ptr(GetContextReg(), r3_offset));
    cbz(w9, slow);
    AddGuestAddressToMembase(w9, x9);

    ldr(w10, ptr(GetContextReg(), r13_offset));
    cbz(w10, slow);
    AddGuestAddressToMembase(w10, x10);
    ldr(w11, ptr(x10, kKpcrCurrentThreadOffset));
    rev(w11, w11);
    cbz(w11, slow);
  };

  auto emit_increment_lock_count = [&]() {
    add(x12, x9, kRtlCriticalSectionLockCountOffset);
    if (cvars::a64_lse_kernel_lock_fastpaths &&
        IsFeatureEnabled(kA64EmitLSE)) {
      mov(w13, 1);
      ldaddal(w13, w14, ptr(x12));
      return;
    }

    auto& retry = NewCachedLabel();
    L(retry);
    ldaxr(w13, ptr(x12));
    add(w13, w13, 1);
    stlxr(w14, w13, ptr(x12));
    cbnz(w14, retry);
  };

  if (name == "RtlEnterCriticalSection") {
    auto& slow = NewCachedLabel();
    auto& try_free_lock = NewCachedLabel();
    auto& free_lock_busy = NewCachedLabel();
    auto& done = NewCachedLabel();

    emit_load_critical_section_and_thread(slow);

    if (!cvars::a64_rtl_enter_free_first) {
      ldr(w13, ptr(x9, kRtlCriticalSectionOwningThreadOffset));
      rev(w13, w13);
      cmp(w13, w11);
      b(NE, try_free_lock);

      emit_increment_lock_count();
      ldr(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
      rev(w13, w13);
      add(w13, w13, 1);
      rev(w13, w13);
      str(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
      b(done);
    }

    L(try_free_lock);
    add(x12, x9, kRtlCriticalSectionLockCountOffset);
    if (cvars::a64_lse_kernel_lock_fastpaths &&
        IsFeatureEnabled(kA64EmitLSE)) {
      mov(w13, 0xFFFFFFFFu);
      mov(w14, 0);
      casal(w13, w14, ptr(x12));
      mov(w15, 0xFFFFFFFFu);
      cmp(w13, w15);
      b(NE, free_lock_busy);
    } else {
      auto& retry_free_lock = NewCachedLabel();
      L(retry_free_lock);
      ldaxr(w13, ptr(x12));
      mov(w14, 0xFFFFFFFFu);
      cmp(w13, w14);
      b(NE, free_lock_busy);
      mov(w14, 0);
      stlxr(w15, w14, ptr(x12));
      cbnz(w15, retry_free_lock);
    }

    mov(w13, w11);
    rev(w13, w13);
    str(w13, ptr(x9, kRtlCriticalSectionOwningThreadOffset));
    mov(w13, 1);
    rev(w13, w13);
    str(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
    b(done);

    L(free_lock_busy);
    clrex(15);
    if (cvars::a64_rtl_enter_free_first) {
      ldr(w13, ptr(x9, kRtlCriticalSectionOwningThreadOffset));
      rev(w13, w13);
      cmp(w13, w11);
      b(NE, slow);

      emit_increment_lock_count();
      ldr(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
      rev(w13, w13);
      add(w13, w13, 1);
      rev(w13, w13);
      str(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
      b(done);
    }
    L(slow);
    EmitKernelExternHostCall(function);

    L(done);
    return true;
  }

  if (name == "RtlTryEnterCriticalSection") {
    auto& slow = NewCachedLabel();
    auto& check_recursive = NewCachedLabel();
    auto& fail = NewCachedLabel();
    auto& success = NewCachedLabel();
    auto& done = NewCachedLabel();

    emit_load_critical_section_and_thread(slow);

    add(x12, x9, kRtlCriticalSectionLockCountOffset);
    if (cvars::a64_lse_kernel_lock_fastpaths &&
        IsFeatureEnabled(kA64EmitLSE)) {
      mov(w13, 0xFFFFFFFFu);
      mov(w14, 0);
      casal(w13, w14, ptr(x12));
      mov(w15, 0xFFFFFFFFu);
      cmp(w13, w15);
      b(NE, check_recursive);
    } else {
      auto& retry_free_lock = NewCachedLabel();
      L(retry_free_lock);
      ldaxr(w13, ptr(x12));
      mov(w14, 0xFFFFFFFFu);
      cmp(w13, w14);
      b(NE, check_recursive);
      mov(w14, 0);
      stlxr(w15, w14, ptr(x12));
      cbnz(w15, retry_free_lock);
    }

    mov(w13, w11);
    rev(w13, w13);
    str(w13, ptr(x9, kRtlCriticalSectionOwningThreadOffset));
    mov(w13, 1);
    rev(w13, w13);
    str(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
    b(success);

    L(check_recursive);
    clrex(15);
    ldr(w13, ptr(x9, kRtlCriticalSectionOwningThreadOffset));
    rev(w13, w13);
    cmp(w13, w11);
    b(NE, fail);

    emit_increment_lock_count();
    ldr(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
    rev(w13, w13);
    add(w13, w13, 1);
    rev(w13, w13);
    str(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));

    L(success);
    mov(x13, 1);
    str(x13, ptr(GetContextReg(), r3_offset));
    b(done);

    L(fail);
    str(xzr, ptr(GetContextReg(), r3_offset));
    b(done);

    L(slow);
    EmitKernelExternHostCall(function);

    L(done);
    return true;
  }

  if (name == "RtlLeaveCriticalSection") {
    auto& slow = NewCachedLabel();
    auto& done = NewCachedLabel();
    const bool audit_rtl_leave = cvars::a64_rtl_leave_fastpath_audit;

    emit_load_critical_section_and_thread(slow);

    ldr(w13, ptr(x9, kRtlCriticalSectionOwningThreadOffset));
    rev(w13, w13);
    cmp(w13, w11);
    b(NE, slow);

    ldr(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
    rev(w13, w13);
    cmp(w13, 1);

    auto emit_recursive_release = [&]() {
      sub(w13, w13, 1);
      rev(w13, w13);
      str(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));

      add(x12, x9, kRtlCriticalSectionLockCountOffset);
      if (cvars::a64_lse_kernel_lock_fastpaths &&
          IsFeatureEnabled(kA64EmitLSE)) {
        mov(w13, 0xFFFFFFFFu);
        ldaddal(w13, w14, ptr(x12));
      } else {
        auto& retry_dec_lock = NewCachedLabel();
        L(retry_dec_lock);
        ldaxr(w13, ptr(x12));
        sub(w13, w13, 1);
        stlxr(w14, w13, ptr(x12));
        cbnz(w14, retry_dec_lock);
      }
      if (audit_rtl_leave) {
        EmitAtomicIncrement64(backend_->rtl_leave_recursive_inline_count());
      }
      b(done);
    };

    if (cvars::a64_inline_rtl_leave_final_unlock) {
      auto& final_release = NewCachedLabel();
      b(EQ, final_release);
      b(LO, slow);

      emit_recursive_release();

      L(final_release);
      add(x12, x9, kRtlCriticalSectionLockCountOffset);
      ldr(w13, ptr(x12));
      cbnz(w13, slow);

      str(wzr, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
      str(wzr, ptr(x9, kRtlCriticalSectionOwningThreadOffset));

      auto& retry_final_unlock = NewCachedLabel();
      auto& restore_slow = NewCachedLabel();
      if (cvars::a64_lse_kernel_lock_fastpaths &&
          IsFeatureEnabled(kA64EmitLSE)) {
        mov(w13, 0);
        mov(w14, 0xFFFFFFFFu);
        casal(w13, w14, ptr(x12));
        cbnz(w13, restore_slow);
      } else {
        L(retry_final_unlock);
        ldaxr(w13, ptr(x12));
        cbnz(w13, restore_slow);
        mov(w14, 0xFFFFFFFFu);
        stlxr(w15, w14, ptr(x12));
        cbnz(w15, retry_final_unlock);
      }
      if (audit_rtl_leave) {
        EmitAtomicIncrement64(backend_->rtl_leave_final_inline_count());
      }
      b(done);

      L(restore_slow);
      clrex(15);
      mov(w13, 1);
      rev(w13, w13);
      str(w13, ptr(x9, kRtlCriticalSectionRecursionCountOffset));
      mov(w13, w11);
      rev(w13, w13);
      str(w13, ptr(x9, kRtlCriticalSectionOwningThreadOffset));
      if (audit_rtl_leave) {
        EmitAtomicIncrement64(backend_->rtl_leave_restore_slow_count());
      }
      b(slow);
    } else {
      b(LS, slow);
      emit_recursive_release();
    }

    L(slow);
    if (audit_rtl_leave) {
      EmitAtomicIncrement64(backend_->rtl_leave_native_fallback_count());
    }
    EmitKernelExternHostCall(function);

    L(done);
    return true;
  }

  return false;
}

bool A64Emitter::TryEmitBlueDragonDrawWaitFunctionBody() {
  if (!cvars::arm64_blue_dragon_draw_wait_fastpath ||
      current_guest_function_ != 0x8246B408) {
    return false;
  }

  return EmitBlueDragonDrawWaitFastpathBody();
}

bool A64Emitter::TryEmitBlueDragonDrawWaitInlineCall(GuestFunction* function) {
  if (!cvars::arm64_blue_dragon_draw_wait_fastpath ||
      !cvars::arm64_blue_dragon_draw_wait_inline_in_caller ||
      current_guest_function_ != 0x8246E618 || !function ||
      function->address() != 0x8246B408) {
    return false;
  }

  return EmitBlueDragonDrawWaitFastpathBody();
}

bool A64Emitter::EmitBlueDragonDrawWaitFastpathBody() {
  ForgetFpcrMode();

  auto& return_zero = NewCachedLabel();
  auto& return_one = NewCachedLabel();
  auto& done = NewCachedLabel();
  auto& no_token_change = NewCachedLabel();
  auto& no_owner_refresh = NewCachedLabel();

  // Hand-emits Blue Dragon's draw wait predicate:
  // token progress or current-KTHREAD ownership refreshes wait_state+0xC;
  // elapsed < 5000 ms returns 1, otherwise returns 0. This intentionally skips
  // the timeout helper side effect and is guarded by a title-specific cvar.
  const bool update_kernel_time =
      cvars::arm64_blue_dragon_draw_wait_probe;
  uint32_t inline_step = update_kernel_time
                             ? cvars::arm64_blue_dragon_draw_wait_inline_tick_step
                             : 0;
  const bool host_counter_time =
      update_kernel_time &&
      cvars::arm64_blue_dragon_draw_wait_fastpath_host_counter_time &&
      inline_step == 0;
  if (update_kernel_time && inline_step == 0 && !host_counter_time) {
    uint32_t stride =
        std::max<uint32_t>(cvars::arm64_blue_dragon_draw_wait_probe_stride, 1);
    Xbyak_aarch64::Label* skip_update = nullptr;
    if (stride > 1 && (stride & (stride - 1)) == 0) {
      skip_update = &NewCachedLabel();
      ldr(w17, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                           A64BackendContext,
                                           blue_dragon_draw_wait_probe_counter))));
      add(w17, w17, 1);
      str(w17, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                           A64BackendContext,
                                           blue_dragon_draw_wait_probe_counter))));
      and_(w17, w17, stride - 1);
      cbnz(w17, *skip_update);
    }
    CallNativeSafe(
        reinterpret_cast<void*>(&UpdateBlueDragonDrawWaitKernelTimeForFastpath));
    if (skip_update) {
      L(*skip_update);
    }
  }

  ldr(w9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));
  cbz(w9, return_zero);
  AddGuestAddressToMembase(w9, x9);  // x9 = wait state host pointer.

  ldr(w10, ptr(x9, 0));
  rev(w10, w10);
  cbz(w10, return_zero);
  AddGuestAddressToMembase(w10, x10);  // x10 = draw object host pointer.

  add(x12, x10, 2, 12);
  ldrb(w11, ptr(x12, 0xA39));
  tbnz(w11, 1, return_zero);

  ldr(w13, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[13]))));
  cbz(w13, return_zero);
  AddGuestAddressToMembase(w13, x13);
  ldr(w13, ptr(x13, 0x100));
  rev(w13, w13);
  cbz(w13, return_zero);
  AddGuestAddressToMembase(w13, x13);  // x13 = current KTHREAD host pointer.

  if (host_counter_time) {
    auto& non_negative_time = NewCachedLabel();
    auto& have_time = NewCachedLabel();
    // CNTVCT_EL0 / CNTFRQ_EL0 gives host milliseconds without a native thunk.
    // Subtract the per-context base so the value matches guest uptime shape.
    mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
    mov(x11, uint64_t{1000});
    mul(x17, x17, x11);
    mrs(x11, 3, 3, 14, 0, 0);  // CNTFRQ_EL0.
    udiv(x17, x17, x11);
    ldr(x11, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                      A64BackendContext,
                                      host_uptime_millis_base))));
    cmp(x17, x11);
    b(HS, non_negative_time);
    mov(x17, uint64_t{0});
    b(have_time);
    L(non_negative_time);
    sub(x17, x17, x11);
    L(have_time);
    rev(w11, w17);
    str(w11, ptr(x13, 0x58));
  }

  ldr(w17, ptr(x13, 0x58));
  rev(w17, w17);
  if (inline_step != 0) {
    inline_step = std::min<uint32_t>(inline_step, 0xFFFFu);
    if (inline_step <= 4095) {
      add(w17, w17, inline_step);
    } else {
      mov(w11, inline_step);
      add(w17, w17, w11);
    }
    rev(w11, w17);
    str(w11, ptr(x13, 0x58));
  }

  ldr(w14, ptr(x10, 0x2A10));
  rev(w14, w14);
  cbz(w14, return_zero);
  AddGuestAddressToMembase(w14, x14);
  ldr(w15, ptr(x9, 0x8));
  rev(w15, w15);
  ldr(w16, ptr(x14, 0));
  rev(w16, w16);
  cmp(w15, w16);
  b(EQ, no_token_change);
  mov(w11, w17);
  rev(w11, w11);
  str(w11, ptr(x9, 0xC));
  mov(w11, w16);
  rev(w11, w11);
  str(w11, ptr(x9, 0x8));
  L(no_token_change);

  ldr(w11, ptr(x13, 0x14C));
  rev(w11, w11);
  ldr(w15, ptr(x10, 0x2A08));
  rev(w15, w15);
  cmp(w15, w11);
  b(NE, no_owner_refresh);
  ldr(w15, ptr(x10, 0x2A70));
  rev(w15, w15);
  cbz(w15, no_owner_refresh);
  mov(w11, w17);
  rev(w11, w11);
  str(w11, ptr(x9, 0xC));
  L(no_owner_refresh);

  ldr(w10, ptr(x9, 0xC));
  rev(w10, w10);
  sub(w10, w17, w10);
  mov(w11, cvars::arm64_blue_dragon_draw_wait_fastpath_timeout_ms);
  cmp(w10, w11);
  b(LO, return_one);

  L(return_zero);
  str(xzr, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));
  b(done);

  L(return_one);
  uint32_t native_yield_stride =
      cvars::arm64_blue_dragon_draw_wait_fastpath_native_yield_stride;
  if (native_yield_stride != 0) {
    auto& skip_yield = NewCachedLabel();
    ldr(w11, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                      A64BackendContext,
                                      blue_dragon_draw_wait_yield_counter))));
    add(w11, w11, 1);
    str(w11, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                      A64BackendContext,
                                      blue_dragon_draw_wait_yield_counter))));
    if (native_yield_stride <= 4095) {
      cmp(w11, native_yield_stride);
    } else {
      mov(w15, native_yield_stride);
      cmp(w11, w15);
    }
    b(LO, skip_yield);
    str(wzr, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                      A64BackendContext,
                                      blue_dragon_draw_wait_yield_counter))));
    CallNativeSafe(reinterpret_cast<void*>(&YieldBlueDragonDrawWaitFastpath));
    L(skip_yield);
  }
  mov(w9, uint32_t{1});
  str(x9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));

  L(done);
  return true;
}

bool A64Emitter::TryEmitBlueDragonMemcpyFunctionBody() {
  if (!cvars::arm64_blue_dragon_memcpy_fastpath ||
      current_guest_function_ != 0x826BF770) {
    return false;
  }

  ForgetFpcrMode();

  auto& skip_copy = NewCachedLabel();
  ldr(w9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));
  str(x9, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
  ldr(w10, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[4]))));
  ldr(w2, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[5]))));
  cbz(w2, skip_copy);

  AddGuestAddressToMembase(w9, x0);
  AddGuestAddressToMembase(w10, x1);
  mov(x9, reinterpret_cast<uint64_t>(
              static_cast<void* (*)(void*, const void*, size_t)>(
                  &std::memmove)));
  blr(x9);

  L(skip_copy);
  ldr(x9, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
  str(x9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));
  return true;
}

bool A64Emitter::TryEmitBlueDragonStricmpFunctionBody() {
  if (!cvars::arm64_blue_dragon_stricmp_fastpath ||
      current_guest_function_ != 0x826C5620) {
    return false;
  }

  ForgetFpcrMode();

  auto emit_signed_cr = [&](uint32_t field, const WReg& lhs, uint32_t rhs) {
    int32_t base = static_cast<int32_t>(offsetof(ppc::PPCContext, cr0) +
                                        (4 * field));
    cmp(lhs, rhs);
    cset(w11, LT);
    strb(w11, ptr(GetContextReg(), base + 0));
    cset(w11, GT);
    strb(w11, ptr(GetContextReg(), base + 1));
    cset(w11, EQ);
    strb(w11, ptr(GetContextReg(), base + 2));
  };
  auto emit_cr0_from_result = [&]() {
    int32_t base = static_cast<int32_t>(offsetof(ppc::PPCContext, cr0));
    cmp(w15, 0);
    cset(w11, LT);
    strb(w11, ptr(GetContextReg(), base + 0));
    cset(w11, GT);
    strb(w11, ptr(GetContextReg(), base + 1));
    cset(w11, EQ);
    strb(w11, ptr(GetContextReg(), base + 2));
  };
  auto emit_cr_constant = [&](uint32_t field, bool lt, bool gt, bool eq) {
    int32_t base = static_cast<int32_t>(offsetof(ppc::PPCContext, cr0) +
                                        (4 * field));
    if (lt || gt || eq) {
      mov(w11, 1);
    }
    if (lt) {
      strb(w11, ptr(GetContextReg(), base + 0));
    } else {
      strb(wzr, ptr(GetContextReg(), base + 0));
    }
    if (gt) {
      strb(w11, ptr(GetContextReg(), base + 1));
    } else {
      strb(wzr, ptr(GetContextReg(), base + 1));
    }
    if (eq) {
      strb(w11, ptr(GetContextReg(), base + 2));
    } else {
      strb(wzr, ptr(GetContextReg(), base + 2));
    }
  };

  auto& loop = NewCachedLabel();
  auto& advance = NewCachedLabel();
  auto& lower_second_done = NewCachedLabel();
  auto& lower_first_done = NewCachedLabel();
  auto& raw_diff = NewCachedLabel();
  auto& nul_return = NewCachedLabel();
  auto& return_result = NewCachedLabel();

  ldr(w16, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));
  ldr(w17, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[4]))));
  mov(w9, w16);
  mov(w10, w17);
  AddGuestAddressToMembase(w9, x9);
  AddGuestAddressToMembase(w10, x10);

  L(loop);
  ldrb(w14, ptr(x10));
  ldrb(w13, ptr(x9));

  if (cvars::arm64_blue_dragon_stricmp_deferred_cr_fastpath) {
    sub(x15, x13, x14);
    cbz(w14, nul_return);
    cbnz(w15, raw_diff);
    b(advance);

    L(nul_return);
    emit_cr_constant(7, false, false, true);
    emit_cr0_from_result();
    b(return_result);

    L(raw_diff);
    emit_cr_constant(7, false, true, false);
    emit_signed_cr(5, w14, static_cast<uint32_t>('A'));
    emit_signed_cr(6, w14, static_cast<uint32_t>('Z'));
    cmp(w14, static_cast<uint32_t>('A'));
    b(LT, lower_second_done);
    cmp(w14, static_cast<uint32_t>('Z'));
    b(GT, lower_second_done);
    orr(w14, w14, static_cast<uint32_t>(0x20));
    L(lower_second_done);

    emit_signed_cr(0, w13, static_cast<uint32_t>('A'));
    emit_signed_cr(1, w13, static_cast<uint32_t>('Z'));
    cmp(w13, static_cast<uint32_t>('A'));
    b(LT, lower_first_done);
    cmp(w13, static_cast<uint32_t>('Z'));
    b(GT, lower_first_done);
    orr(w13, w13, static_cast<uint32_t>(0x20));
    L(lower_first_done);

    sub(x15, x13, x14);
    emit_cr0_from_result();
    b(return_result);

    L(advance);
    add(x9, x9, 1);
    add(x10, x10, 1);
    add(w16, w16, 1);
    add(w17, w17, 1);
    b(loop);

    L(return_result);
    str(x15, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));
    str(x17, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[4]))));
    str(x13, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[5]))));
    str(x14, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[6]))));
    str(x16, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[9]))));
    return true;
  }

  emit_signed_cr(7, w14, 0);
  sub(x15, x13, x14);
  emit_cr0_from_result();
  cbz(w14, return_result);
  cbz(w15, advance);

  emit_signed_cr(5, w14, static_cast<uint32_t>('A'));
  emit_signed_cr(6, w14, static_cast<uint32_t>('Z'));
  cmp(w14, static_cast<uint32_t>('A'));
  b(LT, lower_second_done);
  cmp(w14, static_cast<uint32_t>('Z'));
  b(GT, lower_second_done);
  orr(w14, w14, static_cast<uint32_t>(0x20));
  L(lower_second_done);

  emit_signed_cr(0, w13, static_cast<uint32_t>('A'));
  emit_signed_cr(1, w13, static_cast<uint32_t>('Z'));
  cmp(w13, static_cast<uint32_t>('A'));
  b(LT, lower_first_done);
  cmp(w13, static_cast<uint32_t>('Z'));
  b(GT, lower_first_done);
  orr(w13, w13, static_cast<uint32_t>(0x20));
  L(lower_first_done);

  sub(x15, x13, x14);
  emit_cr0_from_result();
  cbz(w15, advance);
  b(return_result);

  L(advance);
  add(x9, x9, 1);
  add(x10, x10, 1);
  add(w16, w16, 1);
  add(w17, w17, 1);
  b(loop);

  L(return_result);
  str(x15, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));
  str(x17, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[4]))));
  str(x13, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[5]))));
  str(x14, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[6]))));
  str(x16, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[9]))));
  return true;
}

bool A64Emitter::TryEmitBlueDragonJumpTableFunctionBody() {
  if (!cvars::arm64_blue_dragon_jump_table_fastpath ||
      current_guest_function_ != 0x827294CC) {
    return false;
  }

  EmitBlueDragonJumpTableDispatch();

  MaybeEmitBodyTimeProfileEnd();
  PopStackpoint();
  ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
  ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
  if (stack_size() <= 4095) {
    add(sp, sp, static_cast<uint32_t>(stack_size()));
  } else {
    mov(x17, static_cast<uint64_t>(stack_size()));
    add(sp, sp, x17, UXTX);
  }
  br(x9);
  return true;
}

bool A64Emitter::TryEmitBlueDragonJumpTableInlineCall(
    const hir::Instr* instr, GuestFunction* function) {
  if (!cvars::arm64_blue_dragon_jump_table_fastpath ||
      !cvars::arm64_blue_dragon_jump_table_inline_in_caller || !function ||
      function->address() != 0x827294CC) {
    return false;
  }

  EmitBlueDragonJumpTableDispatch();

  if (instr->flags & hir::CALL_TAIL) {
    MaybeEmitBodyTimeProfileEnd();
    PopStackpoint();
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
    if (stack_size() <= 4095) {
      add(sp, sp, static_cast<uint32_t>(stack_size()));
    } else {
      mov(x17, static_cast<uint64_t>(stack_size()));
      add(sp, sp, x17, UXTX);
    }
    br(x9);
  } else {
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
    blr(x9);
    synchronize_stack_on_next_instruction_ = true;
  }
  return true;
}

bool A64Emitter::EmitBlueDragonJumpTableDispatch() {
  ForgetFpcrMode();

  mov(w13, 0xB);
  mov(w14, 0x5);
  str(x13, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[25]))));
  str(x14, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[26]))));

  mov(w12, 0x827294ECu);
  str(x12, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[12]))));
  ldr(w16, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[10]))));
  lsl(w16, w16, 2);
  add(w12, w12, w16);
  AddGuestAddressToMembase(w12, x12);
  ldr(w16, ptr(x12));
  rev(w16, w16);
  str(x16, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[0]))));
  str(x16, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, ctr))));

  if (backend_->speed_profile_enabled()) {
    EmitAtomicIncrement64(backend_->speed_profile_indirect_guest_calls());
  }

  if (code_cache_->has_indirection_table()) {
    mov(x0, A64CodeCache::execute_address_high());
    orr(x16, x16, x0);
    ldr(w9, ptr(x16, static_cast<uint32_t>(0)));
    orr(x9, x9, x0);
  } else {
    mov(x0, x20);
    mov(x1, x16);
    mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
    blr(x9);
    mov(x9, x0);
  }

  return true;
}

bool A64Emitter::TryEmitBlueDragonVmxCopyLoopBlock(const hir::Block* block) {
  if (!cvars::arm64_blue_dragon_vmx_copy_loop_fastpath ||
      current_guest_function_ != 0x82486178 ||
      current_block_guest_address_ != 0x8248627C || !block ||
      !block->label_head) {
    return false;
  }

  ForgetFpcrMode();

  const auto gpr_offset = [](int reg) -> int32_t {
    return static_cast<int32_t>(offsetof(ppc::PPCContext, r) +
                                sizeof(uint64_t) *
                                    static_cast<size_t>(reg));
  };
  const auto vmx_offset = [](int reg) -> int32_t {
    return static_cast<int32_t>(offsetof(ppc::PPCContext, v) +
                                sizeof(xe::vec128_t) *
                                    static_cast<size_t>(reg));
  };
  const int32_t cr0_base =
      static_cast<int32_t>(offsetof(ppc::PPCContext, cr0));
  const int32_t xer_ca =
      static_cast<int32_t>(offsetof(ppc::PPCContext, xer_ca));

  ldr(x9, ptr(GetContextReg(), gpr_offset(30)));  // PPC r30 source base.
  ldr(x10, ptr(GetContextReg(), gpr_offset(4)));  // PPC r4 dest base.
  ldr(x11, ptr(GetContextReg(), gpr_offset(31)));
  ldr(x16, ptr(GetContextReg(), gpr_offset(28)));

  auto& loop = NewCachedLabel();
  L(loop);
  add(w14, w9, 0x04);
  and_(x14, x14, ~0xFull);
  AddGuestAddressToMembase(w14, x14);
  add(w15, w10, 0x04);
  and_(x15, x15, ~0xFull);
  AddGuestAddressToMembase(w15, x15);

  ldr(QReg(0), ptr(x14, 0x00));
  ldr(QReg(1), ptr(x14, 0x10));
  ldr(QReg(2), ptr(x14, 0x20));
  ldr(QReg(3), ptr(x14, 0x30));

  rev32(VReg16B(4), VReg16B(0));
  rev32(VReg16B(5), VReg16B(1));
  rev32(VReg16B(6), VReg16B(2));
  rev32(VReg16B(7), VReg16B(3));

  str(QReg(0), ptr(x15, 0x00));
  str(QReg(1), ptr(x15, 0x10));
  str(QReg(2), ptr(x15, 0x20));
  str(QReg(3), ptr(x15, 0x30));

  sub(x16, x16, 1);
  add(x9, x9, 0x40);
  add(x10, x10, 0x40);
  lsl(x11, x11, 1);
  cbnz(w16, loop);

  str(x16, ptr(GetContextReg(), gpr_offset(28)));
  str(x9, ptr(GetContextReg(), gpr_offset(30)));
  str(x10, ptr(GetContextReg(), gpr_offset(4)));
  str(x11, ptr(GetContextReg(), gpr_offset(31)));

  // Preserve the PPC-visible volatile GPR side effects of the final trip.
  sub(x12, x9, 0x0C);
  str(x12, ptr(GetContextReg(), gpr_offset(8)));
  sub(x12, x10, 0x3C);
  str(x12, ptr(GetContextReg(), gpr_offset(7)));
  sub(x12, x10, 0x2C);
  str(x12, ptr(GetContextReg(), gpr_offset(11)));
  sub(x12, x10, 0x1C);
  str(x12, ptr(GetContextReg(), gpr_offset(10)));
  sub(x12, x10, 0x0C);
  str(x12, ptr(GetContextReg(), gpr_offset(9)));

  str(QReg(4), ptr(GetContextReg(), vmx_offset(0)));
  str(QReg(5), ptr(GetContextReg(), vmx_offset(13)));
  str(QReg(6), ptr(GetContextReg(), vmx_offset(12)));
  str(QReg(7), ptr(GetContextReg(), vmx_offset(11)));

  mov(w17, 1);
  strb(w17, ptr(GetContextReg(), xer_ca));
  strb(wzr, ptr(GetContextReg(), cr0_base + 0));
  strb(wzr, ptr(GetContextReg(), cr0_base + 1));
  strb(w17, ptr(GetContextReg(), cr0_base + 2));
  return true;
}

bool A64Emitter::TryEmitBlueDragonWordCopyLoopBlock(const hir::Block* block) {
  if (!cvars::arm64_blue_dragon_word_copy_loop_fastpath ||
      current_guest_function_ != 0x82485DD8 ||
      current_block_guest_address_ != 0x82485E70 || !block ||
      !block->label_head) {
    return false;
  }

  ForgetFpcrMode();

  const auto gpr_offset = [](int reg) -> int32_t {
    return static_cast<int32_t>(offsetof(ppc::PPCContext, r) +
                                sizeof(uint64_t) *
                                    static_cast<size_t>(reg));
  };
  const int32_t cr0_base =
      static_cast<int32_t>(offsetof(ppc::PPCContext, cr0));
  const int32_t xer_ca =
      static_cast<int32_t>(offsetof(ppc::PPCContext, xer_ca));

  ldr(x9, ptr(GetContextReg(), gpr_offset(29)));   // PPC r29 source cursor.
  ldr(x10, ptr(GetContextReg(), gpr_offset(4)));   // PPC r4 dest cursor.
  ldr(x11, ptr(GetContextReg(), gpr_offset(11)));  // PPC r11 count.
  ldr(x13, ptr(GetContextReg(), gpr_offset(31)));  // PPC r31 shift mask.

  auto& loop = NewCachedLabel();
  L(loop);
  add(x9, x9, 0x04);
  AddGuestAddressToMembase(w9, x14);
  ldr(w15, ptr(x14));
  rev(w16, w15);
  add(x10, x10, 0x04);
  AddGuestAddressToMembase(w10, x17);
  str(w15, ptr(x17));

  sub(x11, x11, 1);
  lsl(x13, x13, 1);
  cbnz(w11, loop);

  str(x9, ptr(GetContextReg(), gpr_offset(29)));
  str(x10, ptr(GetContextReg(), gpr_offset(4)));
  str(x11, ptr(GetContextReg(), gpr_offset(11)));
  str(x13, ptr(GetContextReg(), gpr_offset(31)));
  str(x16, ptr(GetContextReg(), gpr_offset(10)));

  mov(w17, 1);
  strb(w17, ptr(GetContextReg(), xer_ca));
  strb(wzr, ptr(GetContextReg(), cr0_base + 0));
  strb(wzr, ptr(GetContextReg(), cr0_base + 1));
  strb(w17, ptr(GetContextReg(), cr0_base + 2));
  return true;
}

void A64Emitter::MaybeEmitBlueDragonDrawWaitCallerProfile() {
  if (!cvars::arm64_blue_dragon_draw_wait_caller_profile ||
      current_guest_function_ != 0x8246B408) {
    return;
  }

  uint32_t stride = std::max<uint32_t>(
      cvars::arm64_blue_dragon_draw_wait_caller_profile_stride, 1);
  auto& skip_sample = NewCachedLabel();
  ldr(w17, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                    A64BackendContext,
                                    blue_dragon_draw_wait_caller_profile_counter))));
  add(w17, w17, 1);
  str(w17, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                    A64BackendContext,
                                    blue_dragon_draw_wait_caller_profile_counter))));
  if (stride <= 4095) {
    cmp(w17, stride);
  } else {
    mov(w11, stride);
    cmp(w17, w11);
  }
  b(LO, skip_sample);
  str(wzr, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                     A64BackendContext,
                                     blue_dragon_draw_wait_caller_profile_counter))));
  CallNativeSafe(reinterpret_cast<void*>(&RecordBlueDragonDrawWaitCallerProfile));
  L(skip_sample);
}

void A64Emitter::MaybeEmitBlueDragonStricmpReturnProfile() {
  if (!cvars::arm64_blue_dragon_stricmp_return_profile ||
      current_guest_function_ != 0x826C5620) {
    return;
  }

  ForgetFpcrMode();
  CallNativeSafe(reinterpret_cast<void*>(&RecordBlueDragonStricmpReturnProfile));
}

void A64Emitter::Call(const hir::Instr* instr, GuestFunction* function) {
  assert_not_null(function);
  std::atomic<uint64_t>* call_edge_entry_counter = nullptr;
  std::atomic<uint64_t>* call_edge_body_ticks_counter = nullptr;
  if (current_guest_function_call_edge_profile_ && current_a64_function_) {
    size_t edge_ordinal = current_call_edge_ordinal_++;
    current_a64_function_->set_profile_call_edge_addresses(
        edge_ordinal, current_block_guest_address_, function->address());
    call_edge_entry_counter =
        current_a64_function_->profile_call_edge_count(edge_ordinal);
    call_edge_body_ticks_counter =
        current_a64_function_->profile_call_edge_body_ticks(edge_ordinal);
  }

  if (TryEmitGprLrHelperCall(instr, function)) {
    return;
  }
  if (TryEmitFprVmxHelperCall(instr, function)) {
    return;
  }
  if (TryEmitPpcThreadFieldLeafHelperCall(instr, function)) {
    return;
  }
  if (TryEmitBlueDragonDrawWaitInlineCall(function)) {
    return;
  }
  if (TryEmitBlueDragonJumpTableInlineCall(instr, function)) {
    return;
  }

  ForgetFpcrMode();
  auto fn = static_cast<A64Function*>(function);
  if (backend_->speed_profile_enabled()) {
    EmitAtomicIncrement64(backend_->speed_profile_direct_guest_calls());
  }

  if (fn->machine_code()) {
    // Direct call — function is already compiled.
    if (!(instr->flags & hir::CALL_TAIL)) {
      // Pass the next call's guest return address in x0.
      MaybeEmitCallEdgeProfileStart(call_edge_entry_counter);
      mov(x9, reinterpret_cast<uint64_t>(fn->machine_code()));
      ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
      blr(x9);
      MaybeEmitCallEdgeProfileEnd(call_edge_body_ticks_counter);
      synchronize_stack_on_next_instruction_ = true;
    } else {
      // Tail call: pass our return address to the callee.
      mov(x9, reinterpret_cast<uint64_t>(fn->machine_code()));
      MaybeEmitBodyTimeProfileEnd();
      PopStackpoint();
      ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
      ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
      if (stack_size() <= 4095) {
        add(sp, sp, static_cast<uint32_t>(stack_size()));
      } else {
        mov(x17, static_cast<uint64_t>(stack_size()));
        add(sp, sp, x17, UXTX);
      }
      br(x9);
    }
    return;
  }

  if (!(instr->flags & hir::CALL_TAIL)) {
    MaybeEmitCallEdgeProfileStart(call_edge_entry_counter);
  }

  if (code_cache_->has_indirection_table()) {
    // Load host code address from indirection table.
    mov(x0,A64CodeCache::execute_address_high());
    mov(w16, function->address());
    orr(x16, x16,x0);
    ldr(w9, ptr(x16, static_cast<uint32_t>(0)));
    orr(x9, x9,x0);
  } else {
    // Fallback: resolve at runtime.
    mov(x0, x20);  // context
    mov(x1, static_cast<uint64_t>(function->address()));
    mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
    blr(x9);
    mov(x9, x0);  // resolved address in x9
  }

  if (instr->flags & hir::CALL_TAIL) {
    MaybeEmitBodyTimeProfileEnd();
    PopStackpoint();
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
    if (stack_size() <= 4095) {
      add(sp, sp, static_cast<uint32_t>(stack_size()));
    } else {
      mov(x17, static_cast<uint64_t>(stack_size()));
      add(sp, sp, x17, UXTX);
    }
    br(x9);
  } else {
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
    blr(x9);
    MaybeEmitCallEdgeProfileEnd(call_edge_body_ticks_counter);
    synchronize_stack_on_next_instruction_ = true;
  }
}

void A64Emitter::CallIndirect(const hir::Instr* instr, int reg_index) {
  ForgetFpcrMode();
  auto target_w = WReg(reg_index);

  // Check if this is a possible return (e.g., PPC blr).
  if (instr->flags & hir::CALL_POSSIBLE_RETURN) {
    // Compare target guest address with our function's return address.
    ldr(w0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    cmp(target_w, w0);
    b(EQ, epilog_label());
  }
  if (backend_->speed_profile_enabled()) {
    EmitAtomicIncrement64(backend_->speed_profile_indirect_guest_calls());
  }

  // Load host code address from indirection table.
  if (code_cache_->has_indirection_table()) {
    mov(x0,A64CodeCache::execute_address_high());
    mov(w16, target_w);  // w16 = guest address (also used by resolve thunk)
    orr(x16, x16,x0);
    ldr(w9, ptr(x16, static_cast<uint32_t>(
                         0)));  // w9 = host code from indirection table
    orr(x9, x9,x0);
  } else {
    // Fallback: resolve at runtime.
    mov(w16, target_w);
    mov(x0, x20);  // context
    mov(x1, x16);  // guest address
    mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
    blr(x9);
    mov(x9, x0);  // resolved address
  }

  if (instr->flags & hir::CALL_TAIL) {
    // Tail call: pass our return address to the callee.
    MaybeEmitBodyTimeProfileEnd();
    PopStackpoint();
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
    if (stack_size() <= 4095) {
      add(sp, sp, static_cast<uint32_t>(stack_size()));
    } else {
      mov(x17, static_cast<uint64_t>(stack_size()));
      add(sp, sp, x17, UXTX);
    }
    br(x9);
  } else {
    // Regular call: pass the next call's return address.
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
    blr(x9);
    synchronize_stack_on_next_instruction_ = true;
  }
}

void A64Emitter::CallExtern(const hir::Instr* instr, const Function* function) {
  ForgetFpcrMode();
  if (backend_->speed_profile_enabled()) {
    EmitAtomicIncrement64(backend_->speed_profile_extern_calls());
  }
  bool undefined = true;
  if (function->behavior() == Function::Behavior::kBuiltin) {
    auto builtin_function = static_cast<const BuiltinFunction*>(function);
    if (builtin_function->handler()) {
      undefined = false;
      // GuestToHostThunk: x0=target, x1=arg0, x2=arg1
      // Thunk rearranges to: x0=context, x1=arg0, x2=arg1, calls target
      mov(x0, reinterpret_cast<uint64_t>(builtin_function->handler()));
      mov(x1, reinterpret_cast<uint64_t>(builtin_function->arg0()));
      mov(x2, reinterpret_cast<uint64_t>(builtin_function->arg1()));
      mov(x9, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
      blr(x9);
    }
  } else if (function->behavior() == Function::Behavior::kExtern) {
    auto extern_function = static_cast<const GuestFunction*>(function);
    if (TryEmitKernelHighFrequencyExternCall(instr, extern_function)) {
      return;
    }
    if (extern_function->extern_handler()) {
      undefined = false;
      // GuestToHostThunk: x0=target, x1=arg0
      EmitKernelExternHostCall(extern_function);
    }
  }
  if (undefined) {
    // Set arg0 = function pointer, then call UndefinedCallExtern via thunk.
    mov(x1, reinterpret_cast<uint64_t>(function));
    CallNativeSafe(reinterpret_cast<void*>(&UndefinedCallExtern));
  }
}

void A64Emitter::CallNative(void* fn) { CallNativeSafe(fn); }

void A64Emitter::CallNativeSafe(void* fn) {
  // GuestToHostThunk: x0=target function, x1/x2=args (set by caller).
  // The thunk rearranges: saves x0 in x9, sets x0=context, calls x9.
  mov(x0, reinterpret_cast<uint64_t>(fn));
  mov(x9, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
  blr(x9);
}

void A64Emitter::SetReturnAddress(uint64_t value) {
  mov(x0, value);
  str(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
}

void A64Emitter::ReloadMembase() {
  // Reload x21 from context->virtual_membase.
  ldr(x21, ptr(x20, static_cast<int32_t>(
                        offsetof(ppc::PPCContext, virtual_membase))));
}

bool A64Emitter::ChangeFpcrMode(FPCRMode new_mode, bool already_set) {
  if (fpcr_mode_ == new_mode) {
    return false;
  }
  fpcr_mode_ = new_mode;
  if (!already_set) {
    // Load the pre-computed FPCR value from the backend context.
    // This avoids an expensive MRS + read-modify-write cycle.
    auto bctx = GetBackendCtxReg();
    if (new_mode == FPCRMode::Vmx) {
      ldr(w0, Xbyak_aarch64::ptr(bctx, static_cast<uint32_t>(offsetof(
                                           A64BackendContext, fpcr_vmx))));
    } else {
      ldr(w0, Xbyak_aarch64::ptr(bctx, static_cast<uint32_t>(offsetof(
                                           A64BackendContext, fpcr_fpu))));
    }
    msr(3, 3, 4, 4, 0, x0);  // msr FPCR, x0
  }
  return true;
}

Label& A64Emitter::AddToTail(TailEmitCallback callback, uint32_t alignment) {
  TailEmitter tail;
  tail.alignment = alignment;
  tail.func = std::move(callback);
  tail_code_.push_back(std::move(tail));
  return tail_code_.back().label;
}

Label& A64Emitter::NewCachedLabel() {
  auto* label = new Label();
  label_cache_.push_back(label);
  return *label;
}

Label& A64Emitter::GetLabel(uint32_t label_id) {
  auto it = label_map_.find(label_id);
  if (it != label_map_.end()) {
    return *it->second;
  }
  auto* label = new Label();
  label_map_[label_id] = label;
  return *label;
}

void A64Emitter::HandleStackpointOverflowError(ppc::PPCContext* context) {
  if (debugging::IsDebuggerAttached()) {
    debugging::Break();
  }
  xe::FatalError(
      "Overflowed stackpoints! Please report this error for this title to "
      "Xenia developers.");
}

void A64Emitter::PushStackpoint() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  // x8 = stackpoints array, w9 = current depth
  ldr(x8, ptr(x19,
              static_cast<uint32_t>(offsetof(A64BackendContext, stackpoints))));
  ldr(w9, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));

  // Compute offset into array: x10 = w9 * sizeof(A64BackendStackpoint)
  mov(w10, static_cast<uint32_t>(sizeof(A64BackendStackpoint)));
  umull(x10, w9, w10);
  add(x8, x8, x10);

  // Store host SP.
  mov(x10, sp);
  str(x10, ptr(x8, static_cast<uint32_t>(
                       offsetof(A64BackendStackpoint, host_stack_))));
  // Store guest r1 (32-bit).
  ldr(w10, ptr(x20, static_cast<int32_t>(offsetof(ppc::PPCContext, r[1]))));
  str(w10, ptr(x8, static_cast<uint32_t>(
                       offsetof(A64BackendStackpoint, guest_stack_))));
  // Store guest LR (32-bit).
  ldr(w10, ptr(x20, static_cast<int32_t>(offsetof(ppc::PPCContext, lr))));
  str(w10, ptr(x8, static_cast<uint32_t>(
                       offsetof(A64BackendStackpoint, guest_return_address_))));

  // Increment depth.
  add(w9, w9, 1);
  str(w9, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));

  // Check for overflow.
  mov(w10, static_cast<uint32_t>(cvars::a64_max_stackpoints));
  cmp(w9, w10);
  auto& overflow_label = AddToTail([](A64Emitter& e, Label& lbl) {
    e.CallNativeSafe(
        reinterpret_cast<void*>(A64Emitter::HandleStackpointOverflowError));
  });
  b(GE, overflow_label);
}

void A64Emitter::PopStackpoint() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  // Decrement current_stackpoint_depth.
  ldr(w8, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));
  sub(w8, w8, 1);
  str(w8, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));
}

void A64Emitter::EnsureSynchronizedGuestAndHostStack() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  // Compare current stackpoint depth against the value saved after
  // PushStackpoint in the prolog. If different, a longjmp occurred and
  // some frames' PopStackpoint never ran.
  auto& return_from_sync = NewCachedLabel();

  ldr(w17, ptr(x19, static_cast<uint32_t>(offsetof(A64BackendContext,
                                                   current_stackpoint_depth))));
  ldr(w16, ptr(sp, static_cast<uint32_t>(
                       StackLayout::GUEST_SAVED_STACKPOINT_DEPTH)));
  cmp(w17, w16);

  auto& sync_label = AddToTail([&return_from_sync](A64Emitter& e, Label& lbl) {
    // Set up arguments for the sync helper:
    //   x8 = return address (where to resume after fixup)
    //   x9 = this function's stack size
    e.adr(e.x8, return_from_sync);
    e.mov(e.x9, static_cast<uint64_t>(e.stack_size()));
    e.mov(e.x10, reinterpret_cast<uint64_t>(
                     e.backend()->synchronize_guest_and_host_stack_helper()));
    e.br(e.x10);
  });
  b(NE, sync_label);

  L(return_from_sync);
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
