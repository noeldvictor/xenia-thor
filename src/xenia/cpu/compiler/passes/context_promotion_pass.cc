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
  if (!function_filter) {
    return true;
  }

  for (auto block = builder->first_block(); block; block = block->next) {
    for (auto instr = block->instr_head; instr; instr = instr->next) {
      if (instr->opcode == &OPCODE_SOURCE_OFFSET_info) {
        return static_cast<uint32_t>(instr->src1.offset) == function_filter;
      }
    }
  }
  return false;
}

void ContextPromotionPass::PromoteDominatedGprLocalSlots(HIRBuilder* builder) {
  std::array<Value*, 2> local_slots = {};
  for (size_t n = 0; n < local_slots.size(); ++n) {
    local_slots[n] = builder->AllocLocal(INT64_TYPE);
  }

  std::unordered_map<Block*, GprLocalSlotBlockState> outgoing_states;

  for (auto block = builder->first_block(); block; block = block->next) {
    std::array<GprLocalSlotValue, 2> current = {};

    if (Block* pred = GetSingleDominatingPredecessor(block)) {
      auto pred_state = outgoing_states.find(pred);
      if (pred_state != outgoing_states.end()) {
        for (size_t n = 0; n < current.size(); ++n) {
          current[n].value = pred_state->second.values[n];
        }
      }
    }

    for (Instr* instr = block->instr_head; instr; instr = instr->next) {
      if (instr->opcode->flags & OPCODE_FLAG_VOLATILE) {
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

        auto& slot = current[slot_index];
        if (slot.value) {
          if (slot.value->def && slot.value->def->block != block) {
            Value* local_value = builder->LoadLocal(local_slots[slot_index]);
            builder->last_instr()->MoveBefore(instr);
            slot.value = local_value;
          }
          instr->opcode = &OPCODE_ASSIGN_info;
          instr->set_src1(slot.value);
        } else {
          slot.value = instr->dest;
          slot.dirty = true;
        }
        continue;
      }

      if (instr->opcode == &OPCODE_STORE_CONTEXT_info) {
        size_t offset = instr->src1.offset;
        Value* value = instr->src2.value;
        size_t size = value ? GetTypeSize(value->type) : 1;
        int slot_index = value ? GetPromotedGprIndex(offset, value->type) : -1;
        if (slot_index >= 0) {
          current[slot_index].value = value;
          current[slot_index].dirty = true;
          continue;
        }
        for (size_t n = 0; n < current.size(); ++n) {
          if (RangesOverlap(offset, size, kPromotedGprOffsets[n],
                            kPromotedGprSize)) {
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
      }
    }

    GprLocalSlotBlockState outgoing = {};
    for (size_t n = 0; n < current.size(); ++n) {
      outgoing.values[n] = current[n].value;
    }
    outgoing_states.emplace(block, outgoing);
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
