/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/context_promotion_pass.h"

#include "xenia/apu/apu_flags.h"
#include "xenia/base/cvar.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"

DECLARE_bool(debug);

DEFINE_bool(store_all_context_values, false,
            "Don't strip dead context stores to aid in debugging.", "CPU");

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
