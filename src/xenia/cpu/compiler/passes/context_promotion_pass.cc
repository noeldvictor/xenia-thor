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

bool RangesOverlap(size_t a_offset, size_t a_size, size_t b_offset,
                   size_t b_size) {
  return a_offset < b_offset + b_size && b_offset < a_offset + a_size;
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
  auto block = builder->first_block();
  while (block) {
    PromoteBlock(block);
    block = block->next;
  }

  if (cvars::arm64_context_promotion_gpr_local_slots &&
      ShouldRunGprLocalSlotPromotion(builder)) {
    PromoteDominatedGprLocalSlots(builder);
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

  return true;
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
