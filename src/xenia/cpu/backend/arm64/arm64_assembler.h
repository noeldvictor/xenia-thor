/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_ARM64_ARM64_ASSEMBLER_H_
#define XENIA_CPU_BACKEND_ARM64_ARM64_ASSEMBLER_H_

#include <memory>

#include "xenia/cpu/backend/assembler.h"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {

class Arm64Backend;

class Arm64Assembler : public Assembler {
 public:
  explicit Arm64Assembler(Arm64Backend* backend);
  ~Arm64Assembler() override;

  bool Initialize() override;

  bool Assemble(GuestFunction* function, hir::HIRBuilder* builder,
                uint32_t debug_info_flags,
                std::unique_ptr<FunctionDebugInfo> debug_info) override;

 private:
  Arm64Backend* arm64_backend_ = nullptr;
};

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_ARM64_ARM64_ASSEMBLER_H_
