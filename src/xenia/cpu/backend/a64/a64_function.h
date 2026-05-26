/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_FUNCTION_H_
#define XENIA_CPU_BACKEND_A64_A64_FUNCTION_H_

#include <atomic>
#include <memory>

#include "xenia/cpu/function.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class A64Backend;

struct A64ProfileBlockMetadata {
  uint32_t block_address = 0;
  uint32_t first_source_offset = 0;
  uint32_t last_source_offset = 0;
  uint32_t first_guest_address = 0;
  uint32_t last_guest_address = 0;
  uint32_t first_comment_address = 0;
  uint32_t last_comment_address = 0;
  uint32_t first_label_address = 0;
  uint32_t hir_instr_count = 0;
};

enum A64GuestCallFastEntryPayloadMask : uint32_t {
  kA64GuestCallFastEntryPayloadGpr3 = 1u << 3,
  kA64GuestCallFastEntryPayloadGpr4 = 1u << 4,
  kA64GuestCallFastEntryPayloadGpr5 = 1u << 5,
  kA64GuestCallFastEntryPayloadGpr6 = 1u << 6,
  kA64GuestCallFastEntryPayloadGpr7 = 1u << 7,
  kA64GuestCallFastEntryPayloadGpr8 = 1u << 8,
  kA64GuestCallFastEntryPayloadGpr9 = 1u << 9,
  kA64GuestCallFastEntryPayloadGpr10 = 1u << 10,
  kA64GuestCallFastEntryPayloadLr = 1u << 31,
};

enum A64GuestCallFastEntryDirtyFlushMask : uint32_t {
  kA64GuestCallFastEntryFlushContextBarrier = 1u << 0,
  kA64GuestCallFastEntryFlushHelperCall = 1u << 1,
  kA64GuestCallFastEntryFlushHostCall = 1u << 2,
  kA64GuestCallFastEntryFlushDebugTrap = 1u << 3,
  kA64GuestCallFastEntryFlushTailCall = 1u << 4,
  kA64GuestCallFastEntryFlushReturn = 1u << 5,
  kA64GuestCallFastEntryFlushException = 1u << 6,
  kA64GuestCallFastEntryFlushUnresolvedTarget = 1u << 7,
};

struct A64GuestCallFastEntryContract {
  uint32_t payload_gpr_mask = 0;
  uint32_t dirty_flush_mask = 0;
  uint32_t flags = 0;
};

class A64Function : public GuestFunction {
 public:
  A64Function(Module* module, uint32_t address);
  ~A64Function() override;

  uint8_t* machine_code() const override {
    return machine_code_.load(std::memory_order_acquire);
  }
  size_t machine_code_length() const override {
    return machine_code_length_.load(std::memory_order_acquire);
  }

  void Setup(uint8_t* machine_code, size_t machine_code_length);
  uint8_t* guest_call_fast_entry_code() const {
    return guest_call_fast_entry_code_.load(std::memory_order_acquire);
  }
  size_t guest_call_fast_entry_code_length() const {
    return guest_call_fast_entry_code_length_.load(std::memory_order_acquire);
  }
  A64GuestCallFastEntryContract guest_call_fast_entry_contract() const;
  void SetupGuestCallFastEntry(
      uint8_t* machine_code, size_t machine_code_length,
      const A64GuestCallFastEntryContract& contract);
  void MarkProfileRegistered(A64Backend* backend);
  std::atomic<uint64_t>* profile_entry_count() { return &profile_entry_count_; }
  std::atomic<uint64_t>* profile_body_ticks() { return &profile_body_ticks_; }
  std::atomic<uint64_t>* profile_prolog_ticks() {
    return &profile_prolog_ticks_;
  }
  std::atomic<uint64_t>* profile_epilog_ticks() {
    return &profile_epilog_ticks_;
  }
  void SetupProfileBlockCounts(size_t count);
  size_t profile_block_count_count() const { return profile_block_count_count_; }
  std::atomic<uint64_t>* profile_block_count(size_t ordinal);
  std::atomic<uint64_t>* profile_block_body_ticks(size_t ordinal);
  uint32_t profile_block_address(size_t ordinal) const;
  void set_profile_block_address(size_t ordinal, uint32_t address);
  A64ProfileBlockMetadata profile_block_metadata(size_t ordinal) const;
  void set_profile_block_metadata(size_t ordinal,
                                  const A64ProfileBlockMetadata& metadata);
  void SetupProfileCallEdges(size_t count);
  size_t profile_call_edge_slot_count() const {
    return profile_call_edge_slot_count_;
  }
  std::atomic<uint64_t>* profile_call_edge_count(size_t ordinal);
  std::atomic<uint64_t>* profile_call_edge_body_ticks(size_t ordinal);
  uint32_t profile_call_edge_caller_block_address(size_t ordinal) const;
  uint32_t profile_call_edge_target_address(size_t ordinal) const;
  void set_profile_call_edge_addresses(size_t ordinal,
                                       uint32_t caller_block_address,
                                       uint32_t target_address);

 protected:
  bool CallImpl(ThreadState* thread_state, uint32_t return_address) override;

 private:
  std::atomic<uint8_t*> machine_code_{nullptr};
  std::atomic<size_t> machine_code_length_{0};
  std::atomic<uint8_t*> guest_call_fast_entry_code_{nullptr};
  std::atomic<size_t> guest_call_fast_entry_code_length_{0};
  std::atomic<uint32_t> guest_call_fast_entry_payload_gpr_mask_{0};
  std::atomic<uint32_t> guest_call_fast_entry_dirty_flush_mask_{0};
  std::atomic<uint32_t> guest_call_fast_entry_flags_{0};
  std::atomic<uint64_t> profile_entry_count_{0};
  std::atomic<uint64_t> profile_body_ticks_{0};
  std::atomic<uint64_t> profile_prolog_ticks_{0};
  std::atomic<uint64_t> profile_epilog_ticks_{0};
  std::unique_ptr<std::atomic<uint64_t>[]> profile_block_counts_;
  std::unique_ptr<std::atomic<uint64_t>[]> profile_block_body_ticks_;
  std::unique_ptr<uint32_t[]> profile_block_addresses_;
  std::unique_ptr<A64ProfileBlockMetadata[]> profile_block_metadata_;
  size_t profile_block_count_count_ = 0;
  std::unique_ptr<std::atomic<uint64_t>[]> profile_call_edge_counts_;
  std::unique_ptr<std::atomic<uint64_t>[]> profile_call_edge_body_ticks_;
  std::unique_ptr<uint32_t[]> profile_call_edge_caller_block_addresses_;
  std::unique_ptr<uint32_t[]> profile_call_edge_target_addresses_;
  size_t profile_call_edge_slot_count_ = 0;
  std::atomic<A64Backend*> profile_registered_backend_{nullptr};
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_FUNCTION_H_
