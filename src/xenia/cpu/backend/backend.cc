/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/backend.h"

#include <cstring>

namespace xe {
namespace cpu {
namespace backend {

Backend::Backend() { std::memset(&machine_info_, 0, sizeof(machine_info_)); }
Backend::~Backend() = default;

bool Backend::Initialize(Processor* processor) {
  processor_ = processor;
  return true;
}

void* Backend::AllocThreadData() { return nullptr; }

void Backend::FreeThreadData(void* thread_data) {}

// Registered by the cooperative guest scheduler when it starts, null
// otherwise. A JIT safepoint (stage 2, not yet emitted by any backend in this
// tree) calls it with the PPCContext once the scheduler has raised the
// context's preempt_requested flag.
void (*preempt_yield_handler)(void* raw_context) = nullptr;

}  // namespace backend
}  // namespace cpu
}  // namespace xe
