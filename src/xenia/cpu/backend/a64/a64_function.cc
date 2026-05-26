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

A64GuestCallFastEntryContract MakeA64GuestCallFastEntryStubSkeletonContract() {
  A64GuestCallFastEntryContract contract = {};
  contract.payload_gpr_mask = kA64GuestCallFastEntryRequiredPayloadMask;
  contract.dirty_flush_mask = kA64GuestCallFastEntryRequiredDirtyFlushMask;
  contract.flags = kA64GuestCallFastEntryFlagStubSkeleton;
  return contract;
}

bool A64GuestCallFastEntryContractEnablesBehavior(
    const A64GuestCallFastEntryContract& contract) {
  return (contract.flags & kA64GuestCallFastEntryFlagBehaviorEnabled) != 0;
}

bool A64GuestCallFastEntryContractCoversStubSkeleton(
    const A64GuestCallFastEntryContract& contract) {
  return (contract.payload_gpr_mask &
          kA64GuestCallFastEntryRequiredPayloadMask) ==
             kA64GuestCallFastEntryRequiredPayloadMask &&
         (contract.dirty_flush_mask &
          kA64GuestCallFastEntryRequiredDirtyFlushMask) ==
             kA64GuestCallFastEntryRequiredDirtyFlushMask &&
         (contract.flags & kA64GuestCallFastEntryFlagStubSkeleton) != 0;
}

A64GuestCallFastEntryGuardDecision EvaluateA64GuestCallFastEntryGuard(
    const A64GuestCallFastEntryContract& contract,
    const A64GuestCallFastEntryGuardInputs& inputs,
    const uint8_t* fast_entry_code) {
  A64GuestCallFastEntryGuardDecision decision = {};
  decision.required_dirty_flush_mask =
      kA64GuestCallFastEntryRequiredDirtyFlushMask;

  if (!A64GuestCallFastEntryContractEnablesBehavior(contract)) {
    decision.blockers |= kA64GuestCallFastEntryBlockerBehaviorDisabled;
  }
  if (!A64GuestCallFastEntryContractCoversStubSkeleton(contract)) {
    decision.blockers |= kA64GuestCallFastEntryBlockerContractIncomplete;
  }
  if (!fast_entry_code) {
    decision.blockers |= kA64GuestCallFastEntryBlockerMissingFastEntryCode;
  }
  if (!inputs.direct_guest_call) {
    decision.blockers |= kA64GuestCallFastEntryBlockerNotDirectGuestCall;
  }
  if (!inputs.target_resolved) {
    decision.blockers |= kA64GuestCallFastEntryBlockerTargetUnresolved;
  }
  if (!inputs.payload_population_available) {
    decision.blockers |=
        kA64GuestCallFastEntryBlockerPayloadPopulationMissing;
  }
  if (!inputs.dirty_flush_codegen_available) {
    decision.blockers |= kA64GuestCallFastEntryBlockerDirtyFlushMissing;
  }
  if ((inputs.available_dirty_flush_mask &
       kA64GuestCallFastEntryRequiredDirtyFlushMask) !=
      kA64GuestCallFastEntryRequiredDirtyFlushMask) {
    decision.blockers |= kA64GuestCallFastEntryBlockerDirtyFlushIncomplete;
  }
  if (!inputs.late_bound_fallback_available) {
    decision.blockers |= kA64GuestCallFastEntryBlockerLateBoundFallbackMissing;
  }
  if (!inputs.stackpoint_resume_supported) {
    decision.blockers |= kA64GuestCallFastEntryBlockerStackpointResumeMissing;
  }
  if (inputs.crosses_debug_or_exception_boundary) {
    decision.blockers |=
        kA64GuestCallFastEntryBlockerDebugExceptionVisibility;
  }
  return decision;
}

A64GuestCallFastEntryPayloadFlushPlan
BuildA64GuestCallFastEntryPayloadFlushPlan(
    const A64GuestCallFastEntryContract& contract,
    const A64GuestCallFastEntryPayloadFlushPlanInputs& inputs) {
  A64GuestCallFastEntryPayloadFlushPlan plan = {};
  plan.required_payload_mask = kA64GuestCallFastEntryRequiredPayloadMask;
  plan.required_dirty_flush_mask =
      kA64GuestCallFastEntryRequiredDirtyFlushMask;

  if (!A64GuestCallFastEntryContractCoversStubSkeleton(contract)) {
    plan.payload_blockers |=
        kA64GuestCallFastEntryPlanBlockerMissingWritablePayloadSlots;
    plan.dirty_flush_blockers |=
        kA64GuestCallFastEntryPlanBlockerMissingDirtyFlushCoverage;
  }

  if ((inputs.available_source_payload_mask &
       kA64GuestCallFastEntryRequiredPayloadMask) !=
      kA64GuestCallFastEntryRequiredPayloadMask) {
    plan.payload_blockers |=
        kA64GuestCallFastEntryPlanBlockerMissingSourcePayload;
  }
  if (!inputs.payload_slots_writable ||
      (inputs.writable_payload_mask &
       kA64GuestCallFastEntryRequiredPayloadMask) !=
          kA64GuestCallFastEntryRequiredPayloadMask) {
    plan.payload_blockers |=
        kA64GuestCallFastEntryPlanBlockerMissingWritablePayloadSlots;
  }

  uint32_t dirty_payload_mask =
      inputs.dirty_payload_mask & kA64GuestCallFastEntryRequiredPayloadMask;
  if (dirty_payload_mask &&
      (inputs.available_dirty_flush_mask &
       kA64GuestCallFastEntryRequiredDirtyFlushMask) !=
          kA64GuestCallFastEntryRequiredDirtyFlushMask) {
    plan.dirty_flush_blockers |=
        kA64GuestCallFastEntryPlanBlockerMissingDirtyFlushCoverage;
  }
  if (!inputs.context_writeback_available) {
    plan.dirty_flush_blockers |=
        kA64GuestCallFastEntryPlanBlockerMissingContextWriteback;
  }
  if (!inputs.stackpoint_resume_flush_available) {
    plan.dirty_flush_blockers |=
        kA64GuestCallFastEntryPlanBlockerMissingStackpointResumeFlush;
  }
  if (!inputs.debug_exception_flush_available) {
    plan.dirty_flush_blockers |=
        kA64GuestCallFastEntryPlanBlockerMissingDebugExceptionFlush;
  }

  return plan;
}

A64GuestCallFastEntryCodegenProtocolDecision
EvaluateA64GuestCallFastEntryCodegenProtocol(
    const A64GuestCallFastEntryContract& contract,
    const A64GuestCallFastEntryCodegenProtocolInputs& inputs,
    const uint8_t* fast_entry_code) {
  A64GuestCallFastEntryCodegenProtocolDecision decision = {};

  A64GuestCallFastEntryGuardDecision guard =
      EvaluateA64GuestCallFastEntryGuard(contract, inputs.guard_inputs,
                                         fast_entry_code);
  A64GuestCallFastEntryPayloadFlushPlan payload_flush_plan =
      BuildA64GuestCallFastEntryPayloadFlushPlan(
          contract, inputs.payload_flush_inputs);

  decision.guard_blockers = guard.blockers;
  decision.payload_blockers = payload_flush_plan.payload_blockers;
  decision.dirty_flush_blockers = payload_flush_plan.dirty_flush_blockers;

  if (!A64GuestCallFastEntryContractEnablesBehavior(contract)) {
    decision.blockers |=
        kA64GuestCallFastEntryCodegenBlockerBehaviorDisabled;
  }
  if (guard.blockers) {
    decision.blockers |= kA64GuestCallFastEntryCodegenBlockerGuardBlocked;
  }
  if (!payload_flush_plan.ready_for_codegen()) {
    decision.blockers |=
        kA64GuestCallFastEntryCodegenBlockerPayloadFlushPlanBlocked;
  }
  if (!inputs.guard_emission_available) {
    decision.blockers |=
        kA64GuestCallFastEntryCodegenBlockerMissingGuardEmission;
  }
  if (!inputs.payload_population_emission_available) {
    decision.blockers |=
        kA64GuestCallFastEntryCodegenBlockerMissingPayloadPopulationEmission;
  }
  if (!inputs.dirty_flush_emission_available) {
    decision.blockers |=
        kA64GuestCallFastEntryCodegenBlockerMissingDirtyFlushEmission;
  }
  if (!inputs.late_bound_fallback_emission_available) {
    decision.blockers |=
        kA64GuestCallFastEntryCodegenBlockerMissingLateBoundFallbackEmission;
  }
  if (!inputs.stackpoint_resume_emission_available) {
    decision.blockers |=
        kA64GuestCallFastEntryCodegenBlockerMissingStackpointResumeEmission;
  }
  if (!inputs.debug_exception_visibility_available) {
    decision.blockers |=
        kA64GuestCallFastEntryCodegenBlockerMissingDebugExceptionVisibility;
  }

  return decision;
}

void A64Function::Setup(uint8_t* machine_code, size_t machine_code_length) {
  machine_code_length_.store(machine_code_length, std::memory_order_relaxed);
  machine_code_.store(machine_code, std::memory_order_release);
}

A64GuestCallFastEntryContract A64Function::guest_call_fast_entry_contract()
    const {
  A64GuestCallFastEntryContract contract = {};
  contract.payload_gpr_mask = guest_call_fast_entry_payload_gpr_mask_.load(
      std::memory_order_acquire);
  contract.dirty_flush_mask = guest_call_fast_entry_dirty_flush_mask_.load(
      std::memory_order_acquire);
  contract.flags =
      guest_call_fast_entry_flags_.load(std::memory_order_acquire);
  return contract;
}

void A64Function::SetupGuestCallFastEntry(
    uint8_t* machine_code, size_t machine_code_length,
    const A64GuestCallFastEntryContract& contract) {
  guest_call_fast_entry_payload_gpr_mask_.store(contract.payload_gpr_mask,
                                               std::memory_order_relaxed);
  guest_call_fast_entry_dirty_flush_mask_.store(contract.dirty_flush_mask,
                                               std::memory_order_relaxed);
  guest_call_fast_entry_flags_.store(contract.flags, std::memory_order_relaxed);
  guest_call_fast_entry_code_length_.store(machine_code_length,
                                           std::memory_order_relaxed);
  guest_call_fast_entry_code_.store(machine_code, std::memory_order_release);
}

void A64Function::SetupGuestCallFastEntryStubSkeleton() {
  SetupGuestCallFastEntry(nullptr, 0,
                          MakeA64GuestCallFastEntryStubSkeletonContract());
}

void A64Function::MarkProfileRegistered(A64Backend* backend) {
  profile_registered_backend_.store(backend, std::memory_order_release);
}

void A64Function::SetupProfileBlockCounts(size_t count) {
  if (count == profile_block_count_count_ && profile_block_counts_ &&
      profile_block_body_ticks_ && profile_block_addresses_ &&
      profile_block_metadata_) {
    return;
  }

  profile_block_counts_ = std::make_unique<std::atomic<uint64_t>[]>(count);
  profile_block_body_ticks_ =
      std::make_unique<std::atomic<uint64_t>[]>(count);
  profile_block_addresses_ = std::make_unique<uint32_t[]>(count);
  profile_block_metadata_ = std::make_unique<A64ProfileBlockMetadata[]>(count);
  profile_block_count_count_ = count;
  for (size_t i = 0; i < count; ++i) {
    profile_block_counts_[i].store(0, std::memory_order_relaxed);
    profile_block_body_ticks_[i].store(0, std::memory_order_relaxed);
    profile_block_addresses_[i] = 0;
    profile_block_metadata_[i] = {};
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

A64ProfileBlockMetadata A64Function::profile_block_metadata(
    size_t ordinal) const {
  if (!profile_block_metadata_ || ordinal >= profile_block_count_count_) {
    return {};
  }
  return profile_block_metadata_[ordinal];
}

void A64Function::set_profile_block_metadata(
    size_t ordinal, const A64ProfileBlockMetadata& metadata) {
  if (!profile_block_metadata_ || ordinal >= profile_block_count_count_) {
    return;
  }
  profile_block_metadata_[ordinal] = metadata;
  if (profile_block_addresses_ && metadata.block_address) {
    profile_block_addresses_[ordinal] = metadata.block_address;
  }
}

void A64Function::SetupProfileCallEdges(size_t count) {
  if (count == profile_call_edge_slot_count_ && profile_call_edge_counts_ &&
      profile_call_edge_body_ticks_ &&
      profile_call_edge_caller_block_addresses_ &&
      profile_call_edge_target_addresses_) {
    return;
  }

  if (!count) {
    profile_call_edge_counts_.reset();
    profile_call_edge_body_ticks_.reset();
    profile_call_edge_caller_block_addresses_.reset();
    profile_call_edge_target_addresses_.reset();
    profile_call_edge_slot_count_ = 0;
    return;
  }

  profile_call_edge_counts_ =
      std::make_unique<std::atomic<uint64_t>[]>(count);
  profile_call_edge_body_ticks_ =
      std::make_unique<std::atomic<uint64_t>[]>(count);
  profile_call_edge_caller_block_addresses_ =
      std::make_unique<uint32_t[]>(count);
  profile_call_edge_target_addresses_ = std::make_unique<uint32_t[]>(count);
  profile_call_edge_slot_count_ = count;
  for (size_t i = 0; i < count; ++i) {
    profile_call_edge_counts_[i].store(0, std::memory_order_relaxed);
    profile_call_edge_body_ticks_[i].store(0, std::memory_order_relaxed);
    profile_call_edge_caller_block_addresses_[i] = 0;
    profile_call_edge_target_addresses_[i] = 0;
  }
}

std::atomic<uint64_t>* A64Function::profile_call_edge_count(size_t ordinal) {
  if (!profile_call_edge_counts_ || ordinal >= profile_call_edge_slot_count_) {
    return nullptr;
  }
  return &profile_call_edge_counts_[ordinal];
}

std::atomic<uint64_t>* A64Function::profile_call_edge_body_ticks(
    size_t ordinal) {
  if (!profile_call_edge_body_ticks_ ||
      ordinal >= profile_call_edge_slot_count_) {
    return nullptr;
  }
  return &profile_call_edge_body_ticks_[ordinal];
}

uint32_t A64Function::profile_call_edge_caller_block_address(
    size_t ordinal) const {
  if (!profile_call_edge_caller_block_addresses_ ||
      ordinal >= profile_call_edge_slot_count_) {
    return 0;
  }
  return profile_call_edge_caller_block_addresses_[ordinal];
}

uint32_t A64Function::profile_call_edge_target_address(size_t ordinal) const {
  if (!profile_call_edge_target_addresses_ ||
      ordinal >= profile_call_edge_slot_count_) {
    return 0;
  }
  return profile_call_edge_target_addresses_[ordinal];
}

void A64Function::set_profile_call_edge_addresses(
    size_t ordinal, uint32_t caller_block_address, uint32_t target_address) {
  if (!profile_call_edge_caller_block_addresses_ ||
      !profile_call_edge_target_addresses_ ||
      ordinal >= profile_call_edge_slot_count_) {
    return;
  }
  if (caller_block_address) {
    profile_call_edge_caller_block_addresses_[ordinal] = caller_block_address;
  }
  if (target_address) {
    profile_call_edge_target_addresses_[ordinal] = target_address;
  }
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
