/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/context_promotion_pass.h"

#include <array>
#include <cstddef>
#include <unordered_map>

#include "xenia/apu/apu_flags.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"

DECLARE_bool(debug);

DEFINE_bool(store_all_context_values, false,
            "Don't strip dead context stores to aid in debugging.", "CPU");
DEFINE_bool(arm64_context_promotion_gpr_local_slots, false,
            "Thor ARM64 research: promote dominated first loads of selected "
            "whole PPC GPR context slots through HIR locals before register "
            "allocation. Default-off experiment.",
            "CPU");
DEFINE_uint32(arm64_context_promotion_gpr_local_slots_function, 0,
              "Optional guest function start address filter for "
              "arm64_context_promotion_gpr_local_slots. 0 applies globally.",
              "CPU");
DEFINE_bool(arm64_context_promotion_gpr_local_slots_audit, false,
            "Thor ARM64 research: log promotion counts for "
            "arm64_context_promotion_gpr_local_slots. Requires the local-slot "
            "experiment to be enabled.",
            "CPU");
DEFINE_bool(arm64_context_promotion_gpr_livein_r1, false,
            "Thor ARM64 research: use a guarded pre-RA live-in local carrier "
            "for PPC r[1] in selected functions. Default-off experiment.",
            "CPU");
DEFINE_uint32(arm64_context_promotion_gpr_livein_r1_function, 0,
              "Optional guest function start address filter for "
              "arm64_context_promotion_gpr_livein_r1. 0 applies globally.",
              "CPU");
DEFINE_bool(arm64_context_promotion_gpr_livein_r1_preserve_barrier, true,
            "Thor ARM64 research: preserve clean r[1] availability across "
            "HIR context_barrier instructions in the live-in r1 probe.",
            "CPU");
DEFINE_bool(arm64_context_promotion_gpr_livein_r1_audit, false,
            "Thor ARM64 research: log attempted/replaced/skipped counters for "
            "arm64_context_promotion_gpr_livein_r1.",
            "CPU");
DEFINE_bool(arm64_guest_state_register_cache_audit, false,
            "Thor ARM64 research: count clean INT64 r[1]/r[11] guest-state "
            "register-cache opportunities without changing generated code. "
            "Default-off audit only.",
            "CPU");
DEFINE_uint32(arm64_guest_state_register_cache_audit_function, 0,
              "Optional guest function start address filter for "
              "arm64_guest_state_register_cache_audit. 0 applies globally.",
              "CPU");
DEFINE_bool(arm64_guest_state_register_cache_residual_audit, false,
            "Thor ARM64 research: count remaining clean INT64 r[1]/r[11] "
            "guest-state register-cache opportunities after normal "
            "ContextPromotionPass and DSE, without changing generated code. "
            "Default-off audit only.",
            "CPU");
DEFINE_uint32(
    arm64_guest_state_register_cache_residual_audit_function, 0,
    "Optional guest function start address filter for "
    "arm64_guest_state_register_cache_residual_audit. 0 applies globally.",
    "CPU");
DEFINE_bool(arm64_guest_state_nonclosed_cache_audit, false,
            "Thor ARM64 research: count remaining post-promotion "
            "guest-state register-cache opportunities for non-closed PPC "
            "GPRs r31/r30/r29/r28/r27, without changing generated code. "
            "Default-off audit only.",
            "CPU");
DEFINE_uint32(
    arm64_guest_state_nonclosed_cache_audit_function, 0,
    "Optional guest function start address filter for "
    "arm64_guest_state_nonclosed_cache_audit. 0 applies globally.",
    "CPU");

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Block;
using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using xe::cpu::hir::Value;

namespace {

constexpr size_t kPromotedGprOffsets[] = {
    offsetof(ppc::PPCContext, r) + 1 * sizeof(uint64_t),
    offsetof(ppc::PPCContext, r) + 11 * sizeof(uint64_t),
};
constexpr size_t kPromotedGprSize = sizeof(uint64_t);
constexpr uint32_t kNonClosedGuestStateCacheAuditSlots[] = {31, 30, 29,
                                                            28, 27};
constexpr size_t kNonClosedGuestStateCacheAuditSlotCount =
    sizeof(kNonClosedGuestStateCacheAuditSlots) /
    sizeof(kNonClosedGuestStateCacheAuditSlots[0]);
constexpr size_t kNonClosedGuestStateCacheAuditOffsets[] = {
    offsetof(ppc::PPCContext, r) + 31 * sizeof(uint64_t),
    offsetof(ppc::PPCContext, r) + 30 * sizeof(uint64_t),
    offsetof(ppc::PPCContext, r) + 29 * sizeof(uint64_t),
    offsetof(ppc::PPCContext, r) + 28 * sizeof(uint64_t),
    offsetof(ppc::PPCContext, r) + 27 * sizeof(uint64_t),
};

struct GprLocalSlotValue {
  Value* value = nullptr;
  bool dirty = false;
};

struct GprLocalSlotBlockState {
  std::array<Value*, 2> values = {};
};

struct GprLocalSlotPromotionStats {
  uint32_t blocks = 0;
  uint32_t dominated_blocks = 0;
  uint32_t pred_state_hits = 0;
  uint32_t pred_values_seeded = 0;
  uint32_t promoted_loads_seen = 0;
  uint32_t promoted_stores_seen = 0;
  uint32_t loads_seeded_from_context = 0;
  uint32_t loads_replaced = 0;
  uint32_t local_loads_inserted = 0;
  uint32_t local_stores_inserted = 0;
  uint32_t stores_tracked = 0;
  uint32_t volatile_resets = 0;
  uint32_t overlap_resets = 0;
  uint32_t stores_skipped_no_tail = 0;
  std::array<uint32_t, 2> loads_seen_by_slot = {};
  std::array<uint32_t, 2> stores_seen_by_slot = {};
  std::array<uint32_t, 2> loads_seeded_by_slot = {};
  std::array<uint32_t, 2> loads_replaced_by_slot = {};
  std::array<uint32_t, 2> local_loads_by_slot = {};
  std::array<uint32_t, 2> local_stores_by_slot = {};
  std::array<uint32_t, 2> stores_tracked_by_slot = {};
  std::array<uint32_t, 2> overlap_resets_by_slot = {};
};

enum class GuestStateRegisterCacheAuditMissReason {
  kEntry,
  kMultiPred,
  kCall,
  kHelper,
  kBranch,
  kLabel,
  kReturn,
  kTrap,
  kExternalVisibility,
  kOverlap,
};

struct GuestStateRegisterCacheAuditSlotState {
  bool known = false;
  bool dirty = false;
  GuestStateRegisterCacheAuditMissReason miss_reason =
      GuestStateRegisterCacheAuditMissReason::kEntry;
};

struct GuestStateRegisterCacheAuditStats {
  uint32_t function_address = 0;
  uint64_t blocks = 0;
  uint64_t labeled_blocks = 0;
  uint64_t multi_pred_blocks = 0;
  uint64_t candidate_loads = 0;
  uint64_t candidate_stores = 0;
  uint64_t clean_hits_possible = 0;
  uint64_t dirty_hits_possible = 0;
  uint64_t miss_no_entry = 0;
  uint64_t miss_multi_pred = 0;
  uint64_t miss_after_call = 0;
  uint64_t miss_after_helper = 0;
  uint64_t miss_after_branch = 0;
  uint64_t miss_after_label = 0;
  uint64_t miss_after_return = 0;
  uint64_t miss_after_trap = 0;
  uint64_t miss_external_visibility = 0;
  uint64_t miss_overlap = 0;
  uint64_t miss_volatile = 0;
  uint64_t flush_call = 0;
  uint64_t flush_helper = 0;
  uint64_t flush_branch = 0;
  uint64_t flush_label = 0;
  uint64_t flush_return = 0;
  uint64_t flush_trap = 0;
  uint64_t flush_external_visibility = 0;
  uint64_t estimated_spill_pressure = 0;
  uint64_t normal_fallback = 0;
  std::array<uint64_t, 2> candidate_loads_by_slot = {};
  std::array<uint64_t, 2> candidate_stores_by_slot = {};
  std::array<uint64_t, 2> clean_hits_by_slot = {};
  std::array<uint64_t, 2> dirty_hits_by_slot = {};
  std::array<uint64_t, 2> fallback_by_slot = {};
};

int GetPromotedGprIndex(size_t offset, TypeName type) {
  if (type != INT64_TYPE) {
    return -1;
  }
  for (size_t n = 0; n < sizeof(kPromotedGprOffsets) /
                             sizeof(kPromotedGprOffsets[0]);
       ++n) {
    if (offset == kPromotedGprOffsets[n]) {
      return static_cast<int>(n);
    }
  }
  return -1;
}

uint64_t CountLiveGuestStateRegisterCacheAuditSlots(
    const std::array<GuestStateRegisterCacheAuditSlotState, 2>& current) {
  uint64_t live = 0;
  for (const auto& slot : current) {
    if (slot.known) {
      ++live;
    }
  }
  return live;
}

void UpdateGuestStateRegisterCacheAuditSpillPressure(
    GuestStateRegisterCacheAuditStats* stats,
    const std::array<GuestStateRegisterCacheAuditSlotState, 2>& current) {
  uint64_t live = CountLiveGuestStateRegisterCacheAuditSlots(current);
  if (live > stats->estimated_spill_pressure) {
    stats->estimated_spill_pressure = live;
  }
}

void CountGuestStateRegisterCacheAuditMiss(
    GuestStateRegisterCacheAuditStats* stats,
    GuestStateRegisterCacheAuditMissReason reason) {
  switch (reason) {
    case GuestStateRegisterCacheAuditMissReason::kMultiPred:
      ++stats->miss_multi_pred;
      break;
    case GuestStateRegisterCacheAuditMissReason::kCall:
      ++stats->miss_after_call;
      break;
    case GuestStateRegisterCacheAuditMissReason::kHelper:
      ++stats->miss_after_helper;
      ++stats->miss_volatile;
      break;
    case GuestStateRegisterCacheAuditMissReason::kBranch:
      ++stats->miss_after_branch;
      break;
    case GuestStateRegisterCacheAuditMissReason::kLabel:
      ++stats->miss_after_label;
      break;
    case GuestStateRegisterCacheAuditMissReason::kReturn:
      ++stats->miss_after_return;
      break;
    case GuestStateRegisterCacheAuditMissReason::kTrap:
      ++stats->miss_after_trap;
      break;
    case GuestStateRegisterCacheAuditMissReason::kExternalVisibility:
      ++stats->miss_external_visibility;
      break;
    case GuestStateRegisterCacheAuditMissReason::kOverlap:
      ++stats->miss_overlap;
      break;
    case GuestStateRegisterCacheAuditMissReason::kEntry:
    default:
      ++stats->miss_no_entry;
      break;
  }
}

void FlushGuestStateRegisterCacheAuditState(
    GuestStateRegisterCacheAuditStats* stats,
    std::array<GuestStateRegisterCacheAuditSlotState, 2>* current,
    GuestStateRegisterCacheAuditMissReason reason) {
  uint64_t live = CountLiveGuestStateRegisterCacheAuditSlots(*current);
  switch (reason) {
    case GuestStateRegisterCacheAuditMissReason::kCall:
      stats->flush_call += live;
      break;
    case GuestStateRegisterCacheAuditMissReason::kHelper:
      stats->flush_helper += live;
      break;
    case GuestStateRegisterCacheAuditMissReason::kBranch:
      stats->flush_branch += live;
      break;
    case GuestStateRegisterCacheAuditMissReason::kLabel:
      stats->flush_label += live;
      break;
    case GuestStateRegisterCacheAuditMissReason::kReturn:
      stats->flush_return += live;
      break;
    case GuestStateRegisterCacheAuditMissReason::kTrap:
      stats->flush_trap += live;
      break;
    case GuestStateRegisterCacheAuditMissReason::kExternalVisibility:
    case GuestStateRegisterCacheAuditMissReason::kOverlap:
      stats->flush_external_visibility += live;
      break;
    case GuestStateRegisterCacheAuditMissReason::kEntry:
    case GuestStateRegisterCacheAuditMissReason::kMultiPred:
    default:
      break;
  }
  for (auto& slot : *current) {
    slot = {};
    slot.miss_reason = reason;
  }
}

bool IsGuestStateRegisterCacheAuditCall(Instr* instr) {
  return instr->opcode == &OPCODE_CALL_info ||
         instr->opcode == &OPCODE_CALL_TRUE_info ||
         instr->opcode == &OPCODE_CALL_INDIRECT_info ||
         instr->opcode == &OPCODE_CALL_INDIRECT_TRUE_info;
}

bool IsGuestStateRegisterCacheAuditHelper(Instr* instr) {
  return instr->opcode == &OPCODE_CALL_EXTERN_info;
}

bool IsGuestStateRegisterCacheAuditReturn(Instr* instr) {
  return instr->opcode == &OPCODE_RETURN_info ||
         instr->opcode == &OPCODE_RETURN_TRUE_info ||
         instr->opcode == &OPCODE_DEBUG_BREAK_info ||
         instr->opcode == &OPCODE_DEBUG_BREAK_TRUE_info;
}

bool IsGuestStateRegisterCacheAuditTrap(Instr* instr) {
  return instr->opcode == &OPCODE_TRAP_info ||
         instr->opcode == &OPCODE_TRAP_TRUE_info;
}

bool RangesOverlap(size_t a_offset, size_t a_size, size_t b_offset,
                   size_t b_size) {
  return a_offset < b_offset + b_size && b_offset < a_offset + a_size;
}

enum class NonClosedGuestStateCacheAuditMissReason {
  kEntry,
  kMultiPred,
  kLabel,
  kCall,
  kHelper,
  kBranch,
  kReturn,
  kTrap,
  kExternalVisibility,
  kOverlap,
  kVolatile,
};

struct NonClosedGuestStateCacheAuditSlotState {
  bool known = false;
  bool dirty = false;
  NonClosedGuestStateCacheAuditMissReason miss_reason =
      NonClosedGuestStateCacheAuditMissReason::kEntry;
};

struct NonClosedGuestStateCacheAuditStats {
  uint32_t function_address = 0;
  uint64_t blocks = 0;
  uint64_t labeled_blocks = 0;
  uint64_t multi_pred_blocks = 0;
  uint64_t candidate_loads = 0;
  uint64_t candidate_stores = 0;
  uint64_t clean_hits_possible = 0;
  uint64_t dirty_hits_possible = 0;
  uint64_t miss_no_entry = 0;
  uint64_t miss_multi_pred = 0;
  uint64_t miss_after_label = 0;
  uint64_t miss_after_call = 0;
  uint64_t miss_after_helper = 0;
  uint64_t miss_after_branch = 0;
  uint64_t miss_after_return = 0;
  uint64_t miss_after_trap = 0;
  uint64_t miss_external_visibility = 0;
  uint64_t miss_overlap = 0;
  uint64_t miss_volatile = 0;
  uint64_t flush_multi_pred = 0;
  uint64_t flush_label = 0;
  uint64_t flush_call = 0;
  uint64_t flush_helper = 0;
  uint64_t flush_branch = 0;
  uint64_t flush_return = 0;
  uint64_t flush_trap = 0;
  uint64_t flush_external_visibility = 0;
  uint64_t flush_overlap = 0;
  uint64_t flush_volatile = 0;
  uint64_t peak_live_slots = 0;
  uint64_t estimated_spill_pressure = 0;
  uint64_t normal_fallback = 0;
  std::array<uint64_t, kNonClosedGuestStateCacheAuditSlotCount>
      candidate_loads_by_slot = {};
  std::array<uint64_t, kNonClosedGuestStateCacheAuditSlotCount>
      candidate_stores_by_slot = {};
  std::array<uint64_t, kNonClosedGuestStateCacheAuditSlotCount>
      clean_hits_by_slot = {};
  std::array<uint64_t, kNonClosedGuestStateCacheAuditSlotCount>
      dirty_hits_by_slot = {};
  std::array<uint64_t, kNonClosedGuestStateCacheAuditSlotCount>
      fallback_by_slot = {};
};

int GetNonClosedGuestStateCacheAuditIndex(size_t offset, TypeName type) {
  if (type != INT64_TYPE) {
    return -1;
  }
  for (size_t n = 0; n < kNonClosedGuestStateCacheAuditSlotCount; ++n) {
    if (offset == kNonClosedGuestStateCacheAuditOffsets[n]) {
      return static_cast<int>(n);
    }
  }
  return -1;
}

uint64_t CountLiveNonClosedGuestStateCacheAuditSlots(
    const std::array<NonClosedGuestStateCacheAuditSlotState,
                     kNonClosedGuestStateCacheAuditSlotCount>& current) {
  uint64_t live = 0;
  for (const auto& slot : current) {
    if (slot.known) {
      ++live;
    }
  }
  return live;
}

void UpdateNonClosedGuestStateCacheAuditSpillPressure(
    NonClosedGuestStateCacheAuditStats* stats,
    const std::array<NonClosedGuestStateCacheAuditSlotState,
                     kNonClosedGuestStateCacheAuditSlotCount>& current) {
  uint64_t live = CountLiveNonClosedGuestStateCacheAuditSlots(current);
  if (live > stats->peak_live_slots) {
    stats->peak_live_slots = live;
  }

  // A conservative signal for this lane: after two cached GPRs, host GPR
  // pressure is likely to compete with address, context, and scratch values.
  uint64_t pressure = live > 2 ? live - 2 : 0;
  if (pressure > stats->estimated_spill_pressure) {
    stats->estimated_spill_pressure = pressure;
  }
}

void CountNonClosedGuestStateCacheAuditMiss(
    NonClosedGuestStateCacheAuditStats* stats,
    NonClosedGuestStateCacheAuditMissReason reason) {
  switch (reason) {
    case NonClosedGuestStateCacheAuditMissReason::kMultiPred:
      ++stats->miss_multi_pred;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kLabel:
      ++stats->miss_after_label;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kCall:
      ++stats->miss_after_call;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kHelper:
      ++stats->miss_after_helper;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kBranch:
      ++stats->miss_after_branch;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kReturn:
      ++stats->miss_after_return;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kTrap:
      ++stats->miss_after_trap;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kExternalVisibility:
      ++stats->miss_external_visibility;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kOverlap:
      ++stats->miss_overlap;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kVolatile:
      ++stats->miss_volatile;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kEntry:
    default:
      ++stats->miss_no_entry;
      break;
  }
}

void FlushNonClosedGuestStateCacheAuditState(
    NonClosedGuestStateCacheAuditStats* stats,
    std::array<NonClosedGuestStateCacheAuditSlotState,
               kNonClosedGuestStateCacheAuditSlotCount>* current,
    NonClosedGuestStateCacheAuditMissReason reason) {
  uint64_t live = CountLiveNonClosedGuestStateCacheAuditSlots(*current);
  switch (reason) {
    case NonClosedGuestStateCacheAuditMissReason::kMultiPred:
      stats->flush_multi_pred += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kLabel:
      stats->flush_label += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kCall:
      stats->flush_call += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kHelper:
      stats->flush_helper += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kBranch:
      stats->flush_branch += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kReturn:
      stats->flush_return += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kTrap:
      stats->flush_trap += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kExternalVisibility:
      stats->flush_external_visibility += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kOverlap:
      stats->flush_overlap += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kVolatile:
      stats->flush_volatile += live;
      break;
    case NonClosedGuestStateCacheAuditMissReason::kEntry:
    default:
      break;
  }
  for (auto& slot : *current) {
    slot = {};
    slot.miss_reason = reason;
  }
}

Block* GetSingleDominatingPredecessor(Block* block) {
  auto edge = block->incoming_edge_head;
  if (!edge || edge->incoming_next || !(edge->flags & Edge::DOMINATES)) {
    return nullptr;
  }
  return edge->src;
}

Instr* FirstTailBranch(Block* block) {
  Instr* first_tail_branch = nullptr;
  Instr* instr = block->instr_tail;
  while (instr && (instr->opcode->flags & OPCODE_FLAG_BRANCH)) {
    first_tail_branch = instr;
    instr = instr->prev;
  }
  return first_tail_branch;
}

uint32_t FindFirstSourceOffset(HIRBuilder* builder) {
  for (auto block = builder->first_block(); block; block = block->next) {
    for (auto instr = block->instr_head; instr; instr = instr->next) {
      if (instr->opcode == &OPCODE_SOURCE_OFFSET_info) {
        return static_cast<uint32_t>(instr->src1.offset);
      }
    }
  }
  return 0;
}

bool HasLiveGprLocalValue(const std::array<GprLocalSlotValue, 2>& current) {
  for (const auto& slot : current) {
    if (slot.value) {
      return true;
    }
  }
  return false;
}

uint32_t CountDirtyGprLocalValues(
    const std::array<GprLocalSlotValue, 2>& current) {
  uint32_t count = 0;
  for (const auto& slot : current) {
    if (slot.value && slot.dirty) {
      ++count;
    }
  }
  return count;
}

constexpr size_t kR1ContextOffset =
    offsetof(ppc::PPCContext, r) + 1 * sizeof(uint64_t);

struct GprLiveInR1Availability {
  bool clean = false;
  bool needs_entry_local = false;
};

enum class GprLiveInR1DirtyReason {
  kEntry,
  kCall,
  kBarrier,
  kAlias,
  kExit,
};

struct GprLiveInR1RewriteState {
  Value* value = nullptr;
  bool clean = false;
  bool dirty = false;
  GprLiveInR1DirtyReason dirty_reason = GprLiveInR1DirtyReason::kEntry;
};

struct GprLiveInR1Stats {
  uint32_t function_address = 0;
  uint32_t blocks = 0;
  uint32_t target_loads_seen = 0;
  uint32_t target_stores_seen = 0;
  uint32_t target_alias_stores_seen = 0;
  uint32_t entry_clean_blocks = 0;
  uint32_t needs_entry_local_blocks = 0;
  uint32_t loads_attempted = 0;
  uint32_t loads_replaced = 0;
  uint32_t loads_seeded_from_context = 0;
  uint32_t local_loads_inserted = 0;
  uint32_t local_stores_inserted = 0;
  uint32_t branch_stores_inserted = 0;
  uint32_t final_stores_inserted = 0;
  uint32_t skipped_dirty_entry = 0;
  uint32_t skipped_after_call = 0;
  uint32_t skipped_after_barrier = 0;
  uint32_t skipped_after_alias = 0;
  uint32_t skipped_after_exit = 0;
  uint32_t skipped_no_value_for_store = 0;
  uint32_t call_resets = 0;
  uint32_t barrier_resets = 0;
  uint32_t alias_resets = 0;
  uint32_t exit_resets = 0;
  uint32_t conditional_branch_preserves = 0;
};

bool IsTargetR1Load(Instr* instr) {
  return instr->opcode == &OPCODE_LOAD_CONTEXT_info &&
         instr->src1.offset == kR1ContextOffset && instr->dest &&
         instr->dest->type == INT64_TYPE;
}

bool IsTargetR1Store(Instr* instr) {
  return instr->opcode == &OPCODE_STORE_CONTEXT_info &&
         instr->src1.offset == kR1ContextOffset && instr->src2.value &&
         instr->src2.value->type == INT64_TYPE;
}

bool IsTargetR1OverlapStore(Instr* instr) {
  if (instr->opcode != &OPCODE_STORE_CONTEXT_info || !instr->src2.value) {
    return false;
  }
  size_t size = GetTypeSize(instr->src2.value->type);
  return RangesOverlap(instr->src1.offset, size, kR1ContextOffset,
                       sizeof(uint64_t));
}

bool IsContextStateKillingInstr(Instr* instr, bool preserve_barrier,
                                bool* killed_by_call,
                                bool* killed_by_barrier,
                                bool* killed_by_exit) {
  *killed_by_call = false;
  *killed_by_barrier = false;
  *killed_by_exit = false;
  if (instr->opcode == &OPCODE_CONTEXT_BARRIER_info) {
    if (!preserve_barrier) {
      *killed_by_barrier = true;
      return true;
    }
    return false;
  }
  if (instr->opcode == &OPCODE_CALL_info ||
      instr->opcode == &OPCODE_CALL_TRUE_info ||
      instr->opcode == &OPCODE_CALL_INDIRECT_info ||
      instr->opcode == &OPCODE_CALL_INDIRECT_TRUE_info ||
      instr->opcode == &OPCODE_CALL_EXTERN_info) {
    *killed_by_call = true;
    return true;
  }
  if (instr->opcode == &OPCODE_RETURN_info ||
      instr->opcode == &OPCODE_RETURN_TRUE_info ||
      instr->opcode == &OPCODE_TRAP_info ||
      instr->opcode == &OPCODE_TRAP_TRUE_info ||
      instr->opcode == &OPCODE_DEBUG_BREAK_info ||
      instr->opcode == &OPCODE_DEBUG_BREAK_TRUE_info) {
    *killed_by_exit = true;
    return true;
  }
  if (instr->opcode == &OPCODE_BRANCH_TRUE_info ||
      instr->opcode == &OPCODE_BRANCH_FALSE_info) {
    return false;
  }
  if (instr->opcode->flags & OPCODE_FLAG_VOLATILE) {
    *killed_by_call = true;
    return true;
  }
  return false;
}

Block* GetBranchTargetBlock(Instr* instr) {
  if (instr->opcode == &OPCODE_BRANCH_info) {
    return instr->src1.label ? instr->src1.label->block : nullptr;
  }
  if (instr->opcode == &OPCODE_BRANCH_TRUE_info ||
      instr->opcode == &OPCODE_BRANCH_FALSE_info) {
    return instr->src2.label ? instr->src2.label->block : nullptr;
  }
  return nullptr;
}

Instr* FirstTailBranchOrNull(Block* block) {
  return FirstTailBranch(block);
}

uint32_t GetEdgeKey(Block* src, Block* dest) {
  return (uint32_t(src->ordinal) << 16) | uint32_t(dest->ordinal);
}

}  // namespace

ContextPromotionPass::ContextPromotionPass() : CompilerPass() {}

ContextPromotionPass::~ContextPromotionPass() {}

bool ContextPromotionPass::Initialize(Compiler* compiler) {
  if (!CompilerPass::Initialize(compiler)) {
    return false;
  }

  // This is a terrible implementation.
  context_values_.resize(sizeof(ppc::PPCContext));
  context_validity_.resize(static_cast<uint32_t>(sizeof(ppc::PPCContext)));

  return true;
}

bool ContextPromotionPass::Run(HIRBuilder* builder) {
  // Like mem2reg, but because context memory is unaliasable it's easier to
  // check and convert LoadContext/StoreContext into value operations.
  // Example of load->value promotion:
  //   v0 = load_context +100
  //   store_context +200, v0
  //   v1 = load_context +100  <-- replace with v1 = v0
  //   store_context +200, v1
  //
  // It'd be possible in this stage to also remove redundant context stores:
  // Example of dead store elimination:
  //   store_context +100, v0  <-- removed due to following store
  //   store_context +100, v1
  // This is more generally done by DSE, however if it could be done here
  // instead as it may be faster (at least on the block-level).

  // Promote loads to values.
  // Process each block independently, for now.
  if (cvars::arm64_guest_state_register_cache_audit &&
      ShouldRunGuestStateRegisterCacheAudit(
          builder, cvars::arm64_guest_state_register_cache_audit_function)) {
    AuditGuestStateRegisterCache(builder, false);
  }

  auto block = builder->first_block();
  while (block) {
    PromoteBlock(block);
    block = block->next;
  }

  if (cvars::arm64_context_promotion_gpr_local_slots &&
      ShouldRunGprLocalSlotPromotion(builder)) {
    PromoteDominatedGprLocalSlots(builder);
  }
  if (cvars::arm64_context_promotion_gpr_livein_r1 &&
      ShouldRunGprLiveInR1Promotion(builder)) {
    PromoteGprLiveInR1(builder);
  }

  // Remove all dead stores.
  // This will break debugging as we can't recover this information when
  // trying to extract stack traces/register values, so we don't do that.
  if (!cvars::debug && !cvars::store_all_context_values) {
    block = builder->first_block();
    while (block) {
      RemoveDeadStoresBlock(block);
      block = block->next;
    }
  }

  if (cvars::arm64_guest_state_register_cache_residual_audit &&
      ShouldRunGuestStateRegisterCacheAudit(
          builder,
          cvars::arm64_guest_state_register_cache_residual_audit_function)) {
    AuditGuestStateRegisterCache(builder, true);
  }
  if (cvars::arm64_guest_state_nonclosed_cache_audit &&
      ShouldRunGuestStateRegisterCacheAudit(
          builder, cvars::arm64_guest_state_nonclosed_cache_audit_function)) {
    AuditGuestStateNonClosedCache(builder);
  }

  return true;
}

bool ContextPromotionPass::ShouldRunGuestStateRegisterCacheAudit(
    HIRBuilder* builder, uint32_t function_filter) const {
  return !function_filter || FindFirstSourceOffset(builder) == function_filter;
}

void ContextPromotionPass::AuditGuestStateRegisterCache(HIRBuilder* builder,
                                                        bool residual_phase) {
  GuestStateRegisterCacheAuditStats stats;
  stats.function_address = FindFirstSourceOffset(builder);

  for (auto block = builder->first_block(); block; block = block->next) {
    ++stats.blocks;
    std::array<GuestStateRegisterCacheAuditSlotState, 2> current = {};
    GuestStateRegisterCacheAuditMissReason entry_reason =
        GuestStateRegisterCacheAuditMissReason::kEntry;
    if (block->label_head && block->incoming_edge_head) {
      ++stats.labeled_blocks;
      ++stats.flush_label;
      entry_reason = GuestStateRegisterCacheAuditMissReason::kLabel;
    }
    if (block->incoming_edge_head && block->incoming_edge_head->incoming_next) {
      ++stats.multi_pred_blocks;
      entry_reason = GuestStateRegisterCacheAuditMissReason::kMultiPred;
    }
    for (auto& slot : current) {
      slot.miss_reason = entry_reason;
    }

    for (Instr* instr = block->instr_head; instr; instr = instr->next) {
      if (instr->opcode == &OPCODE_LOAD_CONTEXT_info) {
        size_t offset = instr->src1.offset;
        TypeName type = instr->dest ? instr->dest->type : MAX_TYPENAME;
        int slot_index = GetPromotedGprIndex(offset, type);
        if (slot_index < 0) {
          continue;
        }

        ++stats.candidate_loads;
        ++stats.candidate_loads_by_slot[slot_index];
        ++stats.normal_fallback;
        ++stats.fallback_by_slot[slot_index];
        auto& slot = current[slot_index];
        if (slot.known) {
          if (slot.dirty) {
            ++stats.dirty_hits_possible;
            ++stats.dirty_hits_by_slot[slot_index];
          } else {
            ++stats.clean_hits_possible;
            ++stats.clean_hits_by_slot[slot_index];
          }
        } else {
          CountGuestStateRegisterCacheAuditMiss(&stats, slot.miss_reason);
        }
        slot.known = true;
        slot.dirty = false;
        slot.miss_reason = GuestStateRegisterCacheAuditMissReason::kEntry;
        UpdateGuestStateRegisterCacheAuditSpillPressure(&stats, current);
        continue;
      }

      if (instr->opcode == &OPCODE_STORE_CONTEXT_info) {
        size_t offset = instr->src1.offset;
        Value* value = instr->src2.value;
        TypeName type = value ? value->type : MAX_TYPENAME;
        int slot_index = value ? GetPromotedGprIndex(offset, type) : -1;
        if (slot_index >= 0) {
          ++stats.candidate_stores;
          ++stats.candidate_stores_by_slot[slot_index];
          ++stats.normal_fallback;
          ++stats.fallback_by_slot[slot_index];
          current[slot_index].known = true;
          current[slot_index].dirty = true;
          current[slot_index].miss_reason =
              GuestStateRegisterCacheAuditMissReason::kEntry;
          UpdateGuestStateRegisterCacheAuditSpillPressure(&stats, current);
          continue;
        }

        size_t size = value ? GetTypeSize(value->type) : 1;
        for (size_t n = 0; n < current.size(); ++n) {
          if (RangesOverlap(offset, size, kPromotedGprOffsets[n],
                            kPromotedGprSize) &&
              current[n].known) {
            ++stats.flush_external_visibility;
            current[n] = {};
            current[n].miss_reason =
                GuestStateRegisterCacheAuditMissReason::kOverlap;
          }
        }
        continue;
      }

      if (instr->opcode == &OPCODE_CONTEXT_BARRIER_info) {
        FlushGuestStateRegisterCacheAuditState(
            &stats, &current,
            GuestStateRegisterCacheAuditMissReason::kExternalVisibility);
        continue;
      }

      if (IsGuestStateRegisterCacheAuditCall(instr)) {
        FlushGuestStateRegisterCacheAuditState(
            &stats, &current, GuestStateRegisterCacheAuditMissReason::kCall);
        continue;
      }

      if (IsGuestStateRegisterCacheAuditHelper(instr)) {
        FlushGuestStateRegisterCacheAuditState(
            &stats, &current, GuestStateRegisterCacheAuditMissReason::kHelper);
        continue;
      }

      if (IsGuestStateRegisterCacheAuditReturn(instr)) {
        FlushGuestStateRegisterCacheAuditState(
            &stats, &current, GuestStateRegisterCacheAuditMissReason::kReturn);
        continue;
      }

      if (IsGuestStateRegisterCacheAuditTrap(instr)) {
        FlushGuestStateRegisterCacheAuditState(
            &stats, &current, GuestStateRegisterCacheAuditMissReason::kTrap);
        continue;
      }

      if (instr->opcode->flags & OPCODE_FLAG_BRANCH) {
        FlushGuestStateRegisterCacheAuditState(
            &stats, &current, GuestStateRegisterCacheAuditMissReason::kBranch);
        continue;
      }

      if (instr->opcode->flags & OPCODE_FLAG_VOLATILE) {
        FlushGuestStateRegisterCacheAuditState(
            &stats, &current, GuestStateRegisterCacheAuditMissReason::kHelper);
      }
    }
  }

  const char* audit_name = residual_phase ? "residual audit" : "audit";
  XELOGW(
      "A64 guest-state register-cache {} fn {:08X}: blocks={} "
      "labeled_blocks={} multi_pred_blocks={} candidate_loads={} "
      "candidate_stores={} clean_hits_possible={} dirty_hits_possible={} "
      "normal_fallback={} estimated_spill_pressure={} "
      "payload_materializations_allowed=0 behavior_changed=0",
      audit_name, stats.function_address, stats.blocks, stats.labeled_blocks,
      stats.multi_pred_blocks, stats.candidate_loads, stats.candidate_stores,
      stats.clean_hits_possible, stats.dirty_hits_possible,
      stats.normal_fallback, stats.estimated_spill_pressure);
  XELOGW(
      "A64 guest-state register-cache {} fn {:08X}: "
      "miss_no_entry={} miss_multi_pred={} miss_volatile={} "
      "miss_overlap={} miss_after_call={} miss_after_helper={} "
      "miss_after_branch={} miss_after_label={} miss_after_return={} "
      "miss_after_trap={} miss_external_visibility={}",
      audit_name, stats.function_address, stats.miss_no_entry,
      stats.miss_multi_pred, stats.miss_volatile, stats.miss_overlap,
      stats.miss_after_call, stats.miss_after_helper, stats.miss_after_branch,
      stats.miss_after_label, stats.miss_after_return, stats.miss_after_trap,
      stats.miss_external_visibility);
  XELOGW(
      "A64 guest-state register-cache {} fn {:08X}: "
      "flush_call={} flush_helper={} flush_branch={} flush_label={} "
      "flush_return={} flush_trap={} flush_external_visibility={}",
      audit_name, stats.function_address, stats.flush_call, stats.flush_helper,
      stats.flush_branch, stats.flush_label, stats.flush_return,
      stats.flush_trap, stats.flush_external_visibility);
  XELOGW(
      "A64 guest-state register-cache {} fn {:08X}: "
      "r1 loads/stores/clean_hits/dirty_hits/fallback={}/{}/{}/{}/{}; "
      "r11 loads/stores/clean_hits/dirty_hits/fallback={}/{}/{}/{}/{}",
      audit_name, stats.function_address, stats.candidate_loads_by_slot[0],
      stats.candidate_stores_by_slot[0], stats.clean_hits_by_slot[0],
      stats.dirty_hits_by_slot[0], stats.fallback_by_slot[0],
      stats.candidate_loads_by_slot[1], stats.candidate_stores_by_slot[1],
      stats.clean_hits_by_slot[1], stats.dirty_hits_by_slot[1],
      stats.fallback_by_slot[1]);
}

void ContextPromotionPass::AuditGuestStateNonClosedCache(HIRBuilder* builder) {
  NonClosedGuestStateCacheAuditStats stats;
  stats.function_address = FindFirstSourceOffset(builder);

  for (auto block = builder->first_block(); block; block = block->next) {
    ++stats.blocks;
    std::array<NonClosedGuestStateCacheAuditSlotState,
               kNonClosedGuestStateCacheAuditSlotCount>
        current = {};
    NonClosedGuestStateCacheAuditMissReason entry_reason =
        NonClosedGuestStateCacheAuditMissReason::kEntry;
    if (block->label_head && block->incoming_edge_head) {
      ++stats.labeled_blocks;
      ++stats.flush_label;
      entry_reason = NonClosedGuestStateCacheAuditMissReason::kLabel;
    }
    if (block->incoming_edge_head && block->incoming_edge_head->incoming_next) {
      ++stats.multi_pred_blocks;
      ++stats.flush_multi_pred;
      entry_reason = NonClosedGuestStateCacheAuditMissReason::kMultiPred;
    }
    for (auto& slot : current) {
      slot.miss_reason = entry_reason;
    }

    for (Instr* instr = block->instr_head; instr; instr = instr->next) {
      if (instr->opcode == &OPCODE_LOAD_CONTEXT_info) {
        size_t offset = instr->src1.offset;
        TypeName type = instr->dest ? instr->dest->type : MAX_TYPENAME;
        int slot_index =
            GetNonClosedGuestStateCacheAuditIndex(offset, type);
        if (slot_index < 0) {
          continue;
        }

        ++stats.candidate_loads;
        ++stats.candidate_loads_by_slot[slot_index];
        ++stats.normal_fallback;
        ++stats.fallback_by_slot[slot_index];
        auto& slot = current[slot_index];
        if (slot.known) {
          if (slot.dirty) {
            ++stats.dirty_hits_possible;
            ++stats.dirty_hits_by_slot[slot_index];
          } else {
            ++stats.clean_hits_possible;
            ++stats.clean_hits_by_slot[slot_index];
          }
        } else {
          CountNonClosedGuestStateCacheAuditMiss(&stats, slot.miss_reason);
        }
        slot.known = true;
        slot.dirty = false;
        slot.miss_reason = NonClosedGuestStateCacheAuditMissReason::kEntry;
        UpdateNonClosedGuestStateCacheAuditSpillPressure(&stats, current);
        continue;
      }

      if (instr->opcode == &OPCODE_STORE_CONTEXT_info) {
        size_t offset = instr->src1.offset;
        Value* value = instr->src2.value;
        TypeName type = value ? value->type : MAX_TYPENAME;
        int slot_index = value
                             ? GetNonClosedGuestStateCacheAuditIndex(offset,
                                                                     type)
                             : -1;
        if (slot_index >= 0) {
          ++stats.candidate_stores;
          ++stats.candidate_stores_by_slot[slot_index];
          ++stats.normal_fallback;
          ++stats.fallback_by_slot[slot_index];
          current[slot_index].known = true;
          current[slot_index].dirty = true;
          current[slot_index].miss_reason =
              NonClosedGuestStateCacheAuditMissReason::kEntry;
          UpdateNonClosedGuestStateCacheAuditSpillPressure(&stats, current);
          continue;
        }

        size_t size = value ? GetTypeSize(value->type) : 1;
        for (size_t n = 0; n < current.size(); ++n) {
          if (RangesOverlap(offset, size,
                            kNonClosedGuestStateCacheAuditOffsets[n],
                            kPromotedGprSize) &&
              current[n].known) {
            ++stats.flush_overlap;
            current[n] = {};
            current[n].miss_reason =
                NonClosedGuestStateCacheAuditMissReason::kOverlap;
          }
        }
        continue;
      }

      if (instr->opcode == &OPCODE_CONTEXT_BARRIER_info) {
        FlushNonClosedGuestStateCacheAuditState(
            &stats, &current,
            NonClosedGuestStateCacheAuditMissReason::kExternalVisibility);
        continue;
      }

      if (IsGuestStateRegisterCacheAuditCall(instr)) {
        FlushNonClosedGuestStateCacheAuditState(
            &stats, &current,
            NonClosedGuestStateCacheAuditMissReason::kCall);
        continue;
      }

      if (IsGuestStateRegisterCacheAuditHelper(instr)) {
        FlushNonClosedGuestStateCacheAuditState(
            &stats, &current,
            NonClosedGuestStateCacheAuditMissReason::kHelper);
        continue;
      }

      if (IsGuestStateRegisterCacheAuditReturn(instr)) {
        FlushNonClosedGuestStateCacheAuditState(
            &stats, &current,
            NonClosedGuestStateCacheAuditMissReason::kReturn);
        continue;
      }

      if (IsGuestStateRegisterCacheAuditTrap(instr)) {
        FlushNonClosedGuestStateCacheAuditState(
            &stats, &current,
            NonClosedGuestStateCacheAuditMissReason::kTrap);
        continue;
      }

      if (instr->opcode->flags & OPCODE_FLAG_BRANCH) {
        FlushNonClosedGuestStateCacheAuditState(
            &stats, &current,
            NonClosedGuestStateCacheAuditMissReason::kBranch);
        continue;
      }

      if (instr->opcode->flags & OPCODE_FLAG_VOLATILE) {
        FlushNonClosedGuestStateCacheAuditState(
            &stats, &current,
            NonClosedGuestStateCacheAuditMissReason::kVolatile);
      }
    }
  }

  XELOGW(
      "A64 guest-state nonclosed-cache post-promotion audit fn {:08X}: "
      "target_slots=r31,r30,r29,r28,r27 blocks={} labeled_blocks={} "
      "multi_pred_blocks={} candidate_loads={} candidate_stores={} "
      "clean_hits_possible={} dirty_hits_possible={} normal_fallback={} "
      "peak_live_slots={} estimated_spill_pressure={} "
      "spill_pressure_model=live_slots_over_2 "
      "payload_materializations_allowed=0 store_elision_allowed=0 "
      "context_load_replacement_allowed=0 behavior_changed=0",
      stats.function_address, stats.blocks, stats.labeled_blocks,
      stats.multi_pred_blocks, stats.candidate_loads, stats.candidate_stores,
      stats.clean_hits_possible, stats.dirty_hits_possible,
      stats.normal_fallback, stats.peak_live_slots,
      stats.estimated_spill_pressure);
  XELOGW(
      "A64 guest-state nonclosed-cache post-promotion audit fn {:08X}: "
      "miss_no_entry={} miss_multi_pred={} miss_after_label={} "
      "miss_after_call={} miss_after_helper={} miss_after_branch={} "
      "miss_after_return={} miss_after_trap={} "
      "miss_external_visibility={} miss_overlap={} miss_volatile={}",
      stats.function_address, stats.miss_no_entry, stats.miss_multi_pred,
      stats.miss_after_label, stats.miss_after_call, stats.miss_after_helper,
      stats.miss_after_branch, stats.miss_after_return, stats.miss_after_trap,
      stats.miss_external_visibility, stats.miss_overlap,
      stats.miss_volatile);
  XELOGW(
      "A64 guest-state nonclosed-cache post-promotion audit fn {:08X}: "
      "flush_multi_pred={} flush_label={} flush_call={} flush_helper={} "
      "flush_branch={} flush_return={} flush_trap={} "
      "flush_external_visibility={} flush_overlap={} flush_volatile={}",
      stats.function_address, stats.flush_multi_pred, stats.flush_label,
      stats.flush_call, stats.flush_helper, stats.flush_branch,
      stats.flush_return, stats.flush_trap, stats.flush_external_visibility,
      stats.flush_overlap, stats.flush_volatile);
  XELOGW(
      "A64 guest-state nonclosed-cache post-promotion audit fn {:08X}: "
      "r31 loads/stores/clean_hits/dirty_hits/fallback={}/{}/{}/{}/{}; "
      "r30 loads/stores/clean_hits/dirty_hits/fallback={}/{}/{}/{}/{}; "
      "r29 loads/stores/clean_hits/dirty_hits/fallback={}/{}/{}/{}/{}; "
      "r28 loads/stores/clean_hits/dirty_hits/fallback={}/{}/{}/{}/{}; "
      "r27 loads/stores/clean_hits/dirty_hits/fallback={}/{}/{}/{}/{}",
      stats.function_address, stats.candidate_loads_by_slot[0],
      stats.candidate_stores_by_slot[0], stats.clean_hits_by_slot[0],
      stats.dirty_hits_by_slot[0], stats.fallback_by_slot[0],
      stats.candidate_loads_by_slot[1], stats.candidate_stores_by_slot[1],
      stats.clean_hits_by_slot[1], stats.dirty_hits_by_slot[1],
      stats.fallback_by_slot[1], stats.candidate_loads_by_slot[2],
      stats.candidate_stores_by_slot[2], stats.clean_hits_by_slot[2],
      stats.dirty_hits_by_slot[2], stats.fallback_by_slot[2],
      stats.candidate_loads_by_slot[3], stats.candidate_stores_by_slot[3],
      stats.clean_hits_by_slot[3], stats.dirty_hits_by_slot[3],
      stats.fallback_by_slot[3], stats.candidate_loads_by_slot[4],
      stats.candidate_stores_by_slot[4], stats.clean_hits_by_slot[4],
      stats.dirty_hits_by_slot[4], stats.fallback_by_slot[4]);
}

void ContextPromotionPass::PromoteBlock(Block* block) {
  auto& validity = context_validity_;
  validity.reset();

  Instr* i = block->instr_head;
  while (i) {
    auto next = i->next;
    if (i->opcode->flags & OPCODE_FLAG_VOLATILE) {
      // Volatile instruction - requires all context values be flushed.
      validity.reset();
    } else if (i->opcode == &OPCODE_LOAD_CONTEXT_info) {
      size_t offset = i->src1.offset;
      size_t size = i->dest ? GetTypeSize(i->dest->type) : 1;
      Value* previous_value = nullptr;
      if (i->dest &&
          TryGetContextValue(offset, size, i->dest->type, &previous_value)) {
        // Legit previous value, reuse.
        i->opcode = &hir::OPCODE_ASSIGN_info;
        i->set_src1(previous_value);
      } else {
        // Store the loaded value into the table.
        SetContextValueRange(offset, size, i->dest);
      }
    } else if (i->opcode == &OPCODE_STORE_CONTEXT_info) {
      size_t offset = i->src1.offset;
      Value* value = i->src2.value;
      size_t size = value ? GetTypeSize(value->type) : 1;
      // Store value into the table for later.
      SetContextValueRange(offset, size, value);
    }
    i = next;
  }
}

bool ContextPromotionPass::ShouldRunGprLocalSlotPromotion(
    HIRBuilder* builder) const {
  uint32_t function_filter =
      cvars::arm64_context_promotion_gpr_local_slots_function;
  return !function_filter || FindFirstSourceOffset(builder) == function_filter;
}

void ContextPromotionPass::PromoteDominatedGprLocalSlots(HIRBuilder* builder) {
  std::array<Value*, 2> local_slots = {};
  for (size_t n = 0; n < local_slots.size(); ++n) {
    local_slots[n] = builder->AllocLocal(INT64_TYPE);
  }

  GprLocalSlotPromotionStats stats;
  std::unordered_map<Block*, GprLocalSlotBlockState> outgoing_states;

  for (auto block = builder->first_block(); block; block = block->next) {
    ++stats.blocks;
    std::array<GprLocalSlotValue, 2> current = {};

    if (Block* pred = GetSingleDominatingPredecessor(block)) {
      ++stats.dominated_blocks;
      auto pred_state = outgoing_states.find(pred);
      if (pred_state != outgoing_states.end()) {
        ++stats.pred_state_hits;
        for (size_t n = 0; n < current.size(); ++n) {
          current[n].value = pred_state->second.values[n];
          if (current[n].value) {
            ++stats.pred_values_seeded;
          }
        }
      }
    }

    for (Instr* instr = block->instr_head; instr; instr = instr->next) {
      if (instr->opcode->flags & OPCODE_FLAG_VOLATILE) {
        if (HasLiveGprLocalValue(current)) {
          ++stats.volatile_resets;
        }
        current = {};
        continue;
      }

      if (instr->opcode == &OPCODE_LOAD_CONTEXT_info) {
        size_t offset = instr->src1.offset;
        TypeName type = instr->dest ? instr->dest->type : MAX_TYPENAME;
        int slot_index = GetPromotedGprIndex(offset, type);
        if (slot_index < 0) {
          continue;
        }

        ++stats.promoted_loads_seen;
        ++stats.loads_seen_by_slot[slot_index];
        auto& slot = current[slot_index];
        if (slot.value) {
          if (slot.value->def && slot.value->def->block != block) {
            Value* local_value = builder->LoadLocal(local_slots[slot_index]);
            builder->last_instr()->MoveBefore(instr);
            slot.value = local_value;
            ++stats.local_loads_inserted;
            ++stats.local_loads_by_slot[slot_index];
          }
          instr->opcode = &OPCODE_ASSIGN_info;
          instr->set_src1(slot.value);
          ++stats.loads_replaced;
          ++stats.loads_replaced_by_slot[slot_index];
        } else {
          slot.value = instr->dest;
          slot.dirty = true;
          ++stats.loads_seeded_from_context;
          ++stats.loads_seeded_by_slot[slot_index];
        }
        continue;
      }

      if (instr->opcode == &OPCODE_STORE_CONTEXT_info) {
        size_t offset = instr->src1.offset;
        Value* value = instr->src2.value;
        size_t size = value ? GetTypeSize(value->type) : 1;
        int slot_index = value ? GetPromotedGprIndex(offset, value->type) : -1;
        if (slot_index >= 0) {
          ++stats.promoted_stores_seen;
          ++stats.stores_seen_by_slot[slot_index];
          current[slot_index].value = value;
          current[slot_index].dirty = true;
          ++stats.stores_tracked;
          ++stats.stores_tracked_by_slot[slot_index];
          continue;
        }
        for (size_t n = 0; n < current.size(); ++n) {
          if (RangesOverlap(offset, size, kPromotedGprOffsets[n],
                            kPromotedGprSize)) {
            if (current[n].value) {
              ++stats.overlap_resets;
              ++stats.overlap_resets_by_slot[n];
            }
            current[n] = {};
          }
        }
      }
    }

    if (Instr* insert_before = FirstTailBranch(block)) {
      for (size_t n = 0; n < current.size(); ++n) {
        if (!current[n].value || !current[n].dirty) {
          continue;
        }
        builder->StoreLocal(local_slots[n], current[n].value);
        builder->last_instr()->MoveBefore(insert_before);
        ++stats.local_stores_inserted;
        ++stats.local_stores_by_slot[n];
      }
    } else {
      stats.stores_skipped_no_tail += CountDirtyGprLocalValues(current);
    }

    GprLocalSlotBlockState outgoing = {};
    for (size_t n = 0; n < current.size(); ++n) {
      outgoing.values[n] = current[n].value;
    }
    outgoing_states.emplace(block, outgoing);
  }

  if (cvars::arm64_context_promotion_gpr_local_slots_audit) {
    uint32_t function_address = FindFirstSourceOffset(builder);
    XELOGW(
        "A64 GPR local-slot promotion audit fn {:08X}: blocks={} "
        "dominated_blocks={} pred_state_hits={} pred_values_seeded={} "
        "loads_seen={} loads_seeded={} loads_replaced={} local_loads={} "
        "stores_seen={} stores_tracked={} local_stores={} volatile_resets={} "
        "overlap_resets={} stores_skipped_no_tail={}",
        function_address, stats.blocks, stats.dominated_blocks,
        stats.pred_state_hits, stats.pred_values_seeded,
        stats.promoted_loads_seen, stats.loads_seeded_from_context,
        stats.loads_replaced, stats.local_loads_inserted,
        stats.promoted_stores_seen, stats.stores_tracked,
        stats.local_stores_inserted, stats.volatile_resets,
        stats.overlap_resets, stats.stores_skipped_no_tail);
    XELOGW(
        "A64 GPR local-slot promotion audit fn {:08X}: "
        "r1 loads/seeded/replaced/local_loads stores/tracked/local_stores/"
        "overlap_resets={}/{}/{}/{} {}/{}/{}/{}; "
        "r11 loads/seeded/replaced/local_loads stores/tracked/local_stores/"
        "overlap_resets={}/{}/{}/{} {}/{}/{}/{}",
        function_address, stats.loads_seen_by_slot[0],
        stats.loads_seeded_by_slot[0], stats.loads_replaced_by_slot[0],
        stats.local_loads_by_slot[0], stats.stores_seen_by_slot[0],
        stats.stores_tracked_by_slot[0], stats.local_stores_by_slot[0],
        stats.overlap_resets_by_slot[0], stats.loads_seen_by_slot[1],
        stats.loads_seeded_by_slot[1], stats.loads_replaced_by_slot[1],
        stats.local_loads_by_slot[1], stats.stores_seen_by_slot[1],
        stats.stores_tracked_by_slot[1], stats.local_stores_by_slot[1],
        stats.overlap_resets_by_slot[1]);
  }
}

bool ContextPromotionPass::ShouldRunGprLiveInR1Promotion(
    HIRBuilder* builder) const {
  uint32_t function_filter =
      cvars::arm64_context_promotion_gpr_livein_r1_function;
  return !function_filter || FindFirstSourceOffset(builder) == function_filter;
}

void ContextPromotionPass::PromoteGprLiveInR1(HIRBuilder* builder) {
  const bool preserve_barrier =
      cvars::arm64_context_promotion_gpr_livein_r1_preserve_barrier;
  GprLiveInR1Stats stats;
  stats.function_address = FindFirstSourceOffset(builder);

  std::vector<Block*> blocks;
  std::unordered_map<Block*, GprLiveInR1Availability> availability;
  std::unordered_map<uint32_t, bool> edge_clean;
  for (auto block = builder->first_block(); block; block = block->next) {
    blocks.push_back(block);
    availability[block] = {};
    ++stats.blocks;
    for (auto edge = block->outgoing_edge_head; edge;
         edge = edge->outgoing_next) {
      edge_clean[GetEdgeKey(edge->src, edge->dest)] = true;
    }
  }

  auto transfer_block = [&](Block* block, bool entry_clean,
                            std::unordered_map<uint32_t, bool>* out_edges) {
    bool clean = entry_clean;
    std::unordered_map<Block*, bool> captured_edges;
    for (Instr* instr = block->instr_head; instr; instr = instr->next) {
      bool killed_by_call = false;
      bool killed_by_barrier = false;
      bool killed_by_exit = false;
      if (IsContextStateKillingInstr(instr, preserve_barrier, &killed_by_call,
                                     &killed_by_barrier, &killed_by_exit)) {
        clean = false;
      }
      if (IsTargetR1Store(instr)) {
        clean = true;
      } else if (IsTargetR1OverlapStore(instr)) {
        clean = false;
      } else if (IsTargetR1Load(instr)) {
        clean = true;
      }
      if (Block* target = GetBranchTargetBlock(instr)) {
        captured_edges[target] = clean;
        (*out_edges)[GetEdgeKey(block, target)] = clean;
      }
    }
    for (auto edge = block->outgoing_edge_head; edge;
         edge = edge->outgoing_next) {
      if (!captured_edges.count(edge->dest)) {
        (*out_edges)[GetEdgeKey(edge->src, edge->dest)] = clean;
      }
    }
    return clean;
  };

  bool changed = true;
  for (uint32_t iteration = 0; changed && iteration < 64; ++iteration) {
    changed = false;
    for (Block* block : blocks) {
      bool entry_clean = block->incoming_edge_head != nullptr;
      for (auto edge = block->incoming_edge_head; edge;
           edge = edge->incoming_next) {
        auto edge_state = edge_clean.find(GetEdgeKey(edge->src, edge->dest));
        if (edge_state == edge_clean.end() || !edge_state->second) {
          entry_clean = false;
          break;
        }
      }
      if (availability[block].clean != entry_clean) {
        availability[block].clean = entry_clean;
        changed = true;
      }

      std::unordered_map<uint32_t, bool> new_edge_states;
      transfer_block(block, entry_clean, &new_edge_states);
      for (auto& new_edge_state : new_edge_states) {
        auto old_edge_state = edge_clean.find(new_edge_state.first);
        if (old_edge_state == edge_clean.end() ||
            old_edge_state->second != new_edge_state.second) {
          edge_clean[new_edge_state.first] = new_edge_state.second;
          changed = true;
        }
      }
    }
  }

  auto block_needs_entry_local = [&](Block* block) {
    if (!availability[block].clean) {
      return false;
    }
    bool clean = true;
    for (Instr* instr = block->instr_head; instr; instr = instr->next) {
      bool killed_by_call = false;
      bool killed_by_barrier = false;
      bool killed_by_exit = false;
      if (IsContextStateKillingInstr(instr, preserve_barrier, &killed_by_call,
                                     &killed_by_barrier, &killed_by_exit)) {
        clean = false;
      }
      if (IsTargetR1Store(instr)) {
        clean = true;
      } else if (IsTargetR1OverlapStore(instr)) {
        clean = false;
      } else if (IsTargetR1Load(instr)) {
        return clean;
      }
    }
    return false;
  };

  for (Block* block : blocks) {
    if (availability[block].clean) {
      ++stats.entry_clean_blocks;
    }
    availability[block].needs_entry_local = block_needs_entry_local(block);
    if (availability[block].needs_entry_local) {
      ++stats.needs_entry_local_blocks;
    }
  }

  Value* local_slot = builder->AllocLocal(INT64_TYPE);

  auto ensure_value_from_local = [&](Instr* insert_before,
                                     GprLiveInR1RewriteState* state) {
    if (!state->value) {
      Value* local_value = builder->LoadLocal(local_slot);
      builder->last_instr()->MoveBefore(insert_before);
      state->value = local_value;
      state->dirty = false;
      ++stats.local_loads_inserted;
    }
  };

  auto store_local_if_needed = [&](Instr* insert_before,
                                   GprLiveInR1RewriteState* state,
                                   bool branch_store) {
    if (!state->clean || !state->dirty) {
      return;
    }
    if (!state->value) {
      ++stats.skipped_no_value_for_store;
      return;
    }
    builder->StoreLocal(local_slot, state->value);
    builder->last_instr()->MoveBefore(insert_before);
    state->dirty = false;
    ++stats.local_stores_inserted;
    if (branch_store) {
      ++stats.branch_stores_inserted;
    } else {
      ++stats.final_stores_inserted;
    }
  };

  auto reset_state = [](GprLiveInR1RewriteState* state,
                        GprLiveInR1DirtyReason reason) {
    *state = {};
    state->dirty_reason = reason;
  };

  auto count_skipped_load = [&](GprLiveInR1DirtyReason reason) {
    switch (reason) {
      case GprLiveInR1DirtyReason::kCall:
        ++stats.skipped_after_call;
        break;
      case GprLiveInR1DirtyReason::kBarrier:
        ++stats.skipped_after_barrier;
        break;
      case GprLiveInR1DirtyReason::kAlias:
        ++stats.skipped_after_alias;
        break;
      case GprLiveInR1DirtyReason::kExit:
        ++stats.skipped_after_exit;
        break;
      case GprLiveInR1DirtyReason::kEntry:
      default:
        ++stats.skipped_dirty_entry;
        break;
    }
  };

  for (Block* block : blocks) {
    GprLiveInR1RewriteState state;
    state.clean = availability[block].clean;

    for (Instr* instr = block->instr_head; instr;) {
      Instr* next = instr->next;

      if (instr->opcode == &OPCODE_BRANCH_TRUE_info ||
          instr->opcode == &OPCODE_BRANCH_FALSE_info) {
        ++stats.conditional_branch_preserves;
      }

      bool killed_by_call = false;
      bool killed_by_barrier = false;
      bool killed_by_exit = false;
      if (IsContextStateKillingInstr(instr, preserve_barrier, &killed_by_call,
                                     &killed_by_barrier, &killed_by_exit)) {
        if (state.clean) {
          if (killed_by_barrier) {
            ++stats.barrier_resets;
          } else if (killed_by_exit) {
            ++stats.exit_resets;
          } else {
            ++stats.call_resets;
          }
        }
        reset_state(&state,
                    killed_by_barrier ? GprLiveInR1DirtyReason::kBarrier
                    : killed_by_exit  ? GprLiveInR1DirtyReason::kExit
                                      : GprLiveInR1DirtyReason::kCall);
      }

      if (IsTargetR1Store(instr)) {
        ++stats.target_stores_seen;
        state.clean = true;
        state.value = instr->src2.value;
        state.dirty = true;
      } else if (IsTargetR1OverlapStore(instr)) {
        ++stats.target_alias_stores_seen;
        if (state.clean) {
          ++stats.alias_resets;
        }
        reset_state(&state, GprLiveInR1DirtyReason::kAlias);
      } else if (IsTargetR1Load(instr)) {
        ++stats.target_loads_seen;
        ++stats.loads_attempted;
        if (state.clean) {
          ensure_value_from_local(instr, &state);
          instr->opcode = &hir::OPCODE_ASSIGN_info;
          instr->set_src1(state.value);
          ++stats.loads_replaced;
        } else {
          count_skipped_load(state.dirty_reason);
          state.clean = true;
          state.value = instr->dest;
          state.dirty = true;
          ++stats.loads_seeded_from_context;
        }
      }

      if (Block* target = GetBranchTargetBlock(instr)) {
        auto target_availability = availability.find(target);
        if (target_availability != availability.end() &&
            target_availability->second.needs_entry_local) {
          store_local_if_needed(instr, &state, true);
        }
      }

      instr = next;
    }

    bool has_needing_successor = false;
    for (auto edge = block->outgoing_edge_head; edge;
         edge = edge->outgoing_next) {
      auto target_availability = availability.find(edge->dest);
      if (target_availability != availability.end() &&
          target_availability->second.needs_entry_local) {
        has_needing_successor = true;
        break;
      }
    }
    if (has_needing_successor && state.clean && state.dirty) {
      if (Instr* insert_before = FirstTailBranchOrNull(block)) {
        store_local_if_needed(insert_before, &state, false);
      } else {
        ++stats.skipped_no_value_for_store;
      }
    }
  }

  if (cvars::arm64_context_promotion_gpr_livein_r1_audit) {
    XELOGW(
        "A64 GPR live-in r1 promotion audit fn {:08X}: blocks={} "
        "entry_clean_blocks={} needs_entry_local_blocks={} "
        "loads_attempted={} loads_replaced={} loads_seeded={} "
        "local_loads={} local_stores={} branch_stores={} final_stores={} "
        "stores_seen={} alias_stores={} call_resets={} barrier_resets={} "
        "alias_resets={} exit_resets={} preserve_barrier={}",
        stats.function_address, stats.blocks, stats.entry_clean_blocks,
        stats.needs_entry_local_blocks, stats.loads_attempted,
        stats.loads_replaced, stats.loads_seeded_from_context,
        stats.local_loads_inserted, stats.local_stores_inserted,
        stats.branch_stores_inserted, stats.final_stores_inserted,
        stats.target_stores_seen, stats.target_alias_stores_seen,
        stats.call_resets, stats.barrier_resets, stats.alias_resets,
        stats.exit_resets, preserve_barrier);
    XELOGW(
        "A64 GPR live-in r1 promotion audit fn {:08X}: skipped "
        "dirty_entry={} after_call={} after_barrier={} after_alias={} "
        "after_exit={} no_value_for_store={} branch_preserves={}",
        stats.function_address, stats.skipped_dirty_entry,
        stats.skipped_after_call, stats.skipped_after_barrier,
        stats.skipped_after_alias, stats.skipped_after_exit,
        stats.skipped_no_value_for_store, stats.conditional_branch_preserves);
  }
}

void ContextPromotionPass::RemoveDeadStoresBlock(Block* block) {
  auto& validity = context_validity_;
  validity.reset();

  // Walk backwards and mark offsets that are written to.
  // If the offset was written to earlier, ignore the store.
  Instr* i = block->instr_tail;
  while (i) {
    Instr* prev = i->prev;
    if (i->opcode->flags & (OPCODE_FLAG_VOLATILE | OPCODE_FLAG_BRANCH)) {
      // Volatile instruction - requires all context values be flushed.
      validity.reset();
    } else if (i->opcode == &OPCODE_STORE_CONTEXT_info) {
      size_t offset = i->src1.offset;
      Value* value = i->src2.value;
      size_t size = value ? GetTypeSize(value->type) : 1;
      if (!IsContextRangeValid(offset, size)) {
        // Offset not yet written, mark and continue.
        MarkContextRange(offset, size);
      } else {
        // Already written to. Remove this store.
        i->Remove();
      }
    }
    i = prev;
  }
}

bool ContextPromotionPass::TryGetContextValue(size_t offset, size_t size,
                                              TypeName type,
                                              Value** out_value) const {
  if (size == 0 || offset + size > context_values_.size()) {
    return false;
  }

  Value* value = nullptr;
  for (size_t n = 0; n < size; ++n) {
    uint32_t byte_offset = static_cast<uint32_t>(offset + n);
    if (!context_validity_.test(byte_offset)) {
      return false;
    }
    Value* byte_value = context_values_[offset + n];
    if (!byte_value || byte_value->type != type) {
      return false;
    }
    if (!value) {
      value = byte_value;
    } else if (value != byte_value) {
      return false;
    }
  }

  *out_value = value;
  return value != nullptr;
}

void ContextPromotionPass::SetContextValueRange(size_t offset, size_t size,
                                                Value* value) {
  if (!value || size == 0 || offset + size > context_values_.size()) {
    return;
  }
  for (size_t n = 0; n < size; ++n) {
    size_t byte_offset = offset + n;
    context_values_[byte_offset] = value;
    context_validity_.set(static_cast<uint32_t>(byte_offset));
  }
}

bool ContextPromotionPass::IsContextRangeValid(size_t offset,
                                               size_t size) const {
  if (size == 0 || offset + size > context_values_.size()) {
    return false;
  }
  for (size_t n = 0; n < size; ++n) {
    if (!context_validity_.test(static_cast<uint32_t>(offset + n))) {
      return false;
    }
  }
  return true;
}

void ContextPromotionPass::MarkContextRange(size_t offset, size_t size) {
  if (size == 0 || offset + size > context_values_.size()) {
    return;
  }
  for (size_t n = 0; n < size; ++n) {
    context_validity_.set(static_cast<uint32_t>(offset + n));
  }
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
