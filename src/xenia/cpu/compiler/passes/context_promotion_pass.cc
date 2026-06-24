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
#include <atomic>
#include <cstddef>
#include <unordered_map>

#include "xenia/apu/apu_flags.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"

DECLARE_bool(debug);
#if XE_ARCH_ARM64
// Defined by the a64 backend (a64_backend.cc). The cross-call r1 carrier is
// unsound while this stack-sync net is enabled (it reloads r1 from context and
// resumes into the caller after a longjmp without reseeding the carrier), so
// PromoteGprLiveInR1 disables preserve_call when it is on. Declared only on
// ARM64 builds — the symbol does not exist in the x64-only host build.
DECLARE_bool(a64_enable_host_guest_stack_synchronization);
#endif

DEFINE_bool(store_all_context_values, false,
            "Don't strip dead context stores to aid in debugging.", "CPU");
DEFINE_bool(
    ppc_cross_block_dead_flag_elim, false,
    "Thor codegen (guest-JIT tier-2): cross-block dead-store elimination for "
    "the PowerPC condition register (CR0-7) and XER carry context slots. The "
    "default block-scoped DSE resets liveness at each block boundary, so flag "
    "stores dead across ALL successor paths survive to ARM64 (PPC sets "
    "record-form CR / carry liberally). This computes per-block CR/XER live-out "
    "via a backward dataflow (calls/returns/context barriers conservatively "
    "mark all flag slots live, so live state is NEVER elided across a call - "
    "only dead STORES are removed, staying on the safe side of the cross-"
    "barrier wall) and drops the dead stores; DCE then reaps the feeding "
    "compares. Bit-exact; default-off experiment, host-validated first.",
    "CPU");
DEFINE_bool(ppc_cross_block_dead_flag_elim_audit, false,
            "Log per-function store/removal/iteration counts for "
            "ppc_cross_block_dead_flag_elim (requires it to be enabled).",
            "CPU");
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
DEFINE_bool(arm64_context_promotion_gpr_livein_r1_preserve_call, false,
            "Thor ARM64 research (cross-call register preservation): keep the "
            "live-in r[1] (PPC stack pointer) carrier valid across a DIRECT "
            "guest-to-guest bl (OPCODE_CALL/CALL_TRUE) to a normal guest "
            "function only. Guarded (adversarial red-team 2026-06-13): resets on "
            "CALL_POSSIBLE_RETURN (guest blr/longjmp/EH), on ALL indirect calls, "
            "on direct calls to extern/import/epilog targets (e.g. "
            "KeSetCurrentStackPointers), and is auto-disabled while "
            "a64_enable_host_guest_stack_synchronization is on (the sync net "
            "reloads r1 from context on a longjmp resume without reseeding the "
            "carrier). Eliminates the per-iteration r1 reload our JIT emits "
            "across calls in hot loops (e.g. Burnout's entity-traversal loop "
            "0x82382798). Default-off experiment.",
            "CPU");
DEFINE_bool(arm64_context_promotion_gpr_livein_r1_audit, false,
            "Thor ARM64 research: log attempted/replaced/skipped counters for "
            "arm64_context_promotion_gpr_livein_r1.",
            "CPU");
DEFINE_bool(arm64_context_promotion_gpr_crossblock, false,
            "Thor ARM64 codegen (tier-2 cross-block register caching): carry "
            "whole PPC GPR context slots (default: callee-saved r27-r31) across "
            "single-dominating-predecessor block boundaries through HIR locals, "
            "eliminating the redundant per-block LOAD_CONTEXT guest-context "
            "reload our JIT emits for a register that stays live across a "
            "call-free chain of blocks (loop counters / base pointers in a hot "
            "loop body). Generalizes the proven dominated-pred carrier (same "
            "mechanism + guards that already safely promote the r1 stack "
            "pointer: resets at every volatile/call/barrier and on any "
            "overlapping partial store; only seeds across a single DOMINATING "
            "edge so merge points re-load). Default-off experiment.",
            "CPU");
DEFINE_uint32(arm64_context_promotion_gpr_crossblock_mask, 0xF8000000u,
              "Bitmask of PPC GPR indices (bit i selects r[i]) promoted by "
              "arm64_context_promotion_gpr_crossblock. Default 0xF8000000 = the "
              "callee-saved set r27,r28,r29,r30,r31 (the registers the "
              "nonclosed-cache audit identified as cross-block-live). Widen to "
              "include e.g. r1 (bit 1) / r11 (bit 11) at higher host-register "
              "pressure risk.",
              "CPU");
DEFINE_uint32(arm64_context_promotion_gpr_crossblock_function, 0,
              "Optional guest function start address filter for "
              "arm64_context_promotion_gpr_crossblock. 0 applies globally.",
              "CPU");
DEFINE_bool(arm64_context_promotion_gpr_crossblock_audit, false,
            "Thor ARM64 codegen: log per-function cross-block GPR promotion "
            "counters (loads replaced, local carriers inserted, resets).",
            "CPU");
DEFINE_bool(
    arm64_context_promotion_gpr_crossblock_cond_branch_carry, false,
    "Thor ARM64 codegen EXPERIMENTAL (default-off, do NOT enable): also carry "
    "the cross-block GPR carrier across CONDITIONAL branches (BRANCH_TRUE / "
    "BRANCH_FALSE), not just unconditional ones. A conditional branch does not "
    "modify guest registers, so this is sound in isolation and is fully "
    "host-validated (crossblock_gpr_promotion_test, x64) - BUT it CRASHES Blue "
    "Dragon on-device (SIGBUS in JIT code ~2s into boot, 2026-06-23): a "
    "real-guest-code / call-interaction pattern the synthetic host+qemu tests "
    "don't reproduce makes a promoted register read a stale/zero carrier. With "
    "this OFF, the main crossblock cvar resets the carrier at every volatile "
    "(incl. conditional branches), matching the proven dominated-slot mechanism "
    "that already promotes r1 (it still carries across UNCONDITIONAL branches / "
    "straight-line fallthrough chains). Keep OFF until the device crash is "
    "root-caused (needs device or a call-capable test harness).",
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

struct CrossBlockGprStats {
  uint32_t function_address = 0;
  uint32_t slots = 0;
  uint32_t blocks = 0;
  uint32_t dominated_blocks = 0;
  uint32_t pred_state_hits = 0;
  uint32_t pred_values_seeded = 0;
  uint32_t loads_seen = 0;
  uint32_t loads_seeded_from_context = 0;
  uint32_t loads_replaced = 0;
  uint32_t local_loads_inserted = 0;
  uint32_t stores_tracked = 0;
  uint32_t local_stores_inserted = 0;
  uint32_t overlap_resets = 0;
  uint32_t volatile_resets = 0;
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

// True if `block` can be entered at runtime by any path OTHER than the single
// recorded DOMINATES edge from its emission-order predecessor. The cross-block
// register carrier is only sound on blocks that are NOT externally enterable.
//
// CRITICAL (this is the BD ~2s-boot SIGBUS root cause): Edge::DOMINATES here
// means "exactly one RECORDED incoming edge", and ControlFlowAnalysisPass records
// edges ONLY for direct branches - indirect control flow (bctr/blr/bcctr/jump
// tables -> OPCODE_CALL_INDIRECT) adds NO HIR edge. So a jump-table landing block
// that also has one direct predecessor looks single-dominating-pred, yet is
// really re-entered from the dispatcher on a path where the predecessor's carrier
// deposit never ran. Seeding the carrier there promotes a LOAD_CONTEXT to a
// LOAD_LOCAL that reads an uninitialized/stale stack slot -> wild base pointer ->
// SIGBUS. The synthetic host/qemu tests miss it because they build only
// direct-branch CFGs where DOMINATES really is the dominator.
//
// We cannot enumerate indirect targets (the scanner doesn't decode jump tables),
// so this is the sound, cheap over-approximation: a block is INTERNAL (safe) only
// if it is reached purely by a direct fallthrough from the immediately-preceding
// emission block. Any other entry - function entry, CFG merge, forward/back-edge,
// or a potential indirect target - is treated as enterable. (E4 in the design doc
// later relaxes the fallthrough rule using a scanner address-taken bit.)
bool IsExternallyEnterable(HIRBuilder* builder, Block* block) {
  if (block == builder->first_block()) {
    return true;  // E1: function entry - cold start from caller/dispatcher.
  }
  Edge* in = block->incoming_edge_head;
  if (!in) {
    return true;  // No recorded predecessor => reachable only indirectly or dead.
  }
  if (in->incoming_next) {
    return true;  // E2: CFG merge (more than one recorded predecessor).
  }
  if (!(in->flags & Edge::DOMINATES)) {
    return true;  // E2: not the sole recorded predecessor edge.
  }
  if (in->src != block->prev) {
    return true;  // E3: not a straight-line fallthrough from the prior emission
                  // block; a jump table could land here - be conservative.
  }
  return false;
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
                                bool preserve_call, bool* killed_by_call,
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
  // Cross-call carrier preservation (preserve_call) is sound ONLY for a DIRECT
  // guest-to-guest call (PPC bl, lk=1) to a normal guest function. Everything
  // else can change r1 without an EABI-conforming return and MUST kill the
  // carrier (adversarial red-team 2026-06-13, all guards source-verified):
  //  - CALL_POSSIBLE_RETURN marks guest blr/bclr-to-LR = the longjmp / C++ EH
  //    non-local-exit path (ppc_emit_control.cc); the callee can resume an OUTER
  //    frame at a different r1.
  //  - CALL_INDIRECT/_TRUE (bctrl/blr) hit runtime targets that may be import
  //    stubs or stack-switching/fiber shims that legally change the reg file.
  //  - A direct call whose target is NOT a plain guest function (kExtern/kBuiltin
  //    import like KeSetCurrentStackPointers, or a prolog/epilog gpr-restore
  //    thunk) sets context r1 and returns normally; the caller's pass only sees
  //    OPCODE_CALL (hir_builder.cc), so it must treat these as carrier-killing.
  // The remaining longjmp-resume hole (the a64 stack-sync net reloads r1 from
  // context and re-enters the caller without reseeding the carrier) is closed in
  // PromoteGprLiveInR1 by disabling preserve_call while that net is enabled.
  if (instr->opcode == &OPCODE_CALL_info ||
      instr->opcode == &OPCODE_CALL_TRUE_info) {
    const bool preservable =
        preserve_call && !(instr->flags & hir::CALL_POSSIBLE_RETURN) &&
        instr->src1.symbol &&
        instr->src1.symbol->behavior() ==
            xe::cpu::Function::Behavior::kDefault;
    if (!preservable) {
      *killed_by_call = true;
      return true;
    }
    return false;
  }
  if (instr->opcode == &OPCODE_CALL_INDIRECT_info ||
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

  if (cvars::arm64_context_promotion_gpr_crossblock &&
      ShouldRunCrossBlockGprPromotion(builder)) {
    // General cross-block carrier (tier-2): supersedes the narrow r1/r11
    // local-slot promoter; both share the same dominated-pred mechanism, so
    // running only one keeps each promoted slot owned by a single carrier.
    std::vector<size_t> slot_offsets;
    uint32_t mask = cvars::arm64_context_promotion_gpr_crossblock_mask;
    for (uint32_t i = 0; i < 32; ++i) {
      if (mask & (1u << i)) {
        slot_offsets.push_back(offsetof(ppc::PPCContext, r) +
                               i * sizeof(uint64_t));
      }
    }
    PromoteCrossBlockGprSlots(builder, slot_offsets);
  } else if (cvars::arm64_context_promotion_gpr_local_slots &&
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

bool ContextPromotionPass::ShouldRunCrossBlockGprPromotion(
    HIRBuilder* builder) const {
  uint32_t function_filter =
      cvars::arm64_context_promotion_gpr_crossblock_function;
  return !function_filter || FindFirstSourceOffset(builder) == function_filter;
}

void ContextPromotionPass::PromoteCrossBlockGprSlots(
    HIRBuilder* builder, const std::vector<size_t>& slot_offsets) {
  const size_t slot_count = slot_offsets.size();
  if (slot_count == 0) {
    return;
  }

  // Default-off experimental: carry across conditional branches too (host-
  // correct but device-crashes BD). Off => proven reset-on-all-volatile.
  const bool carry_cond_branches =
      cvars::arm64_context_promotion_gpr_crossblock_cond_branch_carry;

  // One INT64 HIR local per promoted GPR slot. A LOAD_CONTEXT is an opaque,
  // conservatively-aliased guest-memory read that the register allocator cannot
  // hoist across blocks; a HIR local can be kept in a host register along a
  // dominated chain. So replacing the redundant first per-block context reload
  // of a still-live GPR with a single local carrier removes guest-context
  // traffic in call-free hot loops (the same trick that already promotes r1).
  std::vector<Value*> local_slots(slot_count);
  for (size_t n = 0; n < slot_count; ++n) {
    local_slots[n] = builder->AllocLocal(INT64_TYPE);
  }

  auto slot_index_for = [&](size_t offset, TypeName type) -> int {
    if (type != INT64_TYPE) {
      return -1;
    }
    for (size_t n = 0; n < slot_count; ++n) {
      if (offset == slot_offsets[n]) {
        return static_cast<int>(n);
      }
    }
    return -1;
  };

  CrossBlockGprStats stats;
  stats.function_address = FindFirstSourceOffset(builder);
  stats.slots = static_cast<uint32_t>(slot_count);
  std::unordered_map<Block*, std::vector<Value*>> outgoing_states;

  for (auto block = builder->first_block(); block; block = block->next) {
    ++stats.blocks;
    std::vector<GprLocalSlotValue> current(slot_count);

    // Seed only across a SINGLE DOMINATING predecessor edge that is ALSO a true
    // internal (not externally-enterable) edge - see IsExternallyEnterable for
    // why "single DOMINATES pred" is NOT sufficient (indirect/jump-table entries
    // are edgeless, so a dispatcher re-entry can read an undeposited carrier ->
    // SIGBUS). Merge points and enterable blocks start empty and re-load from
    // context. This gate makes the carrier sound even with cond_branch_carry on.
    Block* pred = IsExternallyEnterable(builder, block)
                      ? nullptr
                      : GetSingleDominatingPredecessor(block);
    if (pred) {
      ++stats.dominated_blocks;
      auto pred_state = outgoing_states.find(pred);
      if (pred_state != outgoing_states.end()) {
        ++stats.pred_state_hits;
        for (size_t n = 0; n < slot_count; ++n) {
          current[n].value = pred_state->second[n];
          if (current[n].value) {
            ++stats.pred_values_seeded;
          }
        }
      }
    }

    for (Instr* instr = block->instr_head; instr; instr = instr->next) {
      // An UNCONDITIONAL branch (OPCODE_BRANCH) is not volatile, so it never
      // triggers this reset -> the carrier always survives straight-line
      // fallthrough chains (the proven r1 dominated-slot behavior). CONDITIONAL
      // branches (BRANCH_TRUE/FALSE) ARE volatile; carrying across them reaches
      // dominated successors of if/loop blocks (the big win) but device-crashes
      // BD, so it is gated behind the default-off cond_branch_carry flag. Calls
      // (OPCODE_CALL*/CALL_EXTERN, also branch-flagged) transfer to guest
      // code/imports that share the PPCContext and can clobber it - they ALWAYS
      // reset (the cross-barrier-elision wall), as do barriers/returns/traps.
      const bool is_carryable_cond_branch =
          carry_cond_branches &&
          (instr->opcode == &OPCODE_BRANCH_TRUE_info ||
           instr->opcode == &OPCODE_BRANCH_FALSE_info);
      if ((instr->opcode->flags & OPCODE_FLAG_VOLATILE) &&
          !is_carryable_cond_branch) {
        for (auto& slot : current) {
          if (slot.value) {
            ++stats.volatile_resets;
            break;
          }
        }
        for (auto& slot : current) {
          slot = {};
        }
        continue;
      }

      if (instr->opcode == &OPCODE_LOAD_CONTEXT_info) {
        size_t offset = instr->src1.offset;
        TypeName type = instr->dest ? instr->dest->type : MAX_TYPENAME;
        int slot_index = slot_index_for(offset, type);
        if (slot_index < 0) {
          continue;
        }
        ++stats.loads_seen;
        auto& slot = current[slot_index];
        if (slot.value) {
          // Carried value from an earlier block: its SSA def is not available
          // here, so materialize it from the local carrier, then fold the load
          // into an ASSIGN of that value.
          if (slot.value->def && slot.value->def->block != block) {
            Value* local_value = builder->LoadLocal(local_slots[slot_index]);
            builder->last_instr()->MoveBefore(instr);
            slot.value = local_value;
            ++stats.local_loads_inserted;
          }
          instr->opcode = &OPCODE_ASSIGN_info;
          instr->set_src1(slot.value);
          ++stats.loads_replaced;
        } else {
          // First sight of this slot in this chain: keep the real load, but
          // remember its value (and mark dirty so the tail deposits it).
          slot.value = instr->dest;
          slot.dirty = true;
          ++stats.loads_seeded_from_context;
        }
        continue;
      }

      if (instr->opcode == &OPCODE_STORE_CONTEXT_info) {
        size_t offset = instr->src1.offset;
        Value* value = instr->src2.value;
        size_t size = value ? GetTypeSize(value->type) : 1;
        int slot_index = value ? slot_index_for(offset, value->type) : -1;
        if (slot_index >= 0) {
          // Exact full-width store: the new value becomes the carrier (the real
          // STORE_CONTEXT is kept so non-promoted readers stay coherent; DSE
          // later strips it if dead).
          current[slot_index].value = value;
          current[slot_index].dirty = true;
          ++stats.stores_tracked;
          continue;
        }
        // A partial / differently-typed store overlapping a tracked slot
        // invalidates the cached full-width value.
        for (size_t n = 0; n < slot_count; ++n) {
          if (RangesOverlap(offset, size, slot_offsets[n], kPromotedGprSize)) {
            if (current[n].value) {
              ++stats.overlap_resets;
            }
            current[n] = {};
          }
        }
      }
    }

    // Deposit dirty carriers into their locals just before the block's tail
    // branch so a dominated successor's LOAD_LOCAL reads the up-to-date value.
    if (Instr* insert_before = FirstTailBranch(block)) {
      for (size_t n = 0; n < slot_count; ++n) {
        if (!current[n].value || !current[n].dirty) {
          continue;
        }
        builder->StoreLocal(local_slots[n], current[n].value);
        builder->last_instr()->MoveBefore(insert_before);
        ++stats.local_stores_inserted;
      }
    }

    std::vector<Value*> outgoing(slot_count);
    for (size_t n = 0; n < slot_count; ++n) {
      outgoing[n] = current[n].value;
    }
    outgoing_states.emplace(block, std::move(outgoing));
  }

  if (cvars::arm64_context_promotion_gpr_crossblock_audit) {
    XELOGW(
        "A64 cross-block GPR promotion fn {:08X}: slots={} blocks={} "
        "dominated_blocks={} pred_state_hits={} pred_values_seeded={} "
        "loads_seen={} loads_seeded={} loads_replaced={} local_loads={} "
        "stores_tracked={} local_stores={} overlap_resets={} volatile_resets={}",
        stats.function_address, stats.slots, stats.blocks,
        stats.dominated_blocks, stats.pred_state_hits, stats.pred_values_seeded,
        stats.loads_seen, stats.loads_seeded_from_context, stats.loads_replaced,
        stats.local_loads_inserted, stats.stores_tracked,
        stats.local_stores_inserted, stats.overlap_resets,
        stats.volatile_resets);
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
  bool preserve_call =
      cvars::arm64_context_promotion_gpr_livein_r1_preserve_call;
#if XE_ARCH_ARM64
  // The a64 host/guest stack-sync net (a64_enable_host_guest_stack_synchronization,
  // default ON) reloads r1 from context and resumes into the caller after a
  // longjmp/EH unwind WITHOUT reseeding the promoted carrier. While it is on, a
  // preserved cross-call r1 read can be stale on that rare path -> silent stack
  // corruption. Disable preserve_call until a backend-side carrier reseed lands.
  if (preserve_call && cvars::a64_enable_host_guest_stack_synchronization) {
    preserve_call = false;
  }
#endif
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
      if (IsContextStateKillingInstr(instr, preserve_barrier, preserve_call,
                                     &killed_by_call, &killed_by_barrier,
                                     &killed_by_exit)) {
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

  // Non-convergence fail-safe (red-team guard): edges are seeded optimistically
  // clean with only a hard 64-iteration cap. On a deep/irreducible CFG the
  // fixpoint can exit still changing, leaving an edge optimistically clean whose
  // carrier was never deposited -> a promoted load would read uninitialized
  // stack. If we did not converge, abandon promotion for this whole function
  // (every guest r1 load stays a real LOAD_CONTEXT). Correctness over the win.
  if (changed) {
    if (cvars::arm64_context_promotion_gpr_livein_r1_audit) {
      XELOGW(
          "A64 GPR live-in r1: availability fixpoint did NOT converge for fn "
          "{:08X} ({} blocks); skipping promotion (fail-safe).",
          stats.function_address, stats.blocks);
    }
    return;
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
      if (IsContextStateKillingInstr(instr, preserve_barrier, preserve_call,
                                     &killed_by_call, &killed_by_barrier,
                                     &killed_by_exit)) {
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
      if (IsContextStateKillingInstr(instr, preserve_barrier, preserve_call,
                                     &killed_by_call, &killed_by_barrier,
                                     &killed_by_exit)) {
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

  // Defensive entry seed (red-team guard, preserve_call only): deposit the
  // function-entry r1 into the carrier local at the top of the entry block so
  // that if any runtime path reaches a promoted LOAD_LOCAL whose static deposit
  // was skipped (e.g. a back-edge corner), it reads the entry stack pointer
  // (correct for the common function that never changes its own sp) rather than
  // uninitialized stack memory. Only emitted when cross-call preservation is
  // actually active, so the preserve_call-off path stays byte-identical.
  if (preserve_call) {
    if (Block* entry_block = builder->first_block()) {
      if (Instr* insert_before = entry_block->instr_head) {
        Value* seed = builder->LoadContext(kR1ContextOffset, INT64_TYPE);
        builder->last_instr()->MoveBefore(insert_before);
        builder->StoreLocal(local_slot, seed);
        builder->last_instr()->MoveBefore(insert_before);
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

// ===========================================================================
// CrossBlockFlagDeadStoreEliminationPass
// ===========================================================================
namespace {

// CR0-7 occupy 32 contiguous context bytes; the XER carry is one byte that is
// NOT adjacent to the CR range (xer_ov/xer_so/padding sit between), so it is
// tracked as a separate point. Slot index space: 0..31 = the 32 CR bytes
// (CRn field = cr0 + 4*n + {0:lt,1:gt,2:eq,3:so}), 32 = xer_ca.
constexpr size_t kCrRangeBegin = offsetof(xe::cpu::ppc::PPCContext, cr0);
constexpr size_t kCrRangeEnd = kCrRangeBegin + 32;
constexpr size_t kXerCaOffset = offsetof(xe::cpu::ppc::PPCContext, xer_ca);
constexpr int kFlagSlotCount = 33;
constexpr uint64_t kAllFlagSlots = (uint64_t(1) << kFlagSlotCount) - 1;

// 33-bit mask of tracked flag slots that the byte range [offset, offset+size)
// touches (0 if it touches none).
uint64_t FlagSlotsTouched(size_t offset, size_t size) {
  uint64_t mask = 0;
  const size_t end = offset + size;
  for (int i = 0; i < 32; ++i) {
    const size_t b = kCrRangeBegin + static_cast<size_t>(i);
    if (b >= offset && b < end) {
      mask |= (uint64_t(1) << i);
    }
  }
  if (kXerCaOffset >= offset && kXerCaOffset < end) {
    mask |= (uint64_t(1) << 32);
  }
  return mask;
}

// Any instruction across which the guest CR/XER may be read or must be coherent
// (forces all flag slots live, so a store before it is preserved - the safe side
// of the cross-barrier wall): calls (callee shares PPCContext), returns (caller
// reads + function exit), explicit context barriers, traps, and other volatile
// host ops. CRUCIALLY this is NOT the plain volatile-superset: the intra-
// function conditional branches BRANCH_TRUE / BRANCH_FALSE are volatile but do
// NOT read guest CR/XER themselves (a CR-conditional branch reads CR via a
// separate preceding LOAD_CONTEXT, handled as a normal flag load) - and their
// cross-block flag flow is already captured by LIVE_OUT (the union of successor
// LIVE_IN). Treating them as barriers would force all flags live and prevent
// removing the dead flag store that precedes nearly every conditional branch,
// defeating the pass. So branches are excluded; everything else volatile is a
// barrier.
bool IsFlagBarrier(const Instr* instr) {
  const OpcodeInfo* op = instr->opcode;
  if (op == &OPCODE_CONTEXT_BARRIER_info) {
    return true;
  }
  if (!(op->flags & OPCODE_FLAG_VOLATILE)) {
    // Non-volatile (CR/XER LOAD/STORE_CONTEXT, compares, plain BRANCH, ...) -
    // never makes guest CR/XER live by itself.
    return false;
  }
  if (op == &OPCODE_BRANCH_TRUE_info || op == &OPCODE_BRANCH_FALSE_info) {
    return false;  // intra-function branch - flow captured by LIVE_OUT
  }
  return true;  // call / return[_true] / trap / atomic / memory barrier / ...
}

std::atomic<uint64_t> g_flag_dse_removed{0};

}  // namespace

uint64_t CrossBlockFlagDseStoresRemovedForTest() {
  return g_flag_dse_removed.load(std::memory_order_relaxed);
}

CrossBlockFlagDeadStoreEliminationPass::
    CrossBlockFlagDeadStoreEliminationPass() = default;
CrossBlockFlagDeadStoreEliminationPass::
    ~CrossBlockFlagDeadStoreEliminationPass() = default;

bool CrossBlockFlagDeadStoreEliminationPass::Run(HIRBuilder* builder) {
  if (!cvars::ppc_cross_block_dead_flag_elim) {
    return true;  // default path is byte-identical
  }

  uint64_t stores_seen = 0;
  // Backward transfer over one block: live := LIVE_OUT, walk tail->head. Shared
  // by the fixpoint (remove=false) and the removal phase (remove=true).
  auto transfer = [&stores_seen](Block* b, uint64_t live_out, bool remove,
                                 uint64_t* removed_count) -> uint64_t {
    uint64_t live = live_out;
    for (Instr* i = b->instr_tail; i;) {
      Instr* prev = i->prev;
      if (i->opcode == &OPCODE_STORE_CONTEXT_info) {
        Value* value = i->src2.value;
        size_t size = value ? GetTypeSize(value->type) : 1;
        uint64_t touched = FlagSlotsTouched(i->src1.offset, size);
        if (touched) {
          bool single = (size == 1) && ((touched & (touched - 1)) == 0);
          if (single) {
            if (remove) {
              ++stores_seen;
            }
            if ((live & touched) == 0) {
              // Dead: no path reads this slot before its next def/exit.
              if (remove) {
                i->Remove();
                if (removed_count) {
                  ++*removed_count;
                }
              }
              // Slot stays not-live above (it was already not live).
            } else {
              live &= ~touched;  // real def kills upward liveness
            }
          } else {
            // Multi-byte / overlapping store - never seen from the builder, but
            // conservatively keep it and mark its slots live.
            live |= touched;
          }
        }
      } else if (i->opcode == &OPCODE_LOAD_CONTEXT_info) {
        size_t size = i->dest ? GetTypeSize(i->dest->type) : 1;
        live |= FlagSlotsTouched(i->src1.offset, size);
      } else if (IsFlagBarrier(i)) {
        live = kAllFlagSlots;  // all flags live above a barrier
      }
      i = prev;
    }
    return live;
  };

  // Per-block CR/XER LIVE-IN, keyed on Block* (block ordinals are not assigned
  // at this pipeline stage). Monotone fixpoint - live sets only grow.
  std::unordered_map<Block*, uint64_t> live_in;
  size_t block_count = 0;
  for (Block* b = builder->first_block(); b; b = b->next) {
    live_in[b] = 0;
    ++block_count;
  }
  if (!block_count) {
    return true;
  }

  const size_t kMaxIters = block_count * 4 + 16;
  bool changed = true;
  size_t iters = 0;
  while (changed && iters < kMaxIters) {
    changed = false;
    ++iters;
    for (Block* b = builder->first_block(); b; b = b->next) {
      uint64_t live_out = 0;
      for (Edge* e = b->outgoing_edge_head; e; e = e->outgoing_next) {
        live_out |= live_in[e->dest];
      }
      uint64_t new_in = transfer(b, live_out, false, nullptr);
      if (new_in != live_in[b]) {
        live_in[b] = new_in;
        changed = true;
      }
    }
  }
  if (changed) {
    // Not converged - correctness over win: remove nothing.
    return true;
  }

  uint64_t removed = 0;
  for (Block* b = builder->first_block(); b; b = b->next) {
    uint64_t live_out = 0;
    for (Edge* e = b->outgoing_edge_head; e; e = e->outgoing_next) {
      live_out |= live_in[e->dest];
    }
    transfer(b, live_out, true, &removed);
  }
  g_flag_dse_removed.fetch_add(removed, std::memory_order_relaxed);

  if (cvars::ppc_cross_block_dead_flag_elim_audit) {
    XELOGI("CrossBlockFlagDSE: blocks={} iters={} stores_seen={} removed={}",
           block_count, iters, stores_seen, removed);
  }
  return true;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
