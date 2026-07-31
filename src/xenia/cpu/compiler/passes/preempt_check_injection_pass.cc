/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/preempt_check_injection_pass.h"

#include <unordered_set>

#include "xenia/base/cvar.h"
#include "xenia/cpu/hir/hir_builder.h"

// Defined here (the lowest layer that reads them) so standalone CPU tools
// (ppc-tests) link without the kernel library; the kernel DECLAREs them via
// kernel_flags.h.
DEFINE_bool(
    guest_scheduler, false,
    "Run guest threads as cooperative fibers driven by an in-kernel scheduler "
    "instead of mapping each to its own host OS thread. Experimental; off by "
    "default. Requires a restart to take effect.",
    "Kernel");
DEFINE_bool(
    guest_scheduler_jit_safepoints, false,
    "Stage 2 of the cooperative guest scheduler: inject preemption "
    "safepoints into compiled guest code so a fiber that never waits still "
    "yields at its quantum end. Lowered by all three backends (x64, a64, and "
    "the LLVM AOT backend via the xe_llvm_preempt_yield runtime helper). "
    "Off = cooperative-only scheduling.",
    "Kernel");

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

using namespace xe::cpu::hir;

PreemptCheckInjectionPass::PreemptCheckInjectionPass() : CompilerPass() {}

PreemptCheckInjectionPass::~PreemptCheckInjectionPass() {}

bool PreemptCheckInjectionPass::Run(HIRBuilder* builder) {
  // The bool return is pass success, not whether anything changed, and Compile
  // aborts the whole function on false.
  //
  // Read the cvars here, not in the ctor, so per-title overrides apply.
  // The separate safepoints gate keeps stage 2 individually A/B-able against
  // cooperative-only scheduling.
  if (!cvars::guest_scheduler || !cvars::guest_scheduler_jit_safepoints ||
      !builder->first_block()) {
    return true;
  }
  // Blocks are laid out in guest address order, so every intra-function cycle
  // branches to an already-seen block. Calls, recursion and indirect branches
  // (bcctr lowers to CallIndirect) re-enter at the entry block.
  std::unordered_set<Block*> seen;
  std::unordered_set<Block*> check_blocks;
  check_blocks.insert(builder->first_block());
  for (auto block = builder->first_block(); block != nullptr;
       block = block->next) {
    seen.insert(block);
    auto instr = block->instr_tail;
    while (instr && (instr->opcode->flags & OPCODE_FLAG_BRANCH) != 0) {
      Label* label = nullptr;
      if (instr->opcode == &OPCODE_BRANCH_info) {
        label = instr->src1.label;
      } else if (instr->opcode == &OPCODE_BRANCH_TRUE_info ||
                 instr->opcode == &OPCODE_BRANCH_FALSE_info) {
        label = instr->src2.label;
      }
      if (label && label->block && seen.count(label->block)) {
        check_blocks.insert(label->block);
      }
      instr = instr->prev;
    }
  }
  for (auto block : check_blocks) {
    // A block holding only fake instructions falls through, so the check
    // lands in the first real successor, still on every cycle through it.
    for (auto b = block; b != nullptr; b = b->next) {
      auto first = b->instr_head;
      for (; first && first->IsFake(); first = first->next) {
      }
      if (first) {
        if (first->GetOpcodeNum() != OPCODE_CHECK_PREEMPT) {
          builder->CheckPreempt()->MoveBefore(first);
        }
        break;
      }
    }
  }
  return true;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
