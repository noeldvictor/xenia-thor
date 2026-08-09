/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_MEMORY_POLL_PARK_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_MEMORY_POLL_PARK_PASS_H_

#include "xenia/cpu/compiler/compiler_pass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// Injects a yield/backoff into indefinite guest memory-poll self-loops
// (load -> test -> branch back to self) so a long wait stops burning a core at
// full duty.
//
// Ported in IDEA from XenDroid edaf74cd4 (their tree has an adaptive
// SpinBackoff(units) helper we do not, so this emits a plain DELAY_EXECUTION).
//
// WHY THIS SHAPE RATHER THAN THE ONE WE ALREADY HAVE: `arm64_guest_spin_
// throttle_functions` targets the same problem but is a64-ONLY, hooks at
// FUNCTION ENTRY, and needs a hand-maintained list of guest addresses - it
// ships empty, so it is inert. This is a HIR pass, so it finds the loops
// STRUCTURALLY and benefits both backends.
//
// REMOVES NOTHING. Every iteration still executes and the loop still exits on
// exactly the same condition; only the spin rate changes. That is what makes it
// safe against a guest that is polling for something another thread will write.
//
// DEFAULT-DENY: the body must contain only time-invariant, side-effect-free
// data ops. Stores to guest memory, calls, atomics and clock reads disqualify
// the loop, because slowing those could change what the loop observes or how
// long a timeout takes to elapse.
class MemoryPollParkPass : public CompilerPass {
 public:
  MemoryPollParkPass();
  ~MemoryPollParkPass() override;

  bool Run(hir::HIRBuilder* builder) override;

 private:
  bool TryInstrumentLoop(hir::HIRBuilder* builder, hir::Block* block);
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_MEMORY_POLL_PARK_PASS_H_
