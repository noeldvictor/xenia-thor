/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_function.h"

#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/base/logging.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

A64Function::A64Function(Module* module, uint32_t address)
    : GuestFunction(module, address) {}

A64Function::~A64Function() {
  auto backend =
      profile_registered_backend_.exchange(nullptr, std::memory_order_acq_rel);
  if (backend) {
    backend->UnregisterProfiledFunction(this);
  }
  // machine_code_ is freed by code cache.
}

void A64Function::Setup(uint8_t* machine_code, size_t machine_code_length) {
  machine_code_length_.store(machine_code_length, std::memory_order_relaxed);
  machine_code_.store(machine_code, std::memory_order_release);
}

void A64Function::MarkProfileRegistered(A64Backend* backend) {
  profile_registered_backend_.store(backend, std::memory_order_release);
}

void A64Function::SetupProfileBlockCounts(size_t count) {
  if (count == profile_block_count_count_ && profile_block_counts_ &&
      profile_block_body_ticks_ && profile_block_addresses_) {
    return;
  }

  profile_block_counts_ = std::make_unique<std::atomic<uint64_t>[]>(count);
  profile_block_body_ticks_ =
      std::make_unique<std::atomic<uint64_t>[]>(count);
  profile_block_addresses_ = std::make_unique<uint32_t[]>(count);
  profile_block_count_count_ = count;
  for (size_t i = 0; i < count; ++i) {
    profile_block_counts_[i].store(0, std::memory_order_relaxed);
    profile_block_body_ticks_[i].store(0, std::memory_order_relaxed);
    profile_block_addresses_[i] = 0;
  }
}

std::atomic<uint64_t>* A64Function::profile_block_count(size_t ordinal) {
  if (!profile_block_counts_ || ordinal >= profile_block_count_count_) {
    return nullptr;
  }
  return &profile_block_counts_[ordinal];
}

std::atomic<uint64_t>* A64Function::profile_block_body_ticks(size_t ordinal) {
  if (!profile_block_body_ticks_ || ordinal >= profile_block_count_count_) {
    return nullptr;
  }
  return &profile_block_body_ticks_[ordinal];
}

uint32_t A64Function::profile_block_address(size_t ordinal) const {
  if (!profile_block_addresses_ || ordinal >= profile_block_count_count_) {
    return 0;
  }
  return profile_block_addresses_[ordinal];
}

void A64Function::set_profile_block_address(size_t ordinal, uint32_t address) {
  if (!profile_block_addresses_ || ordinal >= profile_block_count_count_ ||
      !address) {
    return;
  }
  profile_block_addresses_[ordinal] = address;
}

bool A64Function::CallImpl(ThreadState* thread_state, uint32_t return_address) {
  auto backend =
      reinterpret_cast<A64Backend*>(thread_state->processor()->backend());
  auto thunk = backend->host_to_guest_thunk();
  auto* code = machine_code_.load(std::memory_order_acquire);
  if (!thunk || !code) {
    return false;
  }
  thunk(code, thread_state->context(),
        reinterpret_cast<void*>(uintptr_t(return_address)));
  return true;
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
