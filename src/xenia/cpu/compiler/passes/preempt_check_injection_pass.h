/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_PREEMPT_CHECK_INJECTION_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_PREEMPT_CHECK_INJECTION_PASS_H_

#include "xenia/cpu/compiler/compiler_pass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// Prepends a preemption safepoint to the entry block and every back-edge
// target when the guest scheduler is on (guest scheduler stage 2, ported
// from xenia-edge). Lowered by the x64 and a64 emitters and by the LLVM AOT
// backend (inline flag test + xe_llvm_preempt_yield cold call).
class PreemptCheckInjectionPass : public CompilerPass {
 public:
  PreemptCheckInjectionPass();
  ~PreemptCheckInjectionPass() override;

  bool Run(hir::HIRBuilder* builder) override;
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_PREEMPT_CHECK_INJECTION_PASS_H_
