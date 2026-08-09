/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/memory_poll_park_pass.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/hir/hir_builder.h"

DEFINE_bool(
    park_memory_poll_loops, false,
    "Inject a yield (DELAY_EXECUTION) into indefinite guest memory-poll "
    "self-loops - load, test, branch back - so a long wait stops spinning a "
    "core at full duty. Aimed squarely at the power gap: a guest thread "
    "polling a flag burns the same watts as one doing work, and this project's "
    "own profile shows a Burnout wait predicate called ~21M times/sec. "
    "REMOVES NOTHING: every iteration still runs and the loop exits on exactly "
    "the same condition, so a guest waiting on another thread's write cannot "
    "miss it - only the spin RATE changes. "
    "Unlike arm64_guest_spin_throttle_functions (a64-only, function-entry, and "
    "needs a hand-maintained address list so it ships inert) this is a HIR "
    "pass: it finds the loops structurally and both backends lower it. "
    "DEFAULT OFF pending an in-game power measurement.",
    "CPU");
DEFINE_bool(
    log_memory_poll_park, false,
    "Log every memory-poll self-loop this pass accepts or rejects, with the "
    "guest address and the failing condition. The reject reasons are the point "
    "- a pass that silently instruments nothing is indistinguishable from one "
    "that is switched off.",
    "CPU");

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

using namespace xe::cpu::hir;

MemoryPollParkPass::MemoryPollParkPass() : CompilerPass() {}

MemoryPollParkPass::~MemoryPollParkPass() {}

namespace {

// Ops a poll body may massage the loaded value with. Every one is
// time-invariant and side-effect free, so slowing the iteration down cannot
// change what the loop computes or when it decides to exit.
//
// DEFAULT-DENY: anything not listed here leaves the loop untouched. That is
// deliberate - the cost of missing a poll loop is zero, and the cost of
// slowing down a loop that is actually doing work is a silent perf regression
// nobody would attribute to this pass.
bool IsAllowedDataOp(Opcode op) {
  switch (op) {
    case OPCODE_ASSIGN:
    case OPCODE_CAST:
    case OPCODE_TRUNCATE:
    case OPCODE_ZERO_EXTEND:
    case OPCODE_SIGN_EXTEND:
    case OPCODE_BYTE_SWAP:
    case OPCODE_SELECT:
    case OPCODE_AND:
    case OPCODE_OR:
    case OPCODE_XOR:
    case OPCODE_NOT:
    case OPCODE_SHL:
    case OPCODE_SHR:
    case OPCODE_SHA:
    case OPCODE_ROTATE_LEFT:
    case OPCODE_ADD:
    case OPCODE_SUB:
    case OPCODE_COMPARE_EQ:
    case OPCODE_COMPARE_NE:
    case OPCODE_COMPARE_SLT:
    case OPCODE_COMPARE_SLE:
    case OPCODE_COMPARE_SGT:
    case OPCODE_COMPARE_SGE:
    case OPCODE_COMPARE_ULT:
    case OPCODE_COMPARE_ULE:
    case OPCODE_COMPARE_UGT:
    case OPCODE_COMPARE_UGE:
      return true;
    default:
      return false;
  }
}

}  // namespace

bool MemoryPollParkPass::Run(HIRBuilder* builder) {
  // The bool return is pass SUCCESS, not "changed" - Compile aborts the whole
  // function on false, so a loop we decline must still return true.
  if (!cvars::park_memory_poll_loops) {
    return true;
  }
  for (auto block = builder->first_block(); block; block = block->next) {
    TryInstrumentLoop(builder, block);
  }
  return true;
}

bool MemoryPollParkPass::TryInstrumentLoop(HIRBuilder* builder, Block* block) {
  // Candidacy from CFG ground truth rather than from instruction pattern
  // guessing: the block must branch to ITSELF.
  bool has_self_edge = false;
  for (Edge* e = block->outgoing_edge_head; e; e = e->outgoing_next) {
    if (e->dest == block) {
      has_self_edge = true;
      break;
    }
  }
  if (!has_self_edge) {
    return false;
  }

  uint32_t guest_address = 0;
  for (Instr* instr = block->instr_head; instr; instr = instr->next) {
    if (instr->GetOpcodeNum() == OPCODE_SOURCE_OFFSET) {
      guest_address = uint32_t(instr->src1.offset);
      break;
    }
  }
  auto reject = [&](const char* reason) {
    if (cvars::log_memory_poll_park) {
      XELOGI("MemoryPollPark: self-loop at guest {:08X} rejected: {}",
             guest_address, reason);
    }
    return false;
  };

  // The loop-back branch. Conditional either way round: a poll exits on "value
  // changed" (branch_true on NE) or "flag still clear" (branch_false on EQ)
  // depending on which way the guest compiler leaned, so both are accepted. An
  // UNCONDITIONAL self-branch is an infinite loop with no exit - not a poll,
  // and not ours to slow down.
  Instr* loop_branch = nullptr;
  for (Instr* instr = block->instr_head; instr; instr = instr->next) {
    Label* target = nullptr;
    const Opcode op = instr->GetOpcodeNum();
    if (op == OPCODE_BRANCH) {
      target = instr->src1.label;
    } else if (op == OPCODE_BRANCH_TRUE || op == OPCODE_BRANCH_FALSE) {
      target = instr->src2.label;
    } else {
      continue;
    }
    if (!target || target->block != block) {
      continue;
    }
    if (op == OPCODE_BRANCH) {
      return reject("self-branch is unconditional");
    }
    if (loop_branch) {
      return reject("multiple self-branches");
    }
    loop_branch = instr;
  }
  if (!loop_branch) {
    return reject("self edge exists but no self-targeting branch");
  }

  // Body scan, up to the loop-back branch.
  //
  // Guest MEMORY loads are what makes this a poll. Context traffic is this
  // thread's own architectural state and merely repeats, so it is safe to slow.
  // Everything else is denied - in particular stores, calls and atomics (they
  // have effects another thread can observe, so the RATE is semantic) and
  // clock reads (a timeout loop that we slow down would blow its deadline).
  bool saw_load = false;
  for (Instr* instr = block->instr_head; instr != loop_branch;
       instr = instr->next) {
    if (instr->IsFake()) {
      continue;
    }
    const Opcode op = instr->GetOpcodeNum();
    if (op == OPCODE_LOAD) {
      saw_load = true;
      continue;
    }
    if (op == OPCODE_LOAD_CONTEXT || op == OPCODE_STORE_CONTEXT ||
        op == OPCODE_LOAD_LOCAL || op == OPCODE_STORE_LOCAL ||
        op == OPCODE_SOURCE_OFFSET || op == OPCODE_COMMENT ||
        op == OPCODE_NOP || op == OPCODE_CHECK_PREEMPT ||
        op == OPCODE_DELAY_EXECUTION) {
      continue;
    }
    if (IsAllowedDataOp(op)) {
      continue;
    }
    return reject(instr->opcode->name);
  }
  if (!saw_load) {
    return reject("no guest memory load in the body");
  }

  // Already instrumented (the pass can run more than once over a function).
  for (Instr* instr = block->instr_head; instr; instr = instr->next) {
    if (instr->GetOpcodeNum() == OPCODE_DELAY_EXECUTION) {
      return false;
    }
  }

  // Place the yield immediately BEFORE the loop-back branch, so it runs only
  // when the loop is actually going round again - never on the exit path.
  builder->DelayExecution()->MoveBefore(loop_branch);
  if (cvars::log_memory_poll_park) {
    XELOGI("MemoryPollPark: instrumented poll loop at guest {:08X}",
           guest_address);
  }
  return true;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
