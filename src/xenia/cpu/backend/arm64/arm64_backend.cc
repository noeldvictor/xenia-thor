/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/arm64/arm64_backend.h"

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/cpu/backend/arm64/arm64_assembler.h"
#include "xenia/cpu/backend/arm64/arm64_code_cache.h"
#include "xenia/cpu/backend/arm64/arm64_function.h"
#include "xenia/cpu/backend/assembler.h"
#include "xenia/cpu/function.h"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {

Arm64Backend::Arm64Backend() = default;

Arm64Backend::~Arm64Backend() = default;

bool Arm64Backend::Initialize(Processor* processor) {
  if (!Backend::Initialize(processor)) {
    return false;
  }

  machine_info_.supports_extended_load_store = false;

  auto& gprs = machine_info_.register_sets[0];
  gprs.id = 0;
  std::strcpy(gprs.name, "gpr");
  gprs.types = MachineInfo::RegisterSet::INT_TYPES;
  gprs.count = 24;

  auto& vecs = machine_info_.register_sets[1];
  vecs.id = 1;
  std::strcpy(vecs.name, "vec");
  vecs.types = MachineInfo::RegisterSet::FLOAT_TYPES |
               MachineInfo::RegisterSet::VEC_TYPES;
  vecs.count = 32;

  code_cache_ = Arm64CodeCache::Create();
  Backend::code_cache_ = code_cache_.get();
  if (!code_cache_->Initialize()) {
    return false;
  }
  if (!code_cache_->RunSmokeTest()) {
    return false;
  }

  XELOGI(
      "ARM64 CPU backend initialized with executable code cache. This is a "
      "research mini-JIT plus interpreter fallback, not a converted x64-grade "
      "backend.");
  return true;
}

void Arm64Backend::CommitExecutableRange(uint32_t guest_low,
                                         uint32_t guest_high) {
  (void)guest_low;
  (void)guest_high;
}

std::unique_ptr<Assembler> Arm64Backend::CreateAssembler() {
  return std::make_unique<Arm64Assembler>(this);
}

std::unique_ptr<GuestFunction> Arm64Backend::CreateGuestFunction(
    Module* module, uint32_t address) {
  return std::make_unique<Arm64Function>(module, address);
}

uint64_t Arm64Backend::CalculateNextHostInstruction(
    ThreadDebugInfo* thread_info, uint64_t current_pc) {
  (void)thread_info;
  return current_pc + 4;
}

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
