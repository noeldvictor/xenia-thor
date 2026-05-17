/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_ARM64_ARM64_BACKEND_H_
#define XENIA_CPU_BACKEND_ARM64_ARM64_BACKEND_H_

#include <memory>

#include "xenia/cpu/backend/backend.h"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {

class Arm64CodeCache;

class Arm64Backend : public Backend {
 public:
  explicit Arm64Backend();
  ~Arm64Backend() override;

  Arm64CodeCache* code_cache() const { return code_cache_.get(); }

  bool Initialize(Processor* processor) override;

  void CommitExecutableRange(uint32_t guest_low, uint32_t guest_high) override;

  std::unique_ptr<Assembler> CreateAssembler() override;

  std::unique_ptr<GuestFunction> CreateGuestFunction(Module* module,
                                                     uint32_t address) override;

  uint64_t CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                        uint64_t current_pc) override;

 private:
  std::unique_ptr<Arm64CodeCache> code_cache_;
};

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_ARM64_ARM64_BACKEND_H_
